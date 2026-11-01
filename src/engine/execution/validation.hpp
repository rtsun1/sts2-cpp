#pragma once
#include "sts2/state/combat.hpp"

namespace sts2::engine::gameplay {
bool valid_continuations(ConstCombatView, const Ruleset&) noexcept;
}
