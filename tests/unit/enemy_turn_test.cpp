#include "sts2/engine/action.hpp"
#include "sts2/persistence/checkpoint.hpp"
#include <algorithm>
#include <gtest/gtest.h>
#include <vector>

namespace sts2::engine {
namespace {
void discard_all(RunState& state) {
    for (auto zone : state.zone_views()) zone.clear();
    for (std::uint32_t i = 0; i < state.cards().slots().size(); ++i) {
        const auto& slot = state.cards().slots()[i];
        if (slot.live) state.zone(Zone::discard).push_back({i, slot.generation});
    }
}
}
TEST(EnemyTurn, CentennialChoiceBlocksAttackCompletionAndResumesAfterCheckpoint) {
    RunState state; Scratch scratch;
    const std::array relics{SetupRules::find("RELIC.CENTENNIAL_PUZZLE")};
    ASSERT_EQ(initialize_combat_scenario(state, {}, {}, {}, relics).status, StepStatus::awaiting_input);
    ASSERT_EQ(apply_power_scenario(state, {PowerId::stratagem, 1}, scratch).status, StepStatus::awaiting_input);
    // Diagnostic setup keeps real cards and effects but forces the next attack
    // and a shuffle during Puzzle's first one-card draw.
    state.enemy().enemy_ai.selected = BeetleMove::chomp;
    discard_all(state);
    ASSERT_TRUE(valid_run(state));
    ASSERT_EQ(apply_action(state, CombatInput{state.data.boundary, CombatInputKind::end_turn}, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(state.combat().phase, CombatPhase::card_choice);
    EXPECT_TRUE(state.combat().enemy_side);
    EXPECT_EQ(state.player().hp, 73);
    EXPECT_EQ(state.combat().turn, 1);
    EXPECT_EQ(state.enemy().enemy_ai.selected, BeetleMove::chomp);
    EXPECT_TRUE(state.zone(Zone::hand).empty());
    EXPECT_EQ(state.zone(Zone::draw).size(), 10u);
    EXPECT_TRUE(state.combat_overflow.empty());
    ASSERT_TRUE(selection_space(state));
    EXPECT_TRUE(get_actions(state).empty());
    const auto saved = checkpoint(state).bytes;
    RunState restored;
    ASSERT_EQ(restore(restored, saved, scratch), RestoreStatus::restored);
    RunState clone = state;
    EXPECT_TRUE(clone.combat_overflow.empty());
    const std::array cards{state.selection_candidates().front()};
    for (auto* branch : {&state, &clone, &restored}) {
        const SelectionInput answer{branch->data.boundary, branch->combat().selection.id, cards};
        ASSERT_EQ(validate_action(*branch, answer), Restriction::none);
        ASSERT_EQ(apply_action(*branch, answer, scratch).status, StepStatus::awaiting_input);
        EXPECT_FALSE(branch->combat().enemy_side);
        EXPECT_EQ(branch->combat().turn, 2);
        EXPECT_EQ(branch->player().hp, 73);
        EXPECT_EQ(branch->enemy().enemy_ai.selected, BeetleMove::stomp);
        EXPECT_EQ(branch->zone(Zone::hand).size(), 9u);
        EXPECT_TRUE(branch->execution().empty());
        EXPECT_TRUE(valid_run(*branch));
    }
    EXPECT_EQ(state, clone);
    EXPECT_EQ(state, restored);
}

TEST(EnemyTurn, LaterEnemyWaitsAndCheckedTrustedSelectionsHaveEqualFutures) {
    RunState state; Scratch scratch, trusted_scratch;
    CombatConfig config; config.enemy_count = 2; config.centennial_puzzle = true;
    ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
    const auto first = state.enemies()[0], second = state.enemies()[1];
    state.creature(first)->enemy_ai.selected = BeetleMove::chomp;
    state.creature(second)->enemy_ai.selected = BeetleMove::stomp;
    state.creature(second)->block = 17;
    discard_all(state);
    ASSERT_EQ(apply_power_scenario(state, {PowerId::stratagem, 1}, scratch).status, StepStatus::awaiting_input);
    std::vector<CombatEventKind> events;
    scratch.trace = {&events, [](void* context, const RunState&, const CombatEvent& event) {
        static_cast<std::vector<CombatEventKind>*>(context)->push_back(event.kind);
    }};
    RunState trusted = state;
    const CombatInput end{state.data.boundary, CombatInputKind::end_turn};
    ASSERT_EQ(apply_action(state, end, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(apply_action_trusted(trusted, end, trusted_scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(state, trusted);
    ASSERT_EQ(state.combat().phase, CombatPhase::card_choice);
    EXPECT_EQ(state.player().hp, 73);
    EXPECT_EQ(state.creature(second)->block, 17);
    EXPECT_TRUE(state.combat().centennial_puzzle_used);
    EXPECT_EQ(state.creature(second)->enemy_ai.selected, BeetleMove::stomp);
    EXPECT_EQ(std::count(events.begin(), events.end(), CombatEventKind::damaged), 1);
    EXPECT_EQ(std::count(events.begin(), events.end(), CombatEventKind::attacked), 0);
    const auto before = state;
    EXPECT_EQ(apply_action(state, CombatInput{state.data.boundary, CombatInputKind::end_turn}, scratch).reason, Restriction::wrong_phase);
    const SelectionInput empty{state.data.boundary, state.combat().selection.id, {}};
    EXPECT_EQ(apply_action(state, empty, scratch).status, StepStatus::invalid_action);
    EXPECT_EQ(state, before);
    ASSERT_TRUE(selection_space(state));
    const auto space = *selection_space(state);
    EXPECT_EQ(space.minimum, 1u); EXPECT_EQ(space.maximum, 1u);
    EXPECT_EQ(space.candidates.size(), 10u);
    EXPECT_TRUE(get_actions(state).empty());
    EXPECT_EQ(state, before);
    const std::array selected{state.selection_candidates().back()};
    const SelectionInput answer{state.data.boundary, state.combat().selection.id, selected};
    ASSERT_EQ(apply_action(state, answer, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(apply_action_trusted(trusted, answer, trusted_scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(state, trusted);
    EXPECT_EQ(state.player().hp, 60);
    EXPECT_EQ(state.combat().turn, 2);
    EXPECT_EQ(state.creature(first)->enemy_ai.selected, BeetleMove::stomp);
    EXPECT_EQ(state.creature(second)->enemy_ai.selected, BeetleMove::chomp);
    EXPECT_EQ(state.creature(second)->block, 0);
    EXPECT_EQ(state.zone(Zone::hand).size(), 9u);
    EXPECT_EQ(std::count(events.begin(), events.end(), CombatEventKind::damaged), 2);
    EXPECT_EQ(std::count(events.begin(), events.end(), CombatEventKind::attacked), 2);
    EXPECT_TRUE(valid_run(state));
}

TEST(EnemyTurn, PuzzleRequiresUnblockedNonlethalDamageAndTriggersOnce) {
    for (int block : {0, 7}) {
        RunState state; Scratch scratch;
        CombatConfig config; config.centennial_puzzle = true;
        ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
        state.enemy().enemy_ai.selected = BeetleMove::chomp;
        state.player().block = block;
        ASSERT_EQ(apply_action(state, CombatInput{state.data.boundary, CombatInputKind::end_turn}, scratch).status, StepStatus::awaiting_input);
        EXPECT_EQ(state.player().hp, block == 0 ? 73 : 80);
        EXPECT_EQ(state.combat().centennial_puzzle_used, block == 0);
        EXPECT_EQ(state.zone(Zone::hand).size(), block == 0 ? 8u : 5u);
        ASSERT_EQ(apply_action(state, CombatInput{state.data.boundary, CombatInputKind::end_turn}, scratch).status, StepStatus::awaiting_input);
        EXPECT_TRUE(state.combat().centennial_puzzle_used);
        EXPECT_EQ(state.zone(Zone::hand).size(), block == 0 ? 5u : 8u);
        EXPECT_TRUE(valid_run(state));
    }
    RunState lethal; Scratch scratch;
    CombatConfig config; config.centennial_puzzle = true; config.player_hp = 5;
    ASSERT_EQ(initialize_combat(lethal, config, scratch).status, StepStatus::awaiting_input);
    lethal.enemy().enemy_ai.selected = BeetleMove::chomp;
    ASSERT_EQ(apply_action(lethal, CombatInput{lethal.data.boundary, CombatInputKind::end_turn}, scratch).status, StepStatus::player_died);
    EXPECT_FALSE(lethal.combat().centennial_puzzle_used);
    EXPECT_TRUE(lethal.execution().empty());
    EXPECT_TRUE(valid_run(lethal));
}
}
