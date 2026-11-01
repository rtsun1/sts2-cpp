#pragma once
#include "sts2/state/combat.hpp"

namespace sts2::engine::gameplay {
StepResult resume_card_program(CombatView, Scratch&, const Ruleset&);
}
