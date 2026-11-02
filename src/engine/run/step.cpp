#include "sts2/engine/run.hpp"
#include "sts2/engine/rewards.hpp"
#include "run_flow.hpp"
#include <algorithm>
#include <limits>
#include <new>

namespace sts2::engine {
using namespace flow;
StepResult enter_combat_scenario(RunState& state, BoundaryId boundary, std::string_view encounter, CombatTrace trace) {
    if (!valid_run(state)) return {StepStatus::engine_error, state.data.boundary, Restriction::invalid_state};
    if (boundary != state.data.boundary) return {StepStatus::invalid_action, state.data.boundary, Restriction::stale_boundary};
    if (state.data.phase != RunPhase::map || state.data.combat_generation == 0 || (state.data.rewards && !state.data.rewards->resolved))
        return {StepStatus::invalid_action, state.data.boundary, Restriction::wrong_phase};
    if (encounter != "ENCOUNTER.SHRINKER_BEETLE_WEAK" || state.data.ascension != 0 ||
        SetupRules::name(state.data.character) != "CHARACTER.IRONCLAD")
        return {StepStatus::unsupported_content, state.data.boundary, Restriction::unsupported_setup};
    if (!supported_combat_inventory(state))
        return {StepStatus::unsupported_content, state.data.boundary, Restriction::unsupported_setup};
    if (next_combat_generation(state) == 0 || state.act(state.data.act_index).normal_visited >= state.act(state.data.act_index).normal.size()) return {StepStatus::resource_limit, state.data.boundary, Restriction::capacity};
    try {
        RunState next = state;
        const auto generation = next_combat_generation(next);
        Scratch scratch; scratch.trace = trace;
        const auto result = prepare_combat(next, scratch, state.data.boundary);
        if (result.status != StepStatus::awaiting_input) return result;
        next.data.rewards.reset();
        next.data.combat_generation = generation;
        // Scenario entry is not a player action and does not consume the clock.
        trace.emit(next, {CombatEventKind::combat_updated});
        ++next.act(next.data.act_index).normal_visited;
        next.data.phase = RunPhase::combat; next.data.direct_combat = false;
        state = std::move(next);
        return {StepStatus::awaiting_input, state.data.boundary};
    } catch (const std::bad_alloc&) { return {StepStatus::resource_limit, state.data.boundary, Restriction::capacity}; } catch (const std::length_error&) { return {StepStatus::resource_limit, state.data.boundary, Restriction::capacity}; }
}
namespace {
StepResult apply_combat_transition(RunState& state, const CombatInput& input, Scratch& scratch) {
    if (state.data.phase != RunPhase::combat || !state.data.combat)
        return {StepStatus::invalid_action, state.data.boundary, Restriction::wrong_phase};
    const auto result = apply_combat(state, input, scratch);
    if (result.status == StepStatus::invalid_action || result.status == StepStatus::unsupported_content) return result;
    if (result.status == StepStatus::combat_won) state.data.phase = RunPhase::combat_done;
    else if (result.status == StepStatus::player_died) state.data.phase = RunPhase::complete;
    else if (result.status == StepStatus::engine_error || result.status == StepStatus::resource_limit) state.data.phase = RunPhase::invalid;
    if (!state.data.direct_combat && (result.status == StepStatus::combat_won || result.status == StepStatus::player_died)) {
        child_combat_finished(state, result.status == StepStatus::combat_won);
        state.data.combat_generation = highest_combat_generation(state);
        state.reset_combat();
        auto* player = state.creatures().get(state.data.player);
        const auto hp = player->hp, max_hp = player->max_hp;
        player->clear(); player->hp = hp; player->max_hp = max_hp;
    }
    return result;
}
StepResult apply_run_transition(RunState& state, const RunInput& input, Scratch& scratch) {
    RunActionPlan plan;
    const auto checked = plan_run_input(state, input, plan, ValidationMode::execution);
    if (checked.status != StepStatus::awaiting_input) return checked;
    const auto entering_generation = plan.entering_generation;
    const auto generate_rewards = plan.generate_rewards;
    const auto destination = plan.destination;
    const auto reward_kind = plan.reward_kind;
    std::optional<RunState> entering_combat;
    if (destination != no_node) {
        try {
            entering_combat.emplace(state);
            const auto initialized = prepare_combat(*entering_combat, scratch, state.data.boundary);
            if (initialized.status != StepStatus::awaiting_input) return initialized;
        } catch (const std::bad_alloc&) { return {StepStatus::resource_limit, state.data.boundary, Restriction::capacity}; }
          catch (const std::length_error&) { return {StepStatus::resource_limit, state.data.boundary, Restriction::capacity}; }
    }
    if (state.data.boundary.value == UINT32_MAX) { state.data.phase = RunPhase::invalid; return {StepStatus::resource_limit, state.data.boundary, Restriction::capacity}; }
    try {
        if (entering_combat) state = std::move(*entering_combat);
        // All validation precedes mutation. Prepare owning growth before time/payment.
        if (input.kind == RunInputKind::choose_option && state.data.phase == RunPhase::neow) state.relics().reserve(state.relics().size() + 1);
        if (entering_combat) {
            state.visited().reserve(state.visited().size() + 1);
        }
        if (input.kind == RunInputKind::choose_reward_card) state.deck().reserve(state.deck().size() + 1);
        if (input.kind == RunInputKind::take_reward) state.potions().reserve(static_cast<std::size_t>(state.data.potion_slots));
        // Preflight time before reward generation mutates run RNG/odds. Generation
        // itself stages all fallible work locally and commits only on success.
        auto clock = state.data.clock;
        if (!clock.advance_action()) { state.data.phase = RunPhase::invalid; return {StepStatus::engine_error, state.data.boundary, Restriction::clock_exhausted}; }
        if (generate_rewards) {
            const auto generated = generate_combat_rewards(state);
            if (generated.status != StepStatus::awaiting_input)
                return {generated.status, state.data.boundary, generated.reason};
        }
        state.data.clock = clock;
        if (reward_kind) {
            apply_reward_input(state, input, plan);
        } else if (input.kind == RunInputKind::discard_potion) {
            state.potions()[input.option] = no_content;
        } else if (input.kind == RunInputKind::choose_option) {
            apply_room_option(state, input);
        } else if (generate_rewards) {
            state.data.phase = RunPhase::rewards;
        } else if (entering_combat) {
            state.data.rewards.reset();
            state.data.combat_generation = entering_generation;
            state.data.current = destination; state.visited().push_back(destination);
            ++state.act(state.data.act_index).normal_visited;
            state.data.phase = RunPhase::combat;
        } else if (!return_from_room(state)) {
            state.data.phase = RunPhase::invalid;
            return {StepStatus::engine_error, state.data.boundary, Restriction::invalid_state};
        }
        ++state.data.boundary.value;
        if (state.data.phase == RunPhase::combat) {
            scratch.trace.emit(state, {CombatEventKind::combat_updated});
        }
        return {StepStatus::awaiting_input, state.data.boundary};
    } catch (const std::bad_alloc&) { state.data.phase = RunPhase::invalid; return {StepStatus::resource_limit, state.data.boundary, Restriction::capacity}; } catch (const std::length_error&) { state.data.phase = RunPhase::invalid; return {StepStatus::resource_limit, state.data.boundary, Restriction::capacity}; }
}
} // namespace
StepResult apply_input(RunState& state, const PlayerInput& input, Scratch& scratch) {
    if (!valid_creature_storage(state.data, state.overflow) || !valid_room_context(state)) return {StepStatus::engine_error, state.data.boundary, Restriction::invalid_state};
    if (const auto* run = std::get_if<RunInput>(&input)) return apply_run_transition(state, *run, scratch);
    return apply_combat_transition(state, std::get<CombatInput>(input), scratch);
}
} // namespace sts2::engine
