#include "../../benchmarks/mcts/search.hpp"
#include <gtest/gtest.h>

namespace {
struct TwoStepWorld {
    using Action = int;
    int position = 0;
    std::size_t restores = 0;
    void restore_root() { position = 0; ++restores; }
    bool terminal() const { return position == 1 || position >= 3; }
    std::vector<int> actions() const { return position == 0 ? std::vector<int>{1, 2} : std::vector<int>{3, 4}; }
    void apply(int action) { position = action; }
    double evaluate() const { return position == 1 ? 0.4 : position == 3 ? 1.0 : 0.0; }
};
TEST(MctsSearch, LearnsDelayedRewardAndRestoresEverySimulation) {
    TwoStepWorld world;
    const auto result = sts2::benchmark::search(world, {1000, 3, 1});
    EXPECT_EQ(result.action, 2);
    EXPECT_GT(result.stats.best_mean, 0.9);
    EXPECT_EQ(world.restores, 1000);
    EXPECT_EQ(result.stats.terminal_rollouts, 1000);
    EXPECT_EQ(result.stats.cutoffs, 0);
    EXPECT_LE(result.stats.nodes, 5);
}
TEST(MctsSearch, HorizonBoundsBothSelectionAndRollout) {
    TwoStepWorld world;
    const auto result = sts2::benchmark::search(world, {100, 1, 7});
    EXPECT_EQ(result.action, 1);
    EXPECT_EQ(result.stats.transitions, 100);
    EXPECT_GT(result.stats.cutoffs, 0);
    EXPECT_EQ(result.stats.cutoffs + result.stats.terminal_rollouts, 100);
}
TEST(MctsSearch, SameSeedReproducesSearch) {
    TwoStepWorld first, second;
    const auto a = sts2::benchmark::search(first, {50, 3, 42});
    const auto b = sts2::benchmark::search(second, {50, 3, 42});
    EXPECT_EQ(a.action, b.action);
    EXPECT_EQ(a.stats.transitions, b.stats.transitions);
    EXPECT_EQ(a.stats.best_visits, b.stats.best_visits);
    EXPECT_EQ(a.stats.best_mean, b.stats.best_mean);
}
TEST(MctsSearch, RejectsEmptyBudgetsAndInvalidEvaluation) {
    TwoStepWorld world;
    EXPECT_THROW(sts2::benchmark::search(world, {0, 3, 1}), std::invalid_argument);
    EXPECT_THROW(sts2::benchmark::search(world, {1, 0, 1}), std::invalid_argument);
    struct InvalidWorld : TwoStepWorld { double evaluate() const { return 2.0; } } invalid;
    EXPECT_THROW(sts2::benchmark::search(invalid, {1, 3, 1}), std::runtime_error);
}
TEST(MctsSearch, PropagatesBackendFailure) {
    struct FailingWorld : TwoStepWorld {
        void apply(int) { throw std::runtime_error("unsupported transition"); }
    } world;
    EXPECT_THROW(sts2::benchmark::search(world, {1, 3, 1}), std::runtime_error);
}
} // namespace
