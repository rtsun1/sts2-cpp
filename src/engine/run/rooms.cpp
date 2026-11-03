#include "run_flow.hpp"
#include "../content/encounters.hpp"
#include "sts2/engine/action_rules.hpp"
#include <algorithm>
#include <climits>

namespace sts2::engine::flow {
std::uint32_t highest_combat_generation(const RunState& state) noexcept {
    auto highest = state.data.combat_generation;
    if (state.data.combat) highest = std::max(highest, state.cards().data.highest_generation);
    return highest;
}
std::uint32_t next_combat_generation(const RunState& state) noexcept {
    const auto highest = highest_combat_generation(state);
    return highest == UINT32_MAX ? 0 : highest + 1;
}
bool supported_combat_inventory(const RunState& state) {
    for (auto potion : state.potions())
        if (potion != no_content && SetupRules::name(potion) != "POTION.POTION_OF_BINDING") return false;
    // Inventory hooks are supported only for the starting relic and Golden Pearl.
    for (auto relic : state.relics())
        if (SetupRules::name(relic) != "RELIC.BURNING_BLOOD" && SetupRules::name(relic) != "RELIC.GOLDEN_PEARL" &&
            SetupRules::name(relic) != "RELIC.CHARONS_ASHES" && SetupRules::name(relic) != "RELIC.JOSS_PAPER") return false;
    return true;
}
Restriction legal_run_input(const RunState& state, const RunInput& input) noexcept {
    if (input.expected_boundary != state.data.boundary) return Restriction::stale_boundary;
    const bool reward = input.kind == RunInputKind::take_reward || input.kind == RunInputKind::skip_rewards ||
        input.kind == RunInputKind::choose_reward_card || input.kind == RunInputKind::reward_alternative;
    if (reward) {
        if (state.data.phase != RunPhase::rewards || !state.data.rewards || state.data.rewards->resolved) return Restriction::wrong_phase;
        const auto& rewards = *state.data.rewards;
        if (rewards.selecting_card) {
            if (input.kind == RunInputKind::choose_reward_card) return input.option < rewards.cards.size() ? Restriction::none : Restriction::invalid_selection;
            return input.kind == RunInputKind::reward_alternative && input.option == 0 ? Restriction::none : Restriction::invalid_selection;
        }
        if (input.kind == RunInputKind::take_reward) {
            if (!action_rules::reward_available(rewards, input.option)) return Restriction::invalid_selection;
            return Restriction::none;
        }
        return input.kind == RunInputKind::skip_rewards ? Restriction::none : Restriction::wrong_phase;
    }
    switch (input.kind) {
    case RunInputKind::discard_potion:
        if (const auto restriction = action_rules::potion_discard_phase_restriction(state); restriction != Restriction::none) return restriction;
        return action_rules::potion_available(state, input.option) ? Restriction::none : Restriction::invalid_selection;
    case RunInputKind::choose_option:
        if (state.data.phase == RunPhase::room) return input.option < 2 ? Restriction::none : Restriction::invalid_selection;
        if (state.data.phase != RunPhase::neow) return Restriction::wrong_phase;
        return input.option < state.data.neow_options.size() ? Restriction::none : Restriction::invalid_selection;
    case RunInputKind::proceed:
        return (state.data.phase == RunPhase::combat_done && !state.data.direct_combat) || state.data.phase == RunPhase::neow_done ||
            (state.data.phase == RunPhase::rewards && state.data.rewards && state.data.rewards->resolved) ? Restriction::none : Restriction::wrong_phase;
    case RunInputKind::travel: {
        if (state.data.phase != RunPhase::map) return Restriction::wrong_phase;
        const auto& map = *state.data.maps[state.data.act_index]; const auto next = map.at(input.col, input.row);
        return next != no_node && map.nodes[state.data.current].children.contains(next) ? Restriction::none : Restriction::invalid_selection;
    }
    default: return Restriction::unsupported_setup;
    }
}
// Legal inputs still need execution operands, capability and resource checks.
namespace {
template<bool CheckSupport>
StepResult prepare_run_input_impl(const RunState& state, const RunInput& input, RunActionPlan& plan, ValidationMode mode) {
    const auto reject = [&](StepStatus status, Restriction reason) { return StepResult{status, state.data.boundary, reason}; };
    plan.potion_slot = state.potions().size();
    plan.reward_kind = input.kind == RunInputKind::take_reward || input.kind == RunInputKind::skip_rewards ||
        input.kind == RunInputKind::choose_reward_card || input.kind == RunInputKind::reward_alternative;
    if (input.kind == RunInputKind::choose_reward_card) {
        if constexpr (CheckSupport) {
            const auto id = SetupRules::name(state.data.rewards->cards[input.option].definition);
            if (id != "CARD.TRUE_GRIT" && id != "CARD.JUGGLING" && id != "CARD.RAGE") return reject(StepStatus::unsupported_content, Restriction::unsupported_setup);
        }
        if (state.deck().size() >= 4096) return reject(StepStatus::resource_limit, Restriction::capacity);
        for (const auto& card : state.deck()) {
            if (card.instance == UINT32_MAX) return reject(StepStatus::resource_limit, Restriction::capacity);
            plan.next_card_instance = std::max(plan.next_card_instance, card.instance + 1);
        }
    } else if (input.kind == RunInputKind::take_reward) {
        const auto& rewards = *state.data.rewards;
        if (input.option == 0 && state.data.gold > INT32_MAX - rewards.gold) return reject(StepStatus::resource_limit, Restriction::capacity);
        if (input.option == 1 && rewards.potion != no_content)
            for (std::size_t i = 0; i < state.potions().size(); ++i) if (state.potions()[i] == no_content) { plan.potion_slot = i; break; }
    } else if (input.kind == RunInputKind::choose_option) {
        if (state.data.phase == RunPhase::room) {
            const auto gold = std::get<ParentInteractionState>(state.data.room).pending_gold;
            if (input.option == 0 && state.data.gold > INT32_MAX - gold) return reject(StepStatus::resource_limit, Restriction::capacity);
        } else {
            if (state.data.gold > INT32_MAX - 150) return reject(StepStatus::resource_limit, Restriction::capacity);
            if constexpr (CheckSupport) {
                if (SetupRules::name(state.data.neow_options[input.option]) != "RELIC.GOLDEN_PEARL") return reject(StepStatus::unsupported_content, Restriction::unsupported_setup);
            }
        }
    } else if (input.kind == RunInputKind::proceed) {
        plan.generate_rewards = state.data.phase == RunPhase::combat_done && !state.data.direct_combat;
    } else if (input.kind == RunInputKind::travel) {
        const auto& map = *state.data.maps[state.data.act_index]; const auto next = map.at(input.col, input.row);
        plan.destination = next;
        if (mode == ValidationMode::visible) return reject(StepStatus::awaiting_input, Restriction::none);
        if constexpr (CheckSupport) {
            if (map.nodes[next].kind != MapKind::monster || state.data.ascension != 0 || SetupRules::name(state.data.character) != "CHARACTER.IRONCLAD")
                return reject(StepStatus::unsupported_content, Restriction::unsupported_setup);
        }
        plan.entering_generation = next_combat_generation(state);
        if (plan.entering_generation == 0) return reject(StepStatus::resource_limit, Restriction::capacity);
        const auto rooms = state.act(state.data.act_index);
        // Queue bounds remain operational even when content support is assumed.
        if (rooms.normal_visited >= rooms.normal.size())
            return reject(StepStatus::unsupported_content, Restriction::unsupported_setup);
        if constexpr (CheckSupport) {
            if (!gameplay::supported_encounter(SetupRules::name(rooms.normal[rooms.normal_visited])) || !supported_combat_inventory(state))
                return reject(StepStatus::unsupported_content, Restriction::unsupported_setup);
        }
    }
    return reject(StepStatus::awaiting_input, Restriction::none);
}
} // namespace
StepResult prepare_run_input(const RunState& state, const RunInput& input, RunActionPlan& plan, ValidationMode mode) {
    return prepare_run_input_impl<true>(state, input, plan, mode);
}
StepResult prepare_run_input_trusted(const RunState& state, const RunInput& input, RunActionPlan& plan) {
    return prepare_run_input_impl<STS2_DEFENSIVE_VALIDATION>(state, input, plan, ValidationMode::execution);
}
StepResult plan_run_input(const RunState& state, const RunInput& input, RunActionPlan& plan, ValidationMode mode) {
    if (!valid_room_context(state) || state.data.phase == RunPhase::uninitialized || state.data.phase == RunPhase::invalid)
        return {StepStatus::engine_error, state.data.boundary, Restriction::invalid_state};
    const auto restriction = legal_run_input(state, input);
    if (restriction != Restriction::none)
        return {restriction == Restriction::unsupported_setup ? StepStatus::unsupported_content : StepStatus::invalid_action, state.data.boundary, restriction};
    return prepare_run_input(state, input, plan, mode);
}
bool valid_room_context(const RunState& state) noexcept {
    if (state.data.rewards && state.data.rewards->return_to > RewardReturn::parent_room) return false;
    if (std::holds_alternative<std::monostate>(state.data.room))
        return state.data.phase != RunPhase::room && (!state.data.rewards || state.data.rewards->return_to == RewardReturn::map);
    const auto* parent = std::get_if<ParentInteractionState>(&state.data.room);
    if (!parent || parent->pending_gold < 0 || state.data.direct_combat || state.data.combat_generation == 0) return false;
    switch (parent->stage) {
    case ParentRoomStage::child_combat: return state.data.phase == RunPhase::combat && state.data.combat && !state.data.rewards;
    case ParentRoomStage::child_rewards:
        return !state.data.combat && ((state.data.phase == RunPhase::combat_done && !state.data.rewards) ||
            (state.data.phase == RunPhase::rewards && state.data.rewards && state.data.rewards->return_to == RewardReturn::parent_room));
    case ParentRoomStage::choosing: return state.data.phase == RunPhase::room && !state.data.combat && !state.data.rewards;
    }
    return false;
}
void apply_reward_input(RunState& state_view, const RunInput& input, const RunActionPlan& plan) {
    const auto potion_slot = plan.potion_slot;
    const auto next_card_instance = plan.next_card_instance;
    auto& rewards = *state_view.data.rewards;
    if (input.kind == RunInputKind::take_reward) {
        const auto card_index = action_rules::reward_card_option(rewards);
        if (input.option == 0) { state_view.data.gold += rewards.gold; rewards.gold_taken = true; }
        else if (input.option == card_index) rewards.selecting_card = true;
        else if (potion_slot < static_cast<std::size_t>(state_view.data.potion_slots)) {
            if (potion_slot == state_view.potions().size()) state_view.potions().push_back(rewards.potion);
            else state_view.potions()[potion_slot] = rewards.potion;
            rewards.potion_taken = true;
        }
    } else if (input.kind == RunInputKind::choose_reward_card) {
        auto card = rewards.cards[input.option]; card.instance = next_card_instance;
        card.floor_added = static_cast<std::int32_t>(state_view.visited().size()); state_view.deck().push_back(card);
        rewards.selected_card = static_cast<std::uint8_t>(input.option); rewards.card_taken = true; rewards.selecting_card = false;
    } else if (input.kind == RunInputKind::reward_alternative) rewards.selecting_card = false;
    else { rewards.skipped = true; rewards.resolved = true; }
    if (rewards.gold_taken && (rewards.potion == no_content || rewards.potion_taken) && rewards.card_taken) rewards.resolved = true;

}
void apply_room_option(RunState& state, const RunInput& input) {
    if (state.data.phase == RunPhase::room) {
        const auto gold = std::get<ParentInteractionState>(state.data.room).pending_gold;
        if (input.option == 0) state.data.gold += gold;
        state.data.room = std::monostate{}; state.data.phase = RunPhase::map;
    } else {
        state.data.selected_neow = state.data.neow_options[input.option];
        state.relics().push_back(state.data.selected_neow); state.data.gold += 150;
        state.data.phase = RunPhase::neow_done;
    }
}
void child_combat_finished(RunState& state, bool won) noexcept {
    if (auto* parent = std::get_if<ParentInteractionState>(&state.data.room)) {
        if (won) parent->stage = ParentRoomStage::child_rewards;
        else state.data.room = std::monostate{};
    }
}
bool return_from_room(RunState& state) noexcept {
    if (state.data.phase == RunPhase::neow_done) { state.data.phase = RunPhase::map; return true; }
    if (!state.data.rewards || !state.data.rewards->resolved) return false;
    if (state.data.rewards->return_to == RewardReturn::map) { state.data.phase = RunPhase::map; return true; }
    auto* parent = std::get_if<ParentInteractionState>(&state.data.room);
    if (!parent || parent->stage != ParentRoomStage::child_rewards) return false;
    state.data.rewards.reset(); parent->stage = ParentRoomStage::choosing; state.data.phase = RunPhase::room;
    return true;
}
}
