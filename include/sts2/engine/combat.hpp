#pragma once
#include "sts2/state/run.hpp"

namespace sts2::engine {
[[nodiscard]] std::int32_t effective_card_cost(const RunState&, CardHandle, const Ruleset& = Ruleset::pinned()) noexcept;
enum class CombatInputKind : std::uint8_t { play_card, end_turn };
struct CombatInput {
    BoundaryId expected_boundary {};
    CombatInputKind kind = CombatInputKind::end_turn;
    CardHandle card{};
    CreatureHandle target {};
    CombatInput() = default;
    constexpr CombatInput(BoundaryId boundary, CombatInputKind input_kind, CardHandle source = {},
                          CreatureHandle receiver = {}) noexcept
        : expected_boundary(boundary), kind(input_kind), card(source), target(receiver) {}
};
// Borrowed only for the duration of submission. Accepted operands are owned by state.
struct SelectionInput {
    BoundaryId expected_boundary {};
    PromptId prompt {};
    std::span<const CardHandle> cards;
};
[[nodiscard]] StepResult apply_selection(RunState&, const SelectionInput&, Scratch&,
    const Ruleset& = Ruleset::pinned()) noexcept;
[[nodiscard]] Restriction validate_selection(const RunState&, const SelectionInput&,
    const Ruleset& = Ruleset::pinned()) noexcept;
[[nodiscard]] StepResult initialize_combat(RunState&, const CombatConfig&, Scratch&,
    const Ruleset& = Ruleset::pinned(), Limits = {});
[[nodiscard]] StepResult advance_combat(RunState&, Scratch&,
    const Ruleset& = Ruleset::pinned()) noexcept;
[[nodiscard]] StepResult apply_combat(RunState&, const CombatInput&, Scratch&,
    const Ruleset& = Ruleset::pinned()) noexcept;
[[nodiscard]] Restriction validate_combat(const RunState&, const CombatInput&,
    const Ruleset& = Ruleset::pinned()) noexcept;
// Diagnostic operation corresponding to native PowerCmd; no player clock step.
struct PowerApplication {
    PowerId definition;
    std::int32_t amount;
    bool enemy_owner = false, has_applier = true, applier_enemy = false;
    CardHandle source {};
    CreatureHandle owner {}, applier {};
};
[[nodiscard]] StepResult apply_power_scenario(RunState&, const PowerApplication&, Scratch&) noexcept;
// Validate the active combat and its run-owned creature/card storage.
[[nodiscard]] bool valid_combat(const RunState&, const Ruleset& = Ruleset::pinned()) noexcept;
} // namespace sts2::engine
