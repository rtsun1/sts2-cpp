#pragma once
#include "sts2/state/combat.hpp"

namespace sts2::engine::gameplay {
void joss_paper_after_draw(CombatView);
void charons_ashes_after_card_exhausted(CombatView, Scratch&);
void joss_paper_after_card_exhausted(CombatView, Scratch&, const Ruleset&);
}
