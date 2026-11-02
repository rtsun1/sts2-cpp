#include "monsters.hpp"
#include "powers.hpp"
#include "../mechanics/damage.hpp"
#include "../mechanics/powers.hpp"
#include "../combat/trace.hpp"

namespace sts2::engine::gameplay {
void execute_enemy_move(RunState& state, Scratch& scratch) {
    switch (state.combat().enemy_ai.selected) {
    case BeetleMove::shrink: (void)apply_power(state, scratch, PowerId::shrink, -1, false, true, {}, true, true); state.combat().enemy_ai.selected = BeetleMove::chomp; break;
    case BeetleMove::chomp: case BeetleMove::stomp: {
        const auto hit = damage(state.player(), state.combat().enemy_ai.selected == BeetleMove::chomp ? 7 : 13, state.enemy(), state.overflow);
        emit(state, scratch, CombatEventKind::damaged, true, false, {}, 0, 0, hit);
        emit(state, scratch, CombatEventKind::attacked, true, true, {}, 0, 0, hit);
        if (state.player().hp > 0) state.combat().enemy_ai.selected = state.combat().enemy_ai.selected == BeetleMove::chomp ? BeetleMove::stomp : BeetleMove::chomp;
        break;
    }
    }
}
EnemyIntent visible_enemy_intent(const RunState& state) noexcept {
    const auto move = state.combat().enemy_ai.selected;
    const auto base = move == BeetleMove::chomp ? 7 : move == BeetleMove::stomp ? 13 : 0;
    return {move, base == 0 ? 0 : modify_attack_damage(state.player(), base, state.enemy(), state.overflow)};
}
}
