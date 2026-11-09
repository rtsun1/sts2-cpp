#pragma once
#include "sts2/engine/combat.hpp"
#include <algorithm>
#include <stdexcept>

namespace sts2::engine::gameplay {
void clear_selection(RunState&);

void sort_selection_candidates(RunState&);
RangeProgress take_selection(RunState&) noexcept;
void release_selection_cards(RunState&, RangeProgress);
void record_selection(RunState&, const SelectionInput&);

template<typename Predicate>
void request_selection(RunState& state, Zone zone, std::uint32_t minimum,
    std::uint32_t maximum, Predicate&& eligible) {
    clear_selection(state);
    for (CardHandle card : state.zone(zone))
        if (eligible(state, card)) state.selection_candidates().push_back(card);
    const auto count = static_cast<std::uint32_t>(state.selection_candidates().size());

    // handle trivial cases without prompting the client.
    if (count == 0) return;
    if (minimum == 1 && count == 1) {
        record_selection(state, {state.data.boundary, {}, std::span<const CardHandle>(state.selection_candidates())});
        return;
    }

    if (state.combat().next_prompt.value == UINT32_MAX) throw std::length_error("prompt identity exhausted");
    if (zone == Zone::draw) sort_selection_candidates(state);
    state.combat().selection = {state.combat().next_prompt, minimum, std::min(maximum, count)};
    ++state.combat().next_prompt.value;
    state.combat().phase = CombatPhase::card_choice;
}

Restriction validate_selection_input(const RunState&, const SelectionInput&) noexcept;
bool valid_selection(const RunState&, const Ruleset&) noexcept;
}
