#include "sts2/engine/run.hpp"
#include "run_flow.hpp"
#include "../mechanics/cards.hpp"
#include <algorithm>
#include <cmath>

namespace sts2::engine {
namespace {
bool id(ContentId value, std::string_view prefix) noexcept { return SetupRules::name(value).starts_with(prefix); }
bool random(const RngState& state) noexcept { return (state.words[0] | state.words[1] | state.words[2] | state.words[3]) != 0; }
bool card(const PersistentCard& value) noexcept { return id(value.definition, "CARD.") && value.upgrade <= 1 && value.floor_added >= -1 && gameplay::valid_enchantment(value.enchantment); }
bool rewards_valid(const CombatRewards& rewards) noexcept {
    if (rewards.gold < 0 || (rewards.potion != no_content && !id(rewards.potion, "POTION.")) ||
        (rewards.potion_taken && rewards.potion == no_content) || rewards.card_taken != (rewards.selected_card < rewards.cards.size()) ||
        (rewards.selected_card != UINT8_MAX && rewards.selected_card >= rewards.cards.size()) ||
        (rewards.selecting_card && (rewards.resolved || rewards.card_taken))) return false;
    for (const auto& offered : rewards.cards) if (!card(offered) || offered.floor_added != -1) return false;
    const bool all_taken = rewards.gold_taken && (rewards.potion == no_content || rewards.potion_taken) && rewards.card_taken;
    return rewards.resolved == (rewards.skipped || all_taken);
}
bool bag(ConstRelicBag value) noexcept {
    if (value.order.size() > value.tiers.size()) return false;
    for (std::size_t i = 0; i < value.order.size(); ++i) {
        if (static_cast<std::size_t>(value.order[i]) >= value.tiers.size()) return false;
        for (std::size_t j = 0; j < i; ++j) if (value.order[i] == value.order[j]) return false;
    }
    for (const auto& tier : value.tiers) for (auto relic : tier) if (!id(relic, "RELIC.")) return false;
    return true;
}
}
bool valid_run(const RunState& state) noexcept {
    if (!valid_creature_storage(state.data, state.overflow)) return false;
    if (!valid_data_storage(state.data, state.overflow) ||
        (!state.data.combat && !state.combat_overflow.empty())) return false;
    if (!flow::valid_room_context(state)) return false;
    if (state.data.phase == RunPhase::uninitialized || state.data.phase >= RunPhase::invalid || state.data.act_index != 0 ||
        state.data.boundary.value == 0 || state.seed().empty() || state.seed().size() > 4096 || game_seed(state.seed()) != state.data.numeric_seed ||
        !SetupRules::character(state.data.character) || state.data.ascension > 10 || state.player().max_hp < 1 || state.player().hp < 0 || state.player().hp > state.player().max_hp ||
        state.data.gold < 0 || state.data.orb_slots < 0 || state.data.potion_slots < 0 || state.data.potion_slots > 4096 || state.deck().size() > 4096 ||
        state.potions().size() > static_cast<std::size_t>(state.data.potion_slots) || !std::isfinite(state.data.card_rarity_offset) ||
        !std::isfinite(state.data.potion_reward_odds) || !bag(state.shared_relics()) || !bag(state.player_relics())) return false;
    if (state.data.clock.elapsed_seconds() != std::int64_t(state.data.boundary.value - 1) * state.data.clock.config().seconds_per_action ||
        !random(state.data.event_rng.state())) return false;
    for (const auto& rng : state.data.rng.streams) if (!random(rng.state())) return false;
    for (const auto& rng : state.data.player_rng.streams) if (!random(rng.state())) return false;
    if (!state.deck().pool.valid() || !gameplay::valid_persistent_card_extra(state)) return false;
    for (const auto h : state.deck().handles())
        if (!state.deck().get(h) || std::count(state.deck().handles().begin(), state.deck().handles().end(), h) != 1) return false;
    std::size_t live_cards = 0;
    for (const auto& slot : state.deck().pool.slots()) live_cards += slot.live;
    if (live_cards != state.deck().size()) return false;
    for (std::size_t i = 0; i < state.deck().size(); ++i) {
        if (!card(state.deck()[i])) return false;
        for (std::size_t j = 0; j < i; ++j) if (state.deck()[i].instance == state.deck()[j].instance) return false;
    }
    for (auto relic : state.relics()) if (!id(relic, "RELIC.")) return false;
    const auto joss = SetupRules::find("RELIC.JOSS_PAPER");
    if ((state.data.joss_paper_counter >= 5 && !state.data.combat) ||
        (state.data.joss_paper_counter != 0 && std::find(state.relics().begin(), state.relics().end(), joss) == state.relics().end())) return false;
    for (auto potion : state.potions()) if (potion != no_content && !id(potion, "POTION.")) return false;
    for (std::size_t act_index = 0; act_index < state.data.acts.size(); ++act_index) {
        const auto act = state.act(act_index);
        if (!SetupRules::act(act.act) || !id(act.ancient, "EVENT.") || !id(act.boss, "ENCOUNTER.") ||
            (act.second_boss != no_content && !id(act.second_boss, "ENCOUNTER.")) || act.events_visited > act.events.size() + 1 ||
            act.normal_visited > act.normal.size() || act.elites_visited > act.elites.size() || act.bosses_visited > 2) return false;
        for (auto event : act.events) if (!id(event, "EVENT.")) return false;
        for (auto ancient : act.shared_ancients) if (!id(ancient, "EVENT.")) return false;
        for (auto encounter : act.normal) if (!id(encounter, "ENCOUNTER.")) return false;
        for (auto encounter : act.elites) if (!id(encounter, "ENCOUNTER.")) return false;
    }
    for (const auto& map : state.data.maps) if (map && (!valid_map(*map) || !random(map->generator_state))) return false;
    if (!state.data.maps[0] || state.data.current >= state.data.maps[0]->count || !state.data.maps[0]->nodes[state.data.current].active ||
        state.visited().empty() || state.visited().front() != state.data.maps[0]->start || state.visited().back() != state.data.current) return false;
    for (auto node : state.visited()) if (node >= state.data.maps[0]->count || !state.data.maps[0]->nodes[node].active) return false;
    for (auto relic : state.data.neow_options) if (!id(relic, "RELIC.")) return false;
    if (state.data.selected_neow != no_content && std::find(state.data.neow_options.begin(), state.data.neow_options.end(), state.data.selected_neow) == state.data.neow_options.end()) return false;
    auto linked_cards = [](const RunState& state, BasicDeckView<true> deck) noexcept {
        for (const auto& slot : state.cards().slots()) {
            if (!slot.live || !slot.value.origin.present()) continue;
            const auto* found = deck.get(slot.value.origin);
            if (!found || SetupRules::name(found->definition) != Ruleset::pinned().card(slot.value.definition)->model_id) return false;
        }
        return true;
    };
    if (state.data.combat && state.data.combat_generation == 0) return false;
    if (state.data.combat) {
        std::size_t listener = 0; bool burning_blood = false, centennial_puzzle = false;
        for (auto relic : state.relics()) {
            const auto name = SetupRules::name(relic);
            burning_blood |= name == "RELIC.BURNING_BLOOD";
            centennial_puzzle |= name == "RELIC.CENTENNIAL_PUZZLE";
            if (name == "RELIC.CHARONS_ASHES" || name == "RELIC.JOSS_PAPER") {
                const auto expected = name == "RELIC.CHARONS_ASHES" ? ExhaustRelic::charons_ashes : ExhaustRelic::joss_paper;
                if (listener >= state.combat().exhaust_relic_count || state.combat().exhaust_relics[listener++] != expected) return false;
            }
        }
        if (listener != state.combat().exhaust_relic_count || state.combat().burning_blood != burning_blood || state.combat().centennial_puzzle != centennial_puzzle) return false;
        for (const auto& slot : state.cards().slots()) if (slot.generation < state.data.combat_generation) return false;
        if (!valid_combat(state) || !linked_cards(state, state.deck())) return false;
        if (state.data.phase == RunPhase::combat) {
            if ((state.combat().phase != CombatPhase::play && state.combat().phase != CombatPhase::discard_choice && state.combat().phase != CombatPhase::card_choice)) return false;
        } else if (state.data.phase == RunPhase::complete) {
            if (!state.data.direct_combat || state.combat().phase != CombatPhase::player_died) return false;
        } else if (state.data.phase == RunPhase::combat_done) {
            if (!state.data.direct_combat || state.combat().phase != CombatPhase::combat_won) return false;
        } else return false;
        if (state.data.direct_combat && (state.data.rewards || state.data.phase == RunPhase::map)) return false;
    } else {
        if (state.data.phase == RunPhase::combat || state.data.direct_combat) return false;
        if ((state.data.phase == RunPhase::neow || state.data.phase == RunPhase::neow_done) && state.data.combat_generation != 0) return false;
        if ((state.data.phase == RunPhase::combat_done || state.data.phase == RunPhase::rewards || state.data.phase == RunPhase::complete) && state.data.combat_generation == 0) return false;
        if ((state.data.phase == RunPhase::complete) != (state.player().hp == 0)) return false;
    }
    if (state.data.rewards) {
        const auto& rewards = *state.data.rewards;
        if (state.data.combat_generation == 0 || !rewards_valid(rewards) || (state.data.phase != RunPhase::rewards && state.data.phase != RunPhase::map)) return false;
    } else if (state.data.phase == RunPhase::rewards) return false;
    return true;
}
} // namespace sts2::engine
