#include "sts2/engine/action.hpp"
#include "sts2/observation/observe.hpp"
#include "sts2/persistence/checkpoint.hpp"
#include "../support/checkpoint_fixture.hpp"
#include "storage_allocator.hpp"
#include <gtest/gtest.h>
#include <algorithm>

namespace sts2::engine {
namespace {
CardHandle find(const RunState& state, CardId id) {
    for (auto h : state.zone(Zone::hand)) if (state.cards().get(h)->definition == id) return h;
    return {};
}
StepResult play(RunState& state, CardHandle card, Scratch& scratch) {
    return apply_combat(state, {state.data.boundary, CombatInputKind::play_card, card, (Ruleset::pinned().card(state.cards().get(card)->definition)->enemy_target) ? state.enemy_handle() : CreatureHandle{}}, scratch);
}
StepResult choose(RunState& state, Scratch& scratch, std::span<const CardHandle> cards) {
    return apply_selection(state, {state.data.boundary, state.combat().selection.id, cards}, scratch);
}
}
TEST(CardChoices, PurityClampsVisibleMaximumToNativeCandidateCount) {
    const std::array deck{CardSpec{CardId::purity, true}, CardSpec{CardId::strike}};
    CombatConfig config; config.deck = deck;
    RunState state; Scratch scratch;
    ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(play(state, find(state, CardId::purity), scratch).status, StepStatus::awaiting_input);
    const auto prompt = observe_combat(state).selection;
    ASSERT_TRUE(prompt);
    EXPECT_EQ(prompt->maximum, 1u);
    EXPECT_TRUE(valid_combat(state));
}
TEST(CardChoices, EmptyAndForcedSelectionsResolveWithoutPublishingPrompts) {
    for (auto id : {CardId::purity, CardId::armaments, CardId::headbutt, CardId::burning_pact, CardId::true_grit}) {
        for (bool upgraded : {false, true}) {
            const std::array deck{CardSpec{id, upgraded}};
            CombatConfig config; config.deck = deck; RunState state; Scratch scratch;
            ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
            ASSERT_EQ(play(state, state.zone(Zone::hand).front(), scratch).status, StepStatus::awaiting_input);
            EXPECT_FALSE(observe_combat(state).selection); EXPECT_TRUE(valid_combat(state));
        }
    }
    for (auto id : {CardId::armaments, CardId::burning_pact, CardId::true_grit}) {
        const std::array deck{CardSpec{id, true}, CardSpec{CardId::strike}};
        CombatConfig config; config.deck = deck; RunState state; Scratch scratch;
        ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
        ASSERT_EQ(play(state, find(state, id), scratch).status, StepStatus::awaiting_input);
        EXPECT_FALSE(observe_combat(state).selection); EXPECT_TRUE(valid_combat(state));
    }
}
} // namespace sts2::engine

