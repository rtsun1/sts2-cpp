#pragma once
#include "sts2/state/combat.hpp"

namespace sts2::engine::gameplay {
RangeProgress capture_exhaust_cards(CombatView, std::span<const CardHandle>);
StepResult resume_exhaust_cards(CombatView, Scratch&, const Ruleset&);
}
