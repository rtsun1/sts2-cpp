#include "sts2/engine/action.hpp"
#include "sts2/observation/run.hpp"
#include <charconv>
#include <algorithm>
#include <iostream>
#include <string>
#include <sstream>

int main(int argc, char** argv) {
    using namespace sts2::engine;
    SeededConfig config; config.seed = argc > 1 ? argv[1] : "KERNELRUN14";
    RunState run; Scratch scratch;
    if (initialize_run(run, config).status != StepStatus::awaiting_input) return 1;
    for (;;) {
        const auto view = observe(run);
        std::cout << describe(view) << "\nHP " << view.hp << '/' << view.max_hp << "  Gold " << view.gold << '\n';
        for (const auto& card : view.hand_cards)
            std::cout << "  " << card.name << " (" << card.cost << " Energy): " << describe(card) << '\n';
        for (const auto& option : view.options) {
            const auto text = describe(option);
            std::cout << "  Gift " << option.id << ": " << text.name << " — " << text.description << '\n';
        }
        for (const auto& reward : view.rewards) {
            std::cout << "  Reward " << reward.id << ": ";
            if (reward.kind == RewardKind::gold) std::cout << reward.gold << " gold";
            else if (reward.kind == RewardKind::potion) std::cout << reward.model_id;
            else std::cout << "card choice";
            std::cout << (reward.claimed ? " (claimed)\n" : "\n");
        }
        for (const auto& card : view.reward_cards) {
            const auto text = describe(card);
            std::cout << "  Card " << card.instance << ": " << text.name << " (" << card.energy_cost.value_or(-1) << " Energy) — " << text.description << '\n';
        }
        if (const auto selection = selection_space(run)) {
            std::cout << "Choose " << selection->minimum << " to " << selection->maximum << " cards in order (space-separated indices; blank for none):\n";
            for (std::size_t i = 0; i < selection->candidates.size(); ++i)
                std::cout << i << ": " << Ruleset::pinned().card(run.cards().get(selection->candidates[i])->definition)->model_id << '\n';
            std::string line;
            if (!std::getline(std::cin, line) || line == "quit") return 0;
            std::istringstream stream(line);
            std::vector<CardHandle> selected;
            std::size_t index;
            bool valid = true;
            while (stream >> index) {
                if (index >= selection->candidates.size()) { valid = false; break; }
                selected.push_back(selection->candidates[index]);
            }
            if (!valid || !stream.eof()) continue;
            const auto result = apply_action(run, SelectionInput{selection->expected_boundary, selection->prompt, selected}, scratch);
            if (result.reason != Restriction::none) std::cout << describe(result.reason) << '\n';
            continue;
        }
        const RunActionSpaceView offered(run);
        if (offered.empty()) return view.valid ? 0 : 1;
        for (std::size_t i = 0; i < offered.size(); ++i) {
            const auto action = *offered.at(i);
            std::cout << i << ": ";
            if (const auto* input = std::get_if<CombatInput>(&action.input)) {
                if (input->kind == CombatInputKind::end_turn) std::cout << "End turn";
                else {
                    const CardView* card = nullptr;
                    for (const auto* pile : {&view.hand_cards, &view.draw, &view.discard, &view.exhaust}) {
                        const auto found = std::find_if(pile->begin(), pile->end(), [&](const auto& c) { return c.handle == input->card; });
                        if (found != pile->end()) { card = &*found; break; }
                    }
                    const auto verb = "Play ";
                    std::cout << verb << (card ? card->name : "card");
                }
            } else {
                const auto& input_run = std::get<RunInput>(action.input);
                switch (input_run.kind) {
                case RunInputKind::choose_option: std::cout << (view.prompt == PublicPrompt::neow ? "Choose gift " : "Choose option ") << input_run.option; break;
                case RunInputKind::travel: std::cout << "Travel to " << input_run.col << ',' << input_run.row; break;
                case RunInputKind::take_reward: std::cout << "Claim reward " << input_run.option; break;
                case RunInputKind::choose_reward_card: std::cout << "Choose card " << input_run.option; break;
                case RunInputKind::reward_alternative: std::cout << "Skip card"; break;
                case RunInputKind::skip_rewards: std::cout << "Leave rewards"; break;
                case RunInputKind::discard_potion: std::cout << "Discard potion in slot " << input_run.option; break;
                default: break;
                }
            }
            if (action.restriction != Restriction::none) std::cout << " — " << describe(action.restriction);
            std::cout << '\n';
        }
        std::cout << "> " << std::flush;
        std::string line; if (!std::getline(std::cin, line) || line == "quit") return 0;
        std::size_t selected = 0;
        const auto parsed = std::from_chars(line.data(), line.data() + line.size(), selected);
        if (parsed.ec != std::errc{} || parsed.ptr != line.data() + line.size() || selected >= offered.size()) continue;
        const auto result = apply_action(run, offered.at(selected)->input, scratch);
        if (result.reason != Restriction::none) std::cout << describe(result.reason) << '\n';
    }
}