namespace sts2::engine {
TEST(CardChoices, OrderedSubmissionRestoresIndependentFuturesAndConsumesOneAction) {
    const std::array deck{CardSpec{CardId::purity}, CardSpec{CardId::strike}, CardSpec{CardId::defend}, CardSpec{CardId::bash}};
    CombatConfig config; config.deck = deck; config.clock = {1234, 7};
    RunState state; Scratch scratch;
    ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
    const auto source = find(state, CardId::purity);
    ASSERT_EQ(play(state, source, scratch).status, StepStatus::awaiting_input);
    const std::array selected{state.selection_candidates()[1], state.selection_candidates()[0]};
    const auto before = checkpoint(state).bytes;
    const auto boundary = state.data.boundary.value;
    const auto seconds = state.data.clock.elapsed_seconds();
    RunState restored; ASSERT_EQ(restore(restored, before, scratch), RestoreStatus::restored);
    auto alternate = state;
    ASSERT_EQ(choose(state, scratch, selected).status, StepStatus::awaiting_input);
    ASSERT_EQ(choose(restored, scratch, selected).status, StepStatus::awaiting_input);
    EXPECT_EQ(state, restored);
    EXPECT_EQ(state.zone(Zone::exhaust), (std::vector<CardHandle>{selected[0], selected[1], source}));
    ASSERT_EQ(choose(alternate, scratch, std::span(selected).first(1)).status, StepStatus::awaiting_input);
    EXPECT_EQ(alternate.zone(Zone::exhaust).size(), 2u);
    EXPECT_EQ(state.data.boundary.value, boundary + 1);
    EXPECT_EQ(state.data.clock.elapsed_seconds(), seconds + 7);
    EXPECT_TRUE(state.continuation_cards().empty());
    EXPECT_TRUE(valid_combat(state));
}
TEST(CardChoices, CompleteSelectionValidationRejectsWithoutMutation) {
    for (auto id : {CardId::purity, CardId::armaments, CardId::true_grit}) {
        const std::array deck{CardSpec{id, id == CardId::true_grit}, CardSpec{CardId::strike}, CardSpec{CardId::defend}, CardSpec{CardId::bash}, CardSpec{CardId::strike}};
        CombatConfig config; config.deck = deck;
        RunState state; Scratch scratch;
        ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
        const auto source = find(state, id);
        ASSERT_EQ(play(state, source, scratch).status, StepStatus::awaiting_input);
        const auto space = *selection_space(state);
        const auto before = checkpoint(state).bytes;
        const auto reject = [&](SelectionInput input, Restriction reason) {
            EXPECT_EQ(validate_selection(state, input), reason);
            EXPECT_EQ(validate_action(state, input), reason);
            EXPECT_EQ(apply_action(state, input, scratch).reason, reason);
            EXPECT_EQ(checkpoint(state).bytes, before);
        };
        const std::array valid{space.candidates.front()};
        reject({BoundaryId{space.expected_boundary.value - 1}, space.prompt, valid}, Restriction::stale_boundary);
        reject({space.expected_boundary, PromptId{999}, valid}, Restriction::invalid_selection);
        const std::array duplicate{valid[0], valid[0]};
        reject({space.expected_boundary, space.prompt, duplicate}, Restriction::invalid_selection);
        const std::array absent{source};
        reject({space.expected_boundary, space.prompt, absent}, Restriction::invalid_selection);
        const std::array stale{CardHandle{valid[0].slot, valid[0].generation + 1}};
        reject({space.expected_boundary, space.prompt, stale}, Restriction::invalid_selection);
        reject({space.expected_boundary, space.prompt, space.candidates}, Restriction::invalid_selection);
        if (space.minimum != 0) reject({space.expected_boundary, space.prompt, {}}, Restriction::invalid_selection);
        EXPECT_TRUE(get_actions(state).empty());
        unsigned ordinary = 0;
        for_each_legal_action(state, [&](const auto&) { ++ordinary; });
        EXPECT_EQ(ordinary, 0u);
        EXPECT_TRUE(valid_run(state));
    }
}
TEST(CardChoices, EnumerationHasLiteralOrderedOutcomesAndCanStopOrCollectEagerly) {
    const std::array candidates{CardHandle{1, 1}, CardHandle{2, 1}, CardHandle{3, 1}};
    const SelectionSpace space{{7}, {2}, candidates, 0, 2};
    std::array<CardHandle, 2> workspace;
    std::vector<std::vector<CardHandle>> eager;
    EXPECT_TRUE(for_each_selection(space, workspace, [&](SelectionInput input) {
        EXPECT_EQ(input.expected_boundary, BoundaryId{7}); EXPECT_EQ(input.prompt, PromptId{2});
        eager.emplace_back(input.cards.begin(), input.cards.end()); return true;
    }));
    const auto a = candidates[0], b = candidates[1], c = candidates[2];
    const std::vector<std::vector<CardHandle>> expected{{}, {a}, {a,b}, {a,c}, {b}, {b,a}, {b,c}, {c}, {c,a}, {c,b}};
    EXPECT_EQ(eager, expected);
    unsigned count = 0;
    EXPECT_FALSE(for_each_selection(space, workspace, [&](auto) { return ++count < 3; }));
    EXPECT_EQ(count, 3u);
    EXPECT_FALSE(for_each_selection(space, std::span(workspace).first(1), [&](auto) { ADD_FAILURE(); return true; }));
}
TEST(CardChoices, EnumerationMatchesIndependentPermutationsAndCheckedTrustedEffects) {
    const std::array deck{CardSpec{CardId::purity}, CardSpec{CardId::strike}, CardSpec{CardId::defend}, CardSpec{CardId::bash}};
    CombatConfig config; config.deck = deck; RunState root; Scratch scratch;
    ASSERT_EQ(initialize_combat(root, config, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(play(root, find(root, CardId::purity), scratch).status, StepStatus::awaiting_input);
    const auto space = *selection_space(root);
    std::vector<std::vector<CardHandle>> expected, actual;
    // Independent literal candidate-index triples, including every length 0..3.
    expected.push_back({});
    for (std::size_t i = 0; i < 3; ++i) {
        expected.push_back({space.candidates[i]});
        for (std::size_t j = 0; j < 3; ++j) if (i != j) {
            expected.push_back({space.candidates[i], space.candidates[j]});
            for (std::size_t k = 0; k < 3; ++k) if (i != k && j != k)
                expected.push_back({space.candidates[i], space.candidates[j], space.candidates[k]});
        }
    }
    std::array<CardHandle, 3> workspace;
    ASSERT_TRUE(for_each_selection(space, workspace, [&](SelectionInput input) {
        actual.emplace_back(input.cards.begin(), input.cards.end());
        EXPECT_EQ(validate_action(root, input), Restriction::none);
        auto checked = root, trusted = root; Scratch a, b;
        const auto x = apply_action(checked, input, a), y = apply_action_trusted(trusted, input, b);
        EXPECT_EQ(x.status, y.status); EXPECT_EQ(x.reason, y.reason); EXPECT_EQ(checked, trusted);
        EXPECT_TRUE(valid_run(checked)); EXPECT_TRUE(checked.continuation_cards().empty());
        return true;
    }));
    EXPECT_EQ(actual, expected); EXPECT_EQ(actual.size(), 16u);
}
TEST(CardChoices, ArmamentsSubmitsUpgradeOnceAndRejectsRepeatedInput) {
    const std::array deck{CardSpec{CardId::armaments}, CardSpec{CardId::strike}, CardSpec{CardId::defend}};
    CombatConfig config; config.deck = deck; RunState state; Scratch scratch;
    ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(play(state, find(state, CardId::armaments), scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(state.player().block, 5); EXPECT_EQ(state.combat().energy, 2);
    const std::array selected{state.selection_candidates().back()};
    const SelectionInput input{state.data.boundary, state.combat().selection.id, selected};
    auto before = state;
    ASSERT_EQ(apply_selection(state, input, scratch).status, StepStatus::awaiting_input);
    EXPECT_TRUE(state.cards().get(selected[0])->upgraded);
    EXPECT_FALSE(before.cards().get(selected[0])->upgraded);
    EXPECT_TRUE(state.continuation_cards().empty());
    before = state;
    EXPECT_EQ(apply_selection(state, input, scratch).status, StepStatus::invalid_action);
    EXPECT_EQ(state, before);
}
TEST(CardChoices, ExhaustHooksPreserveSubmittedOrderAcrossCloneAndRestore) {
    const std::array deck{CardSpec{CardId::purity}, CardSpec{CardId::burning_pact}, CardSpec{CardId::strike}, CardSpec{CardId::defend}};
    const std::array relics{ExhaustRelic::joss_paper};
    CombatConfig config; config.deck = deck; config.exhaust_relics = relics; config.joss_paper_counter = 2;
    RunState state; Scratch scratch;
    ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
    const auto attack = find(state, CardId::strike), pact = find(state, CardId::burning_pact), defense = find(state, CardId::defend);
    ASSERT_EQ(play(state, attack, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(play(state, find(state, CardId::purity), scratch).status, StepStatus::awaiting_input);
    RunState restored; ASSERT_EQ(restore(restored, checkpoint(state).bytes, scratch), RestoreStatus::restored);
    auto alternate = state;
    const std::array selected{pact, defense};
    ASSERT_EQ(choose(state, scratch, selected).status, StepStatus::awaiting_input);
    ASSERT_EQ(choose(restored, scratch, selected).status, StepStatus::awaiting_input);
    EXPECT_EQ(state, restored); EXPECT_EQ(state.data.joss_paper_counter, 0u);
    EXPECT_EQ(state.zone(Zone::hand), (std::vector<CardHandle>{attack}));
    EXPECT_EQ(state.zone(Zone::exhaust)[0], pact); EXPECT_EQ(state.zone(Zone::exhaust)[1], defense);
    ASSERT_EQ(choose(alternate, scratch, std::span(selected).last(1)).status, StepStatus::awaiting_input);
    EXPECT_EQ(alternate.data.joss_paper_counter, 4u);
    EXPECT_EQ(alternate.zone(Zone::hand), (std::vector<CardHandle>{pact}));
}
TEST(CardChoices, LethalExhaustRetiresOwnedOperands) {
    const std::array deck{CardSpec{CardId::purity, true}, CardSpec{CardId::defend}, CardSpec{CardId::defend}, CardSpec{CardId::defend}};
    const std::array relics{ExhaustRelic::charons_ashes, ExhaustRelic::joss_paper};
    for (const auto hp : {3, 6}) {
        CombatConfig config; config.deck = deck; config.exhaust_relics = relics; config.enemy_hp = hp; config.joss_paper_counter = hp == 3 ? 4 : 3;
        RunState state; Scratch scratch;
        ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
        ASSERT_EQ(play(state, find(state, CardId::purity), scratch).status, StepStatus::awaiting_input);
        const std::array selected{state.selection_candidates()[1], state.selection_candidates()[0]};
        ASSERT_EQ(choose(state, scratch, selected).status, StepStatus::combat_won);
        EXPECT_TRUE(state.continuation_cards().empty()); EXPECT_TRUE(valid_combat(state));
        EXPECT_EQ(state.data.joss_paper_counter, 0u);
    }
}
TEST(CardChoices, CommonPromptClonesAndSubmissionUseInlineStorage) {
    test::AllocationCounter allocation;
    const std::array deck{CardSpec{CardId::purity}, CardSpec{CardId::strike}, CardSpec{CardId::defend}};
    CombatConfig config; config.deck = deck; RunState state(allocation.allocator()), cloned(allocation.allocator()); Scratch scratch;
    ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(play(state, find(state, CardId::purity), scratch).status, StepStatus::awaiting_input);
    ASSERT_TRUE(state.combat_overflow.empty());
    const auto count = allocation.allocations; allocation.fail_after = count;
    ASSERT_EQ(clone_into(cloned, state), CloneStatus::cloned);
    RunState relocated(allocation.allocator());
    std::memcpy(&relocated.data, &state.data, sizeof(RunData));
    EXPECT_TRUE(valid_combat(relocated)); EXPECT_EQ(relocated, state);
    const std::array selected{state.selection_candidates().front()};
    ASSERT_EQ(choose(cloned, scratch, selected).status, StepStatus::awaiting_input);
    EXPECT_TRUE(observe_combat(state).selection); EXPECT_FALSE(observe_combat(cloned).selection);
    EXPECT_EQ(allocation.allocations, count);
}
} // namespace sts2::engine

namespace sts2::engine {
TEST(CardChoices, HeadbuttSelectsDiscardAndAutomaticallyReturnsItToTop) {
    const std::array deck{CardSpec{CardId::headbutt}, CardSpec{CardId::defend}, CardSpec{CardId::defend}};
    CombatConfig config; config.deck = deck;
    RunState state; Scratch scratch;
    ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
    const auto cards = state.zone(Zone::hand);
    const std::vector<CardHandle> hand(cards.begin(), cards.end());
    CardHandle source;
    for (auto card : hand) {
        if (state.cards().get(card)->definition == CardId::headbutt) source = card;
        else ASSERT_EQ(apply_combat(state, {state.data.boundary, CombatInputKind::play_card, card}, scratch).status, StepStatus::awaiting_input);
    }
    ASSERT_EQ(apply_combat(state, {state.data.boundary, CombatInputKind::play_card, source, state.enemy_handle()}, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(state.combat().phase, CombatPhase::card_choice);
    const auto chosen = state.selection_candidates().back();
    EXPECT_EQ(state.combat().selection.minimum, state.combat().selection.maximum);
    EXPECT_EQ(selection_space(state)->candidates.size(), 2u);
    ASSERT_EQ(choose(state, scratch, std::span<const CardHandle>(&chosen, 1)).status, StepStatus::awaiting_input);
    EXPECT_EQ(state.combat().phase, CombatPhase::play);
    ASSERT_FALSE(state.zone(Zone::draw).empty());
    EXPECT_EQ(state.zone(Zone::draw).front(), chosen);
    EXPECT_EQ(state.zone(Zone::discard).back(), source);
    EXPECT_TRUE(valid_combat(state));
}

TEST(CardChoices, TrueGritUpgradedChoosesAfterBlockAndBaseUsesOwnedSelectionRng) {
    for (const bool upgraded : {false, true}) {
        const std::array deck{CardSpec{CardId::true_grit, upgraded}, CardSpec{CardId::strike}, CardSpec{CardId::defend}};
        CombatConfig config; config.deck = deck;
        RunState state; Scratch scratch;
        ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
        const auto source = *std::find_if(state.zone(Zone::hand).begin(), state.zone(Zone::hand).end(), [&](auto h) { return state.cards().get(h)->definition == CardId::true_grit; });
        const auto rng = state.data.rng.get(RunStream::combat_card_selection).state();
        ASSERT_EQ(apply_combat(state, {state.data.boundary, CombatInputKind::play_card, source}, scratch).status, StepStatus::awaiting_input);
        EXPECT_EQ(state.player().block, upgraded ? 9 : 7);
        if (upgraded) {
            ASSERT_EQ(state.combat().phase, CombatPhase::card_choice);
            const auto chosen = state.selection_candidates().back();
            ASSERT_EQ(choose(state, scratch, std::span<const CardHandle>(&chosen, 1)).status, StepStatus::awaiting_input);
            EXPECT_EQ(state.zone(Zone::exhaust), (std::vector<CardHandle>{chosen}));
            EXPECT_EQ(state.data.rng.get(RunStream::combat_card_selection).state(), rng);
        } else {
            EXPECT_EQ(state.combat().phase, CombatPhase::play);
            EXPECT_EQ(state.data.rng.get(RunStream::combat_card_selection).state().counter, rng.counter + 1);
            EXPECT_EQ(state.zone(Zone::exhaust).size(), 1u);
        }
        EXPECT_EQ(state.combat().counters.started_plays, 1u); EXPECT_EQ(state.combat().energy, 2);
        EXPECT_TRUE(valid_combat(state));
    }
}

TEST(CardChoices, DiscardPromptOverflowCopiesRemainIndependent) {
    std::array<CardSpec, 40> deck; deck.fill({CardId::headbutt});
    CombatConfig config; config.deck = deck; RunState state; Scratch scratch;
    ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
    for (int i = 0; i < 3; ++i) ASSERT_EQ(apply_combat(state, {state.data.boundary, CombatInputKind::end_turn}, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(play(state, find(state, CardId::headbutt), scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(observe_combat(state).selection->candidates.size(), 15u);
    auto cloned = state; RunState restored;
    ASSERT_EQ(restore(restored, checkpoint(state).bytes, scratch), RestoreStatus::restored);
    const auto first = observe_combat(state).selection->candidates.front(), last = observe_combat(state).selection->candidates.back();
    ASSERT_EQ(choose(state, scratch, std::span<const CardHandle>(&first, 1)).status, StepStatus::awaiting_input);
    ASSERT_EQ(choose(restored, scratch, std::span<const CardHandle>(&first, 1)).status, StepStatus::awaiting_input);
    EXPECT_EQ(state, restored);
    ASSERT_EQ(choose(cloned, scratch, std::span<const CardHandle>(&last, 1)).status, StepStatus::awaiting_input);
    EXPECT_EQ(state.zone(Zone::draw).front(), first); EXPECT_EQ(cloned.zone(Zone::draw).front(), last);
    EXPECT_TRUE(valid_combat(state)); EXPECT_TRUE(valid_combat(cloned));
}
TEST(CardChoices, ArmamentsUpgradeMakesTrueGritUseChosenExhaustOnNextPlay) {
    const std::array deck{CardSpec{CardId::armaments, true}, CardSpec{CardId::true_grit},
                          CardSpec{CardId::strike}, CardSpec{CardId::defend}};
    CombatConfig config; config.deck = deck;
    RunState state; Scratch scratch;
    ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
    const auto grit = find(state, CardId::true_grit);
    ASSERT_EQ(play(state, find(state, CardId::armaments), scratch).status, StepStatus::awaiting_input);
    ASSERT_TRUE(state.cards().get(grit)->upgraded);
    const auto rng = state.data.rng.get(RunStream::combat_card_selection).state();
    ASSERT_EQ(play(state, grit, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(state.player().block, 14);
    ASSERT_EQ(state.combat().phase, CombatPhase::card_choice);
    const auto selected = state.selection_candidates().front();
    ASSERT_EQ(choose(state, scratch, std::span<const CardHandle>(&selected, 1)).status, StepStatus::awaiting_input);
    EXPECT_EQ(state.zone(Zone::exhaust), (std::vector<CardHandle>{selected}));
    EXPECT_EQ(state.data.rng.get(RunStream::combat_card_selection).state(), rng);
    EXPECT_TRUE(valid_combat(state));
}
}

namespace sts2::engine {
TEST(CardChoices, LegacyCheckpointWithPartialEditsIsRejectedAtomically) {
    const std::array deck{CardSpec{CardId::purity}, CardSpec{CardId::strike}, CardSpec{CardId::defend}};
    CombatConfig config; config.deck = deck; RunState state; Scratch scratch;
    ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(play(state, find(state, CardId::purity), scratch).status, StepStatus::awaiting_input);
    const auto before = checkpoint(state).bytes;
    auto bytes = before;
    // Reserved legacy selected-count precedes seven counters, the power ID and digest.
    bytes[test::combat_payload_range(bytes).end() - 8 - 32 - 4] = std::byte{1};
    test::checksum_combat_and_run(bytes);
    EXPECT_EQ(restore(state, bytes, scratch), RestoreStatus::malformed);
    EXPECT_EQ(checkpoint(state).bytes, before);
}
TEST(CardChoices, PurityRetainsAcrossTurnsAndCompleteSubmissionAdvancesClockOnce) {
    const std::array deck{CardSpec{CardId::purity}, CardSpec{CardId::defend}};
    CombatConfig config; config.deck = deck; config.clock = {1234, 7}; RunState state; Scratch scratch;
    ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
    const auto source = find(state, CardId::purity);
    ASSERT_EQ(apply_combat(state, {state.data.boundary, CombatInputKind::end_turn}, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(find(state, CardId::purity), source);
    ASSERT_EQ(play(state, source, scratch).status, StepStatus::awaiting_input);
    const std::array chosen{state.selection_candidates().front()};
    ASSERT_EQ(choose(state, scratch, chosen).status, StepStatus::awaiting_input);
    EXPECT_EQ(state.data.clock.elapsed_seconds(), 21); EXPECT_EQ(state.data.boundary.value, 4u);
    EXPECT_TRUE(valid_combat(state));
}
}

namespace sts2::engine {
TEST(CardChoices, ExhaustUpToFiveAcceptsEveryLengthAndPreservesOrderBelowMaximum) {
    std::array<CardSpec, 7> deck; deck.fill({CardId::strike}); deck[0] = {CardId::purity, true};
    CombatConfig config; config.deck = deck; RunState root; Scratch scratch;
    ASSERT_EQ(initialize_combat(root, config, scratch).status, StepStatus::awaiting_input);
    // Arrange a seven-card hand so six eligible candidates exceed the cap of five.
    for (auto zone : root.zone_views()) zone.clear();
    for (std::uint32_t i = 0; i < deck.size(); ++i) root.zone(Zone::hand).push_back({i, root.cards().slots()[i].generation});
    const auto source = find(root, CardId::purity);
    ASSERT_EQ(play(root, source, scratch).status, StepStatus::awaiting_input);
    const auto space = *selection_space(root);
    EXPECT_EQ(space.minimum, 0u); ASSERT_EQ(space.maximum, 5u); ASSERT_EQ(space.candidates.size(), 6u);
    const std::array ordered{space.candidates[4], space.candidates[1], space.candidates[5], space.candidates[0], space.candidates[3]};
    for (std::size_t count = 0; count <= 5; ++count) {
        auto state = root;
        ASSERT_EQ(choose(state, scratch, std::span(ordered).first(count)).status, StepStatus::awaiting_input);
        std::vector<CardHandle> expected(ordered.begin(), ordered.begin() + count); expected.push_back(source);
        EXPECT_EQ(state.zone(Zone::exhaust), expected);
        EXPECT_TRUE(valid_run(state));
    }
    std::array<CardHandle, 5> workspace;
    std::array<unsigned, 6> counts{};
    ASSERT_TRUE(for_each_selection(space, workspace, [&](SelectionInput input) { ++counts[input.cards.size()]; return true; }));
    EXPECT_EQ(counts, (std::array<unsigned, 6>{1, 6, 30, 120, 360, 720}));
}
}

namespace sts2::engine {
TEST(CardChoices, BurningPactDrawsOnceAfterCompleteSubmissionAndPromptRestore) {
    std::array<CardSpec, 8> deck; deck.fill({CardId::burning_pact, true});
    CombatConfig config; config.deck = deck;
    RunState state; Scratch scratch;
    ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
    const auto source = *std::find_if(state.zone(Zone::hand).begin(), state.zone(Zone::hand).end(), [&](auto h) { return state.cards().get(h)->definition == CardId::burning_pact; });
    ASSERT_EQ(apply_combat(state, {state.data.boundary, CombatInputKind::play_card, source}, scratch).status, StepStatus::awaiting_input);
    const auto chosen = state.selection_candidates().front();
    EXPECT_EQ(state.zone(Zone::hand).size(), 4u);
    RunState copy; ASSERT_EQ(restore(copy, checkpoint(state).bytes, scratch), RestoreStatus::restored);
    const auto confirm = SelectionInput{copy.data.boundary, copy.combat().selection.id, std::span<const CardHandle>(&chosen, 1)};
    ASSERT_EQ(apply_selection(copy, confirm, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(copy.zone(Zone::exhaust), (std::vector<CardHandle>{chosen}));
    EXPECT_EQ(copy.zone(Zone::hand).size(), 6u); EXPECT_EQ(copy.combat().counters.non_hand_draws_this_turn, 3u);
    EXPECT_EQ(copy.combat().counters.started_plays, 1u); EXPECT_EQ(copy.combat().energy, 2);
    EXPECT_EQ(copy.combat().phase, CombatPhase::play);
    const auto before = copy;
    EXPECT_EQ(apply_selection(copy, confirm, scratch).status, StepStatus::invalid_action);
    EXPECT_EQ(copy, before);
}

}

namespace sts2::engine {
TEST(CardChoices, DiscardSelectionRejectsStaleHandlesBeforeMutation) {
    const std::array deck{CardSpec{CardId::survivor}, CardSpec{CardId::strike}, CardSpec{CardId::defend}};
    CombatConfig config; config.deck = deck; RunState state; Scratch scratch;
    ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(play(state, find(state, CardId::survivor), scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(state.combat().phase, CombatPhase::card_choice);
    auto card = state.zone(Zone::hand).front(); ++card.generation;
    const auto before = checkpoint(state).bytes;
    EXPECT_EQ(apply_selection(state, {state.data.boundary, state.combat().selection.id, std::span<const CardHandle>(&card, 1)}, scratch).reason, Restriction::stale_card);
    EXPECT_EQ(checkpoint(state).bytes, before);
    // Matching a corrupt pile entry must not bypass checked identity validation.
    state.zone(Zone::hand).front() = card;
    const auto corrupted = state;
    EXPECT_EQ(apply_selection(state, {state.data.boundary, state.combat().selection.id, std::span<const CardHandle>(&card, 1)}, scratch).reason, Restriction::invalid_state);
    EXPECT_EQ(state, corrupted);
}
}

namespace sts2::engine {
TEST(CardChoices, UnconsumedAnswerIsNotAPublicBoundary) {
    RunState state; Scratch scratch;
    ASSERT_EQ(initialize_combat(state, {}, scratch).status, StepStatus::awaiting_input);
    const auto before = state;
    state.combat().selected_cards = {0, 1, 0};
    EXPECT_NE(state, before);
    EXPECT_FALSE(valid_combat(state));
    EXPECT_THROW(checkpoint(state), std::invalid_argument);
}
}

namespace sts2::engine {
TEST(CardChoices, PreviousSelectionCheckpointFormatsAreRejectedAtomically) {
    RunState state; Scratch scratch;
    const std::array deck{CardSpec{CardId::purity}, CardSpec{CardId::strike}, CardSpec{CardId::defend}};
    CombatConfig config; config.deck = deck;
    ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(play(state, find(state, CardId::purity), scratch).status, StepStatus::awaiting_input);
    const auto saved = checkpoint(state).bytes;
    for (auto revision : {20u, 21u}) {
        auto previous_run = saved;
        test::checkpoint_u32(previous_run, 8, revision);
        test::checksum_checkpoint(previous_run);
        EXPECT_EQ(restore(state, previous_run, scratch), RestoreStatus::incompatible);
        EXPECT_EQ(checkpoint(state).bytes, saved);
    }
    auto previous_combat = saved;
    const auto payload = test::combat_payload_range(previous_combat);
    test::checkpoint_u32(previous_combat, payload.begin + 8, 3);
    test::checksum_combat_and_run(previous_combat);
    EXPECT_EQ(restore(state, previous_combat, scratch), RestoreStatus::incompatible);
    EXPECT_EQ(checkpoint(state).bytes, saved);
}
}

namespace sts2::engine {
TEST(CardChoices, UpgradePromptRejectsStalePileIdentityBeforeMutation) {
    RunState state; Scratch scratch;
    const std::array deck{CardSpec{CardId::armaments}, CardSpec{CardId::strike}, CardSpec{CardId::defend}};
    CombatConfig config; config.deck = deck;
    ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(play(state, find(state, CardId::armaments), scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(state.combat().phase, CombatPhase::card_choice);
    ++state.zone(Zone::hand).front().generation;
    const auto before = state;
    const auto candidate = state.selection_candidates().front();
    const SelectionInput answer{state.data.boundary, state.combat().selection.id, std::span(&candidate, 1)};
    EXPECT_EQ(apply_selection(state, answer, scratch).reason, Restriction::invalid_state);
    EXPECT_EQ(state, before);
}
}
