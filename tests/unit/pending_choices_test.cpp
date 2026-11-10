#include "../support/continuation_fixture.hpp"
#include "../support/selection_input.hpp"
#include "storage_allocator.hpp"
#include "../../src/engine/combat/lifecycle.hpp"
#include "sts2/engine/action.hpp"
#include "sts2/persistence/checkpoint.hpp"
#include "../../src/engine/execution/interpreter.hpp"
#include "../../src/engine/execution/pending_choices.hpp"
#include "../../src/engine/execution/selection.hpp"
#include "../../src/engine/mechanics/cards.hpp"
#include "../../src/engine/mechanics/exhaust.hpp"
#include "../../src/engine/mechanics/powers.hpp"
#include <gtest/gtest.h>

namespace sts2::engine {
namespace {
void request_child_choice(RunState& state, Scratch& scratch, std::uint32_t join = 0) {
    gameplay::begin_detachable_frames(state, join);
    state.execution().push_back(DrawFrame{.remaining = 1, .from_hand_draw = false});
    while (!state.combat().selection_requested) {
        ASSERT_EQ(gameplay::resume_frame(state, scratch, Ruleset::pinned()).status, StepStatus::awaiting_input);
        ASSERT_FALSE(state.execution().empty());
    }
}
CardHandle available_candidate(const RunState& state) {
    for (auto card : state.selection_candidates())
        if (std::find(state.zone(Zone::draw).begin(), state.zone(Zone::draw).end(), card) != state.zone(Zone::draw).end()) return card;
    return {};
}
StepResult answer(RunState& state, Scratch& scratch) {
    return apply_action(state, test::selection_input(state, available_candidate(state), state.combat().selection.id), scratch);
}
}

TEST(PendingChoices, CallerDrawsBeforeDetachedChoiceIsPresented) {
    RunState state; Scratch scratch;
    scenario::prepare_exhaust_shuffle(state);
    state.execution().push_back(DrawFrame{.remaining = 1, .from_hand_draw = false});
    request_child_choice(state, scratch);
    const auto prompt = state.combat().selection.id;
    EXPECT_EQ(state.combat().phase, CombatPhase::play);
    EXPECT_EQ(state.combat().counters.non_hand_draws_this_turn, 0u);
    ASSERT_EQ(advance_combat(state, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(state.combat().counters.non_hand_draws_this_turn, 1u);
    ASSERT_EQ(state.combat().phase, CombatPhase::card_choice);
    EXPECT_EQ(state.combat().selection.id, prompt);
    EXPECT_EQ(state.selection_candidates().size(), state.zone(Zone::draw).size());
    for (auto card : state.selection_candidates())
        EXPECT_NE(std::find(state.zone(Zone::draw).begin(), state.zone(Zone::draw).end(), card), state.zone(Zone::draw).end());
    EXPECT_EQ(state.pending_choice_order().size(), 1u);
    EXPECT_TRUE(valid_combat(state));
    ASSERT_EQ(answer(state, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(state.combat().counters.non_hand_draws_this_turn, 2u);
    EXPECT_TRUE(state.pending_choice_order().empty());
    EXPECT_TRUE(state.execution().empty());
    EXPECT_TRUE(state.captured_hook_listeners().empty());
    EXPECT_TRUE(valid_combat(state));
}

TEST(PendingChoices, OrdinaryCallerChoicePrecedesQueuedDetachedChildAcrossCheckpoint) {
    RunState state; Scratch scratch;
    scenario::prepare_exhaust_shuffle(state);
    // The root shuffle is ordinary; only the child draw has a detachable boundary.
    state.execution().push_back(ShuffleFrame{});
    request_child_choice(state, scratch);
    const auto child_prompt = state.combat().selection.id;
    ASSERT_EQ(advance_combat(state, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(state.combat().phase, CombatPhase::card_choice);
    EXPECT_NE(state.combat().selection.id, child_prompt);
    EXPECT_EQ(state.combat().active_choice, 0u);
    ASSERT_EQ(state.pending_choice_order().size(), 1u);
    const auto child_slot = state.pending_choice_order().front();
    EXPECT_EQ(state.pending_choices()[child_slot - 1].phase, PendingChoicePhase::pending);
    ASSERT_EQ(state.execution().size(), 1u);
    EXPECT_TRUE(std::holds_alternative<ShuffleFrame>(state.execution().back()));
    EXPECT_EQ(state.captured_hook_listeners().size(), 1u);
    EXPECT_EQ(state.pending_choices()[child_slot - 1].listeners.size(), 1u);
    EXPECT_EQ(state.combat().counters.non_hand_draws_this_turn, 0u);
    ASSERT_TRUE(valid_combat(state));
    RunState restored;
    ASSERT_EQ(restore(restored, checkpoint(state).bytes, scratch), RestoreStatus::restored);
    ASSERT_EQ(restored, state);
    ASSERT_EQ(answer(restored, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(restored.combat().selection.id, child_prompt);
    EXPECT_EQ(restored.combat().active_choice, child_slot);
    EXPECT_EQ(restored.pending_choices()[child_slot - 1].phase, PendingChoicePhase::active);
    EXPECT_EQ(restored.combat().counters.non_hand_draws_this_turn, 0u);
    EXPECT_EQ(restored.selection_candidates().size(), restored.zone(Zone::draw).size());
    ASSERT_TRUE(valid_combat(restored));
    ASSERT_EQ(answer(restored, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(restored.combat().counters.non_hand_draws_this_turn, 1u);
    EXPECT_TRUE(restored.execution().empty());
    EXPECT_TRUE(restored.pending_choice_order().empty());
    EXPECT_TRUE(restored.captured_hook_listeners().empty());
    EXPECT_TRUE(valid_combat(restored));
}

TEST(PendingChoices, TwoChoicesRetainIndependentArenasAcrossCloneAndCheckpoint) {
    RunState state; Scratch scratch;
    scenario::prepare_exhaust_shuffle(state);
    gameplay::begin_detachable_frames(state); // This root group is independently detachable.
    state.execution().push_back(ShuffleFrame{});
    request_child_choice(state, scratch);
    const auto first = state.combat().selection.id;
    ASSERT_EQ(advance_combat(state, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(state.pending_choice_order().size(), 2u);
    EXPECT_EQ(state.combat().selection.id, first);
    ASSERT_TRUE(valid_combat(state));
    const auto bytes = checkpoint(state).bytes;
    RunState clone = state, restored;
    ASSERT_EQ(restore(restored, bytes, scratch), RestoreStatus::restored);
    EXPECT_EQ(state, restored);
    for (auto* branch : {&clone, &restored}) {
        ASSERT_EQ(answer(*branch, scratch).status, StepStatus::awaiting_input);
        EXPECT_EQ(branch->pending_choice_order().size(), 1u);
        EXPECT_NE(branch->combat().selection.id, first);
        EXPECT_TRUE(valid_combat(*branch));
        ASSERT_EQ(answer(*branch, scratch).status, StepStatus::awaiting_input);
        EXPECT_TRUE(branch->pending_choice_order().empty());
        EXPECT_TRUE(branch->captured_hook_listeners().empty());
        EXPECT_TRUE(valid_combat(*branch));
    }
    EXPECT_EQ(clone, restored);
    EXPECT_EQ(checkpoint(state).bytes, bytes);
}

TEST(PendingChoices, JoinedChildStopsAtItsBaseBeforeParkedParentRuns) {
    RunState state; Scratch scratch;
    scenario::prepare_exhaust_shuffle(state);
    state.execution().push_back(DrawFrame{.remaining = 1, .from_hand_draw = false});
    const auto join = gameplay::create_frame_join(state);
    request_child_choice(state, scratch, join);
    ASSERT_TRUE(gameplay::advance_pending_choices(state));
    gameplay::wait_for_frame_join(state, join);
    ASSERT_EQ(advance_combat(state, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(state.pending_choices()[state.combat().active_choice - 1].depth, 1u);
    EXPECT_EQ(state.combat().counters.non_hand_draws_this_turn, 0u);
    EXPECT_TRUE(gameplay::has_pending_frames(state, join));
    ASSERT_TRUE(valid_combat(state));
    ASSERT_EQ(answer(state, scratch).status, StepStatus::awaiting_input);
    EXPECT_FALSE(gameplay::has_pending_frames(state, join));
    EXPECT_EQ(state.combat().counters.non_hand_draws_this_turn, 2u);
    EXPECT_EQ(state.combat().waiting_join, 0u);
    EXPECT_TRUE(valid_combat(state));
}

TEST(PendingChoices, ParentOperandsSurviveChildCompletionAtAJoin) {
    RunState state; Scratch scratch;
    scenario::prepare_exhaust_shuffle(state);
    const auto card = state.zone(Zone::hand).front();
    const auto cards = gameplay::capture_exhaust_cards(state, std::span(&card, 1));
    state.execution().push_back(ExhaustCardsFrame{.cards = cards});
    const auto join = gameplay::create_frame_join(state);
    request_child_choice(state, scratch, join);
    ASSERT_TRUE(gameplay::advance_pending_choices(state));
    gameplay::wait_for_frame_join(state, join);
    ASSERT_EQ(advance_combat(state, scratch).status, StepStatus::awaiting_input);
    EXPECT_TRUE(state.continuation_cards().empty());
    const auto& active = state.pending_choices()[state.combat().active_choice - 1];
    ASSERT_EQ(active.cards.size(), 1u);
    EXPECT_EQ(active.cards.values(state.combat_overflow).front(), card);
    ASSERT_TRUE(valid_combat(state));
    ASSERT_EQ(answer(state, scratch).status, StepStatus::awaiting_input);
    EXPECT_NE(std::find(state.zone(Zone::exhaust).begin(), state.zone(Zone::exhaust).end(), card), state.zone(Zone::exhaust).end());
    EXPECT_TRUE(state.continuation_cards().empty());
    EXPECT_TRUE(valid_combat(state));
}

TEST(PendingChoices, OrdinaryNestedHooksRemainBlocking) {
    RunState state; Scratch scratch;
    scenario::prepare_exhaust_shuffle(state);
    state.execution().push_back(DrawFrame{.remaining = 1, .from_hand_draw = false});
    state.execution().push_back(DrawFrame{.remaining = 1, .from_hand_draw = false});
    ASSERT_EQ(advance_combat(state, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(state.combat().counters.non_hand_draws_this_turn, 0u);
    EXPECT_TRUE(state.pending_choice_order().empty());
    ASSERT_EQ(answer(state, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(state.combat().counters.non_hand_draws_this_turn, 2u);
}
TEST(PendingChoices, ActiveEntryKeepsItsPositionWhenItRequestsAnotherChoice) {
    RunState state; Scratch scratch;
    scenario::prepare_exhaust_shuffle(state);
    gameplay::begin_detachable_frames(state); // This root group is independently detachable.
    state.execution().push_back(ShuffleFrame{}); // Later detached choice B.
    gameplay::begin_detachable_frames(state);
    state.execution().push_back(ShuffleFrame{}); // A's later choice C.
    state.execution().push_back(ShuffleFrame{}); // A's first choice.
    ASSERT_EQ(advance_combat(state, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(state.pending_choice_order().size(), 2u);
    const auto slot = state.combat().active_choice;
    const auto first = state.combat().selection.id;
    ASSERT_EQ(answer(state, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(state.combat().active_choice, slot);
    EXPECT_EQ(state.pending_choice_order().front(), slot);
    EXPECT_NE(state.combat().selection.id, first);
    ASSERT_TRUE(valid_combat(state));
    ASSERT_EQ(answer(state, scratch).status, StepStatus::awaiting_input);
    EXPECT_NE(state.combat().active_choice, slot);
    ASSERT_EQ(answer(state, scratch).status, StepStatus::awaiting_input);
    EXPECT_TRUE(state.execution().empty());
    EXPECT_TRUE(state.pending_choice_order().empty());
}

TEST(PendingChoices, NestedJoinRestoresThePreviousActiveArenaAndDepth) {
    RunState state; Scratch scratch;
    scenario::prepare_exhaust_shuffle(state);
    request_child_choice(state, scratch);
    ASSERT_EQ(advance_combat(state, scratch).status, StepStatus::awaiting_input);
    const auto parent = state.combat().active_choice;
    // Consume A's answer before it starts a joined child, as all requesters must.
    gameplay::record_selection(state, test::selection_input(state, available_candidate(state), state.combat().selection.id));
    ASSERT_EQ(gameplay::resume_frame(state, scratch, Ruleset::pinned()).status, StepStatus::awaiting_input);
    const auto join = gameplay::create_frame_join(state);
    gameplay::begin_detachable_frames(state, join);
    state.execution().push_back(ShuffleFrame{});
    ASSERT_EQ(gameplay::resume_frame(state, scratch, Ruleset::pinned()).status, StepStatus::awaiting_input);
    ASSERT_TRUE(state.combat().selection_requested);
    ASSERT_TRUE(gameplay::advance_pending_choices(state));
    gameplay::wait_for_frame_join(state, join);
    ASSERT_EQ(advance_combat(state, scratch).status, StepStatus::awaiting_input);
    const auto child = state.combat().active_choice;
    ASSERT_NE(child, parent);
    EXPECT_EQ(state.pending_choices()[child - 1].previous, parent);
    ASSERT_TRUE(valid_combat(state));
    const auto bytes = checkpoint(state).bytes;
    RunState restored;
    ASSERT_EQ(restore(restored, bytes, scratch), RestoreStatus::restored);
    ASSERT_EQ(restored, state);
    ASSERT_EQ(answer(restored, scratch).status, StepStatus::awaiting_input);
    EXPECT_TRUE(restored.pending_choice_order().empty());
    EXPECT_TRUE(restored.execution().empty());
    EXPECT_EQ(restored.combat().active_choice, 0u);
    EXPECT_EQ(restored.combat().counters.non_hand_draws_this_turn, 1u);
    EXPECT_TRUE(valid_combat(restored));
}

TEST(PendingChoices, RemovedPowerRetainsItsValueInEveryParkedArena) {
    RunState state; Scratch scratch;
    scenario::prepare_exhaust_shuffle(state);
    state.execution().push_back(ShuffleFrame{});
    request_child_choice(state, scratch);
    ASSERT_EQ(advance_combat(state, scratch).status, StepStatus::awaiting_input);
    auto& power = *state.player().find_power(PowerId::stratagem, state.overflow);
    const auto instance = power.instance;
    gameplay::expire_power(state, scratch, state.player(), instance);
    for (const auto& entry : state.pending_choices())
        for (const auto& listener : entry.listeners.values(state.combat_overflow))
            if (listener.power_instance == instance) EXPECT_EQ(listener.removed_power_amount, 0);
    ASSERT_TRUE(valid_combat(state));
    ASSERT_EQ(answer(state, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(answer(state, scratch).status, StepStatus::awaiting_input);
    EXPECT_TRUE(state.pending_choice_order().empty());
}

TEST(PendingChoices, CheckedExecutionRejectsInvalidActiveChainWithoutMutation) {
    RunState state; Scratch scratch;
    scenario::prepare_exhaust_shuffle(state);
    request_child_choice(state, scratch);
    ASSERT_EQ(advance_combat(state, scratch).status, StepStatus::awaiting_input);
    const auto input = test::selection_input(state, available_candidate(state), state.combat().selection.id);
    state.pending_choices()[state.combat().active_choice - 1].previous = state.combat().active_choice;
    const auto before = state;
    EXPECT_EQ(apply_action(state, input, scratch).reason, Restriction::invalid_state);
    EXPECT_EQ(state, before);
}

TEST(PendingChoices, InlineDetachmentAndCloneDoNotAllocate) {
    test::AllocationCounter allocation;
    {
        RunState state(allocation.allocator()); Scratch scratch;
        scenario::prepare_exhaust_shuffle(state);
        request_child_choice(state, scratch);
        const auto before = allocation.allocations;
        allocation.fail_after = before;
        ASSERT_EQ(advance_combat(state, scratch).status, StepStatus::awaiting_input);
        RunState clone(std::as_const(state));
        EXPECT_EQ(clone, state);
        ASSERT_EQ(answer(clone, scratch).status, StepStatus::awaiting_input);
        EXPECT_EQ(allocation.allocations, before);
    }
    EXPECT_EQ(allocation.outstanding, 0u);
}

TEST(PendingChoices, OverflowFailureIsReportedAndAllOwnedStorageIsReleased) {
    test::AllocationCounter allocation;
    {
        RunState state(allocation.allocator()); Scratch scratch;
        scenario::prepare_exhaust_shuffle(state, true);
        request_child_choice(state, scratch);
        allocation.fail_after = allocation.allocations;
        const auto stopped = advance_combat(state, scratch);
        EXPECT_EQ(stopped.status, StepStatus::resource_limit);
        EXPECT_EQ(stopped.reason, Restriction::capacity);
        EXPECT_EQ(state.combat().phase, CombatPhase::invalid);
    }
    EXPECT_EQ(allocation.outstanding, 0u);
}

TEST(PendingChoices, CombatEndCancelsActiveAndQueuedFrames) {
    RunState state; Scratch scratch;
    scenario::prepare_exhaust_shuffle(state);
    gameplay::begin_detachable_frames(state); // This root group is independently detachable.
    state.execution().push_back(ShuffleFrame{});
    request_child_choice(state, scratch);
    ASSERT_EQ(advance_combat(state, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(state.pending_choice_order().size(), 2u);
    state.enemy().hp = 0;
    gameplay::end_combat(state, scratch, true);
    EXPECT_TRUE(state.execution().empty());
    EXPECT_TRUE(state.pending_choice_order().empty());
    EXPECT_TRUE(state.pending_choices().empty());
    EXPECT_TRUE(state.detachment_boundaries().empty());
    EXPECT_EQ(state.combat().active_choice, 0u);
    EXPECT_TRUE(state.valid_combat_storage());
    EXPECT_TRUE(valid_combat(state));
}

TEST(PendingChoices, DeferredSelectionBecomesAutomaticAfterCallerConsumesThePile) {
    RunState state; Scratch scratch;
    scenario::prepare_exhaust_shuffle(state);
    while (state.zone(Zone::discard).size() > 1)
        gameplay::move_card(state, Zone::discard, Zone::hand, state.zone(Zone::discard).front());
    state.execution().push_back(DrawFrame{.remaining = 1, .from_hand_draw = false});
    request_child_choice(state, scratch);
    EXPECT_TRUE(state.selection_candidates().empty());
    ASSERT_EQ(advance_combat(state, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(state.combat().phase, CombatPhase::play);
    EXPECT_EQ(state.combat().counters.non_hand_draws_this_turn, 1u);
    EXPECT_TRUE(state.pending_choice_order().empty());
    EXPECT_TRUE(state.execution().empty());
    EXPECT_EQ(state.combat().selected_cards, RangeProgress{});
    EXPECT_TRUE(valid_combat(state));
}

TEST(PendingChoices, OverflowingPendingGroupsCloneAndRestoreTheirNestedStorage) {
    RunState state; Scratch scratch;
    scenario::prepare_exhaust_shuffle(state, true);
    gameplay::begin_detachable_frames(state); // This root group is independently detachable.
    state.execution().push_back(ShuffleFrame{});
    request_child_choice(state, scratch);
    ASSERT_EQ(advance_combat(state, scratch).status, StepStatus::awaiting_input);
    ASSERT_TRUE(state.combat().pending_choices.overflowed());
    ASSERT_TRUE(state.combat().selection_candidates.overflowed());
    ASSERT_TRUE(state.valid_combat_storage());
    RunState clone = state, restored;
    ASSERT_EQ(restore(restored, checkpoint(state).bytes, scratch), RestoreStatus::restored);
    EXPECT_EQ(clone, restored);
    for (auto* branch : {&clone, &restored}) {
        ASSERT_EQ(answer(*branch, scratch).status, StepStatus::awaiting_input);
        ASSERT_EQ(answer(*branch, scratch).status, StepStatus::awaiting_input);
        EXPECT_TRUE(branch->pending_choice_order().empty());
        EXPECT_TRUE(branch->valid_combat_storage());
        EXPECT_TRUE(valid_combat(*branch));
    }
    EXPECT_EQ(clone, restored);
    RunState completed;
    ASSERT_EQ(restore(completed, checkpoint(clone).bytes, scratch), RestoreStatus::restored);
    EXPECT_EQ(completed, clone);
}

TEST(PendingChoices, JoinReleasesItsParentBeforeAnUnrelatedLaterChoice) {
    RunState state; Scratch scratch;
    scenario::prepare_exhaust_shuffle(state);
    state.execution().push_back(DrawFrame{.remaining = 1, .from_hand_draw = false});
    const auto join = gameplay::create_frame_join(state);
    request_child_choice(state, scratch, join);
    ASSERT_TRUE(gameplay::advance_pending_choices(state));
    gameplay::begin_detachable_frames(state);
    state.execution().push_back(ShuffleFrame{});
    ASSERT_EQ(gameplay::resume_frame(state, scratch, Ruleset::pinned()).status, StepStatus::awaiting_input);
    ASSERT_TRUE(gameplay::advance_pending_choices(state));
    gameplay::wait_for_frame_join(state, join);
    ASSERT_EQ(advance_combat(state, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(state.combat().counters.non_hand_draws_this_turn, 0u);
    ASSERT_EQ(answer(state, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(state.combat().phase, CombatPhase::card_choice);
    EXPECT_EQ(state.combat().counters.non_hand_draws_this_turn, 2u);
    EXPECT_FALSE(gameplay::has_pending_frames(state, join));
    EXPECT_EQ(state.pending_choice_order().size(), 1u);
    EXPECT_TRUE(valid_combat(state));
    ASSERT_EQ(answer(state, scratch).status, StepStatus::awaiting_input);
    EXPECT_TRUE(state.pending_choice_order().empty());
}

TEST(PendingChoices, ExhaustOperandsAndJossPaperCompletionSurviveNonLifoDispatch) {
    RunState state; Scratch scratch;
    const std::array relics{SetupRules::find("RELIC.JOSS_PAPER")};
    scenario::prepare_exhaust_shuffle(state, false, relics, 4);
    gameplay::begin_detachable_frames(state); // This root group is independently detachable.
    state.execution().push_back(ShuffleFrame{});
    gameplay::begin_detachable_frames(state);
    const auto card = state.zone(Zone::hand).front();
    const auto cards = gameplay::capture_exhaust_cards(state, std::span(&card, 1));
    state.execution().push_back(ExhaustCardsFrame{.cards = cards});
    ASSERT_EQ(advance_combat(state, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(state.pending_choice_order().size(), 2u);
    EXPECT_EQ(state.data.joss_paper_counter, 5u);
    ASSERT_TRUE(valid_combat(state));
    RunState restored;
    ASSERT_EQ(restore(restored, checkpoint(state).bytes, scratch), RestoreStatus::restored);
    ASSERT_EQ(answer(restored, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(restored.data.joss_paper_counter, 0u);
    EXPECT_TRUE(restored.continuation_cards().empty());
    EXPECT_EQ(restored.pending_choice_order().size(), 1u);
    ASSERT_TRUE(valid_combat(restored));
    ASSERT_EQ(answer(restored, scratch).status, StepStatus::awaiting_input);
    EXPECT_TRUE(restored.pending_choice_order().empty());
    EXPECT_TRUE(restored.captured_hook_listeners().empty());
    EXPECT_TRUE(valid_combat(restored));
}

TEST(PendingChoices, OrdinaryAndDetachedAnswersHaveIdenticalCheckedTrustedFutures) {
    RunState checked; Scratch scratch;
    scenario::prepare_exhaust_shuffle(checked);
    checked.execution().push_back(ShuffleFrame{});
    request_child_choice(checked, scratch);
    ASSERT_EQ(advance_combat(checked, scratch).status, StepStatus::awaiting_input);
    RunState trusted = checked;
    for (unsigned i = 0; i < 2; ++i) {
        const std::array chosen{available_candidate(checked)};
        const SelectionInput input{checked.data.boundary, checked.combat().selection.id, chosen};
        const auto expected = apply_action(checked, input, scratch);
        const auto actual = apply_action_trusted(trusted, input, scratch);
        EXPECT_EQ(actual.status, expected.status);
        EXPECT_EQ(actual.reason, expected.reason);
        EXPECT_EQ(actual.boundary, expected.boundary);
        EXPECT_EQ(trusted, checked);
    }
    EXPECT_TRUE(trusted.pending_choice_order().empty());
}

}
