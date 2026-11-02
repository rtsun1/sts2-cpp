#include "sts2/engine/action.hpp"
#include "sts2/observation/observe.hpp"
#include "sts2/persistence/checkpoint.hpp"
#include "sts2/json_io/native_run.hpp"
#include "../../src/engine/mechanics/cards.hpp"
#include "../../src/engine/content/monsters.hpp"
#include "../support/continuation_fixture.hpp"
#include <gtest/gtest.h>
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
    ASSERT_EQ(apply_action(state, CombatInput{state.data.boundary, CombatInputKind::select_card, state.selection_candidates().front(), {}, prompt}, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(apply_action(state, CombatInput{state.data.boundary, CombatInputKind::confirm_selection, {}, {}, prompt}, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(state.combat().selection.effect, SelectionEffect::add_to_hand);
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
            CombatInput input{branch->data.boundary, CombatInputKind::confirm_selection, {}, {}, branch->combat().selection.id};
            if (branch->selection_order().empty()) { input.kind = CombatInputKind::select_card; input.card = branch->selection_candidates().front(); }
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
} // namespace sts2::engine
