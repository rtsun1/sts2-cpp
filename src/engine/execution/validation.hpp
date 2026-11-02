#pragma once
#include "sts2/state/run.hpp"

namespace sts2::engine::gameplay {
bool valid_continuations(const RunState&, const Ruleset&) noexcept;
}
