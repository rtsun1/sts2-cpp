#include "powers.hpp"
#include "../content/powers.hpp"
#include "../hooks/dispatch.hpp"
#include "../combat/trace.hpp"
#include <algorithm>
#include <stdexcept>

namespace sts2::engine::gameplay {
std::uint32_t apply_power(RunState& s, Scratch& scratch, PowerId id, int amount, bool enemy, bool enemy_side, CardHandle source, bool has_applier, bool applier_enemy, PowerInstance::Stacking stacking, CreatureHandle applier, CreatureHandle target, std::uint32_t existing_instance) {
    auto* recipient = target.present() ? s.creature(target) : (enemy ? &s.enemy() : &s.player());
    if (!recipient) throw std::invalid_argument("Stale power target");
    const auto owner_handle = s.creature_handle(*recipient);
    auto& creature = *recipient;
    if (!has_applier) applier = {};
    if (has_applier && !applier.present()) applier = applier_enemy ? s.enemy_handle() : s.data.player;
    auto powers = creature.power_instances(s.overflow);
    PowerInstance* power = existing_instance == UINT32_MAX ? nullptr : creature.find_power_instance(existing_instance, s.overflow);
    if (existing_instance != UINT32_MAX && (!power || power->definition != id)) throw std::invalid_argument("Stale power instance");
    if (!power) for (auto& candidate : powers) if (candidate.definition == id && candidate.stacking == stacking &&
        (stacking == PowerInstance::Stacking::by_type || (stacking == PowerInstance::Stacking::by_applier && candidate.applier == applier))) { power = &candidate; break; }
    const auto modifier = before_power_application(creature, s.overflow, id, amount);
    amount = modifier.amount;
    if (!power && amount == 0) { after_power_application(s, scratch, creature, modifier, enemy, enemy_side, target); return UINT32_MAX; }
    if (!power) {
        if (s.combat().next_power_instance == UINT32_MAX) throw std::length_error("power identity exhausted");
        PowerInstance next{s.combat().next_power_instance++, id, amount, 0, enemy, true, skip_initial_power_tick(id, enemy)};
        next.source = source; next.applier_enemy = applier_enemy; next.has_applier = has_applier; next.applier = applier; next.stacking = stacking;
        powers.push_back(next); power = &powers.back();
    } else {
        const auto next = std::int64_t(power->amount) + amount;
        if (next < INT32_MIN || next > INT32_MAX) throw std::length_error("power amount exhausted");
        power->amount = static_cast<std::int32_t>(std::clamp<std::int64_t>(next, -999999999, 999999999));
    }
    const auto identity = power->instance;
    emit(s, scratch, CombatEventKind::power_received, enemy_side, enemy, {}, identity, amount, {}, has_applier, applier_enemy, applier, s.creature_handle(creature));
    // Child effects or trace callbacks may grow the creature/power storage.
    auto* current = s.creature(owner_handle);
    if (!current) return identity;
    after_power_application(s, scratch, *current, modifier, enemy, enemy_side, owner_handle);
    current = s.creature(owner_handle);
    if (!current) return identity;
    power = current->find_power_instance(identity, s.overflow);
    if (power && power_is_depleted(id, power->amount)) {
        power->active = false;
        const auto removed = *power;
        emit(s, scratch, CombatEventKind::power_changed, enemy_side, enemy, {}, identity, 0, {}, true, false, owner_handle, owner_handle);
        retain_removed_power(s, owner_handle, removed);
        current = s.creature(owner_handle);
        if (!current) return identity;
        auto remaining = current->power_instances(s.overflow);
        const auto at = std::find_if(remaining.begin(), remaining.end(), [&](const auto& p) { return p.instance == identity; });
        if (at != remaining.end()) remaining.erase(at);
    }
    return identity;
}
void remove_power(RunState& state, Scratch& scratch, CreatureHandle owner, std::uint32_t identity) {
    auto* creature = state.creature(owner);
    auto* power = creature ? creature->find_power_instance(identity, state.overflow) : nullptr;
    if (!power) return;
    power->active = false; const auto removed = *power;
    emit(state, scratch, CombatEventKind::power_changed, state.combat().enemy_side, removed.enemy_owner, {}, identity, 0, {}, true, false, owner, owner);
    retain_removed_power(state, owner, removed);
    if (auto* current = state.creature(owner)) {
        auto powers = current->power_instances(state.overflow);
        const auto at = std::find_if(powers.begin(), powers.end(), [&](const auto& p) { return p.instance == identity; });
        if (at != powers.end()) powers.erase(at);
    }
}
void expire_power(RunState& state, Scratch& scratch, CreatureState& creature, std::uint32_t identity) {
    const auto owner = state.creature_handle(creature);
    auto* power = creature.find_power_instance(identity, state.overflow);
    if (!power || power->amount <= 0) return;
    if (power->skip_next_tick) { power->skip_next_tick = false; return; }
    --power->amount; power->active = power->amount != 0;
    const auto changed = *power;
    emit(state, scratch, CombatEventKind::power_received, true, changed.enemy_owner, {}, identity, -1, {}, false, false, owner, owner);
    if (changed.amount == 0) {
        retain_removed_power(state, owner, changed);
        if (auto* current = state.creature(owner)) {
            auto powers = current->power_instances(state.overflow);
            const auto at = std::find_if(powers.begin(), powers.end(), [&](const auto& p) { return p.instance == identity; });
            if (at != powers.end()) powers.erase(at);
        }
    }
}
void snapshot_power_turn(RunState& state, Scratch& scratch, CreatureState& creature, bool enemy) {
    const auto owner = state.creature_handle(creature);
    std::size_t index = 0;
    while (auto* current = state.creature(owner)) {
        auto powers = current->power_instances(state.overflow);
        if (index >= powers.size()) break;
        const auto identity = powers[index].instance;
        powers[index].amount_on_turn_start = powers[index].amount;
        emit(state, scratch, CombatEventKind::power_changed, true, enemy, {}, identity, 0, {}, true, false, owner, owner);
        current = state.creature(owner);
        if (!current) break;
        const auto remaining = current->power_instances(state.overflow);
        if (index < remaining.size() && remaining[index].instance == identity) ++index;
    }
}
}
