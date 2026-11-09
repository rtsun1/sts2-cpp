#include "../../benchmarks/mcts/native/search_bridge.hpp"
#include <gtest/gtest.h>

namespace {
using namespace sts2::benchmark::native;
int position, restores;
int restore() { position = 0; ++restores; return 1; }
int count() { return 2; }
int action(int index, Action* out) { *out = {position == 0 ? index : index + 2, -1, 0}; return 1; }
int apply(Action input) { position = input.card + 1; return 1; }
int terminal() { return position == 1 || position >= 3; }
double evaluate() { return position == 1 ? 0.4 : position == 3 ? 1.0 : 0.0; }
TEST(MctsBridge, CallbackSearchFindsDelayedRewardAndPreservesAbi) {
    restores = 0;
    Result result{};
    ASSERT_EQ(mcts_search({restore, count, action, apply, terminal, evaluate}, 1000, 3, 1, &result), 1);
    EXPECT_EQ(result.action.card, 1);
    EXPECT_EQ(result.iterations, 1000);
    EXPECT_EQ(restores, 1000);
    EXPECT_EQ(result.terminals, 1000);
    EXPECT_GT(result.mean, 0.9);
}
TEST(MctsBridge, CallbackFailureDoesNotUnwindAcrossAbi) {
    Result result{};
    auto fail = []() -> int { return 0; };
    EXPECT_EQ(mcts_search({fail, count, action, apply, terminal, evaluate}, 1, 3, 1, &result), 0);
    EXPECT_EQ(mcts_search({restore, count, action, apply, terminal, evaluate}, 0, 3, 1, &result), 0);
}
}
