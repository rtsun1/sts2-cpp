#include "pending_choices.hpp"
#include "selection.hpp"
#include "../defensive.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace sts2::engine::gameplay {
namespace {
std::uint32_t group_base(const RunState& state) noexcept {
    return state.combat().active_choice ? state.pending_choices()[state.combat().active_choice - 1].depth : 0;
}
void rebase_frame(ExecutionFrame& frame, std::uint32_t cards, std::uint32_t listeners) {
    std::visit([&](auto& value) {
        if constexpr (requires { value.listeners; }) {
            if (value.listeners.count) value.listeners.begin -= listeners;
            else value.listeners.begin = 0;
        }
        if constexpr (requires { value.cards; }) {
            if (value.cards.count) value.cards.begin -= cards;
            else value.cards.begin = 0;
        }
    }, frame);
}
std::uint32_t acquire_choice(RunState& state) {
    for (std::uint32_t i = 0; i < state.pending_choices().size(); ++i)
        if (state.pending_choices()[i].phase == PendingChoicePhase::free) return i + 1;
    if (state.pending_choices().size() == UINT32_MAX) throw std::length_error("Pending choice capacity exhausted");
    state.pending_choices().push_back({});
    return static_cast<std::uint32_t>(state.pending_choices().size());
}
void park_choice(RunState& state, std::uint32_t depth, std::uint32_t join) {
    STS2_CHECK_INVARIANT(state.combat().selected_cards == RangeProgress{});
    const auto slot = acquire_choice(state);
    auto& entry = state.pending_choices()[slot - 1];
    auto& owner = state.combat_overflow;
    auto frames = SequenceView(entry.frames, owner);
    frames.assign(state.execution().begin() + depth, state.execution().end());
    auto card_begin = static_cast<std::uint32_t>(state.continuation_cards().size());
    auto listener_begin = static_cast<std::uint32_t>(state.captured_hook_listeners().size());
    for (const auto& frame : frames) std::visit([&](const auto& value) {
        if constexpr (requires { value.cards; })
            if (value.cards.count) card_begin = std::min(card_begin, value.cards.begin);
        if constexpr (requires { value.listeners; })
            if (value.listeners.count) listener_begin = std::min(listener_begin, value.listeners.begin);
    }, frame);
    SequenceView(entry.cards, owner).assign(state.continuation_cards().begin() + card_begin, state.continuation_cards().end());
    SequenceView(entry.listeners, owner).assign(state.captured_hook_listeners().begin() + listener_begin, state.captured_hook_listeners().end());
    for (auto& frame : frames) rebase_frame(frame, card_begin, listener_begin);
    auto boundaries = SequenceView(entry.detachment_boundaries, owner);
    boundaries.clear();
    // The boundary that detached this child has served its purpose. Preserve
    // only nested boundaries; saved depths are relative to the parked suffix.
    for (auto boundary : state.detachment_boundaries()) if (boundary.depth > depth) {
        boundary.depth -= depth; boundaries.push_back(boundary);
    }
    while (!state.detachment_boundaries().empty() && state.detachment_boundaries().back().depth >= depth) state.detachment_boundaries().pop_back();
    while (state.execution().size() > depth) state.execution().pop_back();
    while (state.continuation_cards().size() > card_begin) state.continuation_cards().pop_back();
    while (state.captured_hook_listeners().size() > listener_begin) state.captured_hook_listeners().pop_back();
    entry.selection = state.combat().selection;
    entry.phase = PendingChoicePhase::pending;
    entry.join = join; entry.depth = entry.previous = entry.waiting_join = 0;
    state.pending_choice_order().push_back(slot);
    clear_selection(state);
}
void swap_arena(RunState& state, PendingChoice& entry) noexcept {
    std::swap(state.combat().continuation_cards, entry.cards);
    std::swap(state.combat().captured_hook_listeners, entry.listeners);
    std::swap(state.combat().detachment_boundaries, entry.detachment_boundaries);
    std::swap(state.combat().waiting_join, entry.waiting_join);
}
bool activate_choice(RunState& state, std::uint32_t slot) {
    auto& entry = state.pending_choices()[slot - 1];
    entry.depth = static_cast<std::uint32_t>(state.execution().size());
    entry.previous = state.combat().active_choice;
    for (auto frame : entry.frames.values(state.combat_overflow)) state.execution().push_back(frame);
    entry.frames.clear();
    for (auto& boundary : entry.detachment_boundaries.values(state.combat_overflow)) boundary.depth += entry.depth;
    swap_arena(state, entry);
    state.combat().active_choice = slot;
    entry.phase = PendingChoicePhase::active;
    state.combat().selection = entry.selection; entry.selection = {};
    return present_selection(state, Ruleset::pinned());
}
void finish_choice(RunState& state) {
    const auto slot = state.combat().active_choice;
    auto& entry = state.pending_choices()[slot - 1];
    STS2_CHECK_INVARIANT(state.continuation_cards().empty() && state.captured_hook_listeners().empty() && state.combat().selected_cards == RangeProgress{});
    swap_arena(state, entry);
    state.combat().active_choice = entry.previous;
    auto order = state.pending_choice_order();
    order.erase(std::find(order.begin(), order.end(), slot));
    entry.phase = PendingChoicePhase::free;
    entry.depth = entry.previous = entry.join = entry.waiting_join = 0;
    entry.cards.clear(); entry.listeners.clear(); entry.detachment_boundaries.clear();
}
std::uint32_t first_pending_choice(const RunState& state) noexcept {
    for (auto slot : state.pending_choice_order())
        if (state.pending_choices()[slot - 1].phase == PendingChoicePhase::pending) return slot;
    return 0;
}
}
void begin_detachable_frames(RunState& state, std::uint32_t join) {
    state.detachment_boundaries().push_back({static_cast<std::uint32_t>(state.execution().size()), join});
}
std::uint32_t create_frame_join(RunState& state) {
    if (state.combat().next_frame_join == UINT32_MAX) throw std::length_error("Frame join identity exhausted");
    return state.combat().next_frame_join++;
}
void wait_for_frame_join(RunState& state, std::uint32_t join) noexcept { state.combat().waiting_join = join; }
bool has_pending_frames(const RunState& state, std::uint32_t join) noexcept {
    for (auto slot : state.pending_choice_order()) if (state.pending_choices()[slot - 1].join == join) return true;
    return false;
}
void clear_finished_detachment_boundaries(RunState& state) noexcept {
    while (!state.detachment_boundaries().empty() && state.execution().size() <= state.detachment_boundaries().back().depth)
        state.detachment_boundaries().pop_back();
}
bool advance_pending_choices(RunState& state) {
    if (state.combat().selection_requested) {
        if (!state.detachment_boundaries().empty()) {
            const auto boundary = state.detachment_boundaries().back();
            park_choice(state, boundary.depth, boundary.join);
        } else if (!state.combat().active_choice && !state.pending_choice_order().empty()) {
            // An ordinary blocking choice follows older detached work. Park its
            // whole group and present the older choice first.
            park_choice(state, group_base(state), 0);
        } else {
            return present_selection(state, Ruleset::pinned());
        }
    }
    while (true) {
        if (state.combat().waiting_join) {
            if (has_pending_frames(state, state.combat().waiting_join)) {
                const auto slot = first_pending_choice(state);
                // A join includes active children. A child cannot join itself.
                if (!slot) throw std::logic_error("Cyclic frame join");
                return activate_choice(state, slot);
            }
            state.combat().waiting_join = 0;
        }
        if (state.execution().size() > group_base(state)) return true;
        if (state.combat().active_choice) { finish_choice(state); continue; }
        if (const auto slot = first_pending_choice(state)) { return activate_choice(state, slot); }
        return false;
    }
}
}
