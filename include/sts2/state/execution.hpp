#pragma once
#include "sts2/state/cards.hpp"
#include <array>
#include <type_traits>
#include <variant>

namespace sts2::engine {
// Every suspended operand is owned here, never on the C++ call stack or in Scratch.
enum class SelectionEffect : std::uint8_t { none, exhaust, upgrade, topdeck, add_to_hand };
enum class ExhaustRelic : std::uint8_t { charons_ashes, joss_paper };
struct DrawFrame {
    std::int32_t remaining = 0;
    bool from_hand_draw = true;
    friend bool operator==(const DrawFrame&, const DrawFrame&) = default;
};
struct ShuffleFrame {
    std::uint32_t begin = 0, count = 0, cursor = 0;
    // Nonzero while a handler is waiting for this many selected cards.
    std::uint32_t selection_count = 0;
    friend bool operator==(const ShuffleFrame&, const ShuffleFrame&) = default;
};
struct SelectionFrame {
    SelectionEffect effect = SelectionEffect::none;
    Zone zone = Zone::hand;
    std::uint32_t begin = 0, count = 0, cursor = 0;
    friend bool operator==(const SelectionFrame&, const SelectionFrame&) = default;
};
struct ExhaustFrame {
    std::uint32_t begin = 0, count = 0, cursor = 0;
    friend bool operator==(const ExhaustFrame&, const ExhaustFrame&) = default;
};
struct RelicDrawCompletionFrame { friend bool operator==(const RelicDrawCompletionFrame&, const RelicDrawCompletionFrame&) = default; };
struct CardProgramFrame {
    CardHandle source;
    std::uint32_t instruction = 0;
    std::uint32_t play = 0;
    std::int32_t energy = 0;
    // Captured enemy operand, also for untargeted programs such as Shockwave.
    CreatureHandle target {};
    friend bool operator==(const CardProgramFrame&, const CardProgramFrame&) = default;
};
using ExecutionFrame = std::variant<CardProgramFrame, DrawFrame, ShuffleFrame,
    SelectionFrame, ExhaustFrame, RelicDrawCompletionFrame>;
static_assert(std::is_trivially_copyable_v<ExecutionFrame>);
struct CardSelection {
    PromptId id {};
    SelectionEffect effect = SelectionEffect::none;
    Zone zone = Zone::hand;
    std::uint32_t minimum = 0, maximum = 0;
    bool automatic_at_maximum = false;
    std::uint32_t continuation_depth = 0;
    friend bool operator==(const CardSelection&, const CardSelection&) = default;
};
// Captured candidates refer to existing content instances, not a registration pool.
enum class HookHandler : std::uint8_t { dark_embrace, stratagem, charons_ashes, joss_paper };
struct HookListener {
    CreatureHandle owner {};
    std::uint32_t power_instance = UINT32_MAX;
    std::int32_t removed_power_amount = 0;
    HookHandler handler {};
    friend bool operator==(const HookListener&, const HookListener&) = default;
};
static_assert(std::is_trivially_copyable_v<HookListener>);
}
