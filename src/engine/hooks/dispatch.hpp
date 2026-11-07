#pragma once
#include "sts2/state/run.hpp"

namespace sts2::engine::gameplay {
struct PowerModifier { int amount; std::uint32_t artifact; };
enum class HandlerResult : std::uint8_t { complete, yield, execution_budget };
PowerModifier before_power_application(const CreatureState&, const OverflowStorage&, PowerId, int amount) noexcept;
void after_power_application(RunState&, Scratch&, CreatureState&, PowerModifier, bool enemy, bool enemy_side, CreatureHandle target);
RangeProgress capture_exhaust_listeners(RunState&);
RangeProgress capture_shuffle_listeners(RunState&);
RangeProgress capture_draw_listeners(RunState&, bool early);
RangeProgress capture_damage_received_listeners(RunState&);
// The operation owns the range/stage; dispatch never pushes or pops its frame.
HandlerResult resume_listeners(RunState&, Scratch&, RangeProgress&, HandlerProgress&, const Ruleset&,
    std::uint32_t* expected_selection_count = nullptr, const HitResult* hit = nullptr);
void after_creature_death(RunState&, Scratch&, CreatureHandle);
void after_enemy_turn(RunState&, Scratch&);
void retain_removed_power(RunState&, CreatureHandle, const PowerInstance&) noexcept;
}
