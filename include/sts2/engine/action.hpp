#pragma once
#include "sts2/engine/action_rules.hpp"
#include <optional>
#include <vector>

namespace sts2::engine {
// Borrowed prompt description. Reacquire after state mutation. Ordinary action
// views/generation omit selection decisions; callers submit SelectionInput.
struct SelectionSpace {
    BoundaryId expected_boundary;
    PromptId prompt;
    std::span<const CardHandle> candidates;
    std::uint32_t minimum, maximum;
};
// Precondition: valid_run(state), as for trusted legal-action traversal.
[[nodiscard]] std::optional<SelectionSpace> selection_space(const RunState&) noexcept;
// Enumerate a valid space (unique candidates) without replacement. workspace is caller-owned,
// at least maximum handles, disjoint from candidates, and unchanged by consume.
// Each emitted span lasts only through consume. Return false from consume to stop.
// An eager caller can copy emitted spans into its own storage using this seam.
template<class Consumer>
bool for_each_selection(const SelectionSpace& space, std::span<CardHandle> workspace, Consumer&& consume) {
    if (workspace.size() < space.maximum || space.maximum > space.candidates.size() || space.minimum > space.maximum)
        return false;
    const auto walk = [&](auto&& self, std::size_t count) -> bool {
        if (action_rules::selection_count_restriction(count, space.minimum, space.maximum) == Restriction::none &&
            !consume(SelectionInput{space.expected_boundary, space.prompt, workspace.first(count)})) return false;
        if (count == space.maximum) return true;
        for (auto card : space.candidates) {
            if (std::find(workspace.begin(), workspace.begin() + count, card) != workspace.begin() + count) continue;
            workspace[count] = card;
            if (!self(self, count + 1)) return false;
        }
        return true;
    };
    return walk(walk, 0);
}

struct ActionView { CombatInput input; Restriction restriction; };
class ActionSpaceView {
public:
    explicit ActionSpaceView(const RunState& state) noexcept : state_(state) {}
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::optional<ActionView> at(std::size_t index) const noexcept;
private:
    const RunState& state_;
};

// Inputs retain the current boundary. Forced transport continues are omitted.
struct PlayerAction { PlayerInput input; Restriction restriction; };
// Borrowed indexed traversal. Reacquire after mutation; no state copy or allocation.
class RunActionSpaceView {
public:
    explicit RunActionSpaceView(const RunState&) noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool empty() const noexcept { return size() == 0; }
    [[nodiscard]] std::optional<PlayerAction> at(std::size_t index) const noexcept;
private:
    const RunState& state_;
    BoundaryId boundary_;
    bool valid_;
};
// Lists offered actions, including restricted actions and their reasons.
[[nodiscard]] std::vector<PlayerAction> get_actions(const RunState&);
// Travel validation checks visible legality; hidden capability is checked on apply.
[[nodiscard]] Restriction validate_action(const RunState&, const PlayerInput&) noexcept;
// Apply one player action and advance automatically to a decision or terminal result.
[[nodiscard]] StepResult apply_action(RunState&, const PlayerInput&, Scratch&);
// Preconditions: valid_run(state), and input is legal for this exact state or
// an independent exact copy. Without STS2_DEFENSIVE_VALIDATION the caller also
// guarantees supported content and reachable effects. Violations need not be
// rejected. Defensive builds retain content-support checks. Execution limits and
// allocation-failure handling remain active. Failure states must be discarded.
[[nodiscard]] StepResult apply_action_trusted(RunState&, const PlayerInput&, Scratch&);

// Sequential, allocation-free generation. The caller establishes valid_run once
// at the root and must not mutate state from the consumer. Capability and
// operational limits are checked during execution, not used to hide legal actions.
// The template keeps consumer dispatch static and leaves storage with the caller.
template<class Consumer>
void for_each_legal_action(const RunState& state, Consumer&& consume) {
    if (state.data.phase == RunPhase::rewards && state.data.rewards && state.data.rewards->resolved) return;
    const auto offer = [&](RunInputKind kind, std::uint32_t option = 0, int col = 0, int row = 0) {
        consume(PlayerInput{RunInput{state.data.boundary, kind, option, col, row}});
    };
    if (state.data.phase == RunPhase::combat) {
        const auto& combat = state.combat();
        if (combat.enemy_side) return;
        if (combat.phase == CombatPhase::play) {
            for (auto card : state.zone(Zone::hand)) {
                if (action_rules::card_play_restriction(state, card) != Restriction::none) continue;
                const auto* definition = Ruleset::pinned().card(state.cards().get(card)->definition);
                if (definition->enemy_target) {
                    for (auto target : state.enemies()) if (action_rules::is_living_enemy(state, target))
                        consume(PlayerInput{CombatInput{state.data.boundary, CombatInputKind::play_card, card, target}});
                } else consume(PlayerInput{CombatInput{state.data.boundary, CombatInputKind::play_card, card}});
            }
            consume(PlayerInput{CombatInput{state.data.boundary, CombatInputKind::end_turn}});
        }
        return;
    }
    if (state.data.phase == RunPhase::room) {
        offer(RunInputKind::choose_option, 0); offer(RunInputKind::choose_option, 1);
    } else if (state.data.phase == RunPhase::neow) {
        for (std::uint32_t i = 0; i < state.data.neow_options.size(); ++i) offer(RunInputKind::choose_option, i);
    } else if (state.data.phase == RunPhase::map) {
        const auto& map = *state.data.maps[state.data.act_index];
        const auto& children = map.nodes[state.data.current].children;
        for (std::size_t i = 0; i < children.used; ++i) {
            const auto id = children.slots[i];
            if (id != no_node) offer(RunInputKind::travel, 0, map.nodes[id].col, map.nodes[id].row);
        }
    } else if (state.data.phase == RunPhase::rewards && state.data.rewards && !state.data.rewards->resolved) {
        const auto& rewards = *state.data.rewards;
        if (rewards.selecting_card) {
            for (std::uint32_t i = 0; i < rewards.cards.size(); ++i) offer(RunInputKind::choose_reward_card, i);
            offer(RunInputKind::reward_alternative);
        } else {
            if (action_rules::reward_available(rewards, 0)) offer(RunInputKind::take_reward, 0);
            if (rewards.potion != no_content && action_rules::reward_available(rewards, 1)) offer(RunInputKind::take_reward, 1);
            const auto card_option = action_rules::reward_card_option(rewards);
            if (action_rules::reward_available(rewards, card_option)) offer(RunInputKind::take_reward, card_option);
            offer(RunInputKind::skip_rewards);
        }
    }
    if (action_rules::potion_discard_phase_restriction(state) == Restriction::none)
        for (std::uint32_t i = 0; i < state.potions().size(); ++i) if (action_rules::potion_available(state, i)) offer(RunInputKind::discard_potion, i);
}
// Execute automatic transitions until a decision or terminal result is reached.
[[nodiscard]] StepResult advance_to_decision(RunState&, Scratch&);
} // namespace sts2::engine
