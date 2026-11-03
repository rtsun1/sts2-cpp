#include "sts2/engine/action.hpp"
#include "sts2/observation/observe.hpp"
#include "sts2/observation/run.hpp"
#include "sts2/persistence/checkpoint.hpp"
#include <gtest/gtest.h>

namespace sts2::engine {
TEST(Enemies, CrawlerRepeatsNativeMovesAndAccumulatesStrength) {
    RunState state; Scratch scratch; CombatConfig config;
    config.seed = "CRAWLERAUDIT";
    config.encounter = "ENCOUNTER.FUZZY_WURM_CRAWLER_WEAK";
    ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
    EXPECT_GE(observe_combat(state).enemy_hp, 55);
    EXPECT_LE(observe_combat(state).enemy_hp, 57);
    constexpr int hp[] = {76, 76, 65, 54, 54};
    constexpr int attacks[] = {0, 11, 11, 0, 18};
    for (int i = 0; i < 5; ++i) {
        ASSERT_EQ(apply_combat(state, {state.data.boundary, CombatInputKind::end_turn}, scratch).status, StepStatus::awaiting_input);
        EXPECT_EQ(observe_combat(state).hp, hp[i]);
        EXPECT_EQ(observe_combat(state).enemy_attack, attacks[i]);
        EXPECT_TRUE(valid_combat(state));
    }
}
}

