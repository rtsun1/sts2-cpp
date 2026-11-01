#include "interpreter.hpp"
#include "selection.hpp"
#include "../mechanics/cards.hpp"
#include "../mechanics/damage.hpp"
#include "../mechanics/block.hpp"
#include "../mechanics/powers.hpp"
#include "../combat/lifecycle.hpp"
#include "../combat/trace.hpp"
#include "../hooks/dispatch.hpp"
#include "../content/relics.hpp"
#include <algorithm>
#include <limits>

namespace sts2::engine::gameplay {
void defer_draw(CombatView state, int count, bool from_hand_draw) {
    state.execution.push_back(DrawFrame{count, from_hand_draw});
}
void defer_joss_paper_draw_completion(CombatView state) {
    state.execution.push_back(RelicDrawCompletionFrame{});
}
StepResult resume_card_program(CombatView state, Scratch& scratch, const Ruleset& rules) {
    const auto depth = state.execution.size();
    while (true) {
        if (!consume(scratch)) return fail(state, Restriction::execution_budget);
        auto& frame = std::get<CardProgramFrame>(state.execution.back()); const auto* source = state.cards.get(frame.source);
        if (!source) return fail(state, Restriction::invalid_state);
        const auto* definition = rules.card(source->definition);
        if (!definition || frame.instruction >= definition->program(source->upgraded).size()) return fail(state, Restriction::invalid_state);
        auto* target = state.creature(frame.target);
        if (!target) return fail(state, Restriction::invalid_state);
        const auto instruction = definition->program(source->upgraded)[frame.instruction++];
        const int amount = instruction.base;
        switch (instruction.opcode) {
        case Opcode::draw: draw(state, scratch, amount, rules, false); break;
        case Opcode::exhaust_random: {
            const auto& hand = state.zone(Zone::hand);
            if (!hand.empty()) {
                std::int32_t index;
                if (!state.rng.get(RunStream::combat_card_selection).next_int(0, static_cast<std::int32_t>(hand.size()), index)) return fail(state, Restriction::invalid_state);
                exhaust(state, scratch, Zone::hand, hand[index]);
            }
            break;
        }
        case Opcode::exhaust_one:
            if (state.zone(Zone::hand).size() == 1) {
                exhaust(state, scratch, Zone::hand, state.zone(Zone::hand).front());
            } else if (!state.zone(Zone::hand).empty()) {
                if (state.next_prompt.value == UINT32_MAX) return fail(state, Restriction::capacity);
                begin_selection(state, SelectionEffect::exhaust, Zone::hand, 1, 1, false); return result(state);
            }
            break;
        case Opcode::topdeck_choice: {
            const auto& candidates = state.zone(Zone::discard);
            if (state.enemy.hp == 0 || candidates.empty()) break;
            if (candidates.size() == 1) {
                const auto card = candidates.front(); move_card(state, Zone::discard, Zone::draw, card);
                auto pile = state.zone(Zone::draw); std::rotate(pile.begin(), pile.end() - 1, pile.end());
            } else {
                if (state.next_prompt.value == UINT32_MAX) return fail(state, Restriction::capacity);
                begin_selection(state, SelectionEffect::topdeck, Zone::discard, 1, 1, true); return result(state);
            }
            break;
        }
        case Opcode::upgrade_all:
            for (auto card : state.zone(Zone::hand)) state.cards.get(card)->upgraded = true;
            break;
        case Opcode::upgrade_choice: {
            std::size_t count = 0;
            for (auto card : state.zone(Zone::hand)) count += !state.cards.get(card)->upgraded;
            if (count <= 1) {
                for (auto card : state.zone(Zone::hand)) state.cards.get(card)->upgraded = true;
            } else {
                if (state.next_prompt.value == UINT32_MAX) return fail(state, Restriction::capacity);
                begin_selection(state, SelectionEffect::upgrade, Zone::hand, 1, 1, false); return result(state);
            }
            break;
        }
        case Opcode::exhaust_choice:
            if (!state.zone(Zone::hand).empty()) {
                if (state.next_prompt.value == UINT32_MAX) return fail(state, Restriction::capacity);
                begin_selection(state, SelectionEffect::exhaust, Zone::hand, 0, static_cast<std::uint32_t>(amount), false);
                return result(state);
            }
            break;
        case Opcode::damage: {
            const auto hit = damage(*target, amount, state.player, state.creature_overflow);
            emit(state, scratch, CombatEventKind::damaged, false, true, frame.source, 0, 0, hit);
            emit(state, scratch, CombatEventKind::attacked, false, false, frame.source, 0, 0, hit);
            break;
        }
        case Opcode::block: {
            if (!gain_block(state, scratch, amount, frame.source, frame.play)) return fail(state, Restriction::capacity);
            break;
        }
        case Opcode::strength:
            (void)apply_power(state, scratch, PowerId::strength, amount, false, false, frame.source); break;
        case Opcode::weak:
            (void)apply_power(state, scratch, PowerId::weak, amount, true, false, frame.source, true, false, PowerInstance::Stacking::by_type, {}, frame.target); break;
        case Opcode::vulnerable:
            if (amount > std::numeric_limits<int>::max() - target->power_amount(PowerId::vulnerable, state.creature_overflow)) return fail(state, Restriction::capacity);
            (void)apply_power(state, scratch, PowerId::vulnerable, amount, true, false, frame.source, true, false, PowerInstance::Stacking::by_type, {}, frame.target); break;
        case Opcode::discard_choice:
            if (state.zone(Zone::hand).size() == 1) {
                const auto discarded = state.zone(Zone::hand).front();
                discard_card(state, scratch, discarded);
            } else if (!state.zone(Zone::hand).empty()) { state.phase = CombatPhase::discard_choice; return result(state); }
            break;
        case Opcode::finish: {
            const auto card = frame.source; const auto play = frame.play; state.execution.pop_back();
            for (auto it = state.cost_modifiers.begin(); it != state.cost_modifiers.end();) {
                if (it->card == card && (static_cast<unsigned>(it->expiry) & 4U) != 0) it = state.cost_modifiers.erase(it);
                else ++it;
            }
            const bool exhaust_after_play = source->exhaust_on_next_play ||
                has_keyword(source->keyword_mask, CardKeyword::exhaust);
            state.cards.get(card)->exhaust_on_next_play = false;
            if (source->removed) {
                // A captured source can outlive its active zone membership.
            } else if (definition->kind == CardKind::power) {
                auto pile = state.zone(Zone::play); pile.erase(std::find(pile.begin(), pile.end(), card));
                state.cards.get(card)->removed = true;
            } else if (!exhaust_after_play)
                move_card(state, Zone::play, Zone::discard, card);
            if (state.enemy.hp == 0) { end_combat(state, scratch, true); return result(state); }
            ++state.counters.finished_plays;
            state.cards.get(card)->last_finished_player_turn = state.turn;
            emit(state, scratch, CombatEventKind::play_finished, false, false, card, play);
            if (!source->removed && exhaust_after_play)
                exhaust(state, scratch, Zone::play, card);
            if (state.enemy.hp == 0) { end_combat(state, scratch, true); return result(state); }
            return result(state);
        }
        }
        // Reacquire the frame on the next iteration: child work can relocate storage.
        if (state.phase != CombatPhase::play || state.execution.size() != depth) return result(state);
    }
}

namespace {
StepResult resume_draw(CombatView state, Scratch& scratch, const Ruleset& rules) {
    while (true) {
        if (!consume(scratch)) return fail(state, Restriction::execution_budget);
        auto& frame = std::get<DrawFrame>(state.execution.back());
        if (frame.remaining <= 0 || state.zone(Zone::hand).size() >= 10 || state.enemy.hp == 0 || state.player.hp == 0) {
            state.execution.pop_back(); return result(state);
        }
        if (state.zone(Zone::draw).empty()) {
            if (state.zone(Zone::discard).empty()) { state.execution.pop_back(); return result(state); }
            const auto depth = state.execution.size();
            reshuffle(state, rules);
            after_shuffle(state);
            if (state.execution.size() != depth) return result(state);
        }
        const auto from_hand_draw = frame.from_hand_draw;
        --frame.remaining;
        draw_one(state, scratch, from_hand_draw);
    }
}
StepResult resume_selection(CombatView state, Scratch& scratch) {
    const auto depth = state.execution.size();
    while (true) {
        if (!consume(scratch)) return fail(state, Restriction::execution_budget);
        auto& frame = std::get<SelectionFrame>(state.execution.back());
        if (frame.cursor == frame.count) {
            while (state.continuation_cards.size() > frame.begin) state.continuation_cards.pop_back();
            state.execution.pop_back(); return result(state);
        }
        const auto card = state.continuation_cards[frame.begin + frame.cursor++];
        const auto effect = frame.effect; const auto zone = frame.zone;
        if (!state.cards.get(card)) return fail(state, Restriction::invalid_state);
        const bool finished = frame.cursor == frame.count;
        if (finished) {
            // Release operands before the last effect can start child work.
            while (state.continuation_cards.size() > frame.begin) state.continuation_cards.pop_back();
            state.execution.pop_back();
        }
        if (effect == SelectionEffect::upgrade) state.cards.get(card)->upgraded = true;
        else if (effect == SelectionEffect::add_to_hand) add_to_hand(state, zone, card);
        else if (effect == SelectionEffect::topdeck) {
            move_card(state, zone, Zone::draw, card);
            auto pile = state.zone(Zone::draw); std::rotate(pile.begin(), pile.end() - 1, pile.end());
        } else if (effect == SelectionEffect::exhaust) exhaust(state, scratch, zone, card);
        else return fail(state, Restriction::invalid_state);
        // A final effect can pop this frame and push a child at the same depth.
        if (finished || state.phase != CombatPhase::play || state.execution.size() != depth) return result(state);
    }
}
StepResult resume_relic_draw_completion(CombatView state, Scratch& scratch) {
    if (!consume(scratch)) return fail(state, Restriction::execution_budget);
    joss_paper_after_draw(state);
    state.execution.pop_back();
    return result(state);
}
}
StepResult resume_frame(CombatView state, Scratch& scratch, const Ruleset& rules) {
    return std::visit([&](const auto& frame) -> StepResult {
        using Frame = std::decay_t<decltype(frame)>;
        if constexpr (std::is_same_v<Frame, CardProgramFrame>) return resume_card_program(state, scratch, rules);
        else if constexpr (std::is_same_v<Frame, DrawFrame>) return resume_draw(state, scratch, rules);
        else if constexpr (std::is_same_v<Frame, ShuffleFrame>) return resume_shuffle(state, scratch, rules);
        else if constexpr (std::is_same_v<Frame, SelectionFrame>) return resume_selection(state, scratch);
        else if constexpr (std::is_same_v<Frame, ExhaustFrame>) return resume_exhaust(state, scratch, rules);
        else return resume_relic_draw_completion(state, scratch);
    }, state.execution.back());
}

bool valid_continuations(ConstCombatView state, const Ruleset& rules) noexcept {
    std::size_t captured_end = 0, cards_end = 0;
    for (const auto& frame : state.execution) {
        if (const auto* card = std::get_if<CardProgramFrame>(&frame)) {
            const auto* source = state.cards.get(card->source);
            const auto* definition = source ? rules.card(source->definition) : nullptr;
            if (card->energy < 0 || !definition || !state.creature(card->target) || card->play >= state.counters.started_plays || card->instruction >= definition->program(source->upgraded).size()) return false;
            if (!source->removed && std::find(state.zone(Zone::play).begin(), state.zone(Zone::play).end(), card->source) == state.zone(Zone::play).end()) return false;
        } else if (const auto* draw_state = std::get_if<DrawFrame>(&frame)) {
            if (draw_state->remaining < 0) return false;
        } else if (const auto* hook = std::get_if<ShuffleFrame>(&frame)) {
            if (hook->begin != captured_end || hook->cursor > hook->count || hook->count > state.captured_hook_listeners.size() - captured_end) return false;
            captured_end += hook->count;
            if (hook->selection_count != 0 && (&frame != &state.execution.back() || state.phase != CombatPhase::card_choice || hook->cursor == 0)) return false;
        } else if (const auto* choice = std::get_if<SelectionFrame>(&frame)) {
            if (choice->begin != cards_end || choice->cursor > choice->count || choice->count > state.continuation_cards.size() - cards_end ||
                choice->effect == SelectionEffect::none || choice->effect > SelectionEffect::add_to_hand || choice->zone >= Zone::count) return false;
            cards_end += choice->count;
        } else if (const auto* hook = std::get_if<ExhaustFrame>(&frame)) {
            if (hook->begin != captured_end || hook->cursor > hook->count || hook->count > state.captured_hook_listeners.size() - captured_end) return false;
            captured_end += hook->count;
        } else if (!std::holds_alternative<RelicDrawCompletionFrame>(frame)) return false;
    }
    if (captured_end != state.captured_hook_listeners.size() || cards_end != state.continuation_cards.size()) return false;
    for (auto card : state.continuation_cards) if (!state.cards.get(card)) return false;
    for (const auto& listener : state.captured_hook_listeners) {
        if (!listener.owner.present() || listener.handler > HookHandler::joss_paper) return false;
        const bool power = listener.handler == HookHandler::dark_embrace || listener.handler == HookHandler::stratagem;
        if (power ? listener.power_instance >= state.next_power_instance : listener.power_instance != UINT32_MAX) return false;
        if (listener.removed_power_amount < -999999999 || listener.removed_power_amount > 999999999) return false;
        if (const auto* owner = state.creature(listener.owner)) {
            if (const auto* instance = owner->find_power_instance(listener.power_instance, state.creature_overflow)) {
                const auto definition = listener.handler == HookHandler::stratagem ? PowerId::stratagem : PowerId::dark_embrace;
                if (!power || instance->definition != definition) return false;
            }
        }
    }
    for (const auto& frame : state.execution) {
        std::uint32_t begin = 0, count = 0; bool shuffle = false;
        if (const auto* continuation = std::get_if<ShuffleFrame>(&frame)) { begin = continuation->begin; count = continuation->count; shuffle = true; }
        else if (const auto* continuation = std::get_if<ExhaustFrame>(&frame)) { begin = continuation->begin; count = continuation->count; }
        else continue;
        for (std::uint32_t i = 0; i < count; ++i)
            if ((state.captured_hook_listeners[begin + i].handler == HookHandler::stratagem) != shuffle) return false;
    }
    return valid_selection(state, rules);
}
}
