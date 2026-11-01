#include "shuffle.hpp"
#include "sts2/core/sort.hpp"
#include "cards.hpp"
#include "../combat/lifecycle.hpp"
#include "../hooks/dispatch.hpp"
#include <algorithm>

namespace sts2::engine::gameplay {
void reshuffle(CombatView state, const Ruleset& rules) {
    auto pile = state.zone(Zone::draw); auto discard = state.zone(Zone::discard);
    pile.insert(pile.end(), discard.begin(), discard.end()); discard.clear();
    game_sort<CardHandle>(pile, [&](CardHandle a, CardHandle b) {
        const auto& ca = *state.cards.get(a); const auto& cb = *state.cards.get(b);
        const auto ida = rules.card(ca.definition)->model_id, idb = rules.card(cb.definition)->model_id;
        return ida == idb ? ca.upgraded < cb.upgraded : ida < idb;
    });
    state.rng.get(RunStream::shuffle).shuffle<CardHandle>(pile);
}
StepResult resume_shuffle(CombatView state, Scratch& scratch, const Ruleset& rules) {
    auto& frame = std::get<ShuffleFrame>(state.execution.back());
    if (frame.phase == ShufflePhase::reorder) {
        if (scratch.steps >= scratch.step_limit) {
            (void)consume(scratch); return fail(state, Restriction::execution_budget);
        }
        if (!frame.charge_draw_step_if_hooks && !consume(scratch)) return fail(state, Restriction::execution_budget);
        reshuffle(state, rules);
        frame.listeners = capture_shuffle_listeners(state);
        if (frame.charge_draw_step_if_hooks && frame.listeners.count != 0)
            (void)consume(scratch); // Reserved by the draw request before yielding.
        frame.phase = ShufflePhase::hooks;
    }
    const auto outcome = resume_listeners(state, scratch, frame.listeners, frame.handler, rules, &frame.expected_selection_count);
    if (outcome == HandlerResult::yield) return result(state);
    if (outcome == HandlerResult::execution_budget) return fail(state, Restriction::execution_budget);
    state.execution.pop_back();
    return result(state);
}
}
