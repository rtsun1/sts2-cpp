#pragma once
#include "sts2/engine/run.hpp"
#include <algorithm>

namespace sts2::engine::action_rules {
// Gameplay predicates for valid relevant state. Callers establish phase and
// candidate membership; these do not validate state, capability, or operational limits.
// Keep card-wide restrictions here so generation evaluates them before targets.
inline Restriction card_play_restriction(const RunState& state, CardHandle card, const Ruleset& rules = Ruleset::pinned()) noexcept {
    return state.combat().energy < effective_card_cost(state, card, rules) ? Restriction::insufficient_energy : Restriction::none;
}
inline bool is_living_enemy(const RunState& state, CreatureHandle target) noexcept {
    const auto* creature = state.creature(target);
    return creature && creature->enemy_member && creature->hp > 0;
}
inline bool is_card_selected(const RunState& state, CardHandle card) noexcept {
    const auto selected = state.selection_order();
    return std::find(selected.begin(), selected.end(), card) != selected.end();
}
inline Restriction selection_confirmation_restriction(const RunState& state) noexcept {
    const auto& selection = state.combat().selection;
    if (selection.automatic_at_maximum) return Restriction::wrong_phase;
    const auto count = state.selection_order().size();
    return count >= selection.minimum && count <= selection.maximum ? Restriction::none : Restriction::invalid_selection;
}
inline std::uint32_t reward_card_option(const CombatRewards& rewards) noexcept {
    return rewards.potion == no_content ? 1u : 2u;
}
inline bool reward_available(const CombatRewards& rewards, std::uint32_t option) noexcept {
    if (option == 0) return !rewards.gold_taken;
    if (option == reward_card_option(rewards)) return !rewards.card_taken;
    return option == 1 && rewards.potion != no_content && !rewards.potion_taken;
}
inline Restriction potion_discard_phase_restriction(const RunState& state) noexcept {
    if (state.data.phase != RunPhase::rewards && state.data.phase != RunPhase::map) return Restriction::unsupported_setup;
    return state.data.rewards && state.data.rewards->selecting_card ? Restriction::wrong_phase : Restriction::none;
}
inline bool potion_available(const RunState& state, std::uint32_t option) noexcept {
    return option < state.potions().size() && state.potions()[option] != no_content;
}
} // namespace sts2::engine::action_rules
