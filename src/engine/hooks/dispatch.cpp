#include "dispatch.hpp"
#include "../content/powers.hpp"
#include "../content/relics.hpp"
#include "../mechanics/powers.hpp"
#include "../combat/lifecycle.hpp"
#include <algorithm>
#include <stdexcept>

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
HandlerResult resume_listener(CombatView state, Scratch& scratch, HookListener listener,
    HandlerProgress& progress, std::uint32_t* expected_selection_count) {
    // Once started, the handler completes against its captured identity even if
    // its eligibility changes while its child runs.
    if (progress.stage == 0 && !eligible(state, listener)) return HandlerResult::complete;
    const auto* owner = state.creature(listener.owner);
    const auto* power = owner ? owner->find_power_instance(listener.power_instance, state.creature_overflow) : nullptr;
    const auto amount = power ? power->amount : listener.removed_power_amount;
    switch (listener.handler) {
    case HookHandler::dark_embrace: return dark_embrace_after_card_exhausted(state, progress, amount);
    case HookHandler::stratagem: return stratagem_after_shuffle(state, progress, *expected_selection_count, amount);
    case HookHandler::charons_ashes:
        charons_ashes_after_card_exhausted(state, scratch); return HandlerResult::complete;
    case HookHandler::joss_paper: return joss_paper_after_card_exhausted(state, scratch, progress);
    }
    throw std::logic_error("Unsupported hook handler");
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
RangeProgress capture_exhaust_listeners(CombatView state) {
    const auto begin = static_cast<std::uint32_t>(state.captured_hook_listeners.size());
    // Native order is player powers in application order, then relics in inventory order.
    capture_powers(state, PowerId::dark_embrace, HookHandler::dark_embrace);
    for (std::uint8_t i = 0; i < state.exhaust_relic_count; ++i) {
        const auto handler = state.exhaust_relics[i] == ExhaustRelic::charons_ashes ? HookHandler::charons_ashes : HookHandler::joss_paper;
        state.captured_hook_listeners.push_back({state.player_handle, UINT32_MAX, 0, handler});
    }
    return {begin, static_cast<std::uint32_t>(state.captured_hook_listeners.size()) - begin, 0};
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

RangeProgress capture_shuffle_listeners(CombatView state) {
    const auto begin = static_cast<std::uint32_t>(state.captured_hook_listeners.size());
    capture_powers(state, PowerId::stratagem, HookHandler::stratagem);
    return {begin, static_cast<std::uint32_t>(state.captured_hook_listeners.size()) - begin, 0};
}
RangeProgress capture_draw_listeners(CombatView, bool) {
    // No draw listeners are supported yet. Each phase has its own capture point.
    return {};
}
HandlerResult resume_listeners(CombatView state, Scratch& scratch, RangeProgress& listeners,
    HandlerProgress& progress, const Ruleset&, std::uint32_t* expected_selection_count) {
    if (listeners.count == 0) return HandlerResult::complete;
    while (listeners.cursor < listeners.count) {
        if (progress.stage == 0 && !consume(scratch)) return HandlerResult::execution_budget;
        const auto listener = state.captured_hook_listeners[listeners.begin + listeners.cursor];
        if (listener.handler == HookHandler::stratagem && !expected_selection_count)
            throw std::logic_error("Shuffle handler without prompt progress");
        const auto outcome = resume_listener(state, scratch, listener, progress, expected_selection_count);
        // Scheduling can relocate the owning operation. Do not touch references
        // after a yielding handler; the interpreter runs its child next.
        if (outcome != HandlerResult::complete) return outcome;
        ++listeners.cursor;
        progress = {};
    }
    if (!consume(scratch)) return HandlerResult::execution_budget;
    release_candidates(state, listeners.begin);
    listeners = {};
    return HandlerResult::complete;
}
}
