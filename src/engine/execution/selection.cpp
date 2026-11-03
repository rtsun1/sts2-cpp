#include "selection.hpp"
#include "sts2/engine/action_rules.hpp"
#include "../transitions.hpp"
#include "../combat/lifecycle.hpp"
#include "../mechanics/cards.hpp"
#include "../mechanics/exhaust.hpp"
#include "sts2/core/sort.hpp"
#include <algorithm>
#include <stdexcept>

namespace sts2::engine::gameplay {
namespace {
bool pile_choice_less(const RunState& state, CardHandle a, CardHandle b, const Ruleset& rules) noexcept {
    const auto* first = rules.card(state.cards().get(a)->definition);
    const auto* second = rules.card(state.cards().get(b)->definition);
    return first->rarity == second->rarity ? first->model_id < second->model_id : first->rarity < second->rarity;
}
}
void clear_selection(RunState& state) {
    state.combat().selection = {}; state.selection_candidates().clear();
}
void begin_selection(RunState& state, SelectionEffect effect, Zone zone, std::uint32_t minimum, std::uint32_t maximum, bool automatic) {
    if (state.combat().next_prompt.value == UINT32_MAX) throw std::length_error("prompt identity exhausted");
    clear_selection(state);
    state.combat().selection = {state.combat().next_prompt, effect, zone, minimum, maximum, automatic};
    ++state.combat().next_prompt.value;
    state.combat().selection.continuation_depth = static_cast<std::uint32_t>(state.execution().size() - 1);
    for (auto card : state.zone(zone))
        if (effect != SelectionEffect::upgrade || !state.cards().get(card)->upgraded) state.selection_candidates().push_back(card);
    if (effect == SelectionEffect::add_to_hand && zone == Zone::draw)
        game_sort<CardHandle>(state.selection_candidates(), [&](auto a, auto b) { return pile_choice_less(state, a, b, Ruleset::pinned()); });
    state.combat().selection.maximum = std::min(maximum, static_cast<std::uint32_t>(state.selection_candidates().size()));
    state.combat().phase = CombatPhase::card_choice;
}
StepResult apply_card_selection(RunState& state, const SelectionInput& input, Scratch& scratch, const Ruleset& rules) {
    // Copy once before any effect can suspend or invalidate caller storage.
    const auto cards = capture_exhaust_cards(state, input.cards);
    if (state.combat().phase == CombatPhase::discard_choice) {
        const auto card = state.continuation_cards()[cards.begin];
        discard_card(state, scratch, card);
        state.continuation_cards().pop_back();
        state.combat().phase = CombatPhase::play;
    } else {
        const auto effect = state.combat().selection.effect;
        const auto zone = state.combat().selection.zone;
        clear_selection(state);
        state.combat().phase = CombatPhase::play;
        if (effect == SelectionEffect::exhaust) {
            if (cards.count != 0)
                state.execution().push_back(ExhaustCardsFrame{.cards = cards, .source_zone = zone});
            else if (!consume(scratch)) return fail(state, Restriction::execution_budget);
        } else {
            // Supported synchronous operations share the same operand ownership.
            for (std::uint32_t i = 0; i < cards.count; ++i) {
                const auto card = state.continuation_cards()[cards.begin + i];
                if (effect != SelectionEffect::add_to_hand && !consume(scratch)) return fail(state, Restriction::execution_budget);
                if (!state.cards().get(card)) return fail(state, Restriction::invalid_state);
                if (effect == SelectionEffect::upgrade) state.cards().get(card)->upgraded = true;
                else if (effect == SelectionEffect::add_to_hand) add_to_hand(state, zone, card);
                else if (effect == SelectionEffect::topdeck) {
                    move_card(state, zone, Zone::draw, card);
                    auto pile = state.zone(Zone::draw); std::rotate(pile.begin(), pile.end() - 1, pile.end());
                } else return fail(state, Restriction::invalid_state);
            }
            while (state.continuation_cards().size() > cards.begin) state.continuation_cards().pop_back();
        }
    }
    return advance_combat_trusted(state, scratch, rules);
}

Restriction validate_selection_input(const RunState& state, const SelectionInput& input) noexcept {
    const bool discard = state.combat().phase == CombatPhase::discard_choice;
    if (!discard && state.combat().phase != CombatPhase::card_choice) return Restriction::wrong_phase;
    if (input.prompt != (discard ? PromptId{} : state.combat().selection.id)) return Restriction::invalid_selection;
    const std::span<const CardHandle> candidates = discard ? std::span<const CardHandle>(state.zone(Zone::hand)) : std::span<const CardHandle>(state.selection_candidates());
    const auto minimum = discard ? 1u : state.combat().selection.minimum;
    const auto maximum = discard ? 1u : state.combat().selection.maximum;
    const auto count = action_rules::selection_count_restriction(input.cards.size(), minimum, maximum);
    if (count != Restriction::none) return count;
    for (std::size_t i = 0; i < input.cards.size(); ++i) {
        const auto card = input.cards[i];
        if (!state.cards().get(card) || std::find(candidates.begin(), candidates.end(), card) == candidates.end())
            return discard ? Restriction::stale_card : Restriction::invalid_selection;
        for (std::size_t j = 0; j < i; ++j) if (input.cards[j] == card) return Restriction::invalid_selection;
    }
    return Restriction::none;
}

bool valid_selection(const RunState& state, const Ruleset& rules) noexcept {
    if (state.combat().phase == CombatPhase::card_choice) {
        const auto& p = state.combat().selection;
        if (state.execution().empty() || p.continuation_depth != state.execution().size() - 1 || p.id.value == 0 || p.id.value >= state.combat().next_prompt.value ||
            p.minimum > p.maximum || p.maximum == 0 || p.zone >= Zone::count) return false;
        const auto& frame = state.execution().back();
        SelectionEffect effect;
        Zone zone = Zone::hand; std::uint32_t minimum = 1, maximum = 1;
        bool automatic = false;
        if (const auto* hook = std::get_if<ShuffleFrame>(&frame)) {
            if (hook->expected_selection_count == 0) return false;
            effect = SelectionEffect::add_to_hand; zone = Zone::draw; automatic = true;
            minimum = hook->expected_selection_count; maximum = hook->expected_selection_count;
            if (minimum == 0 || minimum >= state.zone(zone).size()) return false;
        } else if (const auto* program = std::get_if<CardProgramFrame>(&frame)) {
            const auto* source = state.cards().get(program->source);
            const auto* definition = source ? rules.card(source->definition) : nullptr;
            if (!definition || program->instruction == 0 || program->instruction >= definition->program(source->upgraded).size()) return false;
            const auto opcode = definition->program(source->upgraded)[program->instruction - 1].opcode;
            if (opcode == Opcode::exhaust_choice) { effect = SelectionEffect::exhaust; minimum = 0; maximum = static_cast<std::uint32_t>(definition->program(source->upgraded)[program->instruction - 1].base); }
            else if (opcode == Opcode::upgrade_choice) effect = SelectionEffect::upgrade;
            else if (opcode == Opcode::topdeck_choice) { effect = SelectionEffect::topdeck; zone = Zone::discard; automatic = true; }
            else if (opcode == Opcode::exhaust_one) effect = SelectionEffect::exhaust;
            else return false;
        } else return false;
        maximum = std::min(maximum, static_cast<std::uint32_t>(state.selection_candidates().size()));
        if (p.effect != effect || p.zone != zone || p.minimum != minimum || p.maximum != maximum || p.automatic_at_maximum != automatic) return false;
        if (effect == SelectionEffect::add_to_hand && zone == Zone::draw) {
            if (state.selection_candidates().size() != state.zone(zone).size()) return false;
            for (std::size_t i = 0; i < state.selection_candidates().size(); ++i) {
                const auto card = state.selection_candidates()[i];
                if (!state.cards().get(card) || std::find(state.zone(zone).begin(), state.zone(zone).end(), card) == state.zone(zone).end()) return false;
                for (std::size_t j = 0; j < i; ++j) if (state.selection_candidates()[j] == card) return false;
                if (i != 0 && pile_choice_less(state, card, state.selection_candidates()[i - 1], rules)) return false;
            }
        } else {
            std::size_t offered = 0;
            for (auto card : state.zone(zone)) {
                if (effect == SelectionEffect::upgrade && state.cards().get(card) && state.cards().get(card)->upgraded) continue;
                if (offered >= state.selection_candidates().size() || state.selection_candidates()[offered++] != card) return false;
            }
            if (offered != state.selection_candidates().size() || offered == 0 || (minimum == 1 && offered <= 1)) return false;
        }
    } else if (state.combat().phase == CombatPhase::discard_choice) {
        if (state.execution().empty() || state.zone(Zone::hand).empty()) return false;
        const auto& frame = state.execution().back();
        const auto* program = std::get_if<CardProgramFrame>(&frame);
        if (!program) return false;
        const auto* card = state.cards().get(program->source); const auto* definition = card ? rules.card(card->definition) : nullptr;
        if (!definition || program->instruction == 0 || program->instruction >= definition->program(card->upgraded).size() ||
            definition->program(card->upgraded)[program->instruction - 1].opcode != Opcode::discard_choice) return false;
    } else if (!state.execution().empty() || !state.zone(Zone::play).empty()) return false;
    return true;
}

}
