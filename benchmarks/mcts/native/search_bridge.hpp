#pragma once
#include <cstdint>
#include <cstddef>

// Linux x64 C ABI, mirrored by BenchmarkMain.cs. Managed delegates must remain
// rooted through mcts_search; callback failures must not unwind across this ABI.
namespace sts2::benchmark::native {
struct Action { std::int32_t card, target, end_turn; };
struct Callbacks {
    int (*restore)();
    int (*count)();
    int (*action)(int, Action*);
    int (*apply)(Action);
    int (*terminal)();
    double (*evaluate)();
};
struct Result {
    Action action;
    std::uint64_t iterations, transitions, nodes, visits, terminals, cutoffs;
    double mean;
};
static_assert(sizeof(Action) == 12 && sizeof(Result) == 72);
static_assert(offsetof(Result, iterations) == 16);
}
extern "C" int mcts_search(sts2::benchmark::native::Callbacks, std::uint64_t iterations,
    std::uint64_t horizon, std::uint64_t seed, sts2::benchmark::native::Result*) noexcept;
