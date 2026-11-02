#pragma once
#include "sts2/state/run.hpp"

namespace sts2::engine::gameplay {
StepResult resume_card_program(RunState&, Scratch&, const Ruleset&);
}
