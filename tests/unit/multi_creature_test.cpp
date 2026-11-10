#include "../support/selection_input.hpp"
#include "sts2/engine/action.hpp"
#include "sts2/observation/observe.hpp"
#include "sts2/persistence/checkpoint.hpp"
#include "sts2/json_io/native_run.hpp"
#include "../../src/engine/mechanics/cards.hpp"
#include "../../src/engine/content/monsters.hpp"
#include "../support/continuation_fixture.hpp"
#include "storage_allocator.hpp"
#include "../../src/engine/combat/lifecycle.hpp"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <vector>
#include <ranges>

namespace sts2::engine {
static_assert(std::ranges::forward_range<EnemyPoolRange>);
namespace {
void two_beetles(RunState& state, int hp = 40, int player_hp = 80) {
    CombatConfig config; config.enemy_count = 2; config.enemy_hp = hp; config.player_hp = player_hp;
    Scratch scratch;
    ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(state.enemies().size(), 2u);
    ASSERT_EQ(state.deck().size(), 10u); // Authored encounter, ordinary starting deck.
    ASSERT_TRUE(valid_run(state));
}
CardHandle put_in_hand(RunState& state, CardId definition) {
    for (auto zone : {Zone::hand, Zone::draw, Zone::discard}) {
        for (auto card : state.zone(zone)) if (state.cards().get(card)->definition == definition) {
            if (zone != Zone::hand) gameplay::move_card(state, zone, Zone::hand, card);
            return card;
        }
    }
    return {};
}
StepResult play(RunState& state, Scratch& scratch, CardId definition, CreatureHandle target) {
    return apply_action(state, CombatInput{state.data.boundary, CombatInputKind::play_card, put_in_hand(state, definition), target}, scratch);
}
struct TurnIntervention {
    RunState* state;
    CreatureHandle first, replaced{}, spawned{};
    bool done = false;
    std::uint32_t encounter_slot = 8;
    static void receive(void* context, const RunState&, const CombatEvent& event) {
        auto& self = *static_cast<TurnIntervention*>(context);
        if (self.done || event.kind != CombatEventKind::damaged || event.actor != self.first) return;
        self.done = true;
        if (self.replaced.present()) {
            if (!self.state->creatures().erase(self.replaced)) throw std::logic_error("Expected live replacement target");
        }
        self.spawned = self.state->create_enemy_at(self.encounter_slot, {40, 40});
        if (!self.spawned.present()) throw std::logic_error("Expected free encounter position");
    }
};
}

TEST(MultiCreature, StartingDeckActionsTargetEitherBeetleAndBashIsLocal) {
    RunState state; Scratch scratch; two_beetles(state);
    const auto first = state.enemies()[0], second = state.enemies()[1];
    put_in_hand(state, CardId::bash); put_in_hand(state, CardId::strike);
    const auto actions = get_actions(state);
    for (auto definition : {CardId::strike, CardId::bash}) {
        for (auto target : {first, second}) {
            EXPECT_TRUE(std::any_of(actions.begin(), actions.end(), [&](const auto& action) {
                const auto* input = std::get_if<CombatInput>(&action.input);
                return input && input->kind == CombatInputKind::play_card && input->target == target &&
                    state.cards().get(input->card)->definition == definition && action.restriction == Restriction::none;
            }));
        }
    }
    ASSERT_EQ(play(state, scratch, CardId::bash, second).status, StepStatus::awaiting_input);
    EXPECT_EQ(state.creature(first)->hp, 40); EXPECT_EQ(state.creature(second)->hp, 32);
    EXPECT_EQ(state.creature(first)->power_amount(PowerId::vulnerable, state.overflow), 0);
    EXPECT_EQ(state.creature(second)->power_amount(PowerId::vulnerable, state.overflow), 2);
    const auto strike = put_in_hand(state, CardId::strike);
    ASSERT_EQ(observe_card(state, strike, Ruleset::pinned(), first)->damage, 6);
    ASSERT_EQ(observe_card(state, strike, Ruleset::pinned(), second)->damage, 9);
    ASSERT_EQ(play(state, scratch, CardId::strike, first).status, StepStatus::awaiting_input);
    EXPECT_EQ(state.creature(first)->hp, 34); EXPECT_EQ(state.creature(second)->hp, 32);
    const auto observation = observe_combat(state);
    ASSERT_EQ(observation.enemies.size(), 2u);
    EXPECT_EQ(observation.enemies.at(1)->handle, second);
    EXPECT_EQ(observation.enemies.at(1)->vulnerable, 2);
    EXPECT_TRUE(valid_run(state));
}

TEST(MultiCreature, ExistingShockwaveAppliesToEachEnemyAndDurationTicksOncePerSide) {
    RunState state; Scratch scratch;
    const std::array deck{CardSpec{CardId::shockwave}};
    CombatConfig config; config.enemy_count = 2; config.enemy_hp = 40; config.deck = deck;
    ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
    const auto first = state.enemies()[0], second = state.enemies()[1];
    PowerApplication artifact{PowerId::artifact, 1, true}; artifact.owner = first;
    ASSERT_EQ(apply_power_scenario(state, artifact, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(play(state, scratch, CardId::shockwave, {}).status, StepStatus::awaiting_input);
    EXPECT_EQ(state.creature(first)->power_amount(PowerId::weak, state.overflow), 0);
    EXPECT_EQ(state.creature(second)->power_amount(PowerId::weak, state.overflow), 3);
    for (auto handle : {first, second}) EXPECT_EQ(state.creature(handle)->power_amount(PowerId::vulnerable, state.overflow), 3);
    ASSERT_EQ(apply_action(state, CombatInput{state.data.boundary, CombatInputKind::end_turn}, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(state.creature(second)->power_amount(PowerId::weak, state.overflow), 2);
    for (auto handle : {first, second}) EXPECT_EQ(state.creature(handle)->power_amount(PowerId::vulnerable, state.overflow), 2);
}

TEST(MultiCreature, IndependentMovesActInPoolOrderAndStopAfterLethalDamage) {
    for (int player_hp : {80, 5}) {
        RunState state; Scratch scratch; two_beetles(state, 40, player_hp);
        const auto first = state.enemies()[0], second = state.enemies()[1];
        state.creature(first)->enemy_ai.selected = BeetleMove::chomp;
        state.creature(second)->enemy_ai.selected = BeetleMove::stomp;
        std::vector<CreatureHandle> actors;
        scratch.trace = {&actors, [](void* context, const RunState&, const CombatEvent& event) {
            if (event.kind == CombatEventKind::damaged) static_cast<std::vector<CreatureHandle>*>(context)->push_back(event.actor);
        }};
        const auto result = apply_action(state, CombatInput{state.data.boundary, CombatInputKind::end_turn}, scratch);
        if (player_hp == 80) {
            EXPECT_EQ(result.status, StepStatus::awaiting_input);
            EXPECT_EQ(state.player().hp, 60);
            EXPECT_EQ(actors, (std::vector<CreatureHandle>{first, second}));
            EXPECT_EQ(state.creature(first)->enemy_ai.selected, BeetleMove::stomp);
            EXPECT_EQ(state.creature(second)->enemy_ai.selected, BeetleMove::chomp);
        } else {
            EXPECT_EQ(result.status, StepStatus::player_died);
            EXPECT_EQ(actors, (std::vector<CreatureHandle>{first}));
            EXPECT_EQ(state.creature(second)->enemy_ai.selected, BeetleMove::stomp);
        }
        EXPECT_TRUE(valid_run(state));
    }
}

TEST(MultiCreature, EitherKillOrderLeavesSurvivorTargetableAndHealsOnlyAfterLastDeath) {
    for (std::size_t killed_first : {0u, 1u}) {
        RunState state; Scratch scratch; two_beetles(state, 6, 70);
        const auto first = state.enemies()[killed_first], survivor = state.enemies()[1 - killed_first];
        ASSERT_EQ(play(state, scratch, CardId::strike, first).status, StepStatus::awaiting_input);
        EXPECT_EQ(state.player().hp, 70);
        auto card = put_in_hand(state, CardId::strike);
        EXPECT_EQ(validate_action(state, CombatInput{state.data.boundary, CombatInputKind::play_card, card, first}), Restriction::invalid_target);
        for (const auto& action : get_actions(state)) if (const auto* input = std::get_if<CombatInput>(&action.input)) EXPECT_NE(input->target, first);
        ASSERT_EQ(apply_action(state, CombatInput{state.data.boundary, CombatInputKind::end_turn}, scratch).status, StepStatus::awaiting_input);
        EXPECT_EQ(state.creature(survivor)->enemy_ai.selected, BeetleMove::chomp);
        ASSERT_EQ(play(state, scratch, CardId::strike, survivor).status, StepStatus::awaiting_input);
        EXPECT_EQ(state.creature(survivor)->hp, 2); // Its Shrink reduced Strike to four.
        ASSERT_EQ(play(state, scratch, CardId::strike, survivor).status, StepStatus::combat_won);
        EXPECT_EQ(state.player().hp, 76);
        const auto saved = checkpoint(state).bytes;
        EXPECT_EQ(apply_action(state, CombatInput{state.data.boundary, CombatInputKind::end_turn}, scratch).reason, Restriction::wrong_phase);
        EXPECT_EQ(checkpoint(state).bytes, saved);
        EXPECT_TRUE(valid_run(state));
    }
}

TEST(MultiCreature, EnemySideSpawnAndReplacementSkipTheCurrentTraversal) {
    for (bool replacement : {false, true}) {
        RunState state; Scratch scratch;
        CombatConfig config; config.enemy_count = replacement ? 2 : 1; config.enemy_hp = 40;
        ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
        const auto first = state.enemy_handle(); state.creature(first)->enemy_ai.selected = BeetleMove::chomp;
        TurnIntervention intervention{&state, first};
        if (replacement) { intervention.replaced = state.enemies()[1]; intervention.encounter_slot = 1; }
        scratch.trace = {&intervention, TurnIntervention::receive};
        ASSERT_EQ(apply_action(state, CombatInput{state.data.boundary, CombatInputKind::end_turn}, scratch).status, StepStatus::awaiting_input);
        ASSERT_TRUE(intervention.done);
        EXPECT_EQ(state.creature(intervention.spawned)->enemy_ai.selected, BeetleMove::shrink);
        EXPECT_FALSE(state.creature(intervention.spawned)->skip_current_enemy_turn);
        EXPECT_EQ(state.player().hp, 73);
        if (replacement) {
            EXPECT_EQ(intervention.spawned.slot, intervention.replaced.slot);
            EXPECT_NE(intervention.spawned.generation, intervention.replaced.generation);
            EXPECT_EQ(state.creature(intervention.replaced), nullptr);
        }
        scratch.trace = {};
        ASSERT_EQ(apply_action(state, CombatInput{state.data.boundary, CombatInputKind::end_turn}, scratch).status, StepStatus::awaiting_input);
        EXPECT_EQ(state.creature(intervention.spawned)->enemy_ai.selected, BeetleMove::chomp);
        EXPECT_TRUE(valid_run(state));
    }
}

TEST(MultiCreature, SideEndCreationKeepsItsFlagUntilDurationWorkFinishes) {
    RunState state; Scratch scratch; two_beetles(state);
    const auto first = state.enemies()[0];
    PowerApplication weak{PowerId::weak, 1, true}; weak.owner = first;
    ASSERT_EQ(apply_power_scenario(state, weak, scratch).status, StepStatus::awaiting_input);
    struct Context { RunState* state; CreatureHandle spawned{}; bool flagged = false; } context{&state};
    scratch.trace = {&context, [](void* data, const RunState&, const CombatEvent& event) {
        auto& context = *static_cast<Context*>(data);
        if (context.spawned.present() || event.kind != CombatEventKind::power_received || event.amount != -1 || event.has_applier) return;
        context.spawned = context.state->create_enemy_at(8, {40, 40});
        if (!context.spawned.present()) throw std::logic_error("Expected free encounter position");
        context.flagged = context.state->creature(context.spawned)->skip_current_enemy_turn;
    }};
    ASSERT_EQ(apply_action(state, CombatInput{state.data.boundary, CombatInputKind::end_turn}, scratch).status, StepStatus::awaiting_input);
    ASSERT_TRUE(context.spawned.present());
    EXPECT_TRUE(context.flagged);
    EXPECT_FALSE(state.creature(context.spawned)->skip_current_enemy_turn);
    EXPECT_EQ(state.creature(context.spawned)->enemy_ai.selected, BeetleMove::shrink);
    EXPECT_EQ(state.creature(first)->power_amount(PowerId::weak, state.overflow), 0);
    EXPECT_TRUE(valid_run(state));
}

TEST(MultiCreature, PlayerSideCreationIsEligibleAndEnemyAiSurvivesRestore) {
    RunState state; Scratch scratch; two_beetles(state);
    const auto first = state.enemies()[0], second = state.enemies()[1];
    state.creature(first)->enemy_ai.selected = BeetleMove::chomp;
    ASSERT_TRUE(state.creatures().erase(second));
    const auto spawned = state.create_enemy_at(1, {40, 40});
    ASSERT_TRUE(spawned.present());
    EXPECT_FALSE(state.creature(spawned)->skip_current_enemy_turn);
    RunState clone = state, restored;
    ASSERT_EQ(restore(restored, checkpoint(state).bytes, scratch), RestoreStatus::restored);
    EXPECT_EQ(state, clone); EXPECT_EQ(state, restored);
    for (auto* branch : {&clone, &restored}) {
        ASSERT_EQ(apply_action(*branch, CombatInput{branch->data.boundary, CombatInputKind::end_turn}, scratch).status, StepStatus::awaiting_input);
        EXPECT_EQ(branch->creature(first)->enemy_ai.selected, BeetleMove::stomp);
        EXPECT_EQ(branch->creature(spawned)->enemy_ai.selected, BeetleMove::chomp);
        EXPECT_FALSE(branch->creature(spawned)->skip_current_enemy_turn);
        EXPECT_FALSE(branch->combat().enemy_side);
        EXPECT_TRUE(valid_run(*branch));
    }
    EXPECT_EQ(clone, restored);
}

TEST(MultiCreature, SuspendedCardTargetRetainsItsIdentityAcrossCloneAndRestore) {
    RunState state; Scratch scratch;
    scenario::prepare_exhaust_shuffle(state);
    const auto second = state.create_enemy_at(1, {40, 40});
    ASSERT_TRUE(second.present());
    // The existing real-content nested-prompt regression sets up suspension.
    ASSERT_EQ(play(state, scratch, CardId::defend, {}).status, StepStatus::awaiting_input);
    auto pact = put_in_hand(state, CardId::burning_pact);
    ASSERT_EQ(apply_action(state, CombatInput{state.data.boundary, CombatInputKind::play_card, pact}, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(state.combat().phase, CombatPhase::card_choice);
    const auto prompt = state.combat().selection.id;
    ASSERT_EQ(apply_action(state, test::selection_input(state, state.selection_candidates().front(), prompt), scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(observe_combat(state).selection->effect, SelectionEffect::add_to_hand);
    // Resume a real damage program after the controlled interruption, as in the
    // existing retained-target regression. No fixture-only handler is installed.
    state.cards().get(pact)->definition = CardId::strike;
    state.cards().get(pact)->origin = {};
    for (auto& frame : state.execution()) if (auto* program = std::get_if<CardProgramFrame>(&frame)) {
        program->target = second; program->instruction = 0;
    }
    state.set_enemy_membership(second, false);
    const auto replacement = state.create_enemy_at(2, {30, 30});
    ASSERT_TRUE(replacement.present());
    RunState clone = state, restored;
    ASSERT_EQ(restore(restored, checkpoint(state).bytes, scratch), RestoreStatus::restored);
    EXPECT_EQ(state, clone); EXPECT_EQ(state, restored);
    for (auto* branch : {&clone, &restored}) {
        while (branch->combat().phase == CombatPhase::card_choice) {
            const auto input = test::selection_input(*branch, branch->selection_candidates().front(), branch->combat().selection.id);
            ASSERT_EQ(apply_action(*branch, input, scratch).status, StepStatus::awaiting_input);
        }
        EXPECT_EQ(branch->creature(second)->hp, 34);
        EXPECT_EQ(branch->creature(replacement)->hp, 30);
    }
    EXPECT_EQ(clone, restored);
}

TEST(MultiCreature, ReusedGenerationCannotBeSelectedByAnOldAction) {
    RunState state; Scratch scratch; two_beetles(state);
    const auto old = state.enemies()[1];
    const auto card = put_in_hand(state, CardId::strike);
    const CombatInput action{state.data.boundary, CombatInputKind::play_card, card, old};
    ASSERT_TRUE(state.creatures().erase(old));
    const auto replacement = state.create_enemy_at(1, {40, 40});
    ASSERT_TRUE(replacement.present());
    EXPECT_EQ(validate_action(state, action), Restriction::invalid_target);
    const auto saved = checkpoint(state).bytes;
    EXPECT_EQ(apply_action(state, action, scratch).reason, Restriction::invalid_target);
    EXPECT_EQ(checkpoint(state).bytes, saved);
    EXPECT_EQ(state.creature(replacement)->hp, 40);
}

TEST(MultiCreature, RunRewardsAreGeneratedOnlyAfterTheLastEnemyDies) {
    RunState state; Scratch scratch;
    ASSERT_EQ(initialize_combat_scenario(state, {}, {}).status, StepStatus::awaiting_input);
    state.enemy().hp = 6;
    const auto second = state.create_enemy_at(1, {6, 6});
    ASSERT_TRUE(second.present());
    state.player().hp = 70;
    ASSERT_EQ(play(state, scratch, CardId::strike, state.enemy_handle()).status, StepStatus::awaiting_input);
    EXPECT_FALSE(state.data.rewards.has_value());
    ASSERT_EQ(play(state, scratch, CardId::strike, second).status, StepStatus::awaiting_input);
    EXPECT_EQ(state.data.phase, RunPhase::rewards);
    EXPECT_TRUE(state.data.rewards.has_value());
    EXPECT_EQ(state.player().hp, 76);
    const auto saved = checkpoint(state).bytes;
    EXPECT_EQ(apply_action(state, CombatInput{state.data.boundary, CombatInputKind::end_turn}, scratch).reason, Restriction::wrong_phase);
    EXPECT_EQ(checkpoint(state).bytes, saved);
}

TEST(MultiCreature, VictoryCleansRetainedPowersBeforeReleasingTheirCardSources) {
    RunState state; Scratch scratch; two_beetles(state, 6);
    const auto first = state.enemies()[0], second = state.enemies()[1];
    const auto retained = state.creatures().create_at(3, {30, 30}, 16);
    ASSERT_TRUE(retained.present());
    const auto strike = put_in_hand(state, CardId::strike);
    PowerApplication power{PowerId::strength, 1, true}; power.owner = retained; power.source = strike;
    ASSERT_EQ(apply_power_scenario(state, power, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(play(state, scratch, CardId::strike, first).status, StepStatus::awaiting_input);
    ASSERT_EQ(play(state, scratch, CardId::strike, second).status, StepStatus::combat_won);
    EXPECT_TRUE(state.creature(retained)->powers.empty());
    EXPECT_TRUE(valid_run(state));
    EXPECT_NO_THROW(checkpoint(state));
}

TEST(MultiCreature, UnmappedNativeDiagnosticsAreRejected) {
    RunState state; two_beetles(state);
    NativeRunExporter exporter;
    EXPECT_THROW(exporter.snapshot(state), std::invalid_argument);
}
namespace {
CardHandle prepare_horn_death(RunState& state, Scratch& scratch, std::size_t cards = 10,
    CardId attack = CardId::strike, bool retain_defend = false, std::uint32_t enemy_count = 2) {
    std::vector<CardSpec> deck(cards, CardSpec{CardId::defend});
    deck[0] = {attack};
    CombatConfig config;
    config.enemy_count = enemy_count; config.enemy_hp = 40;
    config.deck = deck; config.gremlin_horn = true;
    if (initialize_combat(state, config, scratch).status != StepStatus::awaiting_input)
        throw std::runtime_error("Gremlin Horn scenario initialization failed");
    for (auto zone : state.zone_views()) zone.clear();
    CardHandle source{};
    for (std::uint32_t i = 0; i < state.cards().slots().size(); ++i) {
        const auto& slot = state.cards().slots()[i];
        if (!slot.live) continue;
        const CardHandle card{i, slot.generation};
        const bool is_source = slot.value.definition == attack;
        const bool keep = is_source || (retain_defend && i == 1);
        state.zone(keep ? Zone::hand : Zone::discard).push_back(card);
        if (is_source) source = card;
    }
    state.creature(state.enemies()[0])->hp = 6;
    if (apply_power_scenario(state, {PowerId::stratagem, 1}, scratch).status != StepStatus::awaiting_input)
        throw std::runtime_error("Stratagem scenario setup failed");
    return source;
}
StepResult horn_kill(RunState& state, Scratch& scratch, CardHandle source) {
    return apply_action(state, CombatInput{state.data.boundary, CombatInputKind::play_card, source, state.enemies()[0]}, scratch);
}
}

TEST(DeathDetachment, PublicLethalActionFinishesCallerBeforePublishingStratagem) {
    RunState state; Scratch scratch;
    const auto source = prepare_horn_death(state, scratch);
    const auto dead = state.enemies()[0], survivor = state.enemies()[1];
    ASSERT_TRUE(valid_run(state));
    ASSERT_EQ(horn_kill(state, scratch, source).status, StepStatus::awaiting_input);
    ASSERT_EQ(state.combat().phase, CombatPhase::card_choice);
    EXPECT_EQ(state.combat().energy, 3); // Strike paid once, Horn granted once.
    EXPECT_EQ(state.combat().counters.started_plays, 1u);
    EXPECT_EQ(state.combat().counters.finished_plays, 1u);
    EXPECT_EQ(state.combat().counters.non_hand_draws_this_turn, 0u);
    EXPECT_EQ(state.creature(dead)->hp, 0);
    EXPECT_FALSE(state.is_enemy(dead));
    ASSERT_EQ(state.enemies().size(), 1u);
    EXPECT_EQ(state.enemies()[0], survivor);
    EXPECT_EQ(state.creature(survivor)->hp, 40);
    ASSERT_EQ(state.zone(Zone::discard).size(), 1u);
    EXPECT_EQ(state.zone(Zone::discard)[0], source);
    EXPECT_TRUE(state.zone(Zone::hand).empty());
    EXPECT_EQ(state.zone(Zone::draw).size(), 9u);
    ASSERT_EQ(state.selection_candidates().size(), 9u);
    EXPECT_TRUE(std::equal(state.selection_candidates().begin(), state.selection_candidates().end(), state.zone(Zone::draw).begin()));
    ASSERT_EQ(state.pending_choice_order().size(), 1u);
    ASSERT_NE(state.combat().active_choice, 0u);
    ASSERT_EQ(state.execution().size(), 2u);
    const auto& draw = std::get<DrawFrame>(state.execution()[0]);
    EXPECT_EQ(draw.remaining, 1);
    EXPECT_EQ(draw.phase, DrawPhase::after_shuffle);
    EXPECT_FALSE(draw.from_hand_draw);
    const auto& shuffle = std::get<ShuffleFrame>(state.execution()[1]);
    EXPECT_EQ(shuffle.phase, ShufflePhase::hooks);
    EXPECT_EQ(shuffle.handler.stage, 1);
    EXPECT_EQ(shuffle.listeners.count, 1u);
    EXPECT_TRUE(valid_run(state));

    const auto chosen = state.selection_candidates().back();
    const auto input = test::selection_input(state, chosen);
    ASSERT_EQ(apply_action(state, input, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(state.zone(Zone::hand).size(), 2u);
    EXPECT_EQ(state.zone(Zone::hand)[0], chosen);
    EXPECT_EQ(state.zone(Zone::draw).size(), 7u);
    EXPECT_EQ(state.combat().counters.non_hand_draws_this_turn, 1u);
    EXPECT_EQ(state.combat().energy, 3);
    EXPECT_EQ(state.combat().counters.finished_plays, 1u);
    EXPECT_TRUE(state.execution().empty());
    EXPECT_TRUE(state.pending_choice_order().empty());
    EXPECT_TRUE(state.captured_hook_listeners().empty());
    EXPECT_TRUE(state.continuation_cards().empty());
    EXPECT_TRUE(valid_run(state));
    const auto completed = checkpoint(state).bytes;
    EXPECT_EQ(apply_action(state, input, scratch).status, StepStatus::invalid_action);
    EXPECT_EQ(checkpoint(state).bytes, completed);
}

TEST(DeathDetachment, CloneCheckpointAndAlternativeAnswersHaveIdenticalCheckedTrustedFutures) {
    RunState state; Scratch scratch;
    const auto source = prepare_horn_death(state, scratch);
    ASSERT_EQ(horn_kill(state, scratch, source).status, StepStatus::awaiting_input);
    const auto saved = checkpoint(state).bytes;
    for (const auto chosen : state.selection_candidates()) {
        RunState checked = state, trusted = state, restored;
        ASSERT_EQ(restore(restored, saved, scratch), RestoreStatus::restored);
        ASSERT_EQ(restored, state);
        const auto input = test::selection_input(state, chosen);
        const auto expected = apply_action(checked, input, scratch);
        const auto actual = apply_action_trusted(trusted, input, scratch);
        EXPECT_EQ(actual.status, expected.status);
        EXPECT_EQ(actual.reason, expected.reason);
        ASSERT_EQ(apply_action(restored, input, scratch).status, expected.status);
        EXPECT_EQ(trusted, checked);
        EXPECT_EQ(restored, checked);
        EXPECT_TRUE(valid_run(checked));
        EXPECT_EQ(checked.combat().energy, 3);
        EXPECT_EQ(checked.combat().counters.finished_plays, 1u);
        EXPECT_EQ(checked.combat().counters.non_hand_draws_this_turn, 1u);
    }
    EXPECT_EQ(checkpoint(state).bytes, saved);
}

TEST(DeathDetachment, ExecutionBudgetFailuresRemainActiveOnCheckedAndTrustedPaths) {
    RunState root; Scratch scratch;
    const auto source = prepare_horn_death(root, scratch);
    const CombatInput input{root.data.boundary, CombatInputKind::play_card, source, root.enemies()[0]};
    RunState success = root;
    ASSERT_EQ(apply_action(success, input, scratch).status, StepStatus::awaiting_input);
    const auto required = scratch.steps;
    ASSERT_GT(required, 0u);
    for (std::uint32_t limit = 0; limit < required; ++limit) {
        RunState checked = root, trusted = root;
        Scratch first, second; first.step_limit = second.step_limit = limit;
        const auto expected = apply_action(checked, input, first);
        const auto actual = apply_action_trusted(trusted, input, second);
        EXPECT_EQ(expected.status, StepStatus::engine_error) << limit;
        EXPECT_EQ(expected.reason, Restriction::execution_budget) << limit;
        EXPECT_EQ(actual.status, expected.status) << limit;
        EXPECT_EQ(actual.reason, expected.reason) << limit;
        EXPECT_EQ(trusted, checked) << limit;
    }
}

TEST(DeathDetachment, AllocationFailuresReleaseParkedAndActiveStorage) {
    for (std::size_t allowance = 0; allowance < 8; ++allowance) {
        test::AllocationCounter allocation;
        {
            RunState state(allocation.allocator()); Scratch scratch;
            const auto source = prepare_horn_death(state, scratch, 80);
            allocation.fail_after = allocation.allocations + allowance;
            const auto result = horn_kill(state, scratch, source);
            if (result.status == StepStatus::awaiting_input) {
                ASSERT_EQ(state.combat().phase, CombatPhase::card_choice);
                EXPECT_TRUE(valid_run(state));
            } else {
                EXPECT_EQ(result.status, StepStatus::resource_limit) << allowance;
                EXPECT_EQ(result.reason, Restriction::capacity) << allowance;
                EXPECT_EQ(state.combat().phase, CombatPhase::invalid);
            }
        }
        EXPECT_EQ(allocation.outstanding, 0u) << allowance;
    }
}

TEST(DeathDetachment, CombatEndCancelsProductionCreatedChoiceAndOperands) {
    RunState state; Scratch scratch;
    const auto source = prepare_horn_death(state, scratch);
    ASSERT_EQ(horn_kill(state, scratch, source).status, StepStatus::awaiting_input);
    ASSERT_EQ(state.combat().phase, CombatPhase::card_choice);
    const auto stale_prompt = state.combat().selection.id;
    const auto chosen = state.selection_candidates()[0];
    // Exercise lifecycle cancellation using a group created entirely by a public
    // lethal action. No supported independent effect can currently end combat
    // during this single-player prompt, so invoke the terminal lifecycle seam.
    state.creature(state.enemies()[0])->hp = 0;
    gameplay::end_combat(state, scratch, true);
    EXPECT_TRUE(state.execution().empty());
    EXPECT_TRUE(state.pending_choice_order().empty());
    EXPECT_TRUE(state.pending_choices().empty());
    EXPECT_TRUE(state.continuation_cards().empty());
    EXPECT_TRUE(state.captured_hook_listeners().empty());
    EXPECT_EQ(state.combat().active_choice, 0u);
    EXPECT_EQ(state.combat().selection.id.value, 0u);
    EXPECT_TRUE(valid_run(state));
    const auto terminal = checkpoint(state).bytes;
    EXPECT_EQ(apply_action(state, test::selection_input(state, chosen, stale_prompt), scratch).reason, Restriction::wrong_phase);
    EXPECT_EQ(checkpoint(state).bytes, terminal);
}

TEST(DeathDetachment, AnswerBudgetCannotRepeatEnergyOrCallerEffects) {
    RunState root; Scratch scratch;
    const auto source = prepare_horn_death(root, scratch);
    ASSERT_EQ(horn_kill(root, scratch, source).status, StepStatus::awaiting_input);
    const auto chosen = root.selection_candidates()[0];
    const auto input = test::selection_input(root, chosen);
    RunState success = root;
    ASSERT_EQ(apply_action(success, input, scratch).status, StepStatus::awaiting_input);
    const auto required = scratch.steps;
    ASSERT_GT(required, 0u);
    for (std::uint32_t limit = 0; limit < required; ++limit) {
        RunState checked = root, trusted = root;
        Scratch first, second; first.step_limit = second.step_limit = limit;
        const auto expected = apply_action(checked, input, first);
        const auto actual = apply_action_trusted(trusted, input, second);
        EXPECT_EQ(expected.status, StepStatus::engine_error) << limit;
        EXPECT_EQ(expected.reason, Restriction::execution_budget) << limit;
        EXPECT_EQ(actual.status, expected.status) << limit;
        EXPECT_EQ(actual.reason, expected.reason) << limit;
        EXPECT_EQ(trusted, checked) << limit;
        EXPECT_EQ(checked.combat().energy, 3);
        EXPECT_EQ(checked.combat().counters.finished_plays, 1u);
    }
}

TEST(DeathDetachment, DaggerThrowKeepsItsOrdinaryHandChoiceAheadOfQueuedHorn) {
    RunState state; Scratch scratch;
    const auto source = prepare_horn_death(state, scratch, 10, CardId::dagger_throw, true);
    ASSERT_EQ(horn_kill(state, scratch, source).status, StepStatus::awaiting_input);
    ASSERT_EQ(state.combat().phase, CombatPhase::card_choice);
    ASSERT_EQ(state.pending_choice_order().size(), 1u);
    const auto horn_slot = state.pending_choice_order()[0];
    EXPECT_EQ(state.combat().active_choice, 0u);
    EXPECT_EQ(state.combat().selection.zone, Zone::hand);
    EXPECT_EQ(state.zone(Zone::hand).size(), 2u);
    EXPECT_EQ(state.zone(Zone::draw).size(), 7u);
    EXPECT_TRUE(state.zone(Zone::discard).empty());
    ASSERT_EQ(state.zone(Zone::play).size(), 1u);
    EXPECT_EQ(state.zone(Zone::play)[0], source);
    EXPECT_EQ(state.combat().counters.non_hand_draws_this_turn, 1u);
    EXPECT_EQ(state.combat().counters.finished_plays, 0u);
    EXPECT_EQ(state.combat().energy, 3);
    const auto& horn = state.pending_choices()[horn_slot - 1];
    ASSERT_EQ(horn.phase, PendingChoicePhase::pending);
    const auto frames = horn.frames.values(state.combat_overflow);
    ASSERT_EQ(frames.size(), 2u);
    EXPECT_TRUE(std::holds_alternative<DrawFrame>(frames[0]));
    EXPECT_EQ(std::get<ShuffleFrame>(frames[1]).handler.stage, 1u);
    ASSERT_EQ(state.execution().size(), 1u);
    EXPECT_EQ(std::get<CardProgramFrame>(state.execution()[0]).instruction, 3u);
    ASSERT_TRUE(valid_run(state));

    const auto first_bytes = checkpoint(state).bytes;
    const auto first_prompt = state.combat().selection.id;
    for (const auto discarded : state.selection_candidates()) {
        RunState checked = state, trusted = state, restored;
        ASSERT_EQ(restore(restored, first_bytes, scratch), RestoreStatus::restored);
        const auto first_input = test::selection_input(state, discarded);
        ASSERT_EQ(apply_action(checked, first_input, scratch).status, StepStatus::awaiting_input);
        ASSERT_EQ(apply_action_trusted(trusted, first_input, scratch).status, StepStatus::awaiting_input);
        ASSERT_EQ(apply_action(restored, first_input, scratch).status, StepStatus::awaiting_input);
        EXPECT_EQ(checked, trusted);
        EXPECT_EQ(checked, restored);
        ASSERT_EQ(checked.pending_choice_order().size(), 1u);
        EXPECT_EQ(checked.combat().active_choice, horn_slot);
        EXPECT_NE(checked.combat().selection.id, first_prompt);
        EXPECT_EQ(checked.combat().selection.zone, Zone::draw);
        EXPECT_EQ(checked.selection_candidates().size(), 7u);
        EXPECT_EQ(checked.zone(Zone::draw).size(), 7u);
        EXPECT_EQ(checked.zone(Zone::hand).size(), 1u);
        ASSERT_EQ(checked.zone(Zone::discard).size(), 2u);
        EXPECT_EQ(checked.zone(Zone::discard)[0], discarded);
        EXPECT_EQ(checked.zone(Zone::discard)[1], source);
        EXPECT_EQ(checked.combat().counters.non_hand_draws_this_turn, 1u);
        EXPECT_EQ(checked.combat().counters.finished_plays, 1u);
        EXPECT_EQ(checked.combat().energy, 3);
        ASSERT_EQ(checked.execution().size(), 2u);
        EXPECT_EQ(std::get<ShuffleFrame>(checked.execution()[1]).handler.stage, 1u);
        ASSERT_TRUE(valid_run(checked));
        const auto second_bytes = checkpoint(checked).bytes;
        for (const auto selected : checked.selection_candidates()) {
            RunState branch = checked, fast = checked, saved;
            ASSERT_EQ(restore(saved, second_bytes, scratch), RestoreStatus::restored);
            const auto second_input = test::selection_input(checked, selected);
            ASSERT_EQ(apply_action(branch, second_input, scratch).status, StepStatus::awaiting_input);
            ASSERT_EQ(apply_action_trusted(fast, second_input, scratch).status, StepStatus::awaiting_input);
            ASSERT_EQ(apply_action(saved, second_input, scratch).status, StepStatus::awaiting_input);
            EXPECT_EQ(branch, fast);
            EXPECT_EQ(branch, saved);
            EXPECT_EQ(branch.combat().energy, 3);
            EXPECT_EQ(branch.combat().counters.finished_plays, 1u);
            EXPECT_EQ(branch.combat().counters.non_hand_draws_this_turn, 2u);
            EXPECT_EQ(branch.zone(Zone::hand).size(), 3u);
            EXPECT_EQ(branch.zone(Zone::hand)[1], selected);
            ASSERT_EQ(branch.zone(Zone::discard).size(), 2u);
            EXPECT_EQ(branch.zone(Zone::discard)[0], discarded);
            EXPECT_EQ(branch.zone(Zone::discard)[1], source);
            EXPECT_TRUE(branch.execution().empty());
            EXPECT_TRUE(branch.pending_choice_order().empty());
            EXPECT_TRUE(branch.continuation_cards().empty());
            EXPECT_TRUE(branch.captured_hook_listeners().empty());
            EXPECT_TRUE(valid_run(branch));
        }
    }
    EXPECT_EQ(checkpoint(state).bytes, first_bytes);
}

TEST(DeathDetachment, CommonPublicPathCloneAndAnswerNeedNoOverflowAllocations) {
    test::AllocationCounter allocation;
    {
        RunState state(allocation.allocator()); Scratch scratch;
        const auto source = prepare_horn_death(state, scratch);
        const auto before = allocation.allocations;
        allocation.fail_after = before;
        ASSERT_EQ(horn_kill(state, scratch, source).status, StepStatus::awaiting_input);
        ASSERT_EQ(state.combat().phase, CombatPhase::card_choice);
        RunState clone(std::as_const(state));
        EXPECT_EQ(clone, state);
        const auto chosen = clone.selection_candidates()[0];
        ASSERT_EQ(apply_action(clone, test::selection_input(clone, chosen), scratch).status, StepStatus::awaiting_input);
        EXPECT_TRUE(valid_run(clone));
        EXPECT_EQ(allocation.allocations, before);
    }
    EXPECT_EQ(allocation.outstanding, 0u);
}

TEST(DeathDetachment, PostDamageDebuffsDoNotReappearOnTheCleanedUpDeadTarget) {
    for (auto attack : {CardId::bash, CardId::uppercut}) {
        RunState state; Scratch scratch;
        const auto source = prepare_horn_death(state, scratch, 10, attack);
        const auto dead = state.enemies()[0];
        PowerApplication artifact{PowerId::artifact, 1, true}; artifact.owner = dead;
        ASSERT_EQ(apply_power_scenario(state, artifact, scratch).status, StepStatus::awaiting_input);
        ASSERT_EQ(horn_kill(state, scratch, source).status, StepStatus::awaiting_input);
        ASSERT_EQ(state.combat().phase, CombatPhase::card_choice);
        EXPECT_FALSE(state.is_enemy(dead));
        EXPECT_TRUE(state.creature(dead)->power_instances(state.overflow).empty());
        EXPECT_EQ(state.combat().counters.finished_plays, 1u);
        EXPECT_TRUE(valid_run(state));
    }
}

TEST(DeathDetachment, FinalLethalDaggerThrowCancelsDrawAndHandChoice) {
    RunState state; Scratch scratch;
    const auto source = prepare_horn_death(state, scratch, 10, CardId::dagger_throw, true, 1);
    ASSERT_TRUE(valid_run(state));
    ASSERT_EQ(horn_kill(state, scratch, source).status, StepStatus::combat_won);
    EXPECT_EQ(state.combat().phase, CombatPhase::combat_won);
    EXPECT_EQ(state.combat().counters.non_hand_draws_this_turn, 0u);
    EXPECT_TRUE(state.execution().empty());
    EXPECT_TRUE(state.pending_choices().empty());
    EXPECT_TRUE(state.pending_choice_order().empty());
    EXPECT_TRUE(state.selection_candidates().empty());
    EXPECT_EQ(state.combat().active_choice, 0u);
    EXPECT_EQ(state.combat().selection.id.value, 0u);
    EXPECT_TRUE(valid_run(state));
    RunState restored;
    ASSERT_EQ(restore(restored, checkpoint(state).bytes, scratch), RestoreStatus::restored);
    EXPECT_EQ(restored, state);
}

TEST(DeathDetachment, PublicStatesMatchThePinnedNativeQueueTrace) {
    using Json = nlohmann::json;
    std::ifstream input(KERNEL_DETACHMENT_FIXTURE);
    ASSERT_TRUE(input.good());
    const auto evidence = Json::parse(input);
    ASSERT_EQ(evidence.at("game_dll_sha256"), game_digest);
    for (const auto& scenario : evidence.at("cases")) {
        const bool dagger = scenario.at("case_name") == "dagger-throw";
        SCOPED_TRACE(scenario.at("case_name").get<std::string>());
        RunState state; Scratch scratch;
        const auto source = prepare_horn_death(state, scratch, 10,
            dagger ? CardId::dagger_throw : CardId::strike, dagger);
        const auto dead = state.enemies()[0];
        ASSERT_EQ(horn_kill(state, scratch, source).status, StepStatus::awaiting_input);
        const auto& observations = scenario.at("observations");
        for (std::size_t step = 0; step < observations.size(); ++step) {
            const auto& native = observations.at(step);
            SCOPED_TRACE(native.at("stage").get<std::string>());
            EXPECT_EQ(state.combat().energy, native.at("energy"));
            EXPECT_EQ(state.is_enemy(dead), native.at("victim_member"));
            EXPECT_EQ(state.creature(dead)->hp, native.at("victim_hp"));
            EXPECT_EQ(state.creature(dead)->power_instances(state.overflow).size(), native.at("victim_powers"));
            Json enemies = Json::array();
            for (auto enemy : state.enemies()) enemies.push_back(state.creature(enemy)->hp);
            EXPECT_EQ(enemies, native.at("enemies"));
            for (const auto& pile : native.at("piles")) {
                const auto name = pile.at("zone").get<std::string>();
                constexpr std::array names{"Hand", "Draw", "Discard", "Exhaust", "Play"};
                const auto found = std::find(names.begin(), names.end(), name);
                ASSERT_NE(found, names.end());
                Json cards = Json::array();
                for (auto card : state.zone(static_cast<Zone>(found - names.begin()))) cards.push_back(card.slot);
                EXPECT_EQ(cards, pile.at("cards")) << name;
            }
            EXPECT_EQ(state.combat().counters.finished_plays == 1, native.at("action_completed"));
            const auto& hooks = native.at("native_hook_actions");
            ASSERT_EQ(hooks.size(), 1u);
            EXPECT_EQ(hooks[0].at("source"), "RELIC.GREMLIN_HORN");
            EXPECT_EQ(state.pending_choice_order().empty(), hooks[0].at("task_completed"));
            EXPECT_EQ(state.pending_choice_order().empty(), hooks[0].at("action_completed"));
            ASSERT_TRUE(valid_run(state));
            if (step + 1 == observations.size()) {
                EXPECT_EQ(native.at("stage"), "complete");
                EXPECT_TRUE(state.execution().empty());
                EXPECT_TRUE(state.pending_choice_order().empty());
            } else {
                ASSERT_EQ(state.combat().phase, CombatPhase::card_choice);
                const auto zone = native.at("stage") == "horn-choice" ? Zone::draw : Zone::hand;
                ASSERT_EQ(state.combat().selection.zone, zone);
                Json candidates = Json::array();
                for (auto card : state.selection_candidates()) candidates.push_back(card.slot);
                EXPECT_EQ(candidates, native.at("candidates"));
                EXPECT_EQ(state.combat().selection.minimum, native.at("minimum"));
                EXPECT_EQ(state.combat().selection.maximum, native.at("maximum"));
                const auto chosen = state.zone(zone).front();
                ASSERT_EQ(apply_action(state, test::selection_input(state, chosen), scratch).status, StepStatus::awaiting_input);
            }
        }
    }
}

} // namespace sts2::engine
