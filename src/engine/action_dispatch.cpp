#include "sts2/engine/action.hpp"
#include "run/run_flow.hpp"
#include "transitions.hpp"
#include "defensive.hpp"
#include <algorithm>

namespace sts2::engine {
namespace {
bool needs_automatic_transition(const RunState& state) {
    return state.data.phase == RunPhase::neow_done ||
        (state.data.phase == RunPhase::combat_done && !state.data.direct_combat) ||
        (state.data.phase == RunPhase::rewards && state.data.rewards && state.data.rewards->resolved);
}
bool is_canonical(const RunInput& input) {
    if (input.kind > RunInputKind::discard_potion) return false;
    if (input.kind == RunInputKind::travel) return input.option == 0;
    if (input.col != 0 || input.row != 0) return false;
    if (input.kind == RunInputKind::skip_rewards || input.kind == RunInputKind::proceed || input.kind == RunInputKind::abandon)
        return input.option == 0;
    return true;
}
StepResult submit_action(RunState& state, const PlayerInput& input, Scratch& scratch) {
    if (!valid_creature_storage(state.data, state.overflow) || !flow::valid_room_context(state)) return {StepStatus::engine_error, state.data.boundary, Restriction::invalid_state};
    if (const auto* run = std::get_if<RunInput>(&input)) {
        if (run->expected_boundary != state.data.boundary) return {StepStatus::invalid_action, state.data.boundary, Restriction::stale_boundary};
        if (!is_canonical(*run)) return {StepStatus::invalid_action, state.data.boundary, Restriction::malformed_action};
        if (run->kind == RunInputKind::proceed || needs_automatic_transition(state)) return {StepStatus::invalid_action, state.data.boundary, Restriction::wrong_phase};
    }
    return apply_input_checked_state(state, input, scratch);
}
}
std::size_t ActionSpaceView::size() const noexcept {
    if (!state_.valid_combat_storage() || state_.combat().enemy_side) return 0;
    if (state_.combat().phase == CombatPhase::play) {
        std::size_t targets = 0, count = 1;
        for (auto enemy : state_.enemies()) targets += state_.creature(enemy)->hp > 0;
        for (auto card : state_.zone(Zone::hand)) {
            const auto* instance = state_.cards().get(card);
            const auto* def = instance ? Ruleset::pinned().card(instance->definition) : nullptr;
            count += def && def->enemy_target ? targets : 1;
        }
        return count;
    }
    if (state_.combat().phase == CombatPhase::discard_choice) return state_.zone(Zone::hand).size();
    if (state_.combat().phase == CombatPhase::card_choice) return state_.selection_candidates().size() + !state_.combat().selection.automatic_at_maximum;
    return 0;
}
std::optional<ActionView> ActionSpaceView::at(std::size_t index) const noexcept {
    if (index >= size()) return std::nullopt;
    if (state_.combat().phase == CombatPhase::card_choice) {
        CombatInput input{state_.data.boundary, CombatInputKind::confirm_selection, {}, {}, state_.combat().selection.id};
        if (index < state_.selection_candidates().size()) {
            input.card = state_.selection_candidates()[index];
            input.kind = std::find(state_.selection_order().begin(), state_.selection_order().end(), input.card) == state_.selection_order().end() ? CombatInputKind::select_card : CombatInputKind::deselect_card;
        }
        return ActionView{input, validate_combat(state_, input)};
    }
    const auto hand = state_.zone(Zone::hand);
    if (state_.combat().phase == CombatPhase::discard_choice) {
        CombatInput input{state_.data.boundary, CombatInputKind::select_card, hand[index]};
        return ActionView{input, validate_combat(state_, input)};
    }
    for (auto card : hand) {
        const auto* instance = state_.cards().get(card);
        const auto* def = instance ? Ruleset::pinned().card(instance->definition) : nullptr;
        if (def && def->enemy_target) {
            for (auto target : state_.enemies()) {
                if (state_.creature(target)->hp <= 0) continue;
                if (index-- == 0) {
                    CombatInput input{state_.data.boundary, CombatInputKind::play_card, card, target};
                    return ActionView{input, validate_combat(state_, input)};
                }
            }
        } else if (index-- == 0) {
            CombatInput input{state_.data.boundary, CombatInputKind::play_card, card};
            return ActionView{input, validate_combat(state_, input)};
        }
    }
    CombatInput input{state_.data.boundary, CombatInputKind::end_turn};
    return ActionView{input, validate_combat(state_, input)};
}

namespace {
Restriction validate_visible_input(const RunState& state_, const PlayerInput& input) noexcept {
    if (const auto* run = std::get_if<RunInput>(&input)) {
        if (run->expected_boundary != state_.data.boundary) return Restriction::stale_boundary;
        if (!is_canonical(*run)) return Restriction::malformed_action;
        if (run->kind == RunInputKind::proceed || needs_automatic_transition(state_)) return Restriction::wrong_phase;
        flow::RunActionPlan plan;
        const auto checked = flow::plan_run_input(state_, *run, plan, flow::ValidationMode::visible);
        if (checked.reason != Restriction::none) return checked.reason;
        // Travel previews never expose hidden room capability or operational work.
        if (run->kind == RunInputKind::travel) return Restriction::none;
    } else {
        if (state_.data.phase != RunPhase::combat || !state_.data.combat) return Restriction::wrong_phase;
        const auto restriction = validate_combat(state_, std::get<CombatInput>(input));
        if (restriction != Restriction::none) return restriction;
    }
    if (state_.data.boundary.value == UINT32_MAX) return Restriction::capacity;
    auto clock = state_.data.clock;
    return clock.advance_action() ? Restriction::none : Restriction::clock_exhausted;
}
}
Restriction validate_action(const RunState& state, const PlayerInput& input) noexcept {
    if (!valid_creature_storage(state.data, state.overflow)) return Restriction::invalid_state;
    if (state.data.phase != RunPhase::uninitialized && state.data.phase != RunPhase::invalid && !valid_run(state)) return Restriction::invalid_state;
    return validate_visible_input(state, input);
}
RunActionSpaceView::RunActionSpaceView(const RunState& state) noexcept
    : state_(state), boundary_(state.data.boundary), valid_(valid_run(state)) {}
std::size_t RunActionSpaceView::size() const noexcept {
    if (!valid_ || boundary_ != state_.data.boundary) return 0;
    std::size_t count = 0;
    if (state_.data.phase == RunPhase::room) count = 2;
    else if (state_.data.phase == RunPhase::neow) count = state_.data.neow_options.size();
    else if (state_.data.phase == RunPhase::map) count = state_.data.maps[state_.data.act_index]->nodes[state_.data.current].children.values().size();
    else if (state_.data.phase == RunPhase::combat && state_.data.combat) return ActionSpaceView(state_).size();
    else if (state_.data.phase == RunPhase::rewards && state_.data.rewards && !state_.data.rewards->resolved) {
        const auto& rewards = *state_.data.rewards;
        if (rewards.selecting_card) count = rewards.cards.size() + 1;
        else count = !rewards.gold_taken + (rewards.potion != no_content && !rewards.potion_taken) + !rewards.card_taken + 1;
    }
    if ((state_.data.phase == RunPhase::map || state_.data.phase == RunPhase::rewards) && !(state_.data.rewards && state_.data.rewards->selecting_card))
        for (auto potion : state_.potions()) count += potion != no_content;
    return count;
}
std::optional<PlayerAction> RunActionSpaceView::at(std::size_t index) const noexcept {
    if (index >= size()) return std::nullopt;
    if (state_.data.phase == RunPhase::combat && state_.data.combat) {
        const auto action = ActionSpaceView(state_).at(index);
        if (!action) return std::nullopt;
        return PlayerAction{action->input, action->restriction};
    }
    std::optional<PlayerAction> result;
    const auto offer = [&](RunInputKind kind, std::uint32_t option = 0, int col = 0, int row = 0) {
        if (result) return;
        if (index-- != 0) return;
        PlayerInput input = RunInput{state_.data.boundary, kind, option, col, row};
        result = PlayerAction{input, validate_visible_input(state_, input)};
    };
    if (state_.data.phase == RunPhase::room) {
        offer(RunInputKind::choose_option, 0); offer(RunInputKind::choose_option, 1);
    } else if (state_.data.phase == RunPhase::neow) {
        for (std::uint32_t i = 0; i < state_.data.neow_options.size(); ++i) offer(RunInputKind::choose_option, i);
    } else if (state_.data.phase == RunPhase::map) {
        const auto& map = *state_.data.maps[state_.data.act_index];
        for (auto id : map.nodes[state_.data.current].children.values()) offer(RunInputKind::travel, 0, map.nodes[id].col, map.nodes[id].row);
    } else if (state_.data.phase == RunPhase::rewards && state_.data.rewards && !state_.data.rewards->resolved) {
        const auto& rewards = *state_.data.rewards;
        if (rewards.selecting_card) {
            for (std::uint32_t i = 0; i < rewards.cards.size(); ++i) offer(RunInputKind::choose_reward_card, i);
            offer(RunInputKind::reward_alternative);
        } else {
            if (!rewards.gold_taken) offer(RunInputKind::take_reward, 0);
            if (rewards.potion != no_content && !rewards.potion_taken) offer(RunInputKind::take_reward, 1);
            if (!rewards.card_taken) offer(RunInputKind::take_reward, rewards.potion == no_content ? 1 : 2);
            offer(RunInputKind::skip_rewards);
        }
    }
    if ((state_.data.phase == RunPhase::map || state_.data.phase == RunPhase::rewards) && !(state_.data.rewards && state_.data.rewards->selecting_card))
        for (std::uint32_t i = 0; i < state_.potions().size(); ++i) if (state_.potions()[i] != no_content) offer(RunInputKind::discard_potion, i);
    return result;
}
std::vector<PlayerAction> get_actions(const RunState& state) {
    const RunActionSpaceView offered(state);
    std::vector<PlayerAction> actions; actions.reserve(offered.size());
    for (std::size_t i = 0; i < offered.size(); ++i) actions.push_back(*offered.at(i));
    return actions;
}
namespace {
StepResult advance_decision(RunState& state, Scratch& scratch, bool trusted) {
    while (needs_automatic_transition(state)) {
        const RunInput input{state.data.boundary, RunInputKind::proceed};
        const auto result = trusted ? apply_input_trusted(state, input, scratch) : apply_automatic_run_input(state, input, scratch);
        if (result.status != StepStatus::awaiting_input) {
            // The initiating player input has already executed. A missing
            // automatic capability cannot be published as a valid partial stop.
            state.data.phase = RunPhase::invalid;
            return {StepStatus::engine_error, state.data.boundary, result.reason};
        }
    }
    if (state.data.phase == RunPhase::invalid || state.data.phase == RunPhase::uninitialized)
        return {StepStatus::engine_error, state.data.boundary, Restriction::invalid_state};
    STS2_CHECK_INVARIANT(valid_run(state));
    if (state.data.phase == RunPhase::complete) return {StepStatus::player_died, state.data.boundary};
    if (state.data.phase == RunPhase::combat_done) return {StepStatus::combat_won, state.data.boundary};
    return {StepStatus::awaiting_input, state.data.boundary};
}
} // namespace
StepResult advance_to_decision(RunState& state, Scratch& scratch) {
    if (!valid_creature_storage(state.data, state.overflow) || !flow::valid_room_context(state)) return {StepStatus::engine_error, state.data.boundary, Restriction::invalid_state};
    return advance_decision(state, scratch, false);
}
StepResult apply_action_trusted(RunState& state, const PlayerInput& input, Scratch& scratch) {
    STS2_CHECK_INVARIANT(valid_run(state));
    const auto result = apply_input_trusted(state, input, scratch);
    if (result.status != StepStatus::awaiting_input && result.status != StepStatus::combat_won) return result;
    return advance_decision(state, scratch, true);
}
StepResult apply_action(RunState& state, const PlayerInput& input, Scratch& scratch) {
    const auto result = submit_action(state, input, scratch);
    if (result.status != StepStatus::awaiting_input && result.status != StepStatus::combat_won) return result;
    return advance_decision(state, scratch, false);
}
} // namespace sts2::engine
