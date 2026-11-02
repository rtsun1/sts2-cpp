#include "relics.hpp"
#include "../mechanics/damage.hpp"
#include "../mechanics/cards.hpp"
#include "../hooks/dispatch.hpp"
#include "../combat/lifecycle.hpp"
#include "../combat/trace.hpp"

namespace sts2::engine::gameplay {
void charons_ashes_after_card_exhausted(RunState& state, Scratch& scratch) {
    if (state.enemy().hp <= 0) return;
    const auto hit = receive_damage(state.enemy(), 3);
    emit(state, scratch, CombatEventKind::damaged, false, true, {}, 4, 0, hit);
}
HandlerResult joss_paper_after_card_exhausted(RunState& state, Scratch& scratch, HandlerProgress& progress) {
    if (progress.stage == 0) {
        ++state.data.joss_paper_counter;
        if (state.data.joss_paper_counter < 5) return HandlerResult::complete;
        progress.stage = 1;
        if (state.enemy().hp != 0 && state.player().hp != 0)
            state.execution().push_back(DrawFrame{.remaining = static_cast<int>(state.data.joss_paper_counter / 5), .from_hand_draw = false});
        return HandlerResult::yield;
    }
    if (!consume(scratch)) return HandlerResult::execution_budget;
    state.data.joss_paper_counter %= 5;
    return HandlerResult::complete;
}
}
