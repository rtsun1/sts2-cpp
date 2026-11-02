#pragma once
#include "sts2/core/result.hpp"
#include "sts2/core/storage.hpp"
#include "sts2/content/cards.hpp"
#include "sts2/state/clock.hpp"
#include "sts2/state/rng.hpp"
#include "sts2/state/creature.hpp"
#include "sts2/state/execution.hpp"
#include <array>
#include <algorithm>
#include <string_view>

namespace sts2::engine {
enum class CombatPhase : std::uint8_t { uninitialized, play, discard_choice, combat_won, player_died, card_choice, invalid };
enum class BeetleMove : std::uint8_t { shrink, chomp, stomp };
// Sufficient summaries for the audited single-player consumers. Card types are
// immutable in supported content; mutable-type mechanics need a fresh audit.
struct CombatCounters {
    std::uint32_t started_plays = 0, finished_plays = 0;
    std::uint32_t started_this_turn = 0, attacks_this_turn = 0;
    std::uint32_t zero_energy_attacks_this_turn = 0, non_hand_draws_this_turn = 0;
    std::uint32_t finished_ethereal = 0;
    friend bool operator==(const CombatCounters&, const CombatCounters&) = default;
};
struct EnemyAiState {
    // Shrinker's deterministic cycle needs only its selected move, not a
    // duplicate intent or invented history. Other AI can add required records.
    BeetleMove selected = BeetleMove::shrink;
};
struct CombatData {
    std::uint32_t rules = rules_revision;
    CombatPhase phase = CombatPhase::uninitialized;
    std::int32_t energy = 0;
    std::int32_t turn = 0;
    EnemyAiState enemy_ai {};
    bool burning_blood = true;
    CardPool cards {};
    InlineSequence<CombatCardExtra, 20> card_extra {};
    InlineSequence<EnchantmentRuntime, 2> enchantment_runtime {};
    InlineSequence<CostModifier, 8> cost_modifiers {};
    std::array<InlineSequence<CardHandle, 32>, static_cast<std::size_t>(Zone::count)> zones {};
    InlineSequence<ExecutionFrame, 4> execution {};
    CombatCounters counters {};
    std::array<ExhaustRelic, 2> exhaust_relics {};
    std::uint8_t exhaust_relic_count = 0;
    InlineSequence<HookListener, 8> captured_hook_listeners {};
    InlineSequence<CardHandle, 16> continuation_cards {};
    CardSelection selection {};
    InlineSequence<CardHandle, 10> selection_candidates {}, selection_order {};
    PromptId next_prompt {1};
    std::uint32_t next_power_instance = 0;
    std::uint32_t next_creation_order = 0;
    template<class Self, class Visit> static void visit_sequences(Self& r, Visit visit) noexcept {
        visit(r.cards.slots); visit(r.cards.free); visit(r.card_extra); visit(r.enchantment_runtime); visit(r.cost_modifiers);
        for (auto& zone : r.zones) visit(zone);
        visit(r.execution); visit(r.selection_candidates); visit(r.selection_order);
        visit(r.captured_hook_listeners); visit(r.continuation_cards);
    }
    template<class Visit> void for_each_sequence(Visit visit) const noexcept { visit_sequences(*this, visit); }
    template<class Visit> void for_each_sequence(Visit visit) noexcept { visit_sequences(*this, visit); }
};
static_assert(std::is_trivially_copyable_v<CombatData>);
struct RunState;
// Optional synchronous diagnostic output. No pointer is stored in cloned state.
// The receiver may allocate; exceptions stop execution through the normal error path.
struct CombatTrace {
    void* context = nullptr;
    void (*receive)(void*, const RunState&, const CombatEvent&) = nullptr;
    void emit(const RunState& state, const CombatEvent& event) const {
        if (receive) receive(context, state, event);
    }
};
// Scratch is discardable and contains no continuation or authoritative values.
// trace is optional instrumentation supplied by the caller for this execution.
struct Scratch {
    std::uint32_t step_limit = 10000;
    std::uint32_t steps = 0;
    CombatTrace trace {};
};
struct Limits { std::uint32_t cards = 4096; std::uint32_t continuations = 4096; };
struct CardSpec { CardId definition; bool upgraded = false; PersistentCardHandle origin {}; CardEnchantment enchantment {}; };
// Run-owned combat setup. Seeded integration supplies the generated RNG bank;
// a new scenario initializes the run inventory and map before combat.
struct CombatConfig {
    std::span<const ExhaustRelic> exhaust_relics;
    std::uint32_t joss_paper_counter = 0;
    std::string_view build = game_build;
    std::string_view seed = "ORACLECHECK01";
    ClockConfig clock;
    const RngBank* initial_rng = nullptr;
    std::span<const CardSpec> deck; // Empty selects the Ironclad starting deck.
    std::int32_t player_hp = 80;
    std::int32_t player_max_hp = 80;
    std::int32_t enemy_hp = 0; // Zero rolls the native A0 38..40 range.
    bool burning_blood = true;
    std::uint32_t card_generation = 1;
    std::uint8_t ascension = 0;
    std::string_view encounter = "ENCOUNTER.SHRINKER_BEETLE_WEAK";
};
} // namespace sts2::engine
