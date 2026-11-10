#include "relics.hpp"
#include "../mechanics/damage.hpp"
#include "../mechanics/cards.hpp"
#include "../hooks/dispatch.hpp"
#include "../combat/lifecycle.hpp"
#include "../combat/trace.hpp"
#include <stdexcept>

namespace sts2::engine::gameplay {
void gremlin_horn_after_death(RunState& state, CreatureHandle dead) {
    // Native Gremlin Horn reacts only to an opposing-side death.
    if (dead == state.data.player) return;
    if (state.combat().energy == INT32_MAX) throw std::length_error("Energy capacity exhausted");
    ++state.combat().energy;
    if (!state.has_living_enemies() || state.player().hp == 0) return;
    // No work follows the native awaited draw. This child owns the complete
    // unfinished listener; the continuing death caller stores no handler stage.
    state.execution().push_back(DrawFrame{.remaining = 1, .from_hand_draw = false});
}
HandlerResult centennial_puzzle_after_damage_received(RunState& state, HandlerProgress& progress, const HitResult& hit) {
    if (progress.stage == 0) {
        if (hit.unblocked <= 0 || state.player().hp == 0 || state.combat().centennial_puzzle_used)
            return HandlerResult::complete;
        state.combat().centennial_puzzle_used = true;
    }
    if (progress.stage == 3) return HandlerResult::complete;
    // Native Puzzle awaits three separate one-card draws, not one batch draw.
    ++progress.stage;
    state.execution().push_back(DrawFrame{.remaining = 1, .from_hand_draw = false});
    return HandlerResult::yield;
}
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
