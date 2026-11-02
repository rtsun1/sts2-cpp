#include "exhaust.hpp"
#include "cards.hpp"
#include "../combat/lifecycle.hpp"
#include "../combat/trace.hpp"
#include "../hooks/dispatch.hpp"
#include <algorithm>

namespace sts2::engine::gameplay {
bool exhaust_one(RunState& state, Scratch& scratch, Zone from, CardHandle card) {
    if (!state.has_living_enemies() || state.player().hp == 0) return false;
    move_card(state, from, Zone::exhaust, card);
    emit(state, scratch, CombatEventKind::exhausted, false, false, card);
    return true;
}
RangeProgress capture_exhaust_cards(RunState& state, std::span<const CardHandle> cards) {
    const auto begin = static_cast<std::uint32_t>(state.continuation_cards().size());
    for (auto card : cards) state.continuation_cards().push_back(card);
    return {begin, static_cast<std::uint32_t>(cards.size()), 0};
}
StepResult resume_exhaust_cards(RunState& state, Scratch& scratch, const Ruleset& rules) {
    while (true) {
        auto& frame = std::get<ExhaustCardsFrame>(state.execution().back());
        if (frame.phase == ExhaustPhase::start_card) {
            if (frame.cards.cursor == frame.cards.count) {
                if (frame.cards.count == 0 && frame.charge_card_step && !consume(scratch))
                    return fail(state, Restriction::execution_budget);
                if (frame.cards.count != 0)
                    while (state.continuation_cards().size() > frame.cards.begin) state.continuation_cards().pop_back();
                state.execution().pop_back(); return result(state);
            }
            if (frame.charge_card_step && !consume(scratch)) return fail(state, Restriction::execution_budget);
            const auto card = state.continuation_cards()[frame.cards.begin + frame.cards.cursor];
            if (!state.cards().get(card)) return fail(state, Restriction::invalid_state);
            if (!state.has_living_enemies() || state.player().hp == 0) {
                ++frame.cards.cursor; continue;
            }
            const auto zone = state.zone(frame.source_zone);
            if (std::find(zone.begin(), zone.end(), card) == zone.end()) return fail(state, Restriction::invalid_state);
            frame.phase = ExhaustPhase::hooks;
            (void)exhaust_one(state, scratch, frame.source_zone, card);
            frame.listeners = capture_exhaust_listeners(state);
        }
        if (frame.phase == ExhaustPhase::hooks) {
            const auto outcome = resume_listeners(state, scratch, frame.listeners, frame.handler, rules);
            if (outcome == HandlerResult::yield) return result(state);
            if (outcome == HandlerResult::execution_budget) return fail(state, Restriction::execution_budget);
            frame.phase = ExhaustPhase::finish_card;
        }
        ++frame.cards.cursor;
        frame.phase = ExhaustPhase::start_card;
    }
}
}
