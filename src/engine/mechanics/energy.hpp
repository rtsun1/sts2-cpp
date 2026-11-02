#pragma once
#include "sts2/state/run.hpp"

namespace sts2::engine::gameplay {
void spend_energy(RunState&, int amount) noexcept;
void refresh_energy(RunState&, int amount) noexcept;
}
