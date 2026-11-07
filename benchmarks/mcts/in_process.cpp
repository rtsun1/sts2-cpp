#include "search.hpp"
#include "combat.hpp"
#include "sts2/json_io/session.hpp"
#include <chrono>
#include <fstream>
#include <iostream>

namespace {
using namespace sts2::engine;
using Json = nlohmann::json;
struct EngineAdapter {
    using Action = PlayerInput;
    const RunState& root;
    bool checked;
    bool verify;
    RunState state;
    Scratch scratch;
    EngineAdapter(const RunState& initial, bool checked_mode, bool verification)
        : root(initial), checked(checked_mode), verify(verification) {}
    void restore_root() {
        if (clone_into(state, root) != CloneStatus::cloned) throw std::runtime_error("Clone failed");
        scratch = Scratch{};
    }
    bool terminal() const { return state.player().hp <= 0 || state.data.phase != RunPhase::combat; }
    std::vector<Action> actions() const {
        std::vector<Action> result;
        for_each_legal_action(state, [&](const PlayerInput& input) { result.push_back(input); });
        return result;
    }
    void apply(const Action& action) {
        std::optional<RunState> comparison;
        if (verify) {
            if (clone_into(comparison.emplace(), state) != CloneStatus::cloned) throw std::runtime_error("Verification clone failed");
            Scratch verification_scratch;
            const auto reference = apply_action(*comparison, action, verification_scratch);
            if (reference.status != StepStatus::awaiting_input && reference.status != StepStatus::player_died)
                throw std::runtime_error("Checked verification failed");
        }
        const auto result = checked ? apply_action(state, action, scratch) : apply_action_trusted(state, action, scratch);
        if (result.status != StepStatus::awaiting_input && result.status != StepStatus::player_died)
            throw std::runtime_error("Search action failed; discard branch");
        if (verify && !(state == *comparison)) throw std::runtime_error("Checked/trusted state divergence");
    }
    double evaluate() const {
        if (state.player().hp <= 0) return 0;
        return (terminal() ? 0.5 : 0) + 0.5 * state.player().hp / state.player().max_hp;
    }
};
PlayerInput decode(const RunState& state, const Json& action) {
    const auto kind = action.at("kind").get<std::string>();
    if (kind == "choose_option") return RunInput{state.data.boundary, RunInputKind::choose_option, action.at("option_index")};
    if (kind == "select_map_node") return RunInput{state.data.boundary, RunInputKind::travel, 0, action.at("col"), action.at("row")};
    if (kind == "end_turn") return CombatInput{state.data.boundary, CombatInputKind::end_turn};
    if (kind == "play_card") {
        const auto index = action.at("card_index").get<std::size_t>();
        const auto hand = state.zone(Zone::hand);
        if (index >= hand.size()) throw std::runtime_error("Invalid hand index");
        CreatureHandle target{};
        if (action.contains("target_index")) {
            if (action.at("target_index") != 0) throw std::runtime_error("Unsupported target");
            target = state.enemy_handle();
        }
        return CombatInput{state.data.boundary, CombatInputKind::play_card, hand[index], target};
    }
    throw std::runtime_error("Unsupported prefix action");
}
}
int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("Usage: mcts_in_process CONFIG.json");
        std::ifstream file(argv[1]);
        const auto config = Json::parse(file);
        const auto scenario = config.at("scenario");
        RunState root;
        Scratch scratch;
        initialize_json_run(root, scenario.at("setup"));
        for (const auto& operation : scenario.at("operations")) {
            StepResult result;
            if (operation.at("cmd") == "scenario_enter_combat")
                result = enter_combat_scenario(root, root.data.boundary, operation.at("encounter").get<std::string>());
            else if (operation.at("cmd") == "action") result = apply_action(root, decode(root, operation.at("action")), scratch);
            else throw std::runtime_error("Unsupported prefix operation");
            if (result.status != StepStatus::awaiting_input) throw std::runtime_error("Prefix failed");
        }
        sts2::benchmark::CombatSearch profile;
        profile.begin(root, true); // Validate root and establish bounded content support once.
        EngineAdapter adapter{root, config.value("checked", false), config.value("verify", false)};
        sts2::benchmark::SearchConfig search_config;
        search_config.iterations = config.value("iterations", 100u);
        search_config.horizon = config.value("horizon", 64u);
        search_config.seed = config.value("seed", std::uint64_t{1});
        const auto repeats = config.value("repeats", 1u);
        if (repeats == 0 || repeats > 1000000) throw std::runtime_error("Invalid repeat count");
        const auto start = std::chrono::steady_clock::now();
        const auto result = sts2::benchmark::search(adapter, search_config);
        for (unsigned repeat = 1; repeat < repeats; ++repeat) (void)sts2::benchmark::search(adapter, search_config);
        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        const auto& action = std::get<CombatInput>(result.action);
        Json selected{{"kind", action.kind == CombatInputKind::end_turn ? "end_turn" : "play_card"}};
        if (action.kind == CombatInputKind::play_card) {
            const auto hand = root.zone(Zone::hand);
            selected["card_index"] = std::distance(hand.begin(), std::find(hand.begin(), hand.end(), action.card));
            if (action.target.present()) selected["target_index"] = action.target.slot - 1;
        }
        std::cout << Json{{"profile", "mcts-in-process-clone-v1"}, {"seconds", elapsed}, {"repeats", repeats}, {"seconds_per_search", elapsed / repeats}, {"action", selected},
            {"iterations", result.stats.iterations}, {"transitions", result.stats.transitions},
            {"nodes", result.stats.nodes}, {"best_visits", result.stats.best_visits}, {"best_mean", result.stats.best_mean},
            {"terminal_rollouts", result.stats.terminal_rollouts}, {"cutoffs", result.stats.cutoffs},
            {"transitions_per_second", static_cast<double>(result.stats.transitions) * repeats / elapsed}, {"checked", adapter.checked}, {"verification_enabled", adapter.verify}}.dump() << '\n';
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
