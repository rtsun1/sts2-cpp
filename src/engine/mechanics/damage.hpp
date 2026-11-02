#pragma once
#include "sts2/state/run.hpp"

namespace sts2::engine::gameplay {
int heal(CreatureState&, int amount) noexcept;
HitResult receive_damage(CreatureState& target, int amount) noexcept;
HitResult damage(CreatureState& target, int base, const CreatureState& source, const OverflowStorage&) noexcept;
}
