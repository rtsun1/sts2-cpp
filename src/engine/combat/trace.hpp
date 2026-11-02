#pragma once
#include "sts2/state/run.hpp"

namespace sts2::engine::gameplay {
void emit(RunState&, Scratch&, CombatEventKind, bool enemy_side = false, bool actor_enemy = false,
    CardHandle = {}, std::uint32_t object = 0, int amount = 0, HitResult = {}, bool has_applier = true, bool applier_enemy = false, CreatureHandle actor = {}, CreatureHandle receiver = {});
}
