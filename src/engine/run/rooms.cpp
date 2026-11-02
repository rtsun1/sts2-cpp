#include "run_flow.hpp"
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
StepResult plan_run_input(const RunState& state_view, const RunInput& input, RunActionPlan& plan, ValidationMode mode) {
    if (!valid_room_context(state_view)) return {StepStatus::engine_error, state_view.data.boundary, Restriction::invalid_state};
    if (state_view.data.phase == RunPhase::uninitialized || state_view.data.phase == RunPhase::invalid)
        return {StepStatus::engine_error, state_view.data.boundary, Restriction::invalid_state};
    if (input.expected_boundary != state_view.data.boundary) return {StepStatus::invalid_action, state_view.data.boundary, Restriction::stale_boundary};
    auto& entering_generation = plan.entering_generation;
    auto& generate_rewards = plan.generate_rewards;
    auto& destination = plan.destination;
    auto& potion_slot = plan.potion_slot; potion_slot = state_view.potions().size();
    auto& next_card_instance = plan.next_card_instance;
    const auto reward_kind = plan.reward_kind = input.kind == RunInputKind::take_reward || input.kind == RunInputKind::skip_rewards ||
        input.kind == RunInputKind::choose_reward_card || input.kind == RunInputKind::reward_alternative;
    if (reward_kind) {
        if (state_view.data.phase != RunPhase::rewards || !state_view.data.rewards || state_view.data.rewards->resolved)
            return {StepStatus::invalid_action, state_view.data.boundary, Restriction::wrong_phase};
        const auto& rewards = *state_view.data.rewards;
        if (rewards.selecting_card) {
            if (input.kind == RunInputKind::choose_reward_card) {
                if (input.option >= rewards.cards.size()) return {StepStatus::invalid_action, state_view.data.boundary, Restriction::invalid_selection};
                const auto id = SetupRules::name(rewards.cards[input.option].definition);
                if (id != "CARD.TRUE_GRIT" && id != "CARD.JUGGLING" && id != "CARD.RAGE")
                    return {StepStatus::unsupported_content, state_view.data.boundary, Restriction::unsupported_setup};
                if (state_view.deck().size() >= 4096) return {StepStatus::resource_limit, state_view.data.boundary, Restriction::capacity};
                for (const auto& existing : state_view.deck()) {
                    if (existing.instance == UINT32_MAX) return {StepStatus::resource_limit, state_view.data.boundary, Restriction::capacity};
                    next_card_instance = std::max(next_card_instance, existing.instance + 1);
                }
            } else if (input.kind != RunInputKind::reward_alternative || input.option != 0)
                return {StepStatus::invalid_action, state_view.data.boundary, Restriction::invalid_selection};
        } else if (input.kind == RunInputKind::take_reward) {
            const auto card_index = rewards.potion == no_content ? 1u : 2u;
            if (input.option > card_index || (input.option == 0 && rewards.gold_taken) ||
                (input.option == 1 && card_index == 2 && rewards.potion_taken) || (input.option == card_index && rewards.card_taken))
                return {StepStatus::invalid_action, state_view.data.boundary, Restriction::invalid_selection};
            if (input.option == 0 && state_view.data.gold > INT32_MAX - rewards.gold)
                return {StepStatus::resource_limit, state_view.data.boundary, Restriction::capacity};
            if (input.option == 1 && card_index == 2) {
                for (std::size_t i = 0; i < state_view.potions().size(); ++i) if (state_view.potions()[i] == no_content) { potion_slot = i; break; }
            }
        } else if (input.kind != RunInputKind::skip_rewards)
            return {StepStatus::invalid_action, state_view.data.boundary, Restriction::wrong_phase};
    } else if (input.kind == RunInputKind::discard_potion) {
        if (state_view.data.phase != RunPhase::rewards && state_view.data.phase != RunPhase::map)
            return {StepStatus::unsupported_content, state_view.data.boundary, Restriction::unsupported_setup};
        if (state_view.data.rewards && state_view.data.rewards->selecting_card) return {StepStatus::invalid_action, state_view.data.boundary, Restriction::wrong_phase};
        if (input.option >= state_view.potions().size() || state_view.potions()[input.option] == no_content)
            return {StepStatus::invalid_action, state_view.data.boundary, Restriction::invalid_selection};
    } else
    if (input.kind == RunInputKind::choose_option) {
        if (state_view.data.phase == RunPhase::room) {
            const auto* parent = std::get_if<ParentInteractionState>(&state_view.data.room);
            if (!parent || parent->stage != ParentRoomStage::choosing || parent->pending_gold < 0) return {StepStatus::engine_error, state_view.data.boundary, Restriction::invalid_state};
            if (input.option >= 2) return {StepStatus::invalid_action, state_view.data.boundary, Restriction::invalid_selection};
            if (input.option == 0 && state_view.data.gold > INT32_MAX - parent->pending_gold) return {StepStatus::resource_limit, state_view.data.boundary, Restriction::capacity};
            return {StepStatus::awaiting_input, state_view.data.boundary};
        }
        if (state_view.data.phase != RunPhase::neow) return {StepStatus::invalid_action, state_view.data.boundary, Restriction::wrong_phase};
        if (input.option >= state_view.data.neow_options.size()) return {StepStatus::invalid_action, state_view.data.boundary, Restriction::invalid_selection};
        if (state_view.data.gold > INT32_MAX - 150)
            return {StepStatus::resource_limit, state_view.data.boundary, Restriction::capacity};
        if (SetupRules::name(state_view.data.neow_options[input.option]) != "RELIC.GOLDEN_PEARL")
            return {StepStatus::unsupported_content, state_view.data.boundary, Restriction::unsupported_setup};
    } else if (input.kind == RunInputKind::proceed) {
        if (state_view.data.phase == RunPhase::combat_done && !state_view.data.direct_combat) {
            generate_rewards = true;
        } else if (state_view.data.phase != RunPhase::neow_done && !(state_view.data.phase == RunPhase::rewards && state_view.data.rewards && state_view.data.rewards->resolved)) return {StepStatus::invalid_action, state_view.data.boundary, Restriction::wrong_phase};
    } else if (input.kind == RunInputKind::travel) {
        if (state_view.data.phase != RunPhase::map) return {StepStatus::invalid_action, state_view.data.boundary, Restriction::wrong_phase};
        const auto& map = *state_view.data.maps[state_view.data.act_index]; const auto next = map.at(input.col, input.row);
        if (next == no_node || !map.nodes[state_view.data.current].children.contains(next)) return {StepStatus::invalid_action, state_view.data.boundary, Restriction::invalid_selection};
        if (mode == ValidationMode::visible) { destination = next; return {StepStatus::awaiting_input, state_view.data.boundary}; }
        if (map.nodes[next].kind != MapKind::monster || state_view.data.ascension != 0 ||
            SetupRules::name(state_view.data.character) != "CHARACTER.IRONCLAD")
            return {StepStatus::unsupported_content, state_view.data.boundary, Restriction::unsupported_setup};
        if (next_combat_generation(state_view) == 0)
            return {StepStatus::resource_limit, state_view.data.boundary, Restriction::capacity};
        const auto rooms = state_view.act(state_view.data.act_index);
        if (rooms.normal_visited >= rooms.normal.size() || SetupRules::name(rooms.normal[rooms.normal_visited]) != "ENCOUNTER.SHRINKER_BEETLE_WEAK")
            return {StepStatus::unsupported_content, state_view.data.boundary, Restriction::unsupported_setup};
        if (!supported_combat_inventory(state_view))
            return {StepStatus::unsupported_content, state_view.data.boundary, Restriction::unsupported_setup};
        entering_generation = next_combat_generation(state_view);
        destination = next;
    } else return {StepStatus::unsupported_content, state_view.data.boundary, Restriction::unsupported_setup};
    return {StepStatus::awaiting_input, state_view.data.boundary};
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
        const auto card_index = rewards.potion == no_content ? 1u : 2u;
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
