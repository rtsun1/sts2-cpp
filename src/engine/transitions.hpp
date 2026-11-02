#pragma once
#include "sts2/engine/run.hpp"
namespace sts2::engine {
// Caller has checked creature storage and room context; gameplay checks remain.
StepResult apply_input_checked_state(RunState&, const PlayerInput&, Scratch&);
// Internal combat entry may modify the run on execution failure.
StepResult initialize_combat_in_place(RunState&, const CombatConfig&, Scratch&);
StepResult initialize_combat_in_place_trusted(RunState&, const CombatConfig&, Scratch&);
// Forced input is already legal; retain support checks for a checked caller.
StepResult apply_automatic_run_input(RunState&, const RunInput&, Scratch&);
// Performance variants require supported behavior; defensive builds check support.
StepResult generate_combat_rewards_trusted(RunState&);
StepResult apply_input_trusted(RunState&, const PlayerInput&, Scratch&);
StepResult apply_combat_trusted(RunState&, const CombatInput&, Scratch&, const Ruleset& = Ruleset::pinned()) noexcept;
StepResult advance_combat_trusted(RunState&, Scratch&, const Ruleset& = Ruleset::pinned()) noexcept;
}
