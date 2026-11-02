#include "sts2/engine/combat.hpp"
#include "validation.hpp"
#include "lifecycle.hpp"
#include "trace.hpp"
#include "../execution/interpreter.hpp"
#include "../execution/selection.hpp"
#include "../mechanics/cards.hpp"
#include "../mechanics/energy.hpp"
#include "../mechanics/powers.hpp"
#include "../content/monsters.hpp"
#include "../hooks/dispatch.hpp"
#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>

namespace sts2::engine {
using namespace gameplay;
namespace {
StepResult end_player_turn(RunState& state, Scratch& scratch, const Ruleset& rules) {
    if (!consume(scratch)) return fail(state, Restriction::execution_budget);
    discard_hand_at_turn_end(state, rules);
    state.combat().enemy_side = true;
    // Reacquire positions each time: creation may grow the pool, and membership
    // or generations can change during a synchronous effect/diagnostic callback.
    for (std::uint32_t index = 1; index < state.creatures().slots().size(); ++index) {
        const auto& slot = state.creatures().slots()[index];
        const CreatureHandle handle{index, slot.generation};
        if (!slot.live || !slot.value.enemy_member || slot.value.hp <= 0 || slot.value.skip_current_enemy_turn) continue;
        if (!consume(scratch)) return fail(state, Restriction::execution_budget);
        state.creature(handle)->block = 0;
        snapshot_power_turn(state, scratch, *state.creature(handle), true);
        if (state.is_enemy(handle) && state.creature(handle)->hp > 0) execute_enemy_move(state, scratch, handle);
        if (state.player().hp == 0 || !state.has_living_enemies()) break;
    }
    if (state.player().hp > 0 && state.has_living_enemies()) after_enemy_turn(state, scratch);
    // Side-end work can create creatures too; clear flags only after it finishes.
    for (auto& slot : state.creatures().slots()) if (slot.live) slot.value.skip_current_enemy_turn = false;
    state.combat().enemy_side = false;
    if (state.player().hp == 0) end_combat(state, scratch, false);
    else if (!state.has_living_enemies()) end_combat(state, scratch, true);
    else {
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

} // namespace
StepResult advance_combat(RunState& state, Scratch& scratch, const Ruleset& rules) noexcept try {
    if (!state.valid_combat_storage() || !valid_powers(state) || !valid_card_extra(state) || !valid_card_modifiers(state)) return fail(state, Restriction::invalid_state);
    for (const auto& slot : state.cards().slots())
        if (slot.live && !supported_card_modifiers(slot.value)) return {StepStatus::unsupported_content, state.data.boundary, Restriction::unsupported_setup};
    if (state.combat().phase != CombatPhase::play || state.execution().empty()) return result(state);
    while (!state.execution().empty()) {
        const auto step = resume_frame(state, scratch, rules);
        if (step.status == StepStatus::engine_error || step.status == StepStatus::resource_limit || state.combat().phase != CombatPhase::play) return step;
    }
    if (!state.has_living_enemies() && state.combat().phase == CombatPhase::play) end_combat(state, scratch, true);
    return result(state);
} catch (const std::bad_alloc&) { state.combat().phase = CombatPhase::invalid; return {StepStatus::resource_limit, state.data.boundary, Restriction::capacity}; } catch (const std::length_error&) { state.combat().phase = CombatPhase::invalid; return {StepStatus::resource_limit, state.data.boundary, Restriction::capacity}; }
catch (...) { return fail(state, Restriction::invalid_state); }
StepResult apply_combat(RunState& state, const CombatInput& input, Scratch& scratch, const Ruleset& rules) noexcept try {
    const auto restriction = validate_combat(state, input, rules);
    if (restriction != Restriction::none) return result(state, restriction);
    if (state.data.boundary.value == std::numeric_limits<std::uint32_t>::max()) return fail(state, Restriction::capacity);
    if (!state.data.clock.advance_action()) return fail(state, Restriction::clock_exhausted);
    scratch.steps = 0;
    if (state.combat().phase == CombatPhase::card_choice) {
        const auto selected = apply_card_selection(state, input, scratch, rules);
        if (selected.status == StepStatus::engine_error || selected.status == StepStatus::resource_limit) return selected;
    } else if (input.kind == CombatInputKind::end_turn) {
        const auto ended = end_player_turn(state, scratch, rules);
        if (ended.status == StepStatus::engine_error || ended.status == StepStatus::resource_limit) return ended;
    } else if (input.kind == CombatInputKind::play_card) {
        const auto* card = state.cards().get(input.card);
        const auto cost = effective_card_cost(state, input.card, rules);
        spend_energy(state, cost);
        move_card(state, Zone::hand, Zone::play, input.card);
        if (state.combat().counters.started_plays == UINT32_MAX) return fail(state, Restriction::capacity);
        const auto play_id = state.combat().counters.started_plays;
        state.execution().push_back(CardProgramFrame{input.card, 0, play_id, cost, input.target.present() ? input.target : state.data.player});
        if (cost > 0) emit(state, scratch, CombatEventKind::energy_spent, false, false, {}, 0, cost);
        ++state.combat().counters.started_plays; ++state.combat().counters.started_this_turn;
        if (rules.card(card->definition)->kind == CardKind::attack) {
            ++state.combat().counters.attacks_this_turn;
            if (cost == 0) ++state.combat().counters.zero_energy_attacks_this_turn;
        }
        emit(state, scratch, CombatEventKind::play_started, false, false, input.card, play_id);
        const auto advanced = advance_combat(state, scratch, rules);
        if (advanced.status == StepStatus::engine_error || advanced.status == StepStatus::resource_limit) return advanced;
    } else {
        discard_card(state, scratch, input.card);
        state.combat().phase = CombatPhase::play;
        const auto advanced = advance_combat(state, scratch, rules);
        if (advanced.status == StepStatus::engine_error || advanced.status == StepStatus::resource_limit) return advanced;
    }
    if (state.combat().phase == CombatPhase::play && !state.execution().empty()) {
        const auto advanced = advance_combat(state, scratch, rules);
        if (advanced.status == StepStatus::engine_error || advanced.status == StepStatus::resource_limit) return advanced;
    }
    if (state.combat().phase == CombatPhase::invalid) return {StepStatus::engine_error, state.data.boundary, Restriction::execution_budget};
    ++state.data.boundary.value;
    emit(state, scratch, CombatEventKind::combat_updated);
    return result(state);
} catch (const std::bad_alloc&) {
    state.combat().phase = CombatPhase::invalid;
    return {StepStatus::resource_limit, state.data.boundary, Restriction::capacity};
} catch (const std::length_error&) {
    state.combat().phase = CombatPhase::invalid;
    return {StepStatus::resource_limit, state.data.boundary, Restriction::capacity};
} catch (...) { return fail(state, Restriction::invalid_state); }
StepResult apply_power_scenario(RunState& state, const PowerApplication& operation, Scratch& scratch) noexcept {
    if (!valid_combat(state)) return result(state, Restriction::invalid_state);
    if (state.combat().phase != CombatPhase::play || !state.execution().empty()) return result(state, Restriction::wrong_phase);
    if (operation.definition >= PowerId::count || operation.amount < -999999999 || operation.amount > 999999999 ||
        (operation.source.present() && !state.cards().get(operation.source))) return result(state, Restriction::malformed_action);
    if ((operation.enemy_owner && !operation.owner.present() && state.enemies().size() != 1) ||
        (operation.applier_enemy && operation.has_applier && !operation.applier.present() && state.enemies().size() != 1)) return result(state, Restriction::invalid_target);
    if ((operation.owner.present() && (!state.creature(operation.owner) || (operation.enemy_owner != (operation.owner != state.data.player)))) ||
        (operation.applier.present() && (!operation.has_applier || !state.creature(operation.applier)))) return result(state, Restriction::invalid_target);
    try {
        (void)apply_power(state, scratch, operation.definition, operation.amount, operation.enemy_owner, false,
            operation.source, operation.has_applier, operation.applier_enemy, PowerInstance::Stacking::by_type, operation.applier, operation.owner);
        return result(state);
    } catch (...) { return fail(state, Restriction::capacity); }
}
}
