#pragma once
#include "sts2/state/run.hpp"

namespace sts2::engine::gameplay {
struct EnemyIntent { BeetleMove move; int attack; };
EnemyIntent visible_enemy_intent(const RunState&, CreatureHandle) noexcept;
void advance_enemy_move(CreatureState&) noexcept;
void execute_enemy_move(RunState&, Scratch&, CreatureHandle);
}
