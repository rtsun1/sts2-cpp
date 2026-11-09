#include "selection.hpp"
#include "sts2/engine/action_rules.hpp"
#include "sts2/core/sort.hpp"
#include <algorithm>
#include <stdexcept>

namespace sts2::engine::gameplay {
namespace {
std::optional<Opcode> pending_selection_opcode(const RunState& state, const Ruleset& rules) noexcept {
    if (state.execution().empty()) return std::nullopt;
    const auto* program = std::get_if<CardProgramFrame>(&state.execution().back());
    const auto* source = program ? state.cards().get(program->source) : nullptr;
    const auto* definition = source ? rules.card(source->definition) : nullptr;
    if (!definition || program->instruction >= definition->program(source->upgraded).size()) return std::nullopt;
    return definition->program(source->upgraded)[program->instruction].opcode;
}
bool pile_choice_less(const RunState& state, CardHandle a, CardHandle b, const Ruleset& rules) noexcept {
    const auto* first = rules.card(state.cards().get(a)->definition);
    const auto* second = rules.card(state.cards().get(b)->definition);
    return first->rarity == second->rarity ? first->model_id < second->model_id : first->rarity < second->rarity;
}
}
void clear_selection(RunState& state) {
    state.combat().selection = {}; state.selection_candidates().clear();
}

void record_selection(RunState& state, const SelectionInput& input) {
    const auto begin = static_cast<std::uint32_t>(state.continuation_cards().size());
    for (auto card : input.cards) state.continuation_cards().push_back(card);
    state.combat().selected_cards = {begin, static_cast<std::uint32_t>(input.cards.size()), 0};
    clear_selection(state);
    state.combat().phase = CombatPhase::play;
}
Restriction validate_selection_input(const RunState& state, const SelectionInput& input) noexcept {
    if (state.combat().phase != CombatPhase::card_choice) return Restriction::wrong_phase;
    if (input.prompt != state.combat().selection.id) return Restriction::invalid_selection;
    const std::span<const CardHandle> candidates = std::span<const CardHandle>(state.selection_candidates());
    const auto minimum = state.combat().selection.minimum;
    const auto maximum = state.combat().selection.maximum;
    const auto count = action_rules::selection_count_restriction(input.cards.size(), minimum, maximum);
    if (count != Restriction::none) return count;
    for (std::size_t i = 0; i < input.cards.size(); ++i) {
        const auto card = input.cards[i];
        if (!state.cards().get(card) || std::find(candidates.begin(), candidates.end(), card) == candidates.end())
            return pending_selection_opcode(state, Ruleset::pinned()) == Opcode::discard_choice ? Restriction::stale_card : Restriction::invalid_selection;
        for (std::size_t j = 0; j < i; ++j) if (input.cards[j] == card) return Restriction::invalid_selection;
    }
    return Restriction::none;
}

void sort_selection_candidates(RunState& state) {
    game_sort<CardHandle>(state.selection_candidates(), [&](auto a, auto b) {
        return pile_choice_less(state, a, b, Ruleset::pinned());
    });
}
RangeProgress take_selection(RunState& state) noexcept {
    const auto cards = state.combat().selected_cards;
    state.combat().selected_cards = {};
    return cards;
}
void release_selection_cards(RunState& state, RangeProgress cards) {
    if (cards.count == 0) return;
    while (state.continuation_cards().size() > cards.begin) state.continuation_cards().pop_back();
}
bool valid_selection(const RunState& state, const Ruleset& rules) noexcept {
    // Answers never survive a published boundary: retained operands belong to frames.
    if (state.combat().selected_cards != RangeProgress{}) return false;
    if (state.combat().phase != CombatPhase::card_choice)
        return state.execution().empty() && state.zone(Zone::play).empty() &&
            state.combat().selection == CardSelection{} && state.selection_candidates().empty();
    const auto& p = state.combat().selection;
    if (state.execution().empty() || p.id.value == 0 || p.id.value >= state.combat().next_prompt.value ||
        p.minimum > p.maximum || p.maximum == 0) return false;
    Zone zone = Zone::hand;
    std::uint32_t minimum = 1, maximum = 1;
    bool upgrade = false;
    const auto& frame = state.execution().back();
    if (const auto* shuffle = std::get_if<ShuffleFrame>(&frame)) {
        if (shuffle->phase != ShufflePhase::hooks || shuffle->handler.stage != 1 ||
            shuffle->listeners.cursor >= shuffle->listeners.count) return false;
        const auto& listener = state.captured_hook_listeners()[shuffle->listeners.begin + shuffle->listeners.cursor];
        if (listener.handler != HookHandler::stratagem) return false;
        const auto* owner = state.creature(listener.owner);
        const auto* power = owner ? owner->find_power_instance(listener.power_instance, state.overflow) : nullptr;
        const auto amount = power ? power->amount : listener.removed_power_amount;
        if (amount <= 0 || static_cast<std::size_t>(amount) >= state.zone(Zone::draw).size()) return false;
        zone = Zone::draw; minimum = maximum = static_cast<std::uint32_t>(amount);
    } else if (const auto* program = std::get_if<CardProgramFrame>(&frame)) {
        const auto* source = state.cards().get(program->source);
        const auto* definition = source ? rules.card(source->definition) : nullptr;
        if (!definition || program->instruction == 0 || program->instruction >= definition->program(source->upgraded).size()) return false;
        const auto instruction = definition->program(source->upgraded)[program->instruction - 1];
        if (instruction.opcode != Opcode::request_choice) return false;
        switch (definition->program(source->upgraded)[program->instruction].opcode) {
        case Opcode::exhaust_choice: case Opcode::upgrade_choice:
        case Opcode::topdeck_choice: case Opcode::discard_choice: break;
        default: return false;
        }
        if (instruction.choice > CardChoice::upgradable_hand || instruction.base <= 0) return false;
        maximum = static_cast<std::uint32_t>(instruction.base);
        if (instruction.choice == CardChoice::hand_optional) minimum = 0;
        if (instruction.choice == CardChoice::discard_one) zone = Zone::discard;
        upgrade = instruction.choice == CardChoice::upgradable_hand;
    } else return false;
    maximum = std::min(maximum, static_cast<std::uint32_t>(state.selection_candidates().size()));
    if (p.minimum != minimum || p.maximum != maximum) return false;
    if (zone == Zone::draw) {
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
            const auto* value = state.cards().get(card);
            if (!value) return false;
            if (upgrade && value->upgraded) continue;
            if (offered >= state.selection_candidates().size() || state.selection_candidates()[offered++] != card) return false;
        }
        if (offered != state.selection_candidates().size() || offered == 0 || (minimum == 1 && offered <= 1)) return false;
    }
    return true;
}
}
