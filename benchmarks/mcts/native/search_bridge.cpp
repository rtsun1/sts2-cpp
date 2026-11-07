#include "../search.hpp"
#include <cstdint>
#include <vector>
#include <stdexcept>

#include "search_bridge.hpp"
using namespace sts2::benchmark::native;

struct Adapter {
    using Action = sts2::benchmark::native::Action;
    Callbacks callbacks;
    void restore_root() { if (!callbacks.restore()) throw std::runtime_error("restore failed"); }
    std::vector<Action> actions() const {
        const int count = callbacks.count();
        if (count < 0) throw std::runtime_error("enumeration failed");
        std::vector<Action> result(count);
        for (int i = 0; i < count; ++i)
            if (!callbacks.action(i, &result[i])) throw std::runtime_error("action failed");
        return result;
    }
    void apply(Action action) { if (!callbacks.apply(action)) throw std::runtime_error("apply failed"); }
    bool terminal() const {
        const int value = callbacks.terminal();
        if (value < 0) throw std::runtime_error("terminal failed");
        return value != 0;
    }
    double evaluate() const { return callbacks.evaluate(); }
};
extern "C" int mcts_search(Callbacks callbacks, std::uint64_t iterations,
                            std::uint64_t horizon, std::uint64_t seed, Result* output) noexcept {
    if (!output || !callbacks.restore || !callbacks.count || !callbacks.action ||
        !callbacks.apply || !callbacks.terminal || !callbacks.evaluate) return 0;
    try {
        Adapter adapter{callbacks};
        const auto result = sts2::benchmark::search(adapter, {iterations, horizon, seed});
        const auto& s = result.stats;
        *output = {result.action, s.iterations, s.transitions, s.nodes, s.best_visits,
                   s.terminal_rollouts, s.cutoffs, s.best_mean};
        return 1;
    } catch (...) { return 0; }
}
