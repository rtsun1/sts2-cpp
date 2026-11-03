#pragma once
#include "sts2/engine/combat.hpp"

namespace sts2::engine::test {
// Submit the result within the lifetime of card (including its full expression).
inline SelectionInput selection_input(const RunState& state, const CardHandle& card, PromptId prompt = {}) {
    return {state.data.boundary, prompt, std::span<const CardHandle>(&card, 1)};
}
}
