#include "lifecycle.hpp"
#include "trace.hpp"
#include "../execution/selection.hpp"
#include "../mechanics/damage.hpp"
#include <cstring>
#include <string_view>
#include <type_traits>

namespace sts2::engine::gameplay {
StepResult result(const RunState& state, Restriction reason) noexcept {
    if (reason != Restriction::none) return {StepStatus::invalid_action, state.data.boundary, reason};
    switch (state.combat().phase) {
    case CombatPhase::combat_won: return {StepStatus::combat_won, state.data.boundary};
    case CombatPhase::player_died: return {StepStatus::player_died, state.data.boundary};
    case CombatPhase::uninitialized: case CombatPhase::invalid: return {StepStatus::engine_error, state.data.boundary, Restriction::invalid_state};
    default: return {StepStatus::awaiting_input, state.data.boundary};
    }
}
StepResult fail(RunState& state, Restriction reason) noexcept {
    state.combat().phase = CombatPhase::invalid; state.data.phase = RunPhase::invalid;
    return {StepStatus::engine_error, state.data.boundary, reason};
}
bool consume(Scratch& scratch) noexcept { return ++scratch.steps <= scratch.step_limit; }
void end_combat(RunState& state, Scratch& scratch, bool won) {
    state.combat().phase = won ? CombatPhase::combat_won : CombatPhase::player_died;
    state.data.phase = won ? RunPhase::combat_done : RunPhase::complete;
    state.execution().clear(); state.cost_modifiers().clear();
    state.continuation_cards().clear(); state.captured_hook_listeners().clear(); clear_selection(state);
    if (won) {
        state.player().clear_powers(); state.enemy().clear_powers();
        for (auto& zone : state.zone_views()) zone.clear();
    } else {
        for (auto& power : state.player().power_instances(state.overflow)) {
            power.active = false; emit(state, scratch, CombatEventKind::power_changed, false, false, {}, power.instance);
        }
        state.player().clear_powers();
    }
    emit(state, scratch, won ? CombatEventKind::combat_won : CombatEventKind::combat_lost);
    state.player().block = 0;
    if (won && state.combat().burning_blood) {
        (void)heal(state.player(), 6);
    }
    if (won) {
        // Terminal combat has no suspended work or visible piles. Preserve all
        // scalars/identity progress, but release its completed collection graph.
        auto terminal = state.combat();
        OverflowStorage owner(state.combat_overflow.allocator());
        terminal.for_each_sequence([](auto& sequence) { sequence = std::remove_reference_t<decltype(sequence)>{}; });
        state.combat() = terminal;
        state.combat_overflow = std::move(owner);
    }
}
}
