#pragma once
#include "sts2/state/combat.hpp"

namespace sts2::engine::gameplay {
StepResult resume_shuffle(CombatView, Scratch&, const Ruleset&);
}
