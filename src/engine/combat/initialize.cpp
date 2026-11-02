#include "sts2/engine/run.hpp"
#include "lifecycle.hpp"
#include "trace.hpp"
#include "../mechanics/cards.hpp"
#include <algorithm>
#include <new>
#include <stdexcept>

namespace sts2::engine {
using namespace gameplay;
namespace {
constexpr std::array starting_deck = {
    CardSpec{CardId::strike}, CardSpec{CardId::strike}, CardSpec{CardId::strike},
    CardSpec{CardId::strike}, CardSpec{CardId::strike}, CardSpec{CardId::defend},
    CardSpec{CardId::defend}, CardSpec{CardId::defend}, CardSpec{CardId::defend}, CardSpec{CardId::bash}};
}
StepResult initialize_combat(RunState& state, const CombatConfig& config, Scratch& scratch, const Ruleset& rules, Limits limits) {
    if (config.build != game_build || config.ascension != 0 || config.encounter != "ENCOUNTER.SHRINKER_BEETLE_WEAK" ||
        config.seed.empty() || config.seed.size() > 4096 || config.player_max_hp < 1 || config.player_hp < 1 || config.player_hp > config.player_max_hp ||
        config.card_generation == 0 || config.enemy_hp < 0 || config.enemy_hp > 40 || config.exhaust_relics.size() > 2 || config.joss_paper_counter >= 5 ||
        (config.seed.starts_with("old") && std::any_of(config.seed.begin(), config.seed.end(), [](unsigned char c) { return c >= 128; })))
        return {StepStatus::unsupported_content, state.data.boundary, Restriction::unsupported_setup};
    RunClock checked_clock;
    if (!checked_clock.initialize(config.clock)) return {StepStatus::unsupported_content, state.data.boundary, Restriction::unsupported_setup};
    const auto deck = config.deck.empty() ? std::span<const CardSpec>(starting_deck) : config.deck;
    if (deck.size() > limits.cards) return {StepStatus::resource_limit, state.data.boundary, Restriction::capacity};
    for (const auto& card : deck) if (!rules.card(card.definition) || !valid_enchantment(card.enchantment) ||
        (card.enchantment.definition != EnchantmentId::none && card.enchantment.definition != EnchantmentId::souls_power))
        return {StepStatus::unsupported_content, state.data.boundary, Restriction::unsupported_setup};
    for (std::size_t i = 0; i < config.exhaust_relics.size(); ++i)
        if (static_cast<unsigned>(config.exhaust_relics[i]) > 1 ||
            std::find(config.exhaust_relics.begin(), config.exhaust_relics.begin() + i, config.exhaust_relics[i]) != config.exhaust_relics.begin() + i)
            return {StepStatus::unsupported_content, state.data.boundary, Restriction::unsupported_setup};
    if (config.joss_paper_counter != 0 && std::find(config.exhaust_relics.begin(), config.exhaust_relics.end(), ExhaustRelic::joss_paper) == config.exhaust_relics.end())
        return {StepStatus::unsupported_content, state.data.boundary, Restriction::unsupported_setup};
    try {
        RunState next(state.overflow.allocator());
        const bool fresh = state.data.phase == RunPhase::uninitialized || state.data.phase == RunPhase::invalid ||
            !state.valid_storage() || state.seed() != config.seed;
        if (!fresh && state.data.clock.config() != config.clock)
            return {StepStatus::unsupported_content, state.data.boundary, Restriction::unsupported_setup};
        if (fresh) {
            SeededConfig setup; setup.build = config.build; setup.seed = config.seed; setup.clock = config.clock;
            const auto initialized = initialize_run(next, setup);
            if (initialized.status != StepStatus::awaiting_input) return initialized;
            next.data.direct_combat = true;
            next.relics().clear();
            if (config.burning_blood) next.relics().push_back(SetupRules::find("RELIC.BURNING_BLOOD"));
            for (auto relic : config.exhaust_relics)
                next.relics().push_back(SetupRules::find(relic == ExhaustRelic::charons_ashes ? "RELIC.CHARONS_ASHES" : "RELIC.JOSS_PAPER"));
            next.deck().clear();
            for (const auto& card : deck)
                next.deck().push_back({SetupRules::find(rules.card(card.definition)->model_id), static_cast<std::uint32_t>(next.deck().size()), static_cast<std::uint32_t>(card.upgraded), -1, card.enchantment});
        } else next = state;
        next.emplace_combat();
        next.data.phase = RunPhase::combat;
        next.data.combat_generation = config.card_generation;
        next.combat().exhaust_relic_count = static_cast<std::uint8_t>(config.exhaust_relics.size());
        std::copy(config.exhaust_relics.begin(), config.exhaust_relics.end(), next.combat().exhaust_relics.begin());
        next.data.joss_paper_counter = config.joss_paper_counter;
        next.reserve_combat(deck.size()); next.seed() = config.seed; next.data.numeric_seed = game_seed(config.seed); next.data.rng = config.initial_rng ? *config.initial_rng : RngBank(config.seed);
        next.player().clear(); next.player().hp = config.player_hp; next.player().max_hp = config.player_max_hp;
        int enemy_hp = config.enemy_hp;
        if (enemy_hp == 0) (void)next.data.rng.get(RunStream::niche).next_int(38, 41, enemy_hp);
        next.enemy().hp = enemy_hp; next.enemy().max_hp = enemy_hp; next.combat().energy = 3; next.combat().turn = 1; next.combat().burning_blood = config.burning_blood;
        for (const auto& card : deck) {
            auto handle = next.cards().create({card.definition, card.upgraded, next.combat().next_creation_order++, card.origin}, limits.cards, config.card_generation);
            if (!handle.present()) return {StepStatus::resource_limit, state.data.boundary, Restriction::capacity};
            auto& instance = *next.cards().get(handle);
            instance.enchantment = card.enchantment;
            instance.keyword_mask = rules.card(card.definition)->keyword_mask;
            if (instance.enchantment.definition == EnchantmentId::souls_power)
                instance.keyword_mask &= ~static_cast<CardKeywordMask>(CardKeyword::exhaust);
            next.zone(Zone::draw).push_back(handle);
        }
        next.data.rng.get(RunStream::shuffle).shuffle<CardHandle>(next.zone(Zone::draw));
        emit(next, scratch, CombatEventKind::combat_started);
        next.combat().phase = CombatPhase::play;
        scratch.steps = 0;
        next.execution().push_back(DrawFrame{.remaining = 5});
        const auto drawn = advance_combat(next, scratch, rules);
        if (drawn.status == StepStatus::engine_error || drawn.status == StepStatus::resource_limit)
            return {drawn.status, state.data.boundary, drawn.reason};
        if (fresh) next.data.boundary.value = 1;
        emit(next, scratch, CombatEventKind::combat_updated);
        state = std::move(next);
    } catch (const std::bad_alloc&) { return {StepStatus::resource_limit, state.data.boundary, Restriction::capacity}; } catch (const std::length_error&) { return {StepStatus::resource_limit, state.data.boundary, Restriction::capacity}; }
    scratch.steps = 0;
    return result(state);
}
}
