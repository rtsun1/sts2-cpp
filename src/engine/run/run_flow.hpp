#pragma once
#include "sts2/engine/run.hpp"

namespace sts2::engine::flow {
enum class ValidationMode { visible, execution };
struct RunActionPlan {
    std::uint32_t entering_generation = 0, next_card_instance = 0;
    bool generate_rewards = false, reward_kind = false;
    MapNodeId destination = no_node;
    std::size_t potion_slot = 0;
};
std::uint32_t highest_combat_generation(const RunState&) noexcept;
std::uint32_t next_combat_generation(const RunState&) noexcept;
bool supported_combat_inventory(const RunState&);
// Initialize combat in place without advancing its clock; discard on failure.
StepResult prepare_combat(RunState&, Scratch&, BoundaryId rejection_boundary, std::int32_t enemy_hp = 0);
bool valid_room_context(const RunState&) noexcept;
void apply_reward_input(RunState&, const RunInput&, const RunActionPlan&);
void apply_room_option(RunState&, const RunInput&);
void child_combat_finished(RunState&, bool won) noexcept;
bool return_from_room(RunState&) noexcept;
Restriction legal_run_input(const RunState&, const RunInput&) noexcept;
StepResult prepare_run_input(const RunState&, const RunInput&, RunActionPlan&, ValidationMode);
// Pure planning distinguishes visible legality from hidden entry capability.
StepResult plan_run_input(const RunState&, const RunInput&, RunActionPlan&, ValidationMode);
}
