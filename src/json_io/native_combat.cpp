#include "sts2/json_io/native_run.hpp"
#include "card_modifiers.hpp"
#include "../engine/mechanics/cards.hpp"
#include <bit>
#include <algorithm>
#include <stdexcept>

namespace sts2::engine {
namespace {
using Json = nlohmann::json;
constexpr const char* player = "/combat/creatures/player_0";
constexpr const char* enemy = "/combat/creatures/enemy_0";
const char* creature(bool is_enemy) { return is_enemy ? enemy : player; }
std::string card_ref(CardHandle h) { return "/combat/cards/card_" + std::to_string(h.slot); }
std::string play_ref(std::uint32_t id) { return "/combat/card_plays/" + std::to_string(id); }
std::string power_ref(std::size_t id) { return "/combat/powers/" + std::to_string(id); }
Json random_state(const RngState& r) {
    return {{"counter", std::bit_cast<std::int32_t>(r.counter)}, {"s0", r.words[0]}, {"s1", r.words[1]}, {"s2", r.words[2]}, {"s3", r.words[3]}};
}
Json variables(const CombatCard& c) {
    switch (c.definition) {
    case CardId::strike: return {{"Damage", c.upgraded ? 9 : 6}};
    case CardId::defend: return {{"Block", c.upgraded ? 8 : 5}};
    case CardId::bash: return {{"Damage", c.upgraded ? 10 : 8}, {"VulnerablePower", c.upgraded ? 3 : 2}};
    case CardId::survivor: return {{"Block", c.upgraded ? 11 : 8}};
    case CardId::purity: return {{"Cards", c.upgraded ? 5 : 3}};
    case CardId::armaments: return {{"Block", 5}};
    case CardId::headbutt: return {{"Damage", c.upgraded ? 12 : 9}};
    case CardId::burning_pact: return {{"Cards", c.upgraded ? 3 : 2}};
    case CardId::true_grit: return {{"Block", c.upgraded ? 9 : 7}};
    case CardId::inflame: return {{"StrengthPower", c.upgraded ? 3 : 2}};
    case CardId::shockwave: return {{"Power", c.upgraded ? 5 : 3}};
    case CardId::tremble: return {{"VulnerablePower", c.upgraded ? 4 : 3}};
    case CardId::uppercut: return {{"Damage", 13}, {"Power", c.upgraded ? 2 : 1}};
    case CardId::count: break;
    }
    throw std::invalid_argument("Unsupported card variables");
}
Json damage_result(const HitResult& h, bool target_enemy) {
    return {{"receiver", creature(target_enemy)}, {"props", 8}, {"blocked_damage", h.blocked}, {"unblocked_damage", h.unblocked},
        {"overkill_damage", h.overkill}, {"was_block_broken", h.block_broken}, {"was_fully_blocked", h.fully_blocked}, {"was_target_killed", h.killed}};
}
}
namespace {
void export_rewards(Json& out, const RunState& run) {
    if (run.data.rewards) {
        const auto& rewards = *run.data.rewards;
        auto saved_reward = [](const char* type, int gold = 0) {
            return Json{{"card_pools", Json::array()}, {"custom_description_encounter_source_id", "NONE.NONE"}, {"gold_amount", gold},
                {"option_count", 0}, {"predetermined_model_id", "NONE.NONE"}, {"rarity_odds", "none"}, {"reward_type", type},
                {"source", "none"}, {"special_card", nullptr}, {"was_gold_stolen_back", false}};
        };
        auto entry = [](const char* kind, Json saved, bool taken) {
            return Json{{"kind", kind}, {"saved", saved}, {"taken", taken}, {"cards", Json::array()}, {"relic", nullptr}, {"potion", nullptr}, {"can_skip", false}};
        };
        Json entries = Json::array({entry("GoldReward", saved_reward("gold", rewards.gold), rewards.gold_taken)});
        if (rewards.potion != no_content) {
            auto p = entry("PotionReward", saved_reward("potion"), rewards.potion_taken);
            p["potion"] = {{"id", SetupRules::name(rewards.potion)}, {"slot_index", -1}}; entries.push_back(std::move(p));
        }
        auto cards = entry("CardReward", nullptr, rewards.card_taken); cards["can_skip"] = true;
        auto saved_card = [](const PersistentCard& c, Json floor = nullptr) {
            return Json{{"current_upgrade_level", c.upgrade}, {"enchantment", saved_enchantment(c.enchantment)}, {"floor_added_to_deck", floor},
                {"id", SetupRules::name(c.definition)}, {"props", nullptr}};
        };
        for (std::size_t i = 0; i < rewards.cards.size(); ++i) if (i != rewards.selected_card)
            cards["cards"].push_back(saved_card(rewards.cards[i]));
        entries.push_back(std::move(cards));
        Json pending = nullptr, operations = Json::array();
        if (!rewards.resolved) operations.push_back({{"kind", "offer_combat_rewards"}, {"index", nullptr}});
        if (rewards.selecting_card) {
            pending = {{"kind", "card_reward"}, {"cards", entries.back()["cards"]},
                {"alternatives", Json::array({"CardRewardAlternative"})}, {"can_skip", true}};
            operations.push_back({{"kind", "take_reward"}, {"index", rewards.potion == no_content ? 1 : 2}});
        }
        out["run_flow"] = {{"room", {{"kind", "combat_end"}}}, {"pending_choice", pending},
            {"reward_sets", rewards.resolved ? Json::array() : Json::array({{{"rewards", entries}, {"can_skip", true}}})},
            {"operations", operations}, {"combat_rewards_offered", true}};
    }
}
}
void export_native_combat(Json& out, const RunState& run, const RecordedCombat& recorded) {
    if (run.data.combat && run.combat().phase == CombatPhase::card_choice &&
        run.combat().selection.effect == SelectionEffect::add_to_hand)
        throw std::invalid_argument("Stratagem pile selection has no native diagnostic mapping yet");
    for (const auto& power : recorded.powers) {
        if (power.definition == PowerId::dark_embrace || power.definition == PowerId::stratagem)
            throw std::invalid_argument("Dark Embrace/Stratagem have no complete native diagnostic mapping yet");
        if (power.stacking != PowerInstance::Stacking::by_type)
            throw std::invalid_argument("Distinct/per-applier controlled powers have no native diagnostic mapping");
    }
    if (run.data.combat) {
        const auto slots = run.creatures().slots();
        for (std::size_t i = 0; i < slots.size(); ++i)
            if (slots[i].live && CreatureHandle{static_cast<std::uint32_t>(i), slots[i].generation} != run.data.player &&
                CreatureHandle{static_cast<std::uint32_t>(i), slots[i].generation} != run.enemies().front())
                throw std::invalid_argument("Retained extra creatures have no native diagnostic mapping");
    }
    if (run.data.combat) for (const auto* creature : {&run.player(), &run.enemy()})
        for (const auto& power : creature->power_instances(run.overflow))
            if (power.stacking != PowerInstance::Stacking::by_type)
                throw std::invalid_argument("Distinct/per-applier controlled powers have no native diagnostic mapping");
    if (!run.data.combat) {
        const bool died = run.data.phase == RunPhase::complete;
        out["current_room"] = {{"encounter_id", "ENCOUNTER.SHRINKER_BEETLE_WEAK"}, {"encounter_state", Json::object()}, {"event_id", nullptr},
            {"extra_rewards", Json::object()}, {"is_pre_finished", !died}, {"parent_event_id", nullptr}, {"reward_proportion", 1}, {"room_type", "monster"}, {"should_resume_parent_event", true}};
        out["run_flow"] = died ? Json(nullptr) : Json{{"room", {{"kind", "combat_end"}}}, {"pending_choice", nullptr},
            {"reward_sets", Json::array()}, {"operations", Json::array()}, {"combat_rewards_offered", false}};
        export_rewards(out, run);
        if (died) { out["boundary_kind"] = "run_complete"; out["run_ending_reason"] = "death"; }
        return;
    }
    const auto map_room = out["current_room"], map_flow = out["run_flow"];
    if (!recorded.attached || recorded.boundary != run.data.boundary ||
        (run.combat().phase != CombatPhase::combat_won && recorded.card_generation != run.data.combat_generation))
        throw std::invalid_argument("Missing exporter history for this execution; attach at creation");
    const bool won = run.combat().phase == CombatPhase::combat_won, died = run.combat().phase == CombatPhase::player_died, terminal = won || died;
    const bool choosing = run.combat().phase == CombatPhase::discard_choice || run.combat().phase == CombatPhase::card_choice;
    Json piles = Json::object(); constexpr std::array<const char*, 5> zone_names{"Hand", "Draw", "Discard", "Exhaust", "Play"};
    const auto zones = run.zone_views();
    for (std::size_t i = 0; i < zones.size(); ++i) {
        Json list = Json::array(); for (auto h : zones[i]) list.push_back(card_ref(h)); piles[zone_names[i]] = std::move(list);
    }
    Json combat = {{"round_number", run.combat().turn}, {"current_side", died ? "Enemy" : "Player"}, {"turn_control", {
        {"is_in_progress", !terminal}, {"is_starting", false}, {"is_enemy_turn_started", died},
        {"ending_player_turn_phase_one", false}, {"ending_player_turn_phase_two", false}, {"has_pending_loss", false},
        {"player_actions_disabled", died}, {"card_or_potion_effect_depth", choosing ? 1 : 0},
        {"players_taking_extra_turn", Json::array()}, {"players_ready_to_end_turn", died ? Json::array({"/players/0"}) : Json::array()}, {"players_ready_to_begin_enemy_turn", died ? Json::array({"/players/0"}) : Json::array()}}},
        {"allies", Json::array({player})}, {"enemies", won ? Json::array() : Json::array({enemy})}, {"escaped_creatures", Json::array()},
        {"spawned_enemy_ids", Json::array({"MONSTER.SHRINKER_BEETLE"})}, {"encounter_fields", Json::object()},
        {"players", Json::array({{{"player", "/players/0"}, {"creature", player}, {"turn_number", run.combat().turn}, {"phase", terminal ? "None" : "Play"},
            {"energy", run.combat().energy}, {"stars", 0}, {"can_use_or_remove_potions", true}, {"is_active_for_hooks", !died},
            {"piles", piles}, {"pets", Json::array()}, {"orb_capacity", 0}, {"orbs", Json::array()}}})}};
    for (const char* field : {"creatures", "cards", "powers", "card_plays", "orbs", "afflictions", "attacks", "retained_inventory_models"}) combat[field] = Json::object();
    combat["active_card_plays"] = Json::array(); combat["history"] = Json::array();
    auto make_creature = [](bool is_enemy, int block) {
        return Json{{"player", is_enemy ? Json(nullptr) : Json("/players/0")}, {"monster", nullptr}, {"side", is_enemy ? "Enemy" : "Player"},
            {"block", block}, {"hp_is_infinite", false}, {"slot_name", nullptr}, {"pet_owner", nullptr}, {"powers", Json::array()}};
    };
    combat["creatures"]["player_0"] = make_creature(false, run.player().block);
    if (!won) {
        auto e = make_creature(true, run.enemy().block);
        constexpr std::array<const char*, 3> moves{"SHRINKER_MOVE", "CHOMP_MOVE", "STOMP_MOVE"};
        Json move_states = Json::object(), log = Json::array();
        for (unsigned i = 0; i < moves.size(); ++i) move_states[moves[i]] = {{"performed_at_least_once", died && i == static_cast<unsigned>(run.combat().enemy_ai.selected)}, {"must_perform_once_before_transitioning", false}, {"follow_up_state", moves[i == 1 ? 2 : 1]}};
        for (int turn = 1; turn <= run.combat().turn; ++turn) log.push_back(moves[turn == 1 ? 0 : (turn % 2 == 0 ? 1 : 2)]);
        const auto& map = *run.data.maps[run.data.act_index]; const auto& coord = map.nodes[run.data.current];
        const GameRng monster_rng(run.data.numeric_seed + static_cast<std::uint64_t>(coord.col + coord.row + run.data.act_index + 1));
        e["monster"] = {{"id", "MONSTER.SHRINKER_BEETLE"}, {"current_hp", run.enemy().hp}, {"max_hp", run.enemy().max_hp},
            {"spawned_this_turn", run.combat().turn == 1}, {"is_performing_move", false}, {"rng", random_state(monster_rng.state())},
            {"ai", {{"current_state", moves[static_cast<std::size_t>(run.combat().enemy_ai.selected)]}, {"initial_state", moves[0]},
                {"next_move", moves[static_cast<std::size_t>(run.combat().enemy_ai.selected)]}, {"state_log", log}, {"performed_first_move", run.combat().turn > 1}, {"move_states", move_states}}}, {"model_fields", Json::object()}};
        combat["creatures"]["enemy_0"] = std::move(e);
    }
    if (won) for (std::size_t i = 0; i < run.deck().size(); ++i) {
        const auto& card = run.deck()[i];
        CardId definition = CardId::count;
        for (unsigned j = 0; j < static_cast<unsigned>(CardId::count); ++j)
            if (Ruleset::pinned().card(static_cast<CardId>(j))->model_id == SetupRules::name(card.definition)) definition = static_cast<CardId>(j);
        if (definition == CardId::count) throw std::invalid_argument("Unsupported terminal card preview");
        out["native_model_state"]["/players/0/deck/" + std::to_string(i)] = {{"model_fields", Json::object()},
            {"dynamic_vars", variables({definition, card.upgrade != 0})}};
    }
    for (std::size_t i = 0; i < run.cards().slots().size(); ++i) {
        const auto& slot = run.cards().slots()[i]; if (!slot.live) continue;
        const auto& card = slot.value;
        const auto handles = run.deck().handles();
        const auto linked = std::find(handles.begin(), handles.end(), card.origin);
        const bool has_origin = card.origin.present();
        const auto deck_index = static_cast<std::size_t>(linked - handles.begin());
        if (has_origin && deck_index >= run.deck().size()) throw std::invalid_argument("Missing permanent card relationship");
        const auto vars = variables(card);
        Json local_modifiers = Json::array();
        for (const auto& modifier : run.cost_modifiers()) if (modifier.card.slot == i && modifier.card.generation == slot.generation)
            local_modifiers.push_back({{"amount", modifier.amount}, {"type", modifier.kind == CostModifierKind::absolute ? "Absolute" : "Relative"}, {"expiration", static_cast<unsigned>(modifier.expiry)}, {"is_reduce_only", modifier.reduce_only}});
        if (has_origin) {
            auto permanent = card; permanent.upgraded = run.deck()[deck_index].upgrade != 0;
            out["native_model_state"]["/players/0/deck/" + std::to_string(deck_index)] = {{"model_fields", Json::object()}, {"dynamic_vars", variables(permanent)}};
        }
        if (won) continue;
        Json saved = has_origin ? out["players"][0]["deck"][deck_index] : Json{{"current_upgrade_level", card.upgraded ? 1 : 0}, {"enchantment", nullptr}, {"floor_added_to_deck", nullptr}, {"id", Ruleset::pinned().card(card.definition)->model_id}, {"props", nullptr}};
        saved["enchantment"] = saved_enchantment(card.enchantment);
        if (card.affliction.definition != AfflictionId::none)
            combat["afflictions"]["affliction_" + std::to_string(i)] = {{"id", model_id(card.affliction.definition)}, {"amount", card.affliction.amount}};
        const Json deck_version = has_origin ? Json("/players/0/deck/" + std::to_string(deck_index)) : Json(nullptr);
        combat["cards"]["card_" + std::to_string(i)] = {{"saved", saved},
            {"owner", "/players/0"}, {"deck_version", deck_version}, {"clone_of", nullptr},
            {"is_dupe", false}, {"base_replay_count", card.base_replay_count}, {"local_keywords", local_keywords(card.keyword_mask)}, {"exhaust_on_next_play", card.exhaust_on_next_play},
            {"has_single_turn_retain", card.single_turn_retain}, {"has_single_turn_sly", card.single_turn_sly}, {"has_been_removed_from_state", card.removed},
            {"dynamic_vars", vars}, {"energy_cost", {{"base", Ruleset::pinned().card(card.definition)->cost}, {"captured_x_value", 0}, {"local_modifiers", local_modifiers}}},
            {"base_star_cost", -1}, {"temporary_star_costs", Json::array()}, {"last_stars_spent", 0}, {"affliction", card.affliction.definition == AfflictionId::none ? Json(nullptr) : Json("/combat/afflictions/affliction_" + std::to_string(i))}, {"enchantment_runtime", enchantment_runtime(card, gameplay::find_enchantment_runtime(run, CardHandle{static_cast<std::uint32_t>(i), slot.generation}))}, {"model_fields", Json::object()}};
    }
    if (!won) for (std::size_t i = 0; i < run.cards().slots().size(); ++i) {
        const auto& slot = run.cards().slots()[i]; if (!slot.live) continue;
        combat["cards"]["card_" + std::to_string(i)]["saved"]["current_upgrade_level"] = slot.value.upgraded ? 1 : 0;
    }
    for (std::size_t i = 0; i < run.relics().size(); ++i) {
        const auto name = SetupRules::name(run.relics()[i]);
        const auto vars = name == "RELIC.BURNING_BLOOD" ? Json{{"Heal", 6}} : name == "RELIC.GOLDEN_PEARL" ? Json{{"Gold", 150}} :
            name == "RELIC.CHARONS_ASHES" ? Json{{"Damage", 3}} : name == "RELIC.JOSS_PAPER" ? Json{{"ExhaustAmount", 5}, {"Cards", 1}} : Json::object();
        out["native_model_state"]["/players/0/relics/" + std::to_string(i)] = {{"model_fields", name == "RELIC.JOSS_PAPER" ? Json{{"JossPaper._etherealCount", 0}} : Json::object()}, {"dynamic_vars", vars}};
    }
    if (!terminal) for (std::size_t i = 0; i < run.potions().size(); ++i) {
        if (run.potions()[i] == no_content) continue;
        if (SetupRules::name(run.potions()[i]) != "POTION.POTION_OF_BINDING") throw std::invalid_argument("Unsupported active potion metadata");
        out["native_model_state"]["/players/0/potions/" + std::to_string(i)] = {
            {"model_fields", Json::object()}, {"dynamic_vars", {{"VulnerablePower", 1}, {"WeakPower", 1}}}};
    }
    for (std::size_t i = 0; i < recorded.powers.size(); ++i) {
        const auto& p = recorded.powers[i]; const bool shrink = p.definition == PowerId::shrink;
        constexpr std::array names{"POWER.SHRINK_POWER", "POWER.VULNERABLE_POWER", "POWER.STRENGTH_POWER", "POWER.DEXTERITY_POWER", "POWER.WEAK_POWER", "POWER.ARTIFACT_POWER", "POWER.DARK_EMBRACE_POWER", "POWER.STRATAGEM_POWER"};
        Json value = {{"id", names[static_cast<std::size_t>(p.definition)]}, {"amount", p.amount},
            {"amount_on_turn_start", p.amount_on_turn_start}, {"skip_next_duration_tick", p.skip_next_tick},
            {"applier", p.has_applier ? Json(creature(p.applier_enemy)) : Json(nullptr)}, {"target", nullptr}, {"dynamic_vars", shrink ? Json{{"DamageDecrease", 30}} : p.definition == PowerId::vulnerable ? Json{{"DamageIncrease", 1.5}} : p.definition == PowerId::weak ? Json{{"DamageDecrease", 0.75}} : Json::object()}, {"model_fields", Json::object()}};
        if (p.active) combat["creatures"][p.enemy_owner ? "enemy_0" : "player_0"]["powers"].push_back(power_ref(i));
        else value["detached_owner"] = creature(p.enemy_owner);
        combat["powers"][std::to_string(i)] = std::move(value);
    }
    for (std::size_t i = 0; i < recorded.plays.size(); ++i) {
        const auto& p = recorded.plays[i];
        combat["card_plays"][std::to_string(i)] = {{"card", card_ref(p.card)}, {"player", "/players/0"}, {"target", p.enemy_target ? Json(enemy) : Json(nullptr)},
            {"result_pile", run.cards().get(p.card) && run.cards().get(p.card)->definition == CardId::inflame ? "None" : run.cards().get(p.card) && (run.cards().get(p.card)->definition == CardId::purity || run.cards().get(p.card)->definition == CardId::shockwave || run.cards().get(p.card)->definition == CardId::tremble) ? "Exhaust" : "Discard"}, {"resources", {{"energy_spent", p.energy}, {"energy_value", p.energy}, {"stars_spent", 0}, {"star_value", 0}}},
            {"is_auto_play", false}, {"play_index", 0}, {"play_count", 1}};
    }
    constexpr std::array<const char*, 9> kinds{"CardDrawnEntry", "CardDiscardedEntry", "EnergySpentEntry", "CardPlayStartedEntry", "CardPlayFinishedEntry", "DamageReceivedEntry", "CreatureAttackedEntry", "BlockGainedEntry", "PowerReceivedEntry"};
    for (const auto& h : recorded.history) {
        Json entry = {{"kind", h.kind == CombatEventKind::exhausted ? "CardExhaustedEntry" : kinds[static_cast<std::size_t>(h.kind)]}, {"actor", creature(h.actor_enemy)}, {"round_number", h.turn},
            {"current_side", h.enemy_side ? "Enemy" : "Player"}, {"player_turn_numbers", Json::array({{{"player", "/players/0"}, {"turn_number", h.turn}}})}};
        switch (h.kind) {
        case CombatEventKind::drawn: entry["card"] = card_ref(h.card); entry["from_hand_draw"] = h.from_hand_draw; break;
        case CombatEventKind::exhausted: case CombatEventKind::discarded: entry["card"] = card_ref(h.card); break;
        case CombatEventKind::energy_spent: entry["amount"] = h.amount; break;
        case CombatEventKind::play_started: case CombatEventKind::play_finished:
            entry["card_play"] = play_ref(h.object); if (h.kind == CombatEventKind::play_finished) entry["was_ethereal"] = false; break;
        case CombatEventKind::damaged:
            entry["result"] = damage_result(h.hit, h.actor_enemy); entry["dealer"] = creature(!h.actor_enemy);
            if (h.object == 4) entry["result"]["props"] = 4;
            entry["card_source"] = h.card.present() ? Json(card_ref(h.card)) : Json(nullptr); break;
        case CombatEventKind::attacked: entry["damage_results"] = Json::array({damage_result(h.hit, !h.actor_enemy)}); break;
        case CombatEventKind::block_gained: entry["amount"] = h.amount; entry["props"] = 8; entry["card_play"] = play_ref(h.object); break;
        case CombatEventKind::power_received: entry["amount"] = h.amount; entry["power"] = power_ref(h.object); entry["applier"] = h.has_applier ? Json(creature(h.applier_enemy)) : Json(nullptr); break;
        default: throw std::logic_error("Unexpected diagnostic event");
        }
        combat["history"].push_back(std::move(entry));
    }
    if (choosing) {
        const auto& frame = std::get<CardProgramFrame>(run.execution().back());
        combat["active_card_plays"].push_back({{"card_play", play_ref(frame.play)},
            {"result_location", {{"player", "/players/0"}, {"pile_type", run.cards().get(frame.source)->definition == CardId::purity ? "Exhaust" : "Discard"}, {"position", "Bottom"}}}});
        Json candidates = Json::array();
        if (run.combat().phase == CombatPhase::card_choice) { for (auto h : run.selection_candidates()) candidates.push_back(card_ref(h)); }
        else { for (auto h : run.zone(Zone::hand)) candidates.push_back(card_ref(h)); }
        out["pending_choice"] = {{"player", "/players/0"}, {"sources", Json::array({card_ref(frame.source)})},
            {"candidates", candidates}, {"min_select", run.combat().phase == CombatPhase::card_choice ? run.combat().selection.minimum : 1}, {"max_select", run.combat().phase == CombatPhase::card_choice ? run.combat().selection.maximum : 1}, {"require_manual_confirmation", run.cards().get(frame.source)->definition == CardId::purity}, {"cancelable", false}};
    }
    out["combat"] = std::move(combat); out["run_flow"] = nullptr;
    out["current_room"] = {{"encounter_id", "ENCOUNTER.SHRINKER_BEETLE_WEAK"}, {"encounter_state", Json::object()}, {"event_id", nullptr},
        {"extra_rewards", Json::object()}, {"is_pre_finished", won}, {"parent_event_id", nullptr}, {"reward_proportion", 1}, {"room_type", "monster"}, {"should_resume_parent_event", true}};

    if (!run.data.direct_combat && terminal) {
        out["combat"] = nullptr; out["native_model_state"] = Json::object();
        out["run_flow"] = died ? Json(nullptr) : Json{{"room", {{"kind", "combat_end"}}}, {"pending_choice", nullptr},
            {"reward_sets", Json::array()}, {"operations", Json::array()}, {"combat_rewards_offered", false}};
    }
    export_rewards(out, run);
    if (run.data.phase == RunPhase::map) {
        out["current_room"] = map_room; out["run_flow"] = map_flow; out["combat"] = nullptr; out["native_model_state"] = Json::object(); out["pending_choice"] = nullptr;
    }
    if (died) { out["boundary_kind"] = "run_complete"; out["run_ending_reason"] = "death"; }
}
} // namespace sts2::engine
