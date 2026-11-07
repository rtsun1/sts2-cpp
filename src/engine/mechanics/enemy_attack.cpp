#include "enemy_attack.hpp"
#include "damage.hpp"
#include "../combat/lifecycle.hpp"
#include "../combat/trace.hpp"
#include "../hooks/dispatch.hpp"

namespace sts2::engine::gameplay {
StepResult resume_enemy_attack(RunState& state, Scratch& scratch, const Ruleset& rules) {
    auto& frame = std::get<EnemyAttackFrame>(state.execution().back());
    if (frame.phase == EnemyAttackPhase::damage) {
        const auto* actor = state.creature(frame.actor);
        if (!actor || actor->hp == 0 || state.player().hp == 0) {
            state.execution().pop_back(); return result(state);
        }
        frame.hit = damage(state.player(), frame.base_damage, *actor, state.overflow);
        frame.phase = EnemyAttackPhase::hooks;
        emit(state, scratch, CombatEventKind::damaged, true, false, {}, 0, 0, frame.hit, true, true, frame.actor, state.data.player);
        // Native damage-received hooks skip ordinary lethal damage.
        if (state.player().hp != 0) frame.listeners = capture_damage_received_listeners(state);
    }
    if (frame.phase == EnemyAttackPhase::hooks) {
        const auto outcome = resume_listeners(state, scratch, frame.listeners, frame.handler, rules, nullptr, &frame.hit);
        if (outcome == HandlerResult::yield) return result(state);
        if (outcome == HandlerResult::execution_budget) return fail(state, Restriction::execution_budget);
        frame.phase = EnemyAttackPhase::finish;
    }
    emit(state, scratch, CombatEventKind::attacked, true, true, {}, 0, 0, frame.hit, true, true, frame.actor, state.data.player);
    state.execution().pop_back();
    return result(state);
}
}
