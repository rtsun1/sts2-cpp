#include "sts2/engine/action.hpp"
#include "sts2/persistence/checkpoint.hpp"
#include "../../src/engine/hooks/dispatch.hpp"
#include "../../src/engine/execution/interpreter.hpp"
#include "../../src/engine/mechanics/exhaust.hpp"
#include "../../src/engine/mechanics/cards.hpp"
#include "../../src/engine/mechanics/powers.hpp"
#include "../support/continuation_fixture.hpp"
#include <gtest/gtest.h>

namespace sts2::engine {
namespace {
void exhaust_into_shuffle(RunState& state, Scratch& scratch) {
    for (auto definition : {CardId::defend, CardId::burning_pact}) {
        CardHandle found;
        for (auto card : state.zone(Zone::hand))
            if (state.cards().get(card)->definition == definition) { found = card; break; }
        ASSERT_TRUE(found.present());
        ASSERT_EQ(apply_action(state, CombatInput{state.data.boundary, CombatInputKind::play_card, found}, scratch).status, StepStatus::awaiting_input);
    }
    auto& combat = state;
    const auto prompt = combat.combat().selection.id;
    ASSERT_EQ(apply_action(state, CombatInput{state.data.boundary, CombatInputKind::select_card, combat.selection_candidates().front(), {}, prompt}, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(apply_action(state, CombatInput{state.data.boundary, CombatInputKind::confirm_selection, {}, {}, prompt}, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(state.combat().selection.effect, SelectionEffect::add_to_hand);
}
StepResult select(RunState& state, Scratch& scratch, CardHandle card) {
    return apply_action(state, CombatInput{state.data.boundary, CombatInputKind::select_card, card, {}, state.combat().selection.id}, scratch);
}
}

TEST(HookDispatch, DarkEmbraceFinishesItsDrawBeforeJossPaperAndBurningPactContinue) {
    RunState state; Scratch scratch;
    const std::array relics{SetupRules::find("RELIC.JOSS_PAPER")};
    scenario::prepare_exhaust_shuffle(state, false, relics, 4);
    ASSERT_EQ(apply_power_scenario(state, {PowerId::dark_embrace, 1}, scratch).status, StepStatus::awaiting_input);
    exhaust_into_shuffle(state, scratch);
    // Powers precede relics even though the relic was installed first.
    EXPECT_EQ(state.data.joss_paper_counter, 4u);
    EXPECT_EQ(state.combat().counters.non_hand_draws_this_turn, 0u);
    const auto chosen = state.selection_candidates().front();
    ASSERT_EQ(select(state, scratch, chosen).status, StepStatus::awaiting_input);
    EXPECT_EQ(state.data.joss_paper_counter, 0u);
    EXPECT_EQ(state.combat().counters.non_hand_draws_this_turn, 4u);
    EXPECT_EQ(state.combat().counters.finished_plays, 2u);
    EXPECT_TRUE(state.execution().empty());
    EXPECT_TRUE(state.captured_hook_listeners().empty());
}

TEST(HookDispatch, StackedStratagemPreservesPartialPileSelectionAcrossCloneAndRestore) {
    RunState state; Scratch scratch;
    scenario::prepare_exhaust_shuffle(state);
    ASSERT_EQ(apply_power_scenario(state, {PowerId::stratagem, 1}, scratch).status, StepStatus::awaiting_input);
    exhaust_into_shuffle(state, scratch);
    auto& combat = state;
    ASSERT_EQ(combat.combat().selection.minimum, 2u);
    ASSERT_EQ(combat.combat().selection.maximum, 2u);
    const auto first = combat.selection_candidates()[0], second = combat.selection_candidates()[1];
    ASSERT_EQ(select(state, scratch, first).status, StepStatus::awaiting_input);
    ASSERT_EQ(state.combat().phase, CombatPhase::card_choice);
    const auto saved = checkpoint(state).bytes;
    RunState clone = state, restored;
    ASSERT_EQ(restore(restored, saved, scratch), RestoreStatus::restored);
    for (auto* branch : {&clone, &restored}) {
        ASSERT_EQ(select(*branch, scratch, second).status, StepStatus::awaiting_input);
        EXPECT_EQ(branch->combat().phase, CombatPhase::play);
        EXPECT_EQ(branch->combat().counters.non_hand_draws_this_turn, 3u);
        EXPECT_NE(std::find(branch->zone(Zone::hand).begin(), branch->zone(Zone::hand).end(), first), branch->zone(Zone::hand).end());
    }
    EXPECT_EQ(clone, restored);
    EXPECT_EQ(checkpoint(state).bytes, saved);
}

TEST(HookDispatch, StratagemOffersSortedCandidatesWithoutReorderingTheDrawPile) {
    const std::array deck{CardSpec{CardId::strike}, CardSpec{CardId::defend}, CardSpec{CardId::strike}};
    CombatConfig config; config.deck = deck;
    RunState state; Scratch scratch;
    ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(apply_power_scenario(state, {PowerId::stratagem, 1}, scratch).status, StepStatus::awaiting_input);
    auto& combat = state;
    // Deliberately establish Strike, Defend, Strike draw order, unlike UI order.
    for (std::uint32_t i = 0; i < deck.size(); ++i)
        gameplay::move_card(combat, Zone::hand, Zone::draw, {i, combat.cards().slots()[i].generation});
    const std::vector<CardHandle> draw(combat.zone(Zone::draw).begin(), combat.zone(Zone::draw).end());
    combat.execution().push_back(DrawFrame{.phase = DrawPhase::after_shuffle, .from_hand_draw = false});
    combat.execution().push_back(ShuffleFrame{.listeners = gameplay::capture_shuffle_listeners(combat), .phase = ShufflePhase::hooks});
    ASSERT_EQ(advance_combat(state, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(combat.combat().phase, CombatPhase::card_choice);
    EXPECT_EQ(combat.cards().get(combat.selection_candidates().front())->definition, CardId::defend);
    EXPECT_EQ(std::vector<CardHandle>(combat.zone(Zone::draw).begin(), combat.zone(Zone::draw).end()), draw);
    EXPECT_TRUE(valid_combat(state));
}

TEST(HookDispatch, StratagemPromptRejectsAlteredSelectionCounts) {
    RunState state; Scratch scratch;
    scenario::prepare_exhaust_shuffle(state);
    exhaust_into_shuffle(state, scratch);
    auto& combat = state;
    combat.combat().selection.minimum = combat.combat().selection.maximum = 2;
    EXPECT_FALSE(valid_run(state));
    EXPECT_THROW(checkpoint(state), std::invalid_argument);
    EXPECT_TRUE(get_actions(state).empty());
}

TEST(HookDispatch, StratagemRunsAgainOnTheNextShuffle) {
    RunState state; Scratch scratch;
    ASSERT_EQ(initialize_combat(state, {}, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(apply_power_scenario(state, {PowerId::stratagem, 1}, scratch).status, StepStatus::awaiting_input);
    PromptId previous;
    for (int repeat = 0; repeat < 2; ++repeat) {
        auto& combat = state;
        for (auto zone : {Zone::hand, Zone::draw})
            while (!combat.zone(zone).empty()) gameplay::move_card(combat, zone, Zone::discard, combat.zone(zone).front());
        combat.execution().push_back(DrawFrame{.remaining = 1, .from_hand_draw = false});
        ASSERT_EQ(advance_combat(state, scratch).status, StepStatus::awaiting_input);
        ASSERT_EQ(state.combat().phase, CombatPhase::card_choice);
        EXPECT_NE(state.combat().selection.id, previous);
        previous = state.combat().selection.id;
        const auto selected = state.selection_candidates().front();
        ASSERT_EQ(apply_combat(state, {state.data.boundary, CombatInputKind::select_card, selected, {}, previous}, scratch).status, StepStatus::awaiting_input);
        EXPECT_EQ(state.zone(Zone::hand).size(), 2u);
        EXPECT_EQ(state.combat().counters.non_hand_draws_this_turn, repeat + 1u);
    }
}

TEST(HookDispatch, CapturedPowerKeepsItsIdentityAndFinalAmountAfterRemovalAndReapplication) {
    RunState state; Scratch scratch;
    ASSERT_EQ(initialize_combat(state, {}, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(apply_power_scenario(state, {PowerId::stratagem, 1}, scratch).status, StepStatus::awaiting_input);
    auto& combat = state;
    combat.execution().push_back(DrawFrame{.phase = DrawPhase::after_shuffle, .from_hand_draw = false});
    combat.execution().push_back(ShuffleFrame{.listeners = gameplay::capture_shuffle_listeners(combat), .phase = ShufflePhase::hooks});
    const auto captured = combat.captured_hook_listeners().front().power_instance;
    gameplay::apply_power(combat, scratch, PowerId::stratagem, -1, false, false);
    gameplay::apply_power(combat, scratch, PowerId::stratagem, 1, false, false);
    EXPECT_NE(combat.player().find_power(PowerId::stratagem, combat.overflow)->instance, captured);
    EXPECT_EQ(combat.captured_hook_listeners().front().removed_power_amount, 0);
    // The old captured power has amount zero; the new instance is not a candidate.
    EXPECT_EQ(advance_combat(state, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(state.combat().phase, CombatPhase::play);
    EXPECT_TRUE(state.execution().empty());
    EXPECT_TRUE(state.captured_hook_listeners().empty());
}

TEST(HookDispatch, StratagemHandOverflowGoesToDiscardWithoutCountingAsDraw) {
    std::array<CardSpec, 12> deck; deck.fill({CardId::strike});
    CombatConfig config; config.deck = deck;
    RunState state; Scratch scratch;
    ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(apply_power_scenario(state, {PowerId::stratagem, 1}, scratch).status, StepStatus::awaiting_input);
    auto& combat = state;
    while (combat.zone(Zone::hand).size() < 10) gameplay::move_card(combat, Zone::draw, Zone::hand, combat.zone(Zone::draw).front());
    combat.execution().push_back(DrawFrame{.phase = DrawPhase::after_shuffle, .from_hand_draw = false});
    combat.execution().push_back(ShuffleFrame{.listeners = gameplay::capture_shuffle_listeners(combat), .phase = ShufflePhase::hooks});
    ASSERT_EQ(advance_combat(state, scratch).status, StepStatus::awaiting_input);
    const auto prompt = combat.combat().selection.id; const auto card = combat.selection_candidates().front();
    ASSERT_EQ(apply_combat(state, {combat.data.boundary, CombatInputKind::select_card, card, {}, prompt}, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(combat.zone(Zone::hand).size(), 10u);
    ASSERT_EQ(combat.zone(Zone::discard).size(), 1u);
    EXPECT_EQ(combat.zone(Zone::discard).front(), card);
    EXPECT_EQ(combat.combat().counters.non_hand_draws_this_turn, 0u);
}

TEST(ExecutionFrames, DrawSchedulesWithoutSuspendingPowers) {
    RunState state; Scratch scratch;
    ASSERT_EQ(initialize_combat(state, {}, scratch).status, StepStatus::awaiting_input);
    auto& combat = state;
    ASSERT_TRUE(combat.execution().empty());
    ASSERT_FALSE(combat.player().has_power(PowerId::stratagem, combat.overflow));
    const auto hand_size = combat.zone(Zone::hand).size();
    combat.execution().push_back(DrawFrame{.remaining = 2, .from_hand_draw = false});
    ASSERT_EQ(combat.execution().size(), 1u);
    EXPECT_EQ(std::get<DrawFrame>(combat.execution().back()).remaining, 2);
    EXPECT_EQ(combat.zone(Zone::hand).size(), hand_size);
    ASSERT_EQ(advance_combat(state, scratch).status, StepStatus::awaiting_input);
    EXPECT_TRUE(combat.execution().empty());
    EXPECT_EQ(combat.zone(Zone::hand).size(), hand_size + 2);
    EXPECT_TRUE(valid_combat(state));
}

TEST(ExecutionFrames, ExhaustAndJossPaperUseFramesWithoutSuspendingPowers) {
    const std::array relics{ExhaustRelic::joss_paper};
    CombatConfig config; config.exhaust_relics = relics; config.joss_paper_counter = 4;
    RunState state; Scratch scratch;
    ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
    auto& combat = state;
    const auto hand_size = combat.zone(Zone::hand).size();
    const auto card = combat.zone(Zone::hand).front();
    const auto cards = gameplay::capture_exhaust_cards(combat, std::span(&card, 1));
    combat.execution().push_back(ExhaustCardsFrame{.cards = cards});
    ASSERT_EQ(combat.execution().size(), 1u);
    ASSERT_TRUE(std::holds_alternative<ExhaustCardsFrame>(combat.execution().back()));
    EXPECT_EQ(combat.data.joss_paper_counter, 4u);
    gameplay::resume_frame(combat, scratch, Ruleset::pinned());
    ASSERT_EQ(combat.execution().size(), 2u);
    EXPECT_TRUE(std::holds_alternative<DrawFrame>(combat.execution().back()));
    EXPECT_EQ(combat.data.joss_paper_counter, 5u);
    gameplay::resume_frame(combat, scratch, Ruleset::pinned());
    ASSERT_TRUE(std::holds_alternative<ExhaustCardsFrame>(combat.execution().back()));
    EXPECT_EQ(std::get<ExhaustCardsFrame>(combat.execution().back()).handler.stage, 1u);
    EXPECT_EQ(combat.zone(Zone::hand).size(), hand_size);
    EXPECT_EQ(combat.data.joss_paper_counter, 5u);
    ASSERT_EQ(advance_combat(state, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(combat.data.joss_paper_counter, 0u);
    EXPECT_TRUE(combat.execution().empty());
    EXPECT_TRUE(valid_combat(state));
}

TEST(ExecutionFrames, InitialDrawBudgetFailurePreservesDestination) {
    RunState state; Scratch scratch;
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
    RunState state; Scratch scratch;
    ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
    auto& combat = state;
    const auto card = combat.zone(Zone::hand).front();
    gameplay::move_card(combat, Zone::hand, Zone::play, card);
    combat.combat().counters.started_plays = 1;
    combat.execution().push_back(CardProgramFrame{card, 0, 0, 1, combat.enemy_handle()});
    scratch.steps = 0;
    gameplay::resume_frame(combat, scratch, Ruleset::pinned());
    EXPECT_TRUE(combat.execution().empty());
    EXPECT_EQ(combat.player().block, 5);
    EXPECT_EQ(combat.zone(Zone::discard).front(), card);
    EXPECT_EQ(scratch.steps, 2u); // Block and finish retain separate budget charges.
    EXPECT_TRUE(valid_combat(state));
}

TEST(ExecutionFrames, DrawBatchesButChargesEachCardAndCompletion) {
    RunState state; Scratch scratch;
    ASSERT_EQ(initialize_combat(state, {}, scratch).status, StepStatus::awaiting_input);
    auto& combat = state;
    const auto hand_size = combat.zone(Zone::hand).size();
    ASSERT_GE(combat.zone(Zone::draw).size(), 3u);
    combat.execution().push_back(DrawFrame{.remaining = 3, .from_hand_draw = false});
    scratch.steps = 0;
    gameplay::resume_frame(combat, scratch, Ruleset::pinned());
    EXPECT_TRUE(combat.execution().empty());
    EXPECT_EQ(combat.zone(Zone::hand).size(), hand_size + 3);
    EXPECT_EQ(combat.combat().counters.non_hand_draws_this_turn, 3u);
    EXPECT_EQ(scratch.steps, 4u);
}

TEST(ExecutionFrames, BatchedDrawStopsAtExecutionBudget) {
    RunState state; Scratch scratch;
    ASSERT_EQ(initialize_combat(state, {}, scratch).status, StepStatus::awaiting_input);
    auto& combat = state;
    const auto hand_size = combat.zone(Zone::hand).size();
    combat.execution().push_back(DrawFrame{.remaining = 3, .from_hand_draw = false});
    scratch.steps = 0; scratch.step_limit = 2;
    const auto stopped = gameplay::resume_frame(combat, scratch, Ruleset::pinned());
    EXPECT_EQ(stopped.reason, Restriction::execution_budget);
    ASSERT_EQ(combat.execution().size(), 1u);
    EXPECT_EQ(std::get<DrawFrame>(combat.execution().back()).remaining, 1);
    EXPECT_EQ(combat.zone(Zone::hand).size(), hand_size + 2);
}

TEST(ExecutionFrames, DrawYieldsToShuffleBeforeDrawingAnyCards) {
    RunState state; Scratch scratch;
    scenario::prepare_exhaust_shuffle(state);
    auto& combat = state;
    const auto hand_size = combat.zone(Zone::hand).size();
    combat.execution().push_back(DrawFrame{.remaining = 3, .from_hand_draw = false});
    scratch.steps = 0;
    gameplay::resume_frame(combat, scratch, Ruleset::pinned());
    ASSERT_EQ(combat.execution().size(), 2u);
    EXPECT_TRUE(std::holds_alternative<ShuffleFrame>(combat.execution().back()));
    EXPECT_EQ(std::get<DrawFrame>(combat.execution().front()).remaining, 3);
    EXPECT_EQ(combat.zone(Zone::hand).size(), hand_size);
    gameplay::resume_frame(combat, scratch, Ruleset::pinned());
    EXPECT_EQ(combat.combat().phase, CombatPhase::card_choice);
    EXPECT_EQ(combat.zone(Zone::hand).size(), hand_size);
    EXPECT_EQ(scratch.steps, 2u);
}

TEST(ExecutionFrames, ExhaustOwnsFinalOperandUntilItsChildFinishes) {
    RunState state; Scratch scratch;
    scenario::prepare_exhaust_shuffle(state);
    auto& combat = state;
    const auto card = combat.zone(Zone::hand).front();
    const auto cards = gameplay::capture_exhaust_cards(combat, std::span(&card, 1));
    combat.execution().push_back(ExhaustCardsFrame{.cards = cards});
    scratch.steps = 0;
    gameplay::resume_frame(combat, scratch, Ruleset::pinned());
    ASSERT_EQ(combat.execution().size(), 2u);
    EXPECT_TRUE(std::holds_alternative<DrawFrame>(combat.execution().back()));
    const auto& parent = std::get<ExhaustCardsFrame>(combat.execution().front());
    EXPECT_EQ(parent.cards.cursor, 0u);
    EXPECT_EQ(parent.handler.stage, 1u);
    ASSERT_EQ(combat.continuation_cards().size(), 1u);
    EXPECT_EQ(combat.continuation_cards().front(), card);
    EXPECT_EQ(scratch.steps, 2u);
    EXPECT_EQ(combat.zone(Zone::exhaust).front(), card);
    ASSERT_EQ(advance_combat(combat, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(combat.combat().phase, CombatPhase::card_choice);
    EXPECT_TRUE(valid_run(state));
}

TEST(ExecutionFrames, ExhaustRecapturesListenersForEachCardAfterChildWork) {
    RunState state; Scratch scratch;
    ASSERT_EQ(initialize_combat(state, {}, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(apply_power_scenario(state, {PowerId::dark_embrace, 1}, scratch).status, StepStatus::awaiting_input);
    auto& combat = state;
    const std::array cards{combat.zone(Zone::hand)[0], combat.zone(Zone::hand)[1]};
    const auto operands = gameplay::capture_exhaust_cards(combat, cards);
    combat.execution().push_back(ExhaustCardsFrame{.cards = operands});
    gameplay::resume_frame(combat, scratch, Ruleset::pinned());
    ASSERT_TRUE(std::holds_alternative<DrawFrame>(combat.execution().back()));
    EXPECT_EQ(std::get<DrawFrame>(combat.execution().back()).remaining, 1);
    const auto old_instance = combat.captured_hook_listeners().front().power_instance;
    gameplay::apply_power(combat, scratch, PowerId::dark_embrace, -1, false, false);
    gameplay::apply_power(combat, scratch, PowerId::dark_embrace, 2, false, false);
    const auto replacement = combat.player().find_power(PowerId::dark_embrace, combat.overflow)->instance;
    ASSERT_NE(old_instance, replacement);
    gameplay::resume_frame(combat, scratch, Ruleset::pinned()); // Finish first card's draw.
    gameplay::resume_frame(combat, scratch, Ruleset::pinned()); // Exhaust second card.
    ASSERT_EQ(combat.execution().size(), 2u);
    ASSERT_TRUE(std::holds_alternative<DrawFrame>(combat.execution().back()));
    EXPECT_EQ(std::get<DrawFrame>(combat.execution().back()).remaining, 2);
    ASSERT_EQ(combat.captured_hook_listeners().size(), 1u);
    EXPECT_EQ(combat.captured_hook_listeners().front().power_instance, replacement);
    ASSERT_EQ(advance_combat(state, scratch).status, StepStatus::awaiting_input);
    EXPECT_TRUE(combat.continuation_cards().empty());
    EXPECT_TRUE(combat.captured_hook_listeners().empty());
    EXPECT_TRUE(valid_combat(state));
}

TEST(ExecutionFrames, ShuffleWithoutListenersPreservesDrawBudget) {
    RunState state; Scratch scratch;
    ASSERT_EQ(initialize_combat(state, {}, scratch).status, StepStatus::awaiting_input);
    auto& combat = state;
    while (!combat.zone(Zone::draw).empty())
        gameplay::move_card(combat, Zone::draw, Zone::discard, combat.zone(Zone::draw).front());
    combat.execution().push_back(DrawFrame{.remaining = 2, .from_hand_draw = false});
    scratch.steps = 0;
    ASSERT_EQ(advance_combat(state, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(scratch.steps, 3u); // Two draws and the original completion check.
    EXPECT_EQ(combat.combat().counters.non_hand_draws_this_turn, 2u);
    EXPECT_TRUE(valid_combat(state));
}

TEST(ExecutionFrames, RejectsCorruptOperationAndHandlerProgress) {
    RunState state; Scratch scratch;
    scenario::prepare_exhaust_shuffle(state);
    exhaust_into_shuffle(state, scratch);
    ASSERT_TRUE(valid_run(state));
    auto copy = state;
    std::get<ShuffleFrame>(copy.execution().back()).handler.stage = 2;
    EXPECT_FALSE(valid_run(copy));
    copy = state;
    std::get<ShuffleFrame>(copy.execution().back()).phase = static_cast<ShufflePhase>(255);
    EXPECT_FALSE(valid_run(copy));
    copy = state;
    for (auto& frame : copy.execution())
        if (auto* exhaust = std::get_if<ExhaustCardsFrame>(&frame)) exhaust->cards.begin = UINT32_MAX;
    EXPECT_FALSE(valid_run(copy));
}

TEST(ExecutionFrames, CompletionSurvivesChildRelocatingTheExecutionStack) {
    const std::array relics{ExhaustRelic::joss_paper};
    CombatConfig config; config.exhaust_relics = relics; config.joss_paper_counter = 4;
    RunState state; Scratch scratch;
    ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
    auto& combat = state;
    for (int i = 0; i < 3; ++i) combat.execution().push_back(DrawFrame{});
    const auto card = combat.zone(Zone::hand).front();
    const auto cards = gameplay::capture_exhaust_cards(combat, std::span(&card, 1));
    combat.execution().push_back(ExhaustCardsFrame{.cards = cards});
    ASSERT_FALSE(combat.combat().execution.overflowed());
    gameplay::resume_frame(combat, scratch, Ruleset::pinned());
    ASSERT_EQ(combat.execution().size(), 5u);
    EXPECT_TRUE(combat.combat().execution.overflowed());
    EXPECT_TRUE(std::holds_alternative<DrawFrame>(combat.execution().back()));
    EXPECT_EQ(std::get<ExhaustCardsFrame>(combat.execution()[3]).handler.stage, 1u);
    EXPECT_EQ(combat.data.joss_paper_counter, 5u);
    ASSERT_EQ(advance_combat(state, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(combat.data.joss_paper_counter, 0u);
    EXPECT_TRUE(combat.execution().empty());
    EXPECT_TRUE(valid_combat(state));
}
}
