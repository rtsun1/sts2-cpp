#pragma once
#include "sts2/state/run.hpp"

namespace sts2::engine::gameplay {
RangeProgress capture_exhaust_cards(RunState&, std::span<const CardHandle>);
StepResult resume_exhaust_cards(RunState&, Scratch&, const Ruleset&);
}
