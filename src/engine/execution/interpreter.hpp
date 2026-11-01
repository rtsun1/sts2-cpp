#pragma once
#include "sts2/state/combat.hpp"

namespace sts2::engine::gameplay {
// Schedule operation-specific saved work.
void defer_draw(CombatView, int count, bool from_hand_draw);
void defer_joss_paper_draw_completion(CombatView);
bool valid_continuations(ConstCombatView, const Ruleset&) noexcept;
// Resume the top frame until completion, child work, input, or failure.
// Each handler charges the execution budget for every work unit it processes.
StepResult resume_frame(CombatView, Scratch&, const Ruleset&);
StepResult resume_card_program(CombatView, Scratch&, const Ruleset&);
}
