#pragma once
#include "sts2/engine/run.hpp"
namespace sts2::engine {
// Caller has checked creature storage and room context; gameplay checks remain.
StepResult apply_input_checked_state(RunState&, const PlayerInput&, Scratch&);
// Internal combat entry may modify the run on execution failure.
StepResult initialize_combat_in_place(RunState&, const CombatConfig&, Scratch&);
// Legal inputs and valid operation state are established by the caller.
StepResult apply_input_trusted(RunState&, const PlayerInput&, Scratch&);
StepResult apply_combat_trusted(RunState&, const CombatInput&, Scratch&, const Ruleset& = Ruleset::pinned()) noexcept;
StepResult advance_combat_trusted(RunState&, Scratch&, const Ruleset& = Ruleset::pinned()) noexcept;
}
