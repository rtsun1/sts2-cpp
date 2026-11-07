#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>
#include <stdexcept>
#include <utility>
#include <vector>

namespace sts2::benchmark {
struct SearchConfig {
    std::size_t iterations = 100;
    std::size_t horizon = 32; // Total actions per simulation, including tree traversal.
    std::uint64_t seed = 1;
    double exploration = std::sqrt(2.0);
};

struct SearchStats {
    std::size_t iterations = 0, transitions = 0, terminal_rollouts = 0, cutoffs = 0;
    std::size_t nodes = 0, best_visits = 0;
    double best_mean = 0;
};

// Adapter owns one mutable simulator. restore_root reconstructs an independent
// root; actions() returns owning values valid for an exact replay of that state.
// apply failures must throw, rather than becoming a score or an empty action set.
// Values are finite in [0, 1], from a single player's perspective.
template<class Action>
struct SearchResult {
    Action action;
    SearchStats stats;
};

template<class Adapter>
auto search(Adapter& adapter, SearchConfig config) {
    using Action = typename Adapter::Action;
    struct Node {
        Action action{};
        std::vector<std::size_t> children;
        std::vector<Action> unexpanded;
        std::size_t visits = 0;
        double total = 0;
        bool initialized = false;
    };
    if (!config.iterations || !config.horizon || !std::isfinite(config.exploration) || config.exploration < 0)
        throw std::invalid_argument("Invalid search budget or exploration constant");
    std::vector<Node> nodes(1);
    SearchStats stats;
    std::mt19937_64 random(config.seed);
    const auto pick = [&](std::size_t count) {
        // Explicit rejection sampling keeps action sampling reproducible across STL implementations.
        const auto bound = static_cast<std::uint64_t>(count);
        const auto threshold = (std::uint64_t{0} - bound) % bound;
        std::uint64_t value;
        do { value = random(); } while (value < threshold);
        return static_cast<std::size_t>(value % bound);
    };
    for (std::size_t iteration = 0; iteration < config.iterations; ++iteration) {
        adapter.restore_root();
        std::vector<std::size_t> path{0};
        std::size_t current = 0, depth = 0;
        while (depth < config.horizon && !adapter.terminal()) {
            if (!nodes[current].initialized) {
                nodes[current].unexpanded = adapter.actions();
                nodes[current].initialized = true;
            }
            if (!nodes[current].unexpanded.empty()) {
                const auto index = pick(nodes[current].unexpanded.size());
                Action action = nodes[current].unexpanded[index];
                nodes[current].unexpanded.erase(nodes[current].unexpanded.begin() + index);
                adapter.apply(action);
                ++stats.transitions;
                ++depth;
                const auto child = nodes.size();
                nodes[current].children.push_back(child);
                Node node;
                node.action = std::move(action);
                nodes.push_back(std::move(node));
                current = child;
                path.push_back(current);
                break; // Expand one node, then use the rollout policy.
            }
            if (nodes[current].children.empty()) throw std::runtime_error("Nonterminal state has no actions");
            std::size_t selected = nodes[current].children.front();
            double best = -std::numeric_limits<double>::infinity();
            for (auto child : nodes[current].children) {
                const auto& node = nodes[child];
                const auto score = node.total / node.visits + config.exploration *
                    std::sqrt(std::log(static_cast<double>(nodes[current].visits)) / node.visits);
                if (score > best) { best = score; selected = child; }
            }
            adapter.apply(nodes[selected].action);
            ++stats.transitions;
            ++depth;
            current = selected;
            path.push_back(current);
        }
        while (depth < config.horizon && !adapter.terminal()) {
            auto actions = adapter.actions();
            if (actions.empty()) throw std::runtime_error("Nonterminal rollout has no actions");
            adapter.apply(actions[pick(actions.size())]);
            ++stats.transitions;
            ++depth;
        }
        const double value = adapter.evaluate();
        if (!std::isfinite(value) || value < 0 || value > 1) throw std::runtime_error("Evaluation outside [0, 1]");
        if (adapter.terminal()) ++stats.terminal_rollouts;
        else ++stats.cutoffs;
        for (auto index : path) { ++nodes[index].visits; nodes[index].total += value; }
        ++stats.iterations;
    }
    if (nodes[0].children.empty()) throw std::runtime_error("Search root has no actions");
    auto best = nodes[0].children.front();
    for (auto child : nodes[0].children)
        if (nodes[child].visits > nodes[best].visits) best = child;
    stats.nodes = nodes.size();
    stats.best_visits = nodes[best].visits;
    stats.best_mean = nodes[best].total / nodes[best].visits;
    return SearchResult<Action>{nodes[best].action, stats};
}
} // namespace sts2::benchmark
