#pragma once
#include "sts2/engine/run.hpp"
#include <algorithm>
#include <optional>
#include <vector>

namespace sts2::engine {
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
// an independent exact copy. Failure states must be discarded; no rollback.
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
        if (combat.phase == CombatPhase::card_choice) {
            const auto selected = state.selection_order();
            for (auto card : state.selection_candidates()) {
                const bool present = std::find(selected.begin(), selected.end(), card) != selected.end();
                consume(PlayerInput{CombatInput{state.data.boundary,
                    present ? CombatInputKind::deselect_card : CombatInputKind::select_card,
                    card, {}, combat.selection.id}});
            }
            if (!combat.selection.automatic_at_maximum && selected.size() >= combat.selection.minimum && selected.size() <= combat.selection.maximum)
                consume(PlayerInput{CombatInput{state.data.boundary, CombatInputKind::confirm_selection, {}, {}, combat.selection.id}});
        } else if (combat.phase == CombatPhase::discard_choice) {
            for (auto card : state.zone(Zone::hand))
                consume(PlayerInput{CombatInput{state.data.boundary, CombatInputKind::select_card, card}});
        } else if (combat.phase == CombatPhase::play) {
            for (auto card : state.zone(Zone::hand)) {
                if (combat.energy < effective_card_cost(state, card)) continue;
                const auto* definition = Ruleset::pinned().card(state.cards().get(card)->definition);
                if (definition->enemy_target) {
                    for (auto target : state.enemies()) if (state.creature(target)->hp > 0)
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
            if (!rewards.gold_taken) offer(RunInputKind::take_reward, 0);
            if (rewards.potion != no_content && !rewards.potion_taken) offer(RunInputKind::take_reward, 1);
            if (!rewards.card_taken) offer(RunInputKind::take_reward, rewards.potion == no_content ? 1 : 2);
            offer(RunInputKind::skip_rewards);
        }
    }
    if ((state.data.phase == RunPhase::map || state.data.phase == RunPhase::rewards) && !(state.data.rewards && state.data.rewards->selecting_card))
        for (std::uint32_t i = 0; i < state.potions().size(); ++i) if (state.potions()[i] != no_content) offer(RunInputKind::discard_potion, i);
}
// Execute automatic transitions until a decision or terminal result is reached.
[[nodiscard]] StepResult advance_to_decision(RunState&, Scratch&);
} // namespace sts2::engine
