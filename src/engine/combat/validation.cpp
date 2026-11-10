#include "sts2/engine/combat.hpp"
#include "validation.hpp"
#include "sts2/engine/action_rules.hpp"
#include "../execution/validation.hpp"
#include "../execution/selection.hpp"
#include "../mechanics/cards.hpp"
#include "../content/powers.hpp"
#include <algorithm>

namespace sts2::engine {
using namespace gameplay;
bool valid_powers(const RunState& state) noexcept {
    const auto for_each_creature = [&](auto visit) {
        for (std::size_t i = 0; i < state.creatures().slots().size(); ++i) {
            const auto& slot = state.creatures().slots()[i];
            if (slot.live) visit(&slot.value, CreatureHandle{static_cast<std::uint32_t>(i), slot.generation} != state.data.player);
        }
    };
    const auto valid_creature = [&](const CreatureState* creature, bool enemy) {
        if (!creature->powers.valid(state.overflow)) return false;
        const auto powers = creature->power_instances(state.overflow);
        for (std::size_t i = 0; i < powers.size(); ++i) {
            const auto& power = powers[i]; const auto id = power.definition;
            if (id >= PowerId::count || !power.active || power.enemy_owner != enemy || power.instance >= state.combat().next_power_instance || power.stacking > PowerInstance::Stacking::separate) return false;
            if (!valid_power_amount(id, power.amount) ||
                (power.source.present() && !state.cards().get(power.source)) || (power.has_applier && (!power.applier.present() || !state.creature(power.applier)))) return false;
            for (std::size_t j = 0; j < i; ++j) {
                const auto& previous = powers[j];
                if (power.instance == previous.instance) return false;
                if (id == previous.definition && power.stacking == previous.stacking &&
                    (power.stacking == PowerInstance::Stacking::by_type || (power.stacking == PowerInstance::Stacking::by_applier && power.applier == previous.applier))) return false;
            }
        }
        return true;
    };
    bool valid = true;
    for_each_creature([&](const CreatureState* creature, bool enemy) { if (!valid_creature(creature, enemy)) valid = false; });
    if (!valid) return false;
    for_each_creature([&](const CreatureState* a, bool) {
        for_each_creature([&](const CreatureState* b, bool) {
            if (a == b) return;
            for (const auto& pa : a->power_instances(state.overflow))
                for (const auto& pb : b->power_instances(state.overflow)) if (pa.instance == pb.instance) valid = false;
        });
    });
    return valid;
}
namespace {
Restriction checked_combat_state_restriction(const RunState& state, const Ruleset& rules) noexcept {
    if (!state.valid_combat_storage() || !valid_powers(state) || !valid_card_extra(state) || !valid_card_modifiers(state) || !valid_continuations(state, rules) ||
        state.combat().phase == CombatPhase::uninitialized || state.combat().phase == CombatPhase::invalid || state.combat().rules != rules_revision)
        return Restriction::invalid_state;
    for (const auto& slot : state.cards().slots())
        if (slot.live && !supported_card_modifiers(slot.value)) return Restriction::unsupported_setup;
    if (state.combat().gremlin_horn &&
        std::find(state.combat().exhaust_relics.begin(), state.combat().exhaust_relics.begin() + state.combat().exhaust_relic_count, ExhaustRelic::charons_ashes) !=
            state.combat().exhaust_relics.begin() + state.combat().exhaust_relic_count) return Restriction::unsupported_setup;
    return Restriction::none;
}
}
Restriction validate_combat(const RunState& state, const CombatInput& input, const Ruleset& rules) noexcept {
    const auto integrity = checked_combat_state_restriction(state, rules);
    return integrity == Restriction::none ? legal_combat_input(state, input, rules) : integrity;
}
Restriction legal_combat_input(const RunState& state, const CombatInput& input, const Ruleset& rules) noexcept {
    if (state.combat().enemy_side) return Restriction::wrong_phase;
    if (input.expected_boundary != state.data.boundary) return Restriction::stale_boundary;
    if (state.combat().phase == CombatPhase::combat_won || state.combat().phase == CombatPhase::player_died) return Restriction::wrong_phase;
    if (state.combat().phase == CombatPhase::card_choice) {
        return Restriction::wrong_phase;
    }
    if (!input.target.present() && input.target != CreatureHandle{}) return Restriction::malformed_action;
    if (input.kind == CombatInputKind::end_turn) {
        if (input.card != CardHandle{} || input.target.present()) return Restriction::malformed_action;
        return state.combat().phase == CombatPhase::play ? Restriction::none : Restriction::wrong_phase;
    }
    if (input.kind != CombatInputKind::play_card) return Restriction::malformed_action;
    if (state.combat().phase != CombatPhase::play) return Restriction::wrong_phase;
    const auto* card = state.cards().get(input.card);
    const auto hand = state.zone(Zone::hand);
    if (!card || std::find(hand.begin(), hand.end(), input.card) == hand.end()) return Restriction::stale_card;
    const auto* definition = rules.card(card->definition);
    if (!definition) return Restriction::invalid_state;
    if (definition->enemy_target != input.target.present() || (input.target.present() && !action_rules::is_living_enemy(state, input.target))) return Restriction::invalid_target;
    return action_rules::card_play_restriction(state, input.card, rules);
}
Restriction legal_selection_input(const RunState& state, const SelectionInput& input) noexcept {
    if (input.expected_boundary != state.data.boundary) return Restriction::stale_boundary;
    return validate_selection_input(state, input);
}
Restriction validate_selection(const RunState& state, const SelectionInput& input, const Ruleset& rules) noexcept {
    const auto integrity = checked_combat_state_restriction(state, rules);
    if (integrity != Restriction::none) return integrity;
    return legal_selection_input(state, input);
}
bool valid_combat(const RunState& state, const Ruleset& rules) noexcept {
    if (!state.valid_combat_storage() || state.enemies().empty()) return false;
    if (!state.data.player.present() || !state.enemy_handle().present() || state.data.player == state.enemy_handle()) return false;
    if (!state.valid_combat_storage() || !state.cards().valid()) return false;
    const auto awaiting_joss = [](std::span<const ExecutionFrame> frames, std::span<const HookListener> listeners) {
        return std::any_of(frames.begin(), frames.end(), [&](const auto& frame) {
            const auto* exhaust = std::get_if<ExhaustCardsFrame>(&frame);
            if (!exhaust || exhaust->phase != ExhaustPhase::hooks || exhaust->handler.stage != 1 || exhaust->listeners.cursor >= exhaust->listeners.count) return false;
            const auto index = std::uint64_t(exhaust->listeners.begin) + exhaust->listeners.cursor;
            return index < listeners.size() && listeners[static_cast<std::size_t>(index)].handler == HookHandler::joss_paper;
        });
    };
    // Pending entries hold either their own arena or an active child's parked
    // parent arena. Inspect matching frame segments rather than global ranges.
    bool joss_pending = false;
    if (state.data.joss_paper_counter >= 5) {
        auto end = state.execution().size();
        auto slot = state.combat().active_choice;
        auto listeners = std::span<const HookListener>(state.captured_hook_listeners());
        for (std::size_t n = 0; n <= state.pending_choices().size(); ++n) {
            if (slot > state.pending_choices().size()) return false;
            const auto base = slot ? state.pending_choices()[slot - 1].depth : 0;
            if (base > end) return false;
            joss_pending |= awaiting_joss(std::span<const ExecutionFrame>(state.execution()).subspan(base, end - base), listeners);
            if (!slot) break;
            const auto& entry = state.pending_choices()[slot - 1];
            end = base; slot = entry.previous; listeners = entry.listeners.values(state.combat_overflow);
        }
        for (const auto& entry : state.pending_choices()) if (entry.phase == PendingChoicePhase::pending)
            joss_pending |= awaiting_joss(entry.frames.values(state.combat_overflow), entry.listeners.values(state.combat_overflow));
    }
    if (state.combat().exhaust_relic_count > state.combat().exhaust_relics.size() ||
        (state.data.joss_paper_counter >= 5 && !joss_pending)) return false;
    bool joss = false;
    for (std::size_t i = 0; i < state.combat().exhaust_relics.size(); ++i) {
        if (i >= state.combat().exhaust_relic_count) { if (state.combat().exhaust_relics[i] != ExhaustRelic{}) return false; continue; }
        const auto relic = state.combat().exhaust_relics[i];
        if (static_cast<unsigned>(relic) > 1 || std::find(state.combat().exhaust_relics.begin(), state.combat().exhaust_relics.begin() + i, relic) != state.combat().exhaust_relics.begin() + i) return false;
        joss |= relic == ExhaustRelic::joss_paper;
    }
    if (!joss && state.data.joss_paper_counter != 0) return false;
    if (!state.combat().centennial_puzzle && state.combat().centennial_puzzle_used) return false;
    if (static_cast<unsigned>(state.combat().phase) > static_cast<unsigned>(CombatPhase::invalid) ||
        static_cast<unsigned>(state.enemy().enemy_ai.selected) > static_cast<unsigned>(EnemyMove::third) ||
        state.seed().empty() || state.seed().size() > 4096 || state.data.numeric_seed != game_seed(state.seed()) ||
        state.combat().rules != rules_revision || state.combat().phase == CombatPhase::uninitialized || state.combat().phase == CombatPhase::invalid ||
        state.data.boundary.value == 0 || state.player().max_hp < 1 || state.player().hp < 0 || state.player().hp > state.player().max_hp ||
        state.enemy().max_hp < 1 || state.enemy().hp < 0 || state.enemy().hp > state.enemy().max_hp ||
        state.player().block < 0 || state.enemy().block < 0 ||
        state.combat().energy < 0 || state.combat().turn < 1 || state.zone(Zone::hand).size() > 10) return false;
    for (auto handle : state.enemies()) {
        const auto& enemy = *state.creature(handle);
        if (enemy.max_hp < 1 || enemy.hp < 0 || enemy.hp > enemy.max_hp || enemy.block < 0) return false;
        if (state.combat().phase == CombatPhase::combat_won && !enemy.powers.empty()) return false;
    }
    if (state.data.clock.elapsed_seconds() != std::int64_t(state.data.boundary.value - 1) * state.data.clock.config().seconds_per_action) return false;
    for (const auto& stream : state.data.rng.streams) {
        const auto& s = stream.state().words;
        if ((s[0] | s[1] | s[2] | s[3]) == 0) return false;
    }
    const bool terminal = state.combat().phase == CombatPhase::combat_won || state.combat().phase == CombatPhase::player_died;
    if ((state.combat().phase == CombatPhase::combat_won && (state.has_living_enemies() || state.player().hp == 0)) ||
        (state.combat().phase == CombatPhase::player_died && state.player().hp != 0) ||
        (!terminal && (!state.has_living_enemies() || state.player().hp == 0))) return false;
    if (state.combat().next_prompt.value == 0 || (state.combat().phase != CombatPhase::card_choice && (state.combat().selection != CardSelection{} || !state.selection_candidates().empty()))) return false;
    if (!valid_continuations(state, rules)) return false;
    if (state.combat().phase == CombatPhase::combat_won && (!state.player().powers.empty() || !state.enemy().powers.empty())) return false;
    const auto& counters = state.combat().counters;
    if (counters.finished_plays > counters.started_plays || counters.started_this_turn > counters.started_plays ||
        counters.attacks_this_turn > counters.started_this_turn || counters.zero_energy_attacks_this_turn > counters.attacks_this_turn ||
        counters.finished_ethereal > counters.finished_plays) return false;
    if (!valid_powers(state) || !valid_card_extra(state) || !valid_card_modifiers(state)) return false;
    for (const auto& modifier : state.cost_modifiers())
        if (!state.cards().get(modifier.card) || static_cast<unsigned>(modifier.kind) > 1 || (static_cast<unsigned>(modifier.expiry) & ~6U) != 0) return false;
    std::size_t live = 0, membership = 0;
    for (const auto& slot : state.cards().slots()) {
        if (slot.generation == 0 || !rules.card(slot.value.definition) || slot.value.creation_order >= state.combat().next_creation_order || slot.value.last_finished_player_turn < 0 || slot.value.last_finished_player_turn > state.combat().turn) return false;
        if (!slot.live) continue;
        ++live;
        if (slot.generation == 0 || !rules.card(slot.value.definition) || slot.value.creation_order >= state.combat().next_creation_order || slot.value.last_finished_player_turn < 0 || slot.value.last_finished_player_turn > state.combat().turn) return false;
    }
    const auto zones = state.zone_views();
    for (std::size_t a = 0; a < zones.size(); ++a) {
        const auto& zone = zones[a]; membership += zone.size();
        if (state.combat().phase == CombatPhase::combat_won && !zone.empty()) return false;
        for (std::size_t i = 0; i < zone.size(); ++i) {
            if (!state.cards().get(zone[i])) return false;
            for (std::size_t b = 0; b <= a; ++b) {
                const auto end = b == a ? i : zones[b].size();
                for (std::size_t j = 0; j < end; ++j) if (zone[i] == zones[b][j]) return false;
            }
        }
    }
    if (state.combat().phase == CombatPhase::combat_won) return membership == 0;
    if (membership > live) return false;
    for (std::size_t i = 0; i < state.cards().slots().size(); ++i) {
        const auto& slot = state.cards().slots()[i];
        if (!slot.live) continue;
        const CardHandle card{static_cast<std::uint32_t>(i), slot.generation};
        bool referenced = false;
        for (const auto& zone : zones) if (std::find(zone.begin(), zone.end(), card) != zone.end()) referenced = true;
        for (const auto& frame : state.execution()) if (const auto* program = std::get_if<CardProgramFrame>(&frame); program && program->source == card) referenced = true;
        if (!referenced && !slot.value.removed) return false;
    }
    return true;
}
}
