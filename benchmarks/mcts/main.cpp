#include "search.hpp"
#include <nlohmann/json.hpp>
#include <iostream>

namespace {
using Json = nlohmann::json;
// The launcher handles backend processes. This request/reply seam also lets
// tests supply a tiny deterministic world without either game implementation.
class ReplayAdapter {
public:
    using Action = Json;
    void restore_root() { observation_ = request({{"cmd", "restore"}}); }
    void apply(const Action& action) { observation_ = request({{"cmd", "step"}, {"action", action}}); }
    bool terminal() const { return observation_.at("terminal").get<bool>(); }
    std::vector<Action> actions() const { return observation_.at("actions").get<std::vector<Action>>(); }
    double evaluate() const { return observation_.at("value").get<double>(); }
private:
    Json observation_;
    static Json request(const Json& message) {
        std::cout << message.dump() << std::endl;
        std::string line;
        if (!std::getline(std::cin, line)) throw std::runtime_error("Launcher disconnected");
        auto response = Json::parse(line);
        if (response.contains("error")) throw std::runtime_error(response.at("error").get<std::string>());
        return response;
    }
};
}

int main() {
    try {
        std::string line;
        if (!std::getline(std::cin, line)) throw std::runtime_error("Missing configuration");
        const auto input = Json::parse(line);
        sts2::benchmark::SearchConfig config;
        config.iterations = input.at("iterations").get<std::size_t>();
        config.horizon = input.at("horizon").get<std::size_t>();
        config.seed = input.at("seed").get<std::uint64_t>();
        ReplayAdapter adapter;
        const auto result = sts2::benchmark::search(adapter, config);
        const auto& s = result.stats;
        std::cout << Json{{"cmd", "result"}, {"action", result.action}, {"iterations", s.iterations},
            {"transitions", s.transitions}, {"terminal_rollouts", s.terminal_rollouts}, {"cutoffs", s.cutoffs},
            {"nodes", s.nodes}, {"best_visits", s.best_visits}, {"best_mean", s.best_mean}}.dump() << std::endl;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
