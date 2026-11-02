#pragma once
#include "sts2/state/run.hpp"

namespace sts2::engine::gameplay {
bool gain_block(RunState&, Scratch&, int amount, CardHandle source, std::uint32_t play);
}
