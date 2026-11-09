#pragma once
#include "sts2/state/cards.hpp"
#include "sts2/state/combat_types.hpp"
#include <array>
#include <type_traits>
#include <variant>

namespace sts2::engine {
// Every suspended operand is owned here, never on the C++ call stack or in Scratch.
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
enum class EnemyTurnPhase : std::uint8_t { actors, after_move, finish };
struct EnemyTurnFrame {
    std::uint32_t cursor = 0;
    EnemyTurnPhase phase = EnemyTurnPhase::actors;
    friend bool operator==(const EnemyTurnFrame&, const EnemyTurnFrame&) = default;
};
enum class EnemyAttackPhase : std::uint8_t { damage, hooks, finish };
// The supported enemy attacks have one hit against the player. This frame owns
// that damage result until received-damage listeners and their children finish.
struct EnemyAttackFrame {
    RangeProgress listeners {};
    CreatureHandle actor {};
    HitResult hit {};
    std::int32_t base_damage = 0;
    HandlerProgress handler {};
    EnemyAttackPhase phase = EnemyAttackPhase::damage;
    friend bool operator==(const EnemyAttackFrame&, const EnemyAttackFrame&) = default;
};
using ExecutionFrame = std::variant<CardProgramFrame, DrawFrame, ExhaustCardsFrame, ShuffleFrame, EnemyTurnFrame, EnemyAttackFrame>;
static_assert(std::is_trivially_copyable_v<ExecutionFrame>);
enum class SelectionEligibility : std::uint8_t { all, upgradable };
struct CardSelection {
    PromptId id {};
    std::uint32_t minimum = 0, maximum = 0;
    Zone zone = Zone::hand;
    SelectionEligibility eligibility = SelectionEligibility::all;
    friend bool operator==(const CardSelection&, const CardSelection&) = default;
};
// Captured candidates refer to existing content instances, not a registration pool.
enum class HookHandler : std::uint8_t { dark_embrace, stratagem, charons_ashes, joss_paper, centennial_puzzle };
struct HookListener {
    CreatureHandle owner {};
    std::uint32_t power_instance = UINT32_MAX;
    std::int32_t removed_power_amount = 0;
    HookHandler handler {};
    friend bool operator==(const HookListener&, const HookListener&) = default;
};
static_assert(std::is_trivially_copyable_v<HookListener>);
// Boundaries mark child frame groups whose native caller may continue at input.
// The caller records its post-child progress before opening the boundary.
struct DetachmentBoundary {
    std::uint32_t depth = 0;
    std::uint32_t join = 0;
    friend bool operator==(const DetachmentBoundary&, const DetachmentBoundary&) = default;
};
enum class PendingChoicePhase : std::uint8_t { free, pending, active };
struct PendingChoice {
    InlineSequence<ExecutionFrame, 2> frames {};
    InlineSequence<HookListener, 8> listeners {};
    InlineSequence<CardHandle, 16> cards {};
    InlineSequence<DetachmentBoundary, 2> detachment_boundaries {};
    CardSelection selection {};
    PendingChoicePhase phase = PendingChoicePhase::free;
    std::uint32_t depth = 0;
    std::uint32_t previous = 0;
    std::uint32_t join = 0;
    std::uint32_t waiting_join = 0;
    // While active, listeners/cards/detachment_boundaries hold the parked parent's arena.
    bool equals(const OverflowStorage& owner, const PendingChoice& other,
                const OverflowStorage& other_owner) const {
        return phase == other.phase && depth == other.depth && previous == other.previous &&
            join == other.join && waiting_join == other.waiting_join && selection == other.selection &&
            frames.equals(owner, other.frames, other_owner) &&
            listeners.equals(owner, other.listeners, other_owner) && cards.equals(owner, other.cards, other_owner) &&
            detachment_boundaries.equals(owner, other.detachment_boundaries, other_owner);
    }
};
static_assert(std::is_trivially_copyable_v<PendingChoice>);

}
