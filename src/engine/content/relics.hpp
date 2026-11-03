#pragma once
#include "sts2/state/run.hpp"

namespace sts2::engine::gameplay {
void charons_ashes_after_card_exhausted(RunState&, Scratch&);
enum class HandlerResult : std::uint8_t;
HandlerResult centennial_puzzle_after_damage_received(RunState&, HandlerProgress&, const HitResult&);
HandlerResult joss_paper_after_card_exhausted(RunState&, Scratch&, HandlerProgress&);
}
