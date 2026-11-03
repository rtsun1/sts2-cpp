#pragma once
#include "sts2/engine/combat.hpp"

namespace sts2::engine {
Restriction legal_combat_input(const RunState&, const CombatInput&, const Ruleset& = Ruleset::pinned()) noexcept;
Restriction legal_selection_input(const RunState&, const SelectionInput&) noexcept;
bool valid_powers(const RunState&) noexcept;
}
