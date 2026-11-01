#pragma once
#include "sts2/state/combat.hpp"

namespace sts2::engine::gameplay {
struct PowerModifier { int amount; std::uint32_t artifact; };
PowerModifier before_power_application(const CreatureState&, const OverflowStorage&, PowerId, int amount) noexcept;
void after_power_application(CombatView, Scratch&, CreatureState&, PowerModifier, bool enemy, bool enemy_side, CreatureHandle target);
void after_card_exhausted(CombatView, Scratch&, CardHandle);
void after_shuffle(CombatView);
void after_enemy_turn(CombatView, Scratch&);
// Resume requires the matching continuation on top of the execution stack.
StepResult resume_shuffle(CombatView, Scratch&, const Ruleset&);
StepResult resume_exhaust(CombatView, Scratch&, const Ruleset&);
// Preserve the final value of a captured power before active storage erases it.
void retain_removed_power(CombatView, CreatureHandle, const PowerInstance&) noexcept;
}
