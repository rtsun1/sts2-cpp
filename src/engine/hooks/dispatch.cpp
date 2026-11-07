#include "dispatch.hpp"
#include "../content/powers.hpp"
#include "../content/relics.hpp"
#include "../mechanics/powers.hpp"
#include "../combat/lifecycle.hpp"
#include "../defensive.hpp"
#include <algorithm>
#include <stdexcept>

namespace sts2::engine::gameplay {
namespace {
void capture_powers(RunState& state, PowerId definition, HookHandler handler) {
    for (const auto& power : state.player().power_instances(state.overflow)) {
        if (power.active && power.definition == definition)
            state.captured_hook_listeners().push_back({state.data.player, power.instance, power.amount, handler});
    }
}
bool eligible(const RunState& state, const HookListener& listener) noexcept {
    // The supported handlers belong to the single player's powers and relics.
    // Native captured powers check owner membership, not current power membership.
    if (listener.owner != state.data.player || !state.creature(listener.owner)) return false;
    if (listener.handler == HookHandler::dark_embrace || listener.handler == HookHandler::stratagem) return true;
    if (listener.handler == HookHandler::centennial_puzzle) return state.combat().centennial_puzzle;
    const auto relic = listener.handler == HookHandler::charons_ashes ? ExhaustRelic::charons_ashes : ExhaustRelic::joss_paper;
    return std::find(state.combat().exhaust_relics.begin(), state.combat().exhaust_relics.begin() + state.combat().exhaust_relic_count, relic) !=
        state.combat().exhaust_relics.begin() + state.combat().exhaust_relic_count;
}
HandlerResult resume_listener(RunState& state, Scratch& scratch, HookListener listener,
    HandlerProgress& progress, std::uint32_t* expected_selection_count, const HitResult* hit) {
    // Once started, the handler completes against its captured identity even if
    // its eligibility changes while its child runs.
    if (progress.stage == 0 && !eligible(state, listener)) return HandlerResult::complete;
    const auto* owner = state.creature(listener.owner);
    const auto* power = owner ? owner->find_power_instance(listener.power_instance, state.overflow) : nullptr;
    const auto amount = power ? power->amount : listener.removed_power_amount;
    switch (listener.handler) {
    case HookHandler::centennial_puzzle: return centennial_puzzle_after_damage_received(state, progress, *hit);
    case HookHandler::dark_embrace: return dark_embrace_after_card_exhausted(state, progress, amount);
    case HookHandler::stratagem: return stratagem_after_shuffle(state, progress, *expected_selection_count, amount);
    case HookHandler::charons_ashes:
        charons_ashes_after_card_exhausted(state, scratch); return HandlerResult::complete;
    case HookHandler::joss_paper: return joss_paper_after_card_exhausted(state, scratch, progress);
    }
    throw std::logic_error("Unsupported hook handler");
}
void release_candidates(RunState& state, std::uint32_t begin) {
    while (state.captured_hook_listeners().size() > begin) state.captured_hook_listeners().pop_back();
}
}

PowerModifier before_power_application(const CreatureState& creature, const OverflowStorage& owner, PowerId id, int amount) noexcept {
    return artifact_before_power_application(creature, owner, id, amount);
}
void after_power_application(RunState& state, Scratch& scratch, CreatureState& creature, PowerModifier modifier, bool enemy, bool enemy_side, CreatureHandle target) {
    artifact_after_power_application(state, scratch, creature, modifier, enemy, enemy_side, target);
}
void retain_removed_power(RunState& state, CreatureHandle owner, const PowerInstance& power) noexcept {
    for (auto& listener : state.captured_hook_listeners())
        if (listener.owner == owner && listener.power_instance == power.instance)
            listener.removed_power_amount = power.amount;
}
RangeProgress capture_exhaust_listeners(RunState& state) {
    const auto begin = static_cast<std::uint32_t>(state.captured_hook_listeners().size());
    // Native order is player powers in application order, then relics in inventory order.
    capture_powers(state, PowerId::dark_embrace, HookHandler::dark_embrace);
    for (std::uint8_t i = 0; i < state.combat().exhaust_relic_count; ++i) {
        const auto handler = state.combat().exhaust_relics[i] == ExhaustRelic::charons_ashes ? HookHandler::charons_ashes : HookHandler::joss_paper;
        state.captured_hook_listeners().push_back({state.data.player, UINT32_MAX, 0, handler});
    }
    return {begin, static_cast<std::uint32_t>(state.captured_hook_listeners().size()) - begin, 0};
}

void after_creature_death(RunState& state, Scratch& scratch, CreatureHandle dead) {
    for (std::uint32_t slot = 0; slot < state.creatures().slots().size(); ++slot) {
        const auto& entry = state.creatures().slots()[slot];
        if (!entry.live || (slot != state.data.player.slot && !entry.value.enemy_member)) continue;
        const CreatureHandle owner{slot, entry.generation};
        std::size_t index = 0;
        while (auto* creature = state.creature(owner)) {
            const auto powers = creature->power_instances(state.overflow);
            if (index >= powers.size()) break;
            const auto power = powers[index];
            if (power.definition == PowerId::shrink) shrink_after_applier_death(state, scratch, owner, power, dead);
            creature = state.creature(owner);
            if (!creature) break;
            const auto remaining = creature->power_instances(state.overflow);
            if (index < remaining.size() && remaining[index].instance == power.instance) ++index;
        }
    }
}

void after_enemy_turn(RunState& state, Scratch& scratch) {
    for (std::size_t index = 0; index < state.creatures().slots().size(); ++index) {
        const auto& slot = state.creatures().slots()[index];
        if (!slot.live || (index != state.data.player.slot && !slot.value.enemy_member)) continue;
        const CreatureHandle owner{static_cast<std::uint32_t>(index), slot.generation};
        std::size_t i = 0;
        while (auto* current = state.creature(owner)) {
            const auto powers = current->power_instances(state.overflow);
            if (i >= powers.size()) break;
            const auto identity = powers[i].instance;
            if (power_ticks_after_enemy_turn(powers[i].definition)) expire_power(state, scratch, *current, identity);
            current = state.creature(owner);
            if (!current) break;
            const auto remaining = current->power_instances(state.overflow);
            if (i < remaining.size() && remaining[i].instance == identity) ++i;
        }
    }
}

RangeProgress capture_shuffle_listeners(RunState& state) {
    const auto begin = static_cast<std::uint32_t>(state.captured_hook_listeners().size());
    capture_powers(state, PowerId::stratagem, HookHandler::stratagem);
    return {begin, static_cast<std::uint32_t>(state.captured_hook_listeners().size()) - begin, 0};
}
RangeProgress capture_damage_received_listeners(RunState& state) {
    const auto begin = static_cast<std::uint32_t>(state.captured_hook_listeners().size());
    if (state.combat().centennial_puzzle)
        state.captured_hook_listeners().push_back({state.data.player, UINT32_MAX, 0, HookHandler::centennial_puzzle});
    return {begin, static_cast<std::uint32_t>(state.captured_hook_listeners().size()) - begin, 0};
}
RangeProgress capture_draw_listeners(RunState&, bool) {
    // No draw listeners are supported yet. Each phase has its own capture point.
    return {};
}
HandlerResult resume_listeners(RunState& state, Scratch& scratch, RangeProgress& listeners,
    HandlerProgress& progress, const Ruleset&, std::uint32_t* expected_selection_count, const HitResult* hit) {
    if (listeners.count == 0) return HandlerResult::complete;
    while (listeners.cursor < listeners.count) {
        if (progress.stage == 0 && !consume(scratch)) return HandlerResult::execution_budget;
        const auto listener = state.captured_hook_listeners()[listeners.begin + listeners.cursor];
        if (listener.handler == HookHandler::stratagem && !expected_selection_count)
            throw std::logic_error("Shuffle handler without prompt progress");
        STS2_CHECK_INVARIANT(listener.handler != HookHandler::centennial_puzzle || hit != nullptr);
        const auto outcome = resume_listener(state, scratch, listener, progress, expected_selection_count, hit);
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
