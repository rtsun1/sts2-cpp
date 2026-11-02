#pragma once
#include "sts2/state/run.hpp"

namespace sts2::engine::gameplay {
StepResult result(const RunState&, Restriction = Restriction::none) noexcept;
StepResult fail(RunState&, Restriction) noexcept;
bool consume(Scratch&) noexcept;
void end_combat(RunState&, Scratch&, bool won);
}
