#include "card_program.hpp"
#include "selection.hpp"
#include "../mechanics/cards.hpp"
#include "../mechanics/damage.hpp"
#include "../mechanics/block.hpp"
#include "../mechanics/powers.hpp"
#include "../combat/lifecycle.hpp"
#include "../combat/trace.hpp"
#include "../mechanics/exhaust.hpp"
#include <algorithm>
#include <limits>

namespace sts2::engine::gameplay {
StepResult resume_card_program(RunState& state, Scratch& scratch, const Ruleset& rules) {
    const auto depth = state.execution().size();
    while (true) {
        if (!consume(scratch)) return fail(state, Restriction::execution_budget);
        auto& frame = std::get<CardProgramFrame>(state.execution().back()); const auto* source = state.cards().get(frame.source);
        if (!source) return fail(state, Restriction::invalid_state);
        const auto* definition = rules.card(source->definition);
        if (!definition || frame.instruction >= definition->program(source->upgraded).size()) return fail(state, Restriction::invalid_state);
        auto* target = state.creature(frame.target);
        if (!target) return fail(state, Restriction::invalid_state);
        const auto instruction = definition->program(source->upgraded)[frame.instruction++];
        const int amount = instruction.base;
        switch (instruction.opcode) {
        case Opcode::draw:
            if (amount > 0 && state.has_living_enemies() && state.player().hp != 0) {
                state.execution().push_back(DrawFrame{.remaining = amount, .from_hand_draw = false});
                return result(state);
            }
            break;
        case Opcode::exhaust_random: {
            const auto& hand = state.zone(Zone::hand);
            if (!hand.empty()) {
                std::int32_t index;
                if (!state.data.rng.get(RunStream::combat_card_selection).next_int(0, static_cast<std::int32_t>(hand.size()), index)) return fail(state, Restriction::invalid_state);
                const auto card = hand[index];
                const auto cards = capture_exhaust_cards(state, std::span(&card, 1));
                state.execution().push_back(ExhaustCardsFrame{.cards = cards, .source_zone = Zone::hand, .charge_card_step = false});
                return result(state);
            }
            break;
        }
        case Opcode::request_choice: {
            const auto zone = instruction.choice == CardChoice::discard_one ? Zone::discard : Zone::hand;
            const auto eligibility = instruction.choice == CardChoice::upgradable_hand ? SelectionEligibility::upgradable : SelectionEligibility::all;
            const bool optional = instruction.choice == CardChoice::hand_optional;
            if (zone == Zone::discard && !state.has_living_enemies()) break;
            request_selection(state, zone, optional ? 0 : 1, static_cast<std::uint32_t>(amount), eligibility);
            return result(state);
        }
        case Opcode::exhaust_choice: {
            const auto cards = take_selection(state);
            if (cards.count != 0) {
                state.execution().push_back(ExhaustCardsFrame{.cards = cards, .source_zone = Zone::hand});
                return result(state);
            }
            break;
        }
        case Opcode::upgrade_choice: case Opcode::topdeck_choice: case Opcode::discard_choice: {
            const auto cards = take_selection(state);
            for (std::uint32_t i = 0; i < cards.count; ++i) {
                const auto card = state.continuation_cards()[cards.begin + i];
                if (instruction.opcode == Opcode::upgrade_choice) state.cards().get(card)->upgraded = true;
                else if (instruction.opcode == Opcode::discard_choice) discard_card(state, scratch, card);
                else {
                    move_card(state, Zone::discard, Zone::draw, card);
                    auto pile = state.zone(Zone::draw); std::rotate(pile.begin(), pile.end() - 1, pile.end());
                }
            }
            release_selection_cards(state, cards);
            break;
        }
        case Opcode::upgrade_all:
            for (auto card : state.zone(Zone::hand)) state.cards().get(card)->upgraded = true;
            break;
        case Opcode::damage: {
            const auto hit = damage(*target, amount, state.player(), state.overflow);
            emit(state, scratch, CombatEventKind::damaged, false, true, frame.source, 0, 0, hit, true, false, state.data.player, frame.target);
            emit(state, scratch, CombatEventKind::attacked, false, false, frame.source, 0, 0, hit, true, false, state.data.player, frame.target);
            break;
        }
        case Opcode::block: {
            if (!gain_block(state, scratch, amount, frame.source, frame.play)) return fail(state, Restriction::capacity);
            break;
        }
        case Opcode::strength:
            (void)apply_power(state, scratch, PowerId::strength, amount, false, false, frame.source); break;
        case Opcode::weak: case Opcode::vulnerable: {
            const auto power = instruction.opcode == Opcode::weak ? PowerId::weak : PowerId::vulnerable;
            if (definition->enemy_target) {
                (void)apply_power(state, scratch, power, amount, true, false, frame.source, true, false,
                    PowerInstance::Stacking::by_type, {}, frame.target);
            } else {
                // Existing Shockwave content applies to every living enemy.
                for (auto enemy : state.enemies()) if (state.creature(enemy)->hp > 0)
                    (void)apply_power(state, scratch, power, amount, true, false, frame.source, true, false,
                        PowerInstance::Stacking::by_type, {}, enemy);
            }
            break;
        }
        case Opcode::finish: {
            const auto card = frame.source; const auto play = frame.play; state.execution().pop_back();
            for (auto it = state.cost_modifiers().begin(); it != state.cost_modifiers().end();) {
                if (it->card == card && (static_cast<unsigned>(it->expiry) & 4U) != 0) it = state.cost_modifiers().erase(it);
                else ++it;
            }
            const bool exhaust_after_play = source->exhaust_on_next_play ||
                has_keyword(source->keyword_mask, CardKeyword::exhaust);
            state.cards().get(card)->exhaust_on_next_play = false;
            if (source->removed) {
                // A captured source can outlive its active zone membership.
            } else if (definition->kind == CardKind::power) {
                auto pile = state.zone(Zone::play); pile.erase(std::find(pile.begin(), pile.end(), card));
                state.cards().get(card)->removed = true;
            } else if (!exhaust_after_play)
                move_card(state, Zone::play, Zone::discard, card);
            if (!state.has_living_enemies()) { end_combat(state, scratch, true); return result(state); }
            ++state.combat().counters.finished_plays;
            state.cards().get(card)->last_finished_player_turn = state.combat().turn;
            emit(state, scratch, CombatEventKind::play_finished, false, false, card, play);
            if (!source->removed && exhaust_after_play) {
                const auto cards = capture_exhaust_cards(state, std::span(&card, 1));
                state.execution().push_back(ExhaustCardsFrame{.cards = cards, .source_zone = Zone::play, .charge_card_step = false});
                return result(state);
            }
            if (!state.has_living_enemies()) { end_combat(state, scratch, true); return result(state); }
            return result(state);
        }
        }
        // Reacquire the frame on the next iteration: child work can relocate storage.
        if (state.combat().phase != CombatPhase::play || state.execution().size() != depth) return result(state);
    }
}
}
