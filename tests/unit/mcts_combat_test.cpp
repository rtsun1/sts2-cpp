#include "../../benchmarks/mcts/combat.hpp"
#include <gtest/gtest.h>

namespace {
using namespace sts2::engine;
using sts2::benchmark::CombatSearch;

TEST(MctsTiming, NestedScopesUnwindAndDisabledScopesDoNotAccumulate) {
    using Timing = sts2::benchmark::WorkerTiming;
    Timing::totals.clear();
    Timing::enabled = true;
    try {
        Timing::Scope outer("execution");
        Timing::Scope inner("enumeration");
        throw std::runtime_error("test unwind");
    } catch (const std::runtime_error&) {}
    Timing::enabled = false;
    EXPECT_EQ(Timing::Scope::current, nullptr);
    const auto before = Timing::snapshot();
    EXPECT_EQ(before["execution"]["calls"], 1);
    EXPECT_EQ(before["enumeration"]["calls"], 1);
    EXPECT_GE(before["execution"]["seconds"].get<double>(), 0);
    { Timing::Scope disabled("execution"); }
    EXPECT_EQ(Timing::snapshot(), before);
    Timing::totals.clear();
}

TEST(MctsCombat, EveryLegalRootActionMatchesCheckedExecution) {
    RunState root;
    Scratch scratch;
    ASSERT_EQ(initialize_combat(root, {}, scratch).status, StepStatus::awaiting_input);
    CombatSearch initial;
    initial.begin(root, true);
    const auto choices = initial.project(root)["observation"]["choices"];
    ASSERT_FALSE(choices.empty());
    for (std::size_t index = 0; index < choices.size(); ++index) {
        RunState checked_state, trusted_state;
        ASSERT_EQ(clone_into(checked_state, root), CloneStatus::cloned);
        ASSERT_EQ(clone_into(trusted_state, root), CloneStatus::cloned);
        CombatSearch checked, trusted;
        checked.begin(checked_state, false);
        trusted.begin(trusted_state, true);
        EXPECT_EQ(checked.project(checked_state), trusted.project(trusted_state));
        checked.apply(checked_state, index, scratch);
        trusted.apply(trusted_state, index, scratch);
        EXPECT_TRUE(checked_state == trusted_state);
        EXPECT_EQ(checked.project(checked_state), trusted.project(trusted_state));
    }
}

TEST(MctsCombat, CachedActionsAreConsumedUntilNextProjection) {
    RunState state;
    Scratch scratch;
    ASSERT_EQ(initialize_combat(state, {}, scratch).status, StepStatus::awaiting_input);
    CombatSearch adapter;
    EXPECT_THROW(adapter.apply(state, 0, scratch), std::runtime_error);
    adapter.begin(state, true);
    adapter.project(state);
    adapter.apply(state, 0, scratch);
    EXPECT_THROW(adapter.apply(state, 0, scratch), std::runtime_error);
}

TEST(MctsCombat, RejectsUnsupportedRootContent) {
    RunState state;
    Scratch scratch;
    const CardSpec deck[]{{CardId::survivor}};
    CombatConfig config;
    config.deck = deck;
    ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
    CombatSearch adapter;
    EXPECT_THROW(adapter.begin(state, true), std::runtime_error);
}
} // namespace
