#pragma once
#include "sts2/engine/run.hpp"
#include <stdexcept>
#include <vector>

namespace sts2::engine::scenario {
// Authored real-content setup: Burning Pact exhausts into Dark Embrace (or
// Joss Paper), whose draw reshuffles and invokes Stratagem's pile selection.
inline void prepare_exhaust_shuffle(RunState& state, bool large_deck = false,
    std::span<const ContentId> relics = {}, std::uint32_t joss_counter = 0) {
    std::vector<CardSpec> deck(large_deck ? 200 : 10, CardSpec{CardId::strike});
    deck[0] = {CardId::burning_pact}; deck[1] = {CardId::defend}; deck[4] = {CardId::defend};
    if (initialize_combat_scenario(state, {}, deck, {}, relics, 0, joss_counter).status != StepStatus::awaiting_input)
        throw std::runtime_error("Exhaust/shuffle scenario initialization failed");
    auto& combat = state;
    // Establish a deterministic hand and a nonempty discard pile, independent
    // of the setup shuffle. All cards are real supported combat instances.
    for (auto zone : combat.zone_views()) zone.clear();
    const auto slots = combat.cards().slots();
    for (std::uint32_t i = 0; i < slots.size(); ++i) {
        if (slots[i].live) combat.zone(i < 5 ? Zone::hand : Zone::discard).push_back({i, slots[i].generation});
    }
    Scratch scratch;
    if (relics.empty() && apply_power_scenario(combat, {PowerId::dark_embrace, 1}, scratch).status != StepStatus::awaiting_input)
        throw std::runtime_error("Dark Embrace setup failed");
    if (apply_power_scenario(combat, {PowerId::stratagem, 1}, scratch).status != StepStatus::awaiting_input)
        throw std::runtime_error("Stratagem setup failed");
}
}
