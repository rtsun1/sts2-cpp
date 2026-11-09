#pragma once
#include <chrono>
#include <map>
#include <string>
#include <nlohmann/json.hpp>

namespace sts2::benchmark {
// Exclusive worker wall times. Scopes nested inside projection (enumeration)
// are subtracted from their parent. Disabled scopes do not read the clock.
struct WorkerTiming {
    inline static bool enabled = false;
    inline static std::map<std::string, std::pair<double, unsigned long long>> totals;
    struct Scope {
        inline static Scope* current = nullptr;
        Scope* parent = nullptr;
        const char* category;
        std::chrono::steady_clock::time_point start;
        double children = 0;
        bool active;
        explicit Scope(const char* name) : category(name), active(enabled) {
            if (!active) return;
            parent = current; current = this;
            start = std::chrono::steady_clock::now();
        }
        ~Scope() {
            if (!active) return;
            const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            auto& total = totals[category];
            total.first += elapsed - children; ++total.second;
            current = parent;
            if (parent) parent->children += elapsed;
        }
    };
    static nlohmann::json snapshot() {
        nlohmann::json result = nlohmann::json::object();
        for (const auto& [name, value] : totals)
            result[name] = {{"seconds", value.first}, {"calls", value.second}};
        return result;
    }
};
} // namespace sts2::benchmark
