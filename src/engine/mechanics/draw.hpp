#pragma once
#include "sts2/state/combat.hpp"

namespace sts2::engine::gameplay {
StepResult resume_draw(CombatView, Scratch&, const Ruleset&);
}
