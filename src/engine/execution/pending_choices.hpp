#pragma once
#include "sts2/state/run.hpp"

namespace sts2::engine::gameplay {
// Open before pushing a complete child operation, after recording caller progress.
// Only audited pause-or-completion call sites may use this boundary.
void begin_detachable_frames(RunState&, std::uint32_t join = 0);
std::uint32_t create_frame_join(RunState&);
void wait_for_frame_join(RunState&, std::uint32_t join) noexcept;
bool has_pending_frames(const RunState&, std::uint32_t join) noexcept;
// Resolve requests, completed groups, joins and FIFO presentation. Returns true
// when the current group can execute; false at a published input boundary.
bool advance_pending_choices(RunState&);
void clear_finished_detachment_boundaries(RunState&) noexcept;
}
