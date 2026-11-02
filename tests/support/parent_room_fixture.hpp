#pragma once
#include "sts2/engine/run.hpp"
#include <stdexcept>

namespace sts2::engine::scenario {
// Controlled enclosing interaction; no native event is added to content.
inline void configure_parent_return(RunState& state, std::int32_t gold) {
    auto& view = state;
    if (view.data.phase != RunPhase::combat || !view.data.combat || view.data.direct_combat || !std::holds_alternative<std::monostate>(view.data.room) || gold < 0)
        throw std::invalid_argument("Parent interaction requires an independent active child combat");
    view.data.room = ParentInteractionState{ParentRoomStage::child_combat, gold};
}
}
