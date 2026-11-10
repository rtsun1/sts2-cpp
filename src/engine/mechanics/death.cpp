#include "death.hpp"
#include "../hooks/dispatch.hpp"
#include "../execution/pending_choices.hpp"
#include "../combat/lifecycle.hpp"

namespace sts2::engine::gameplay {
StepResult resume_death(RunState& state, Scratch& scratch) {
    if (!consume(scratch)) return fail(state, Restriction::execution_budget);
    auto& frame = std::get<DeathFrame>(state.execution().back());
    HookListener listener;
    if (next_death_listener(state, frame.listeners, listener)) {
        // Every remaining listener effect belongs to the child. Its caller has
        // already advanced, so it can finish death cleanup if the child pauses.
        const auto dead = frame.target;
        begin_detachable_frames(state);
        after_death(state, listener, dead);
        return result(state);
    }
    auto* target = state.creature(frame.target);
    for (const auto& power : target->power_instances(state.overflow))
        retain_removed_power(state, frame.target, power);
    target->clear_powers();
    // Retain the last dead enemy as the terminal combat's identity anchor, as in
    // the existing representation. In-progress deaths lose native membership.
    if (state.has_living_enemies()) state.set_enemy_membership(frame.target, false);
    state.execution().pop_back();
    return result(state);
}
}
