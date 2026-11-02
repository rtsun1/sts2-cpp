#pragma once
#include "sts2/engine/run.hpp"

namespace sts2::engine::gameplay {
// Borrowed pointers are invalidated by growth of their side store.
CardExtraData* find_card_extra(RunState&, CardHandle) noexcept;
const CardExtraData* find_card_extra(const RunState&, CardHandle) noexcept;
CardExtraData* find_card_extra(RunState&, PersistentCardHandle) noexcept;
const CardExtraData* find_card_extra(const RunState&, PersistentCardHandle) noexcept;
// One payload per live generational handle. Insertion can overflow inline storage.
void set_card_extra(RunState&, CardHandle, CardExtraData);
void set_card_extra(RunState&, PersistentCardHandle, CardExtraData);
bool valid_card_extra(const RunState&) noexcept;
bool valid_persistent_card_extra(const RunState&) noexcept;
// Absent runtime entry means unused / zero extra damage. Borrowed pointers expire
// when the side store grows. Entries are keyed by live generational card handles.
const EnchantmentRuntimeData* find_enchantment_runtime(const RunState&, CardHandle) noexcept;
void set_enchantment_runtime(RunState&, CardHandle, EnchantmentRuntimeData);
bool valid_enchantment(CardEnchantment) noexcept;
bool valid_card_modifiers(const RunState&) noexcept;
bool supported_card_modifiers(const CombatCard&) noexcept;
void add_to_hand(RunState&, Zone from, CardHandle);
void move_card(RunState&, Zone from, Zone to, CardHandle);
void discard_card(RunState&, Scratch&, CardHandle);
void discard_hand_at_turn_end(RunState&, const Ruleset&);
bool exhaust_one(RunState&, Scratch&, Zone, CardHandle);
void reshuffle(RunState&, const Ruleset&);
void draw_one(RunState&, Scratch&, bool from_hand_draw);
}

namespace sts2::engine {
struct RunState;
namespace detail {
// Upgrade a combat card and its persistent origin.
[[nodiscard]] Restriction upgrade_card_permanently(RunState&, CardHandle) noexcept;
}
}
