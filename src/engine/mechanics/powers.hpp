#pragma once
#include "sts2/state/run.hpp"

namespace sts2::engine::gameplay {
std::uint32_t apply_power(RunState&, Scratch&, PowerId, int amount, bool enemy, bool enemy_side,
    CardHandle source = {}, bool has_applier = true, bool applier_enemy = false, PowerInstance::Stacking stacking = PowerInstance::Stacking::by_type, CreatureHandle applier = {}, CreatureHandle target = {}, std::uint32_t existing_instance = UINT32_MAX);
void remove_power(RunState&, Scratch&, CreatureHandle owner, std::uint32_t identity);
void expire_power(RunState&, Scratch&, CreatureState&, std::uint32_t identity);
void snapshot_power_turn(RunState&, Scratch&, CreatureState&, bool enemy);
}
