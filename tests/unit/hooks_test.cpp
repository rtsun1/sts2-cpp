#include "sts2/engine/action.hpp"
#include "sts2/persistence/checkpoint.hpp"
#include "../../src/engine/hooks/dispatch.hpp"
#include "../../src/engine/execution/interpreter.hpp"
#include "../../src/engine/mechanics/cards.hpp"
#include "../../src/engine/mechanics/powers.hpp"
#include "../support/continuation_fixture.hpp"
#include <gtest/gtest.h>

namespace sts2::engine {
namespace {
void exhaust_into_shuffle(RunState& state, Scratch& scratch) {
    for (auto definition : {CardId::defend, CardId::burning_pact}) {
        CardHandle found;
        for (auto card : state.view().combat->zone(Zone::hand))
            if (state.view().combat->cards.get(card)->definition == definition) { found = card; break; }
        ASSERT_TRUE(found.present());
        ASSERT_EQ(apply_action(state, CombatInput{state.data.boundary, CombatInputKind::play_card, found}, scratch).status, StepStatus::awaiting_input);
    }
    auto combat = *state.view().combat;
    const auto prompt = combat.selection.id;
    ASSERT_EQ(apply_action(state, CombatInput{state.data.boundary, CombatInputKind::select_card, combat.selection_candidates.front(), false, prompt}, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(apply_action(state, CombatInput{state.data.boundary, CombatInputKind::confirm_selection, {}, false, prompt}, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(state.view().combat->selection.effect, SelectionEffect::add_to_hand);
}
StepResult select(RunState& state, Scratch& scratch, CardHandle card) {
    return apply_action(state, CombatInput{state.data.boundary, CombatInputKind::select_card, card, false, state.view().combat->selection.id}, scratch);
}
}

TEST(HookDispatch, DarkEmbraceFinishesItsDrawBeforeJossPaperAndBurningPactContinue) {
    RunState state; Scratch scratch;
    const std::array relics{SetupRules::find("RELIC.JOSS_PAPER")};
    scenario::prepare_exhaust_shuffle(state, false, relics, 4);
    ASSERT_EQ(apply_power_scenario(*state.view().combat, {PowerId::dark_embrace, 1}, scratch).status, StepStatus::awaiting_input);
    exhaust_into_shuffle(state, scratch);
    // Powers precede relics even though the relic was installed first.
    EXPECT_EQ(state.view().joss_paper_counter, 4u);
    EXPECT_EQ(state.view().combat->counters.non_hand_draws_this_turn, 0u);
    const auto chosen = state.view().combat->selection_candidates.front();
    ASSERT_EQ(select(state, scratch, chosen).status, StepStatus::awaiting_input);
    EXPECT_EQ(state.view().joss_paper_counter, 0u);
    EXPECT_EQ(state.view().combat->counters.non_hand_draws_this_turn, 4u);
    EXPECT_EQ(state.view().combat->counters.finished_plays, 2u);
    EXPECT_TRUE(state.view().combat->execution.empty());
    EXPECT_TRUE(state.view().combat->captured_hook_listeners.empty());
}

TEST(HookDispatch, StackedStratagemPreservesPartialPileSelectionAcrossCloneAndRestore) {
    RunState state; Scratch scratch;
    scenario::prepare_exhaust_shuffle(state);
    ASSERT_EQ(apply_power_scenario(*state.view().combat, {PowerId::stratagem, 1}, scratch).status, StepStatus::awaiting_input);
    exhaust_into_shuffle(state, scratch);
    auto combat = *state.view().combat;
    ASSERT_EQ(combat.selection.minimum, 2u);
    ASSERT_EQ(combat.selection.maximum, 2u);
    const auto first = combat.selection_candidates[0], second = combat.selection_candidates[1];
    ASSERT_EQ(select(state, scratch, first).status, StepStatus::awaiting_input);
    ASSERT_EQ(state.view().combat->phase, CombatPhase::card_choice);
    const auto saved = checkpoint(state).bytes;
    RunState clone = state, restored;
    ASSERT_EQ(restore(restored, saved, scratch), RestoreStatus::restored);
    for (auto* branch : {&clone, &restored}) {
        ASSERT_EQ(select(*branch, scratch, second).status, StepStatus::awaiting_input);
        EXPECT_EQ(branch->view().combat->phase, CombatPhase::play);
        EXPECT_EQ(branch->view().combat->counters.non_hand_draws_this_turn, 3u);
        EXPECT_NE(std::find(branch->view().combat->zone(Zone::hand).begin(), branch->view().combat->zone(Zone::hand).end(), first), branch->view().combat->zone(Zone::hand).end());
    }
    EXPECT_EQ(clone, restored);
    EXPECT_EQ(checkpoint(state).bytes, saved);
}

TEST(HookDispatch, StratagemOffersSortedCandidatesWithoutReorderingTheDrawPile) {
    const std::array deck{CardSpec{CardId::strike}, CardSpec{CardId::defend}, CardSpec{CardId::strike}};
    CombatConfig config; config.deck = deck;
    CombatState state; Scratch scratch;
    ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(apply_power_scenario(state, {PowerId::stratagem, 1}, scratch).status, StepStatus::awaiting_input);
    auto combat = state.view();
    // Deliberately establish Strike, Defend, Strike draw order, unlike UI order.
    for (std::uint32_t i = 0; i < deck.size(); ++i)
        gameplay::move_card(combat, Zone::hand, Zone::draw, {i, combat.cards.slots()[i].generation});
    const std::vector<CardHandle> draw(combat.zone(Zone::draw).begin(), combat.zone(Zone::draw).end());
    combat.execution.push_back(DrawFrame{0, false});
    gameplay::after_shuffle(combat);
    ASSERT_EQ(advance_combat(state, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(combat.phase, CombatPhase::card_choice);
    EXPECT_EQ(combat.cards.get(combat.selection_candidates.front())->definition, CardId::defend);
    EXPECT_EQ(std::vector<CardHandle>(combat.zone(Zone::draw).begin(), combat.zone(Zone::draw).end()), draw);
    EXPECT_TRUE(valid_combat(state));
}

TEST(HookDispatch, StratagemPromptRejectsAlteredSelectionCounts) {
    RunState state; Scratch scratch;
    scenario::prepare_exhaust_shuffle(state);
    exhaust_into_shuffle(state, scratch);
    auto combat = *state.view().combat;
    combat.selection.minimum = combat.selection.maximum = 2;
    EXPECT_FALSE(valid_run(state));
    EXPECT_THROW(checkpoint(state), std::invalid_argument);
    EXPECT_TRUE(get_actions(state).empty());
}

TEST(HookDispatch, StratagemRunsAgainOnTheNextShuffle) {
    CombatState state; Scratch scratch;
    ASSERT_EQ(initialize_combat(state, {}, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(apply_power_scenario(state, {PowerId::stratagem, 1}, scratch).status, StepStatus::awaiting_input);
    PromptId previous;
    for (int repeat = 0; repeat < 2; ++repeat) {
        auto combat = state.view();
        for (auto zone : {Zone::hand, Zone::draw})
            while (!combat.zone(zone).empty()) gameplay::move_card(combat, zone, Zone::discard, combat.zone(zone).front());
        gameplay::draw(combat, scratch, 1, Ruleset::pinned(), false);
        ASSERT_EQ(advance_combat(state, scratch).status, StepStatus::awaiting_input);
        ASSERT_EQ(state.view().phase, CombatPhase::card_choice);
        EXPECT_NE(state.view().selection.id, previous);
        previous = state.view().selection.id;
        const auto selected = state.view().selection_candidates.front();
        ASSERT_EQ(apply_combat(state, {state.view().boundary, CombatInputKind::select_card, selected, false, previous}, scratch).status, StepStatus::awaiting_input);
        EXPECT_EQ(state.zone(Zone::hand).size(), 2u);
        EXPECT_EQ(state.view().counters.non_hand_draws_this_turn, repeat + 1u);
    }
}

TEST(HookDispatch, CapturedPowerKeepsItsIdentityAndFinalAmountAfterRemovalAndReapplication) {
    CombatState state; Scratch scratch;
    ASSERT_EQ(initialize_combat(state, {}, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(apply_power_scenario(state, {PowerId::stratagem, 1}, scratch).status, StepStatus::awaiting_input);
    auto combat = state.view();
    combat.execution.push_back(DrawFrame{0, false});
    gameplay::after_shuffle(combat);
    const auto captured = combat.captured_hook_listeners.front().power_instance;
    gameplay::apply_power(combat, scratch, PowerId::stratagem, -1, false, false);
    gameplay::apply_power(combat, scratch, PowerId::stratagem, 1, false, false);
    EXPECT_NE(combat.player.find_power(PowerId::stratagem, combat.creature_overflow)->instance, captured);
    EXPECT_EQ(combat.captured_hook_listeners.front().removed_power_amount, 0);
    // The old captured power has amount zero; the new instance is not a candidate.
    EXPECT_EQ(advance_combat(state, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(state.view().phase, CombatPhase::play);
    EXPECT_TRUE(state.view().execution.empty());
    EXPECT_TRUE(state.view().captured_hook_listeners.empty());
}

TEST(HookDispatch, StratagemHandOverflowGoesToDiscardWithoutCountingAsDraw) {
    std::array<CardSpec, 12> deck; deck.fill({CardId::strike});
    CombatConfig config; config.deck = deck;
    CombatState state; Scratch scratch;
    ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(apply_power_scenario(state, {PowerId::stratagem, 1}, scratch).status, StepStatus::awaiting_input);
    auto combat = state.view();
    while (combat.zone(Zone::hand).size() < 10) gameplay::move_card(combat, Zone::draw, Zone::hand, combat.zone(Zone::draw).front());
    combat.execution.push_back(DrawFrame{0, false});
    gameplay::after_shuffle(combat);
    ASSERT_EQ(advance_combat(state, scratch).status, StepStatus::awaiting_input);
    const auto prompt = combat.selection.id; const auto card = combat.selection_candidates.front();
    ASSERT_EQ(apply_combat(state, {combat.boundary, CombatInputKind::select_card, card, false, prompt}, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(combat.zone(Zone::hand).size(), 10u);
    ASSERT_EQ(combat.zone(Zone::discard).size(), 1u);
    EXPECT_EQ(combat.zone(Zone::discard).front(), card);
    EXPECT_EQ(combat.counters.non_hand_draws_this_turn, 0u);
}

TEST(ExecutionFrames, DrawSchedulesWithoutSuspendingPowers) {
    CombatState state; Scratch scratch;
    ASSERT_EQ(initialize_combat(state, {}, scratch).status, StepStatus::awaiting_input);
    auto combat = state.view();
    ASSERT_TRUE(combat.execution.empty());
    ASSERT_FALSE(combat.player.has_power(PowerId::stratagem, combat.creature_overflow));
    const auto hand_size = combat.zone(Zone::hand).size();
    gameplay::draw(combat, scratch, 2, Ruleset::pinned(), false);
    ASSERT_EQ(combat.execution.size(), 1u);
    EXPECT_EQ(std::get<DrawFrame>(combat.execution.back()).remaining, 2);
    EXPECT_EQ(combat.zone(Zone::hand).size(), hand_size);
    ASSERT_EQ(advance_combat(state, scratch).status, StepStatus::awaiting_input);
    EXPECT_TRUE(combat.execution.empty());
    EXPECT_EQ(combat.zone(Zone::hand).size(), hand_size + 2);
    EXPECT_TRUE(valid_combat(state));
}

TEST(ExecutionFrames, ExhaustAndJossPaperUseFramesWithoutSuspendingPowers) {
    const std::array relics{ExhaustRelic::joss_paper};
    CombatConfig config; config.exhaust_relics = relics; config.joss_paper_counter = 4;
    CombatState state; Scratch scratch;
    ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
    auto combat = state.view();
    const auto hand_size = combat.zone(Zone::hand).size();
    gameplay::exhaust(combat, scratch, Zone::hand, combat.zone(Zone::hand).front());
    ASSERT_EQ(combat.execution.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<ExhaustFrame>(combat.execution.back()));
    EXPECT_EQ(combat.joss_paper_counter, 4u);
    gameplay::resume_frame(combat, scratch, Ruleset::pinned());
    ASSERT_EQ(combat.execution.size(), 3u);
    EXPECT_TRUE(std::holds_alternative<DrawFrame>(combat.execution.back()));
    EXPECT_EQ(combat.joss_paper_counter, 5u);
    gameplay::resume_frame(combat, scratch, Ruleset::pinned());
    ASSERT_TRUE(std::holds_alternative<RelicDrawCompletionFrame>(combat.execution.back()));
    EXPECT_EQ(combat.zone(Zone::hand).size(), hand_size);
    EXPECT_EQ(combat.joss_paper_counter, 5u);
    ASSERT_EQ(advance_combat(state, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(combat.joss_paper_counter, 0u);
    EXPECT_TRUE(combat.execution.empty());
    EXPECT_TRUE(valid_combat(state));
}

TEST(ExecutionFrames, InitialDrawBudgetFailurePreservesDestination) {
    CombatState state; Scratch scratch;
    ASSERT_EQ(initialize_combat(state, {}, scratch).status, StepStatus::awaiting_input);
    const auto saved = checkpoint(state).bytes;
    scratch.step_limit = 2;
    const auto initialized = initialize_combat(state, {}, scratch);
    EXPECT_EQ(initialized.status, StepStatus::engine_error);
    EXPECT_EQ(initialized.reason, Restriction::execution_budget);
    EXPECT_EQ(checkpoint(state).bytes, saved);
}

TEST(ExecutionFrames, CardProgramBatchesThroughFinish) {
    const std::array deck{CardSpec{CardId::defend}};
    CombatConfig config; config.deck = deck;
    CombatState state; Scratch scratch;
    ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
    auto combat = state.view();
    const auto card = combat.zone(Zone::hand).front();
    gameplay::move_card(combat, Zone::hand, Zone::play, card);
    combat.counters.started_plays = 1;
    combat.execution.push_back(CardProgramFrame{card, 0, 0, 1, combat.enemy_handle});
    scratch.steps = 0;
    gameplay::resume_frame(combat, scratch, Ruleset::pinned());
    EXPECT_TRUE(combat.execution.empty());
    EXPECT_EQ(combat.player.block, 5);
    EXPECT_EQ(combat.zone(Zone::discard).front(), card);
    EXPECT_EQ(scratch.steps, 2u); // Block and finish retain separate budget charges.
    EXPECT_TRUE(valid_combat(state));
}

TEST(ExecutionFrames, DrawBatchesButChargesEachCardAndCompletion) {
    CombatState state; Scratch scratch;
    ASSERT_EQ(initialize_combat(state, {}, scratch).status, StepStatus::awaiting_input);
    auto combat = state.view();
    const auto hand_size = combat.zone(Zone::hand).size();
    ASSERT_GE(combat.zone(Zone::draw).size(), 3u);
    gameplay::defer_draw(combat, 3, false);
    scratch.steps = 0;
    gameplay::resume_frame(combat, scratch, Ruleset::pinned());
    EXPECT_TRUE(combat.execution.empty());
    EXPECT_EQ(combat.zone(Zone::hand).size(), hand_size + 3);
    EXPECT_EQ(combat.counters.non_hand_draws_this_turn, 3u);
    EXPECT_EQ(scratch.steps, 4u);
}

TEST(ExecutionFrames, BatchedDrawStopsAtExecutionBudget) {
    CombatState state; Scratch scratch;
    ASSERT_EQ(initialize_combat(state, {}, scratch).status, StepStatus::awaiting_input);
    auto combat = state.view();
    const auto hand_size = combat.zone(Zone::hand).size();
    gameplay::defer_draw(combat, 3, false);
    scratch.steps = 0; scratch.step_limit = 2;
    const auto stopped = gameplay::resume_frame(combat, scratch, Ruleset::pinned());
    EXPECT_EQ(stopped.reason, Restriction::execution_budget);
    ASSERT_EQ(combat.execution.size(), 1u);
    EXPECT_EQ(std::get<DrawFrame>(combat.execution.back()).remaining, 1);
    EXPECT_EQ(combat.zone(Zone::hand).size(), hand_size + 2);
}

TEST(ExecutionFrames, DrawYieldsToShuffleBeforeDrawingAnyCards) {
    RunState state; Scratch scratch;
    scenario::prepare_exhaust_shuffle(state);
    auto combat = *state.view().combat;
    const auto hand_size = combat.zone(Zone::hand).size();
    gameplay::defer_draw(combat, 3, false);
    scratch.steps = 0;
    gameplay::resume_frame(combat, scratch, Ruleset::pinned());
    ASSERT_EQ(combat.execution.size(), 2u);
    EXPECT_TRUE(std::holds_alternative<ShuffleFrame>(combat.execution.back()));
    EXPECT_EQ(std::get<DrawFrame>(combat.execution.front()).remaining, 3);
    EXPECT_EQ(combat.zone(Zone::hand).size(), hand_size);
    gameplay::resume_frame(combat, scratch, Ruleset::pinned());
    EXPECT_EQ(combat.phase, CombatPhase::card_choice);
    EXPECT_EQ(combat.zone(Zone::hand).size(), hand_size);
    EXPECT_EQ(scratch.steps, 2u);
}

TEST(ExecutionFrames, FinalSelectionYieldsWhenChildReplacesItAtSameDepth) {
    RunState state; Scratch scratch;
    scenario::prepare_exhaust_shuffle(state);
    auto combat = *state.view().combat;
    const auto card = combat.zone(Zone::hand).front();
    combat.continuation_cards.push_back(card);
    combat.execution.push_back(SelectionFrame{SelectionEffect::exhaust, Zone::hand, 0, 1, 0});
    scratch.steps = 0;
    gameplay::resume_frame(combat, scratch, Ruleset::pinned());
    ASSERT_EQ(combat.execution.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<ExhaustFrame>(combat.execution.back()));
    EXPECT_TRUE(combat.continuation_cards.empty());
    EXPECT_EQ(scratch.steps, 1u);
    EXPECT_EQ(combat.zone(Zone::exhaust).front(), card);
}

}
