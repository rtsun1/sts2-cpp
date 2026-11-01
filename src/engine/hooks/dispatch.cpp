#include "dispatch.hpp"
#include "../content/powers.hpp"
#include "../content/relics.hpp"
#include "../mechanics/powers.hpp"
#include "../combat/lifecycle.hpp"
#include <algorithm>

namespace sts2::engine::gameplay {
namespace {
void capture_powers(CombatView state, PowerId definition, HookHandler handler) {
    for (const auto& power : state.player.power_instances(state.creature_overflow)) {
        if (power.active && power.definition == definition)
            state.captured_hook_listeners.push_back({state.player_handle, power.instance, power.amount, handler});
    }
}
bool eligible(ConstCombatView state, const HookListener& listener) noexcept {
    // The supported handlers belong to the single player's powers and relics.
    // Native captured powers check owner membership, not current power membership.
    if (listener.owner != state.player_handle || !state.creature(listener.owner)) return false;
    if (listener.handler == HookHandler::dark_embrace || listener.handler == HookHandler::stratagem) return true;
    const auto relic = listener.handler == HookHandler::charons_ashes ? ExhaustRelic::charons_ashes : ExhaustRelic::joss_paper;
    return std::find(state.exhaust_relics.begin(), state.exhaust_relics.begin() + state.exhaust_relic_count, relic) !=
        state.exhaust_relics.begin() + state.exhaust_relic_count;
}
void call_handler(CombatView state, Scratch& scratch, HookListener listener, const Ruleset& rules) {
    if (!eligible(state, listener)) return;
    const auto* owner = state.creature(listener.owner);
    const auto* power = owner->find_power_instance(listener.power_instance, state.creature_overflow);
    const auto amount = power ? power->amount : listener.removed_power_amount;
    switch (listener.handler) {
    case HookHandler::dark_embrace: dark_embrace_after_card_exhausted(state, scratch, amount, rules); break;
    case HookHandler::stratagem: stratagem_after_shuffle(state, amount); break;
    case HookHandler::charons_ashes: charons_ashes_after_card_exhausted(state, scratch); break;
    case HookHandler::joss_paper: joss_paper_after_card_exhausted(state, scratch, rules); break;
    }
}
void release_candidates(CombatView state, std::uint32_t begin) {
    while (state.captured_hook_listeners.size() > begin) state.captured_hook_listeners.pop_back();
}
}

PowerModifier before_power_application(const CreatureState& creature, const OverflowStorage& owner, PowerId id, int amount) noexcept {
    return artifact_before_power_application(creature, owner, id, amount);
}
void after_power_application(CombatView state, Scratch& scratch, CreatureState& creature, PowerModifier modifier, bool enemy, bool enemy_side, CreatureHandle target) {
    artifact_after_power_application(state, scratch, creature, modifier, enemy, enemy_side, target);
}
void retain_removed_power(CombatView state, CreatureHandle owner, const PowerInstance& power) noexcept {
    for (auto& listener : state.captured_hook_listeners)
        if (listener.owner == owner && listener.power_instance == power.instance)
            listener.removed_power_amount = power.amount;
}
void after_card_exhausted(CombatView state, Scratch&, CardHandle) {
    const auto begin = static_cast<std::uint32_t>(state.captured_hook_listeners.size());
    // Native order is player powers in application order, then relics in inventory order.
    capture_powers(state, PowerId::dark_embrace, HookHandler::dark_embrace);
    for (std::uint8_t i = 0; i < state.exhaust_relic_count; ++i) {
        const auto handler = state.exhaust_relics[i] == ExhaustRelic::charons_ashes ? HookHandler::charons_ashes : HookHandler::joss_paper;
        state.captured_hook_listeners.push_back({state.player_handle, UINT32_MAX, 0, handler});
    }
    const auto count = static_cast<std::uint32_t>(state.captured_hook_listeners.size()) - begin;
    if (count == 0) return;
    state.execution.push_back(ExhaustFrame{begin, count, 0});
}

void after_enemy_turn(CombatView state, Scratch& scratch) {
    for (auto* owner : {&state.player, &state.enemy}) {
        // Supported duration handlers only erase their current instance.
        std::size_t i = 0;
        auto powers = owner->power_instances(state.creature_overflow);
        while (i < powers.size()) {
            const auto identity = powers[i].instance; const auto id = powers[i].definition;
            if (power_ticks_after_enemy_turn(id)) expire_power(state, scratch, *owner, identity);
            if (i < powers.size() && powers[i].instance == identity) ++i;
        }
    }
}

void after_shuffle(CombatView state) {
    const auto begin = static_cast<std::uint32_t>(state.captured_hook_listeners.size());
    capture_powers(state, PowerId::stratagem, HookHandler::stratagem);
    const auto count = static_cast<std::uint32_t>(state.captured_hook_listeners.size()) - begin;
    if (count == 0) return;
    state.execution.push_back(ShuffleFrame{begin, count, 0});
}

StepResult resume_shuffle(CombatView state, Scratch& scratch, const Ruleset& rules) {
    const auto depth = state.execution.size();
    while (true) {
        if (!consume(scratch)) return fail(state, Restriction::execution_budget);
        auto& continuation = std::get<ShuffleFrame>(state.execution.back());
        if (continuation.cursor == continuation.count) {
            release_candidates(state, continuation.begin);
            state.execution.pop_back(); return result(state);
        }
        // Copy the identity and advance before a handler can relocate the execution stack.
        const auto listener = state.captured_hook_listeners[continuation.begin + continuation.cursor++];
        call_handler(state, scratch, listener, rules);
        if (state.phase == CombatPhase::card_choice)
            std::get<ShuffleFrame>(state.execution.back()).selection_count = state.selection.minimum;
        if (state.phase != CombatPhase::play || state.execution.size() != depth) return result(state);
    }
}

StepResult resume_exhaust(CombatView state, Scratch& scratch, const Ruleset& rules) {
    const auto depth = state.execution.size();
    while (true) {
        if (!consume(scratch)) return fail(state, Restriction::execution_budget);
        auto& continuation = std::get<ExhaustFrame>(state.execution.back());
        if (continuation.cursor == continuation.count) {
            release_candidates(state, continuation.begin);
            state.execution.pop_back(); return result(state);
        }
        const auto listener = state.captured_hook_listeners[continuation.begin + continuation.cursor++];
        call_handler(state, scratch, listener, rules);
        if (state.phase != CombatPhase::play || state.execution.size() != depth) return result(state);
    }
}
}
