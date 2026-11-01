#pragma once
#include "sts2/state/cards.hpp"
#include <array>
#include <type_traits>
#include <variant>

namespace sts2::engine {
// Every suspended operand is owned here, never on the C++ call stack or in Scratch.
enum class SelectionEffect : std::uint8_t { none, exhaust, upgrade, topdeck, add_to_hand };
enum class ExhaustRelic : std::uint8_t { charons_ashes, joss_paper };
struct RangeProgress {
    std::uint32_t begin = 0, count = 0, cursor = 0;
    friend bool operator==(const RangeProgress&, const RangeProgress&) = default;
};
struct HandlerProgress {
    // Zero starts a handler. Other stages are interpreted by its content code.
    std::uint8_t stage = 0;
    friend bool operator==(const HandlerProgress&, const HandlerProgress&) = default;
};
enum class DrawPhase : std::uint8_t { draw, after_shuffle, early_hooks, normal_hooks };
enum class ExhaustPhase : std::uint8_t { start_card, hooks, finish_card };
enum class ShufflePhase : std::uint8_t { reorder, hooks };
struct DrawFrame {
    RangeProgress listeners {};
    CardHandle current_card {};
    std::int32_t remaining = 0;
    HandlerProgress handler {};
    DrawPhase phase = DrawPhase::draw;
    bool from_hand_draw = true;
    friend bool operator==(const DrawFrame&, const DrawFrame&) = default;
};
struct ExhaustCardsFrame {
    RangeProgress cards {}, listeners {};
    HandlerProgress handler {};
    ExhaustPhase phase = ExhaustPhase::start_card;
    Zone source_zone = Zone::hand;
    // Direct card instructions already pay for their single exhaust mutation.
    bool charge_card_step = true;
    friend bool operator==(const ExhaustCardsFrame&, const ExhaustCardsFrame&) = default;
};
struct ShuffleFrame {
    RangeProgress listeners {};
    std::uint32_t expected_selection_count = 0;
    HandlerProgress handler {};
    ShufflePhase phase = ShufflePhase::reorder;
    // Preserve the former interrupted-draw charge when shuffle listeners exist.
    bool charge_draw_step_if_hooks = false;
    friend bool operator==(const ShuffleFrame&, const ShuffleFrame&) = default;
};
struct CardProgramFrame {
    CardHandle source;
    std::uint32_t instruction = 0;
    std::uint32_t play = 0;
    std::int32_t energy = 0;
    // Captured enemy operand, also for untargeted programs such as Shockwave.
    CreatureHandle target {};
    friend bool operator==(const CardProgramFrame&, const CardProgramFrame&) = default;
};
using ExecutionFrame = std::variant<CardProgramFrame, DrawFrame, ExhaustCardsFrame, ShuffleFrame>;
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
