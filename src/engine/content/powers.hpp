#pragma once
#include "../hooks/dispatch.hpp"

namespace sts2::engine::gameplay {
enum class HandlerResult : std::uint8_t;
HandlerResult dark_embrace_after_card_exhausted(RunState&, HandlerProgress&, int amount);
HandlerResult stratagem_after_shuffle(RunState& state, HandlerProgress& progress, int amount);
PowerModifier artifact_before_power_application(const CreatureState&, const OverflowStorage&, PowerId, int amount) noexcept;
void artifact_after_power_application(RunState&, Scratch&, CreatureState&, PowerModifier, bool enemy, bool enemy_side, CreatureHandle target);
// Combine the supported damage modifiers before one final rounding operation.
int modify_attack_damage(const CreatureState& target, int base, const CreatureState& source, const OverflowStorage&) noexcept;
std::int64_t modify_block(const CreatureState&, const OverflowStorage&, int amount) noexcept;
int preview_attack_damage(const RunState&, int value, bool active_preview) noexcept;
int preview_block(const RunState&, int value, bool active_preview) noexcept;
bool skip_initial_power_tick(PowerId, bool enemy_owner) noexcept;
bool power_is_depleted(PowerId, int amount) noexcept;
bool valid_power_amount(PowerId, int amount) noexcept;
bool power_ticks_after_enemy_turn(PowerId) noexcept;
}
