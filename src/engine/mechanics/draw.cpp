#include "draw.hpp"
#include "cards.hpp"
#include "../combat/lifecycle.hpp"
#include "../combat/trace.hpp"
#include "../hooks/dispatch.hpp"
#include <algorithm>

namespace sts2::engine::gameplay {
void draw_one(RunState& state, Scratch& scratch, bool from_hand_draw) {
    auto pile = state.zone(Zone::draw); auto hand = state.zone(Zone::hand);
    auto card = pile.front(); pile.erase(pile.begin()); hand.push_back(card);
    if (!from_hand_draw) {
        if (state.combat().counters.non_hand_draws_this_turn == UINT32_MAX) throw std::length_error("draw count exhausted");
        ++state.combat().counters.non_hand_draws_this_turn;
    }
    if (scratch.trace.receive) {
        CombatEvent event{CombatEventKind::drawn, state.combat().turn, false, false, card};
        event.from_hand_draw = from_hand_draw; scratch.trace.emit(state, event);
    }
}
StepResult resume_draw(RunState& state, Scratch& scratch, const Ruleset& rules) {
    while (true) {
        auto& frame = std::get<DrawFrame>(state.execution().back());
        if (frame.phase == DrawPhase::after_shuffle) frame.phase = DrawPhase::draw;
        if (frame.phase == DrawPhase::early_hooks || frame.phase == DrawPhase::normal_hooks) {
            const auto phase = frame.phase;
            const auto outcome = resume_listeners(state, scratch, frame.listeners, frame.handler, rules);
            if (outcome == HandlerResult::yield) return result(state);
            if (outcome == HandlerResult::execution_budget) return fail(state, Restriction::execution_budget);
            if (phase == DrawPhase::early_hooks) {
                frame.phase = DrawPhase::normal_hooks;
                frame.listeners = capture_draw_listeners(state, false);
                if (frame.listeners.count != 0) continue;
            }
            frame.current_card = {};
            frame.phase = DrawPhase::draw;
        }
        // Shuffling becomes a child operation, but does not introduce an extra
        // charge when the old draw step could shuffle and draw synchronously.
        if (scratch.steps >= scratch.step_limit) {
            (void)consume(scratch); return fail(state, Restriction::execution_budget);
        }
        if (frame.remaining <= 0 || state.zone(Zone::hand).size() >= 10 || !state.has_living_enemies() || state.player().hp == 0) {
            (void)consume(scratch);
            state.execution().pop_back(); return result(state);
        }
        if (state.zone(Zone::draw).empty()) {
            if (state.zone(Zone::discard).empty()) {
                (void)consume(scratch);
                state.execution().pop_back(); return result(state);
            }
            frame.phase = DrawPhase::after_shuffle;
            state.execution().push_back(ShuffleFrame{.charge_draw_step_if_hooks = true});
            return result(state);
        }
        (void)consume(scratch);
        frame.current_card = state.zone(Zone::draw).front();
        --frame.remaining;
        frame.phase = DrawPhase::early_hooks;
        draw_one(state, scratch, frame.from_hand_draw);
        frame.listeners = capture_draw_listeners(state, true);
        if (frame.listeners.count == 0) {
            frame.phase = DrawPhase::normal_hooks;
            frame.listeners = capture_draw_listeners(state, false);
            if (frame.listeners.count == 0) {
                frame.current_card = {};
                frame.phase = DrawPhase::draw;
            }
        }
    }
}
}
