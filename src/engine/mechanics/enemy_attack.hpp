#pragma once
#include "sts2/state/run.hpp"

namespace sts2::engine::gameplay {
StepResult resume_enemy_attack(RunState&, Scratch&, const Ruleset&);
}
