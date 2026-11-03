#pragma once
#include "sts2/state/run.hpp"

namespace sts2::engine::gameplay {
struct EnemyIntent { EnemyMove move; int attack; };
EnemyIntent visible_enemy_intent(const RunState&, CreatureHandle) noexcept;
void execute_enemy_move(RunState&, Scratch&, CreatureHandle);
void advance_enemy_move(CreatureState&) noexcept;
int roll_enemy_hp(const RunState&, EnemyId, GameRng&);
}
