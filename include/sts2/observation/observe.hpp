#pragma once
#include "sts2/engine/combat.hpp"
#include <optional>
#include <string>

namespace sts2::engine {
enum class SelectionEffect : std::uint8_t { none, exhaust, upgrade, topdeck, add_to_hand, discard };
struct CardSelectionView {
    PromptId id;
    SelectionEffect effect;
    std::span<const CardHandle> candidates;
    std::uint32_t minimum, maximum;
};
struct EnemyObservation {
    CreatureHandle handle;
    std::int32_t hp, max_hp, block, vulnerable, weak, attack;
    BeetleMove intent;
};
class EnemyObservationRange {
    const RunState* state_ = nullptr;
public:
    EnemyObservationRange() = default;
    explicit EnemyObservationRange(const RunState& state) noexcept : state_(&state) {}
    std::size_t size() const noexcept;
    std::optional<EnemyObservation> at(std::size_t index) const noexcept;
};
struct CombatObservationView {
    std::string_view build;
    BoundaryId boundary;
    CombatPhase phase;
    std::int32_t hp, max_hp, block, energy, turn, enemy_hp, enemy_max_hp;
    std::int32_t enemy_vulnerable, enemy_attack;
    bool shrunk;
    BeetleMove intent;
    std::span<const CardHandle> hand;
    std::size_t draw_count, discard_count, exhaust_count;
    std::optional<CardSelectionView> selection;
    std::int32_t player_strength, player_dexterity, player_weak, enemy_weak;
    EnemyObservationRange enemies;
};
// Borrows hand storage; invalidated by state mutation. Hidden draw order, RNG,
// execution frames and unrevealed data are never part of the public projection.
[[nodiscard]] CombatObservationView observe_combat(const RunState&) noexcept;
struct CardView {
    CardHandle handle;
    std::string_view model_id, name;
    std::int32_t cost, damage, block, vulnerable;
    bool upgraded, enemy_target;
    CardRarity rarity = CardRarity::basic;
    std::int32_t strength = 0, weak = 0;
    bool exhaust = false, power = false;
    CardKeywordMask keyword_mask = 0;
    CardEnchantment enchantment {};
    std::optional<EnchantmentRuntimeData> enchantment_runtime {};
    CardAffliction affliction {};
    std::int32_t base_replay_count = 0;
};
[[nodiscard]] std::optional<CardView> observe_card(const RunState&, CardHandle, const Ruleset& = Ruleset::pinned(), CreatureHandle target = {}) noexcept;
// Metadata for inspectable hand/draw/discard/exhaust cards; never pile position.
[[nodiscard]] std::optional<CardView> inspect_card(const RunState&, CardHandle, const Ruleset& = Ruleset::pinned(), CreatureHandle target = {}) noexcept;
// English rendering is outside the kernel; callers pay for it only on demand.
[[nodiscard]] std::string describe(const CardView&);
} // namespace sts2::engine
