#pragma once
#include "sts2/engine/combat.hpp"

namespace sts2::engine::gameplay {
void clear_selection(RunState&);
void begin_selection(RunState&, SelectionEffect, Zone, std::uint32_t minimum, std::uint32_t maximum, bool automatic);
// Call after combat input validation; the caller owns clock and boundary updates.
StepResult apply_card_selection(RunState&, const CombatInput&, Scratch&, const Ruleset&);
Restriction validate_selection_input(const RunState&, const CombatInput&) noexcept;
// Checks prompt/frame consistency after continuation storage has been validated.
bool valid_selection(const RunState&, const Ruleset&) noexcept;

}
