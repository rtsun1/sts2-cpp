#pragma once
#include "sts2/engine/combat.hpp"

namespace sts2::engine::gameplay {
void clear_selection(RunState&);

void sort_selection_candidates(RunState&, const Ruleset&);
RangeProgress take_selection(RunState&) noexcept;
void release_selection_cards(RunState&, RangeProgress);
void record_selection(RunState&, const SelectionInput&);
// Returns false when player input is required, true for an automatic answer.
bool present_selection(RunState&, const Ruleset&);
bool selection_card_eligible(const RunState&, CardHandle, SelectionEligibility) noexcept;
void request_selection(RunState&, Zone, std::uint32_t minimum, std::uint32_t maximum,
    SelectionEligibility = SelectionEligibility::all);

Restriction validate_selection_input(const RunState&, const SelectionInput&) noexcept;
bool valid_selection(const RunState&, const Ruleset&) noexcept;
bool valid_frame_selection(const RunState&, const Ruleset&, std::span<const ExecutionFrame>,
    std::span<const HookListener>, const CardSelection&, std::span<const CardHandle>, bool retained_bounds, bool pending = false) noexcept;
}
