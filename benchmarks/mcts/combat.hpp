#pragma once
#include "sts2/engine/action.hpp"
#include "timing.hpp"
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <vector>

namespace sts2::benchmark {
// Benchmark-owned adapter for the bounded starter-deck Shrinker workload.
// Setup/prefix replay is checked. begin establishes the trusted preconditions
// once per reconstructed root; project caches the exact inputs for the next step.
class CombatSearch {
public:
    using Json = nlohmann::json;

    void begin(const engine::RunState& state, bool trusted) {
        WorkerTiming::Scope timing("root_validation");
        ready_ = false;
        inputs_.clear();
        if (!engine::valid_run(state) || state.data.phase != engine::RunPhase::combat ||
            state.combat().phase != engine::CombatPhase::play)
            throw std::runtime_error("MCTS requires a valid combat decision root");
        if (state.enemies().size() != 1 || state.enemy().enemy_type != engine::EnemyId::shrinker_beetle)
            throw std::runtime_error("MCTS combat profile requires one Shrinker");
        for (const auto& slot : state.cards().slots()) {
            if (!slot.live) continue;
            const auto id = slot.value.definition;
            if (id != engine::CardId::strike && id != engine::CardId::defend && id != engine::CardId::bash)
                throw std::runtime_error("MCTS combat profile requires Strike, Defend and Bash");
            if (slot.value.enchantment.definition != engine::EnchantmentId::none ||
                slot.value.affliction.definition != engine::AfflictionId::none)
                throw std::runtime_error("MCTS combat profile requires unmodified cards");
        }
        // These roots come from checked setup/prefix operations. Restrict the
        // inventory too so additional relic behavior cannot silently enter this profile.
        for (const auto relic : state.relics()) {
            const auto name = engine::SetupRules::name(relic);
            if (name != "RELIC.BURNING_BLOOD" && name != "RELIC.GOLDEN_PEARL")
                throw std::runtime_error("Unsupported MCTS combat relic");
        }
        trusted_ = trusted;
        ready_ = true;
    }

    Json project(const engine::RunState& state) {
        WorkerTiming::Scope timing("projection");
        using namespace engine;
        if (!ready_) throw std::runtime_error("MCTS root has not been prepared");
        inputs_.clear();
        Json observation{{"hp", state.player().hp}, {"max_hp", state.player().max_hp},
            {"prompt", state.player().hp <= 0 ? "dead" : "rewards"},
            {"combat", nullptr}, {"choices", Json::array()}};
        if (state.player().hp > 0 && state.data.phase == RunPhase::combat) {
            const auto hand = state.zone(Zone::hand);
            Json cards = Json::array();
            for (auto handle : hand) {
                const auto& card = *state.cards().get(handle);
                cards.push_back({{"id", Ruleset::pinned().card(card.definition)->model_id},
                    {"cost", effective_card_cost(state, handle)}, {"upgraded", card.upgraded}});
            }
            observation["prompt"] = "combat";
            observation["combat"] = {{"hp", state.player().hp}, {"max_hp", state.player().max_hp},
                {"block", state.player().block}, {"energy", state.combat().energy}, {"turn", state.combat().turn},
                {"enemy_hp", state.enemy().hp}, {"enemy_max_hp", state.enemy().max_hp},
                {"draw", state.zone(Zone::draw).size()}, {"discard", state.zone(Zone::discard).size()},
                {"exhaust", state.zone(Zone::exhaust).size()}, {"hand", std::move(cards)}};
            WorkerTiming::Scope enumeration("enumeration");
            for_each_legal_action(state, [&](const PlayerInput& input) {
                const auto& combat = std::get<CombatInput>(input);
                Json choice{{"enabled", true}};
                if (combat.kind == CombatInputKind::end_turn) choice["kind"] = "end_turn";
                else {
                    choice["kind"] = "play_card";
                    choice["index"] = std::distance(hand.begin(), std::find(hand.begin(), hand.end(), combat.card));
                    choice["target_enemy"] = combat.target.present();
                    if (combat.target.present()) choice["target_index"] = combat.target.slot - 1;
                }
                inputs_.push_back(input);
                observation["choices"].push_back(std::move(choice));
            });
        }
        return Json{{"ok", true}, {"observation", std::move(observation)},
            {"capabilities", std::vector<unsigned>(inputs_.size(), 0)}};
    }

    void apply(engine::RunState& state, std::size_t index, engine::Scratch& scratch) {
        if (!ready_ || index >= inputs_.size()) throw std::runtime_error("Invalid MCTS action index");
        const auto input = inputs_[index];
        inputs_.clear();
        // Invalidate the cache even when execution throws or returns a failure.
        ready_ = false;
        WorkerTiming::Scope timing("execution");
        const auto result = trusted_ ? engine::apply_action_trusted(state, input, scratch)
                                     : engine::apply_action(state, input, scratch);
        if (result.status != engine::StepStatus::awaiting_input && result.status != engine::StepStatus::player_died)
            throw std::runtime_error("MCTS action failed; discard this branch");
        ready_ = true;
    }

private:
    bool ready_ = false, trusted_ = true;
    std::vector<engine::PlayerInput> inputs_;
};
} // namespace sts2::benchmark
