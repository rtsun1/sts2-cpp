#include "monsters.hpp"
#include "powers.hpp"
#include "../mechanics/damage.hpp"
#include "../mechanics/powers.hpp"
#include "../combat/trace.hpp"

namespace sts2::engine::gameplay {
void execute_enemy_move(RunState& state, Scratch& scratch, CreatureHandle actor) {
    switch (state.creature(actor)->enemy_ai.selected) {
    case BeetleMove::shrink: (void)apply_power(state, scratch, PowerId::shrink, -1, false, true, {}, true, true, PowerInstance::Stacking::by_type, actor); if (auto* enemy = state.creature(actor)) enemy->enemy_ai.selected = BeetleMove::chomp; break;
    case BeetleMove::chomp: case BeetleMove::stomp: {
        const auto hit = damage(state.player(), state.creature(actor)->enemy_ai.selected == BeetleMove::chomp ? 7 : 13, *state.creature(actor), state.overflow);
        emit(state, scratch, CombatEventKind::damaged, true, false, {}, 0, 0, hit, true, true, actor, state.data.player);
        emit(state, scratch, CombatEventKind::attacked, true, true, {}, 0, 0, hit, true, true, actor, state.data.player);
        if (auto* enemy = state.creature(actor); enemy && state.player().hp > 0) enemy->enemy_ai.selected = enemy->enemy_ai.selected == BeetleMove::chomp ? BeetleMove::stomp : BeetleMove::chomp;
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
