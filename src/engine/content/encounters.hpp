#pragma once
#include "sts2/state/combat_types.hpp"
#include "sts2/state/run.hpp"
#include <array>
#include <string_view>

namespace sts2::engine::gameplay {
// Maximum returned initial composition in the pinned 90-encounter inventory.
// This is not a bound on the live creature pool after summons.
struct EncounterComposition {
    std::array<EnemyId, 4> enemies {};
    std::uint8_t count = 0;
};
bool supported_encounter(std::string_view) noexcept;
EncounterComposition generate_encounter(std::string_view, const RunState&) noexcept;
}
