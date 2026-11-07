#include "enemy_turn.hpp"
#include "lifecycle.hpp"
#include "../content/enemies.hpp"
#include "../mechanics/cards.hpp"
#include "../mechanics/energy.hpp"
#include "../mechanics/powers.hpp"
#include "../hooks/dispatch.hpp"
#include <limits>

namespace sts2::engine::gameplay {
StepResult resume_enemy_turn(RunState& state, Scratch& scratch, const Ruleset&) {
    auto& frame = std::get<EnemyTurnFrame>(state.execution().back());
    if (frame.phase == EnemyTurnPhase::after_move) {
        if (auto* actor = state.creature(state.enemy_turn_order()[frame.cursor])) actor->enemy_ai.performed = true;
        ++frame.cursor;
        frame.phase = EnemyTurnPhase::actors;
    }
    while (frame.phase == EnemyTurnPhase::actors) {
        if (state.player().hp == 0 || !state.has_living_enemies() || frame.cursor == state.enemy_turn_order().size()) {
            frame.phase = EnemyTurnPhase::finish; break;
        }
        const auto handle = state.enemy_turn_order()[frame.cursor];
        auto* actor = state.creature(handle);
        if (!state.is_enemy(handle) || actor->hp <= 0 || actor->skip_current_enemy_turn) {
            ++frame.cursor; continue;
        }
        if (!consume(scratch)) return fail(state, Restriction::execution_budget);
        actor->block = 0;
        snapshot_power_turn(state, scratch, *actor, true);
        // Power/trace callbacks may remove or replace the captured actor.
        actor = state.creature(handle);
        if (!state.is_enemy(handle) || actor->hp <= 0) {
            ++frame.cursor; continue;
        }
        // Record caller progress before content can insert a child frame.
        frame.phase = EnemyTurnPhase::after_move;
        execute_enemy_move(state, scratch, handle);
        return result(state);
    }
    if (state.player().hp > 0 && state.has_living_enemies()) after_enemy_turn(state, scratch);
    // Side-end work can create creatures too; clear flags only after it finishes.
    for (auto& slot : state.creatures().slots()) if (slot.live) slot.value.skip_current_enemy_turn = false;
    state.combat().enemy_side = false;
    state.enemy_turn_order().clear();
    state.execution().pop_back();
    if (state.player().hp == 0) end_combat(state, scratch, false);
    else if (!state.has_living_enemies()) end_combat(state, scratch, true);
    else {
        for (auto handle : state.enemies()) {
            auto* enemy = state.creature(handle);
            if (enemy && enemy->enemy_ai.performed) advance_enemy_move(*enemy);
        }
        snapshot_power_turn(state, scratch, state.player(), false);
        state.combat().counters.started_this_turn = state.combat().counters.attacks_this_turn = 0;
        state.combat().counters.zero_energy_attacks_this_turn = state.combat().counters.non_hand_draws_this_turn = 0;
        state.player().block = 0;
        if (state.combat().turn == std::numeric_limits<int>::max()) return fail(state, Restriction::capacity);
        for (auto it = state.cost_modifiers().begin(); it != state.cost_modifiers().end();) {
            if ((static_cast<unsigned>(it->expiry) & 2U) != 0) it = state.cost_modifiers().erase(it);
            else ++it;
        }
        ++state.combat().turn; refresh_energy(state, 3); state.execution().push_back(DrawFrame{.remaining = 5});
    }
    return result(state);
}
}
