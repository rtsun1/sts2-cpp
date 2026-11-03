#include "relics.hpp"
#include "../mechanics/damage.hpp"
#include "../mechanics/cards.hpp"
#include "../hooks/dispatch.hpp"
#include "../combat/lifecycle.hpp"
#include "../combat/trace.hpp"

namespace sts2::engine::gameplay {
void charons_ashes_after_card_exhausted(RunState& state, Scratch& scratch) {
    for (auto handle : state.enemies()) {
        if (state.creature(handle)->hp <= 0) continue;
        const auto hit = receive_damage(*state.creature(handle), 3);
        emit(state, scratch, CombatEventKind::damaged, false, true, {}, 4, 0, hit, true, false, state.data.player, handle);
        if (hit.killed) after_creature_death(state, scratch, handle);
    }
}
HandlerResult joss_paper_after_card_exhausted(RunState& state, Scratch& scratch, HandlerProgress& progress) {
    if (progress.stage == 0) {
        ++state.data.joss_paper_counter;
        if (state.data.joss_paper_counter < 5) return HandlerResult::complete;
        progress.stage = 1;
        if (state.has_living_enemies() && state.player().hp != 0)
            state.execution().push_back(DrawFrame{.remaining = static_cast<int>(state.data.joss_paper_counter / 5), .from_hand_draw = false});
        return HandlerResult::yield;
    }
    if (!consume(scratch)) return HandlerResult::execution_budget;
    state.data.joss_paper_counter %= 5;
    return HandlerResult::complete;
}
}
