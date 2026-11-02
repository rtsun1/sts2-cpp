#include "energy.hpp"

namespace sts2::engine::gameplay {
void spend_energy(RunState& state, int amount) noexcept { state.combat().energy -= amount; }
void refresh_energy(RunState& state, int amount) noexcept { state.combat().energy = amount; }
}