namespace sts2::engine {
TEST(Enemies, CrawlerCheckpointPreservesTypeAndFuture) {
    RunState state; Scratch scratch; CombatConfig config;
    config.encounter = "ENCOUNTER.FUZZY_WURM_CRAWLER_WEAK";
    ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
    for (int i = 0; i < 2; ++i)
        ASSERT_EQ(apply_combat(state, {state.data.boundary, CombatInputKind::end_turn}, scratch).status, StepStatus::awaiting_input);
    const auto saved = checkpoint(state);
    RunState restored;
    ASSERT_EQ(restore(restored, saved.bytes, scratch), RestoreStatus::restored);
    EXPECT_EQ(state, restored);
    ASSERT_EQ(apply_combat(state, {state.data.boundary, CombatInputKind::end_turn}, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(apply_combat(restored, {restored.data.boundary, CombatInputKind::end_turn}, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(observe_combat(restored).hp, 65);
    EXPECT_EQ(state, restored);
}
}

namespace sts2::engine {
TEST(Enemies, SeededRunSetupUsesRequestedMixedEncounter) {
    RunState state; Scratch scratch; SeededConfig config;
    config.encounter = "ENCOUNTER.OVERGROWTH_CRAWLERS";
    ASSERT_EQ(initialize_seeded_combat(state, config).status, StepStatus::awaiting_input);
    ASSERT_EQ(observe_combat(state).enemies.size(), 2u);
    ASSERT_EQ(apply_action(state, CombatInput{state.data.boundary, CombatInputKind::end_turn}, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(observe_combat(state).hp, 76);
    EXPECT_TRUE(observe_combat(state).shrunk);
    EXPECT_EQ(observe_combat(state).enemies.at(0)->attack, 7);
    EXPECT_EQ(observe_combat(state).enemies.at(1)->attack, 0);
}
}

namespace sts2::engine {
TEST(Enemies, MixedTargetsRetainIdentityAndBeetleDeathRemovesShrink) {
    RunState state; Scratch scratch; CombatConfig config;
    constexpr std::array deck{CardSpec{CardId::strike, true}, CardSpec{CardId::strike, true},
        CardSpec{CardId::strike, true}, CardSpec{CardId::strike, true}, CardSpec{CardId::strike, true}};
    config.encounter = "ENCOUNTER.OVERGROWTH_CRAWLERS"; config.deck = deck; config.enemy_hp = 6;
    ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
    auto view = observe_combat(state);
    ASSERT_EQ(view.enemies.size(), 2u);
    const auto beetle = view.enemies.at(0)->handle, crawler = view.enemies.at(1)->handle;
    EXPECT_EQ(view.enemies.at(0)->definition, EnemyId::shrinker_beetle);
    EXPECT_EQ(view.enemies.at(1)->definition, EnemyId::fuzzy_wurm_crawler);
    ASSERT_EQ(apply_combat(state, {state.data.boundary, CombatInputKind::end_turn}, scratch).status, StepStatus::awaiting_input);
    ASSERT_TRUE(observe_combat(state).shrunk);
    ASSERT_FALSE(observe_combat(state).hand.empty());
    auto card = observe_combat(state).hand.front();
    ASSERT_EQ(apply_combat(state, {state.data.boundary, CombatInputKind::play_card, card, beetle}, scratch).status, StepStatus::awaiting_input);
    EXPECT_FALSE(observe_combat(state).shrunk);
    EXPECT_EQ(observe_combat(state).enemies.at(1)->hp, 6);
    card = observe_combat(state).hand.front();
    EXPECT_EQ(validate_combat(state, {state.data.boundary, CombatInputKind::play_card, card, beetle}), Restriction::invalid_target);
    EXPECT_EQ(validate_combat(state, {state.data.boundary, CombatInputKind::play_card, card, crawler}), Restriction::none);
    EXPECT_EQ(apply_combat(state, {state.data.boundary, CombatInputKind::play_card, card, crawler}, scratch).status, StepStatus::combat_won);
}
TEST(Enemies, RepeatedBeetleSetupUsesUniqueHpAndOneRngDrawPerEnemy) {
    for (const auto seed : {"ENEMYHP1", "ENEMYHP2", "ENEMYHP3"}) {
        RunState state; Scratch scratch; CombatConfig config; config.seed = seed; config.enemy_count = 2;
        ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
        const auto view = observe_combat(state);
        ASSERT_EQ(view.enemies.size(), 2u);
        EXPECT_NE(view.enemies.at(0)->max_hp, view.enemies.at(1)->max_hp);
        EXPECT_EQ(state.data.rng.get(RunStream::niche).state().counter, 2u);
    }
}
TEST(Enemies, MixedCheckpointAndCopyResumeIndependentFutures) {
    RunState state; Scratch scratch; CombatConfig config;
    config.encounter = "ENCOUNTER.OVERGROWTH_CRAWLERS";
    ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(apply_combat(state, {state.data.boundary, CombatInputKind::end_turn}, scratch).status, StepStatus::awaiting_input);
    RunState copy = state, restored;
    ASSERT_EQ(restore(restored, checkpoint(state).bytes, scratch), RestoreStatus::restored);
    ASSERT_EQ(state, restored);
    ASSERT_EQ(apply_combat(restored, {restored.data.boundary, CombatInputKind::end_turn}, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(observe_combat(restored).hp, 69);
    EXPECT_EQ(observe_combat(state).hp, 76);
    ASSERT_EQ(apply_combat(copy, {copy.data.boundary, CombatInputKind::end_turn}, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(copy, restored);
}
}

namespace sts2::engine {
TEST(Enemies, NaturalTravelEntersGeneratedCrawlerWithCheckedAndTrustedAgreement) {
    RunState state; Scratch scratch; SeededConfig config; config.seed = "CRAWLER12";
    ASSERT_EQ(initialize_run(state, config).status, StepStatus::awaiting_input);
    const auto view = observe(state);
    const auto pearl = std::find_if(view.options.begin(), view.options.end(), [](const auto& option) { return option.model_id == "RELIC.GOLDEN_PEARL"; });
    ASSERT_NE(pearl, view.options.end());
    ASSERT_EQ(apply_action(state, RunInput{state.data.boundary, RunInputKind::choose_option, pearl->id}, scratch).status, StepStatus::awaiting_input);
    const RunInput travel{state.data.boundary, RunInputKind::travel, 0, 0, 1};
    ASSERT_EQ(validate_action(state, travel), Restriction::none);
    RunState trusted = state;
    ASSERT_EQ(apply_action(state, travel, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(apply_action_trusted(trusted, travel, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(state, trusted);
    ASSERT_EQ(observe_combat(state).enemies.size(), 1u);
    EXPECT_EQ(observe_combat(state).enemies.at(0)->definition, EnemyId::fuzzy_wurm_crawler);
    EXPECT_EQ(state.data.combat_encounter, SetupRules::find("ENCOUNTER.FUZZY_WURM_CRAWLER_WEAK"));
}
TEST(Enemies, MixedLegalActionSetAgreesWithCheckedAndTrustedExecution) {
    RunState state; Scratch scratch; CombatConfig config;
    config.encounter = "ENCOUNTER.OVERGROWTH_CRAWLERS";
    ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
    const auto offered = get_actions(state);
    std::size_t count = 0;
    for_each_legal_action(state, [&](const PlayerInput& input) {
        ++count;
        EXPECT_EQ(validate_action(state, input), Restriction::none);
        RunState checked = state, trusted = state;
        const auto a = apply_action(checked, input, scratch), b = apply_action_trusted(trusted, input, scratch);
        EXPECT_EQ(a.status, b.status); EXPECT_EQ(a.reason, b.reason); EXPECT_EQ(checked, trusted);
    });
    EXPECT_EQ(count, offered.size());
    EXPECT_GT(count, 1u);
}
TEST(Enemies, CompletedScenarioCanEnterMixedEncounterWithoutEditingGeneratedQueue) {
    RunState state; Scratch scratch; SeededConfig config;
    constexpr std::array deck{CardSpec{CardId::strike, true}, CardSpec{CardId::strike, true},
        CardSpec{CardId::strike, true}, CardSpec{CardId::strike, true}, CardSpec{CardId::strike, true}};
    ASSERT_EQ(initialize_combat_scenario(state, config, deck, {}, {}, 6).status, StepStatus::awaiting_input);
    ASSERT_EQ(apply_action(state, CombatInput{state.data.boundary, CombatInputKind::play_card, observe_combat(state).hand.front(), state.enemy_handle()}, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(state.data.phase, RunPhase::rewards);
    ASSERT_EQ(apply_action(state, RunInput{state.data.boundary, RunInputKind::skip_rewards}, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(state.data.phase, RunPhase::map);
    const auto queued = std::vector<ContentId>(state.act(0).normal.begin(), state.act(0).normal.end());
    ASSERT_EQ(enter_combat_scenario(state, state.data.boundary, "ENCOUNTER.OVERGROWTH_CRAWLERS").status, StepStatus::awaiting_input);
    EXPECT_EQ(observe_combat(state).enemies.size(), 2u);
    EXPECT_TRUE(std::equal(queued.begin(), queued.end(), state.act(0).normal.begin(), state.act(0).normal.end()));
    EXPECT_TRUE(valid_run(state));
}
}

namespace sts2::engine {
TEST(Enemies, CrawlerDeathDoesNotRemoveLivingBeetlesShrink) {
    RunState state; Scratch scratch; CombatConfig config;
    constexpr std::array deck{CardSpec{CardId::strike, true}, CardSpec{CardId::strike, true},
        CardSpec{CardId::strike, true}, CardSpec{CardId::strike, true}, CardSpec{CardId::strike, true}};
    config.encounter = "ENCOUNTER.OVERGROWTH_CRAWLERS"; config.deck = deck; config.enemy_hp = 6;
    ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
    const auto beetle = observe_combat(state).enemies.at(0)->handle;
    const auto crawler = observe_combat(state).enemies.at(1)->handle;
    ASSERT_EQ(apply_combat(state, {state.data.boundary, CombatInputKind::end_turn}, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(apply_combat(state, {state.data.boundary, CombatInputKind::play_card, observe_combat(state).hand.front(), crawler}, scratch).status, StepStatus::awaiting_input);
    EXPECT_TRUE(observe_combat(state).shrunk);
    EXPECT_EQ(observe_combat(state).enemies.at(0)->hp, 6);
    EXPECT_EQ(apply_combat(state, {state.data.boundary, CombatInputKind::play_card, observe_combat(state).hand.front(), beetle}, scratch).status, StepStatus::combat_won);
}
}
