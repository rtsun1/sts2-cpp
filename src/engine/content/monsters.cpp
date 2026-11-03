#include "monsters.hpp"
#include "powers.hpp"
#include "../mechanics/powers.hpp"

namespace sts2::engine::gameplay {
void advance_enemy_move(CreatureState& enemy) noexcept {
    enemy.enemy_ai.selected = enemy.enemy_ai.selected == BeetleMove::chomp ? BeetleMove::stomp : BeetleMove::chomp;
    enemy.enemy_ai.performed = false;
}
void execute_enemy_move(RunState& state, Scratch& scratch, CreatureHandle actor) {
    switch (state.creature(actor)->enemy_ai.selected) {
    case BeetleMove::shrink: (void)apply_power(state, scratch, PowerId::shrink, -1, false, true, {}, true, true, PowerInstance::Stacking::by_type, actor); break;
    case BeetleMove::chomp: case BeetleMove::stomp: {
        state.execution().push_back(EnemyAttackFrame{.actor = actor,
            .base_damage = state.creature(actor)->enemy_ai.selected == BeetleMove::chomp ? 7 : 13});
        break;
    }
    }
}
EnemyIntent visible_enemy_intent(const RunState& state, CreatureHandle actor) noexcept {
    const auto move = state.creature(actor)->enemy_ai.selected;
    const auto base = move == BeetleMove::chomp ? 7 : move == BeetleMove::stomp ? 13 : 0;
    return {move, base == 0 ? 0 : modify_attack_damage(state.player(), base, *state.creature(actor), state.overflow)};
}
}
