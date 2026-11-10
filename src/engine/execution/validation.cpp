#include "validation.hpp"
#include "selection.hpp"
#include <algorithm>

namespace sts2::engine::gameplay {
namespace {
bool valid_frame_group(const RunState& state, const Ruleset& rules, std::span<const ExecutionFrame> frames,
    std::span<const HookListener> listeners, std::span<const CardHandle> cards, bool prompt) noexcept {
    std::size_t captured_end = 0, cards_end = 0;
    bool enemy_turn = false, enemy_attack = false;
    const auto range_valid = [](const RangeProgress& range, std::size_t size, std::size_t& end) {
        if (range.cursor > range.count || range.begin > size) return false;
        if (range.count == 0) return range.cursor == 0;
        if (range.begin != end || range.count > size - end) return false;
        end += range.count;
        return true;
    };
    for (const auto& frame : frames) {
        if (const auto* card = std::get_if<CardProgramFrame>(&frame)) {
            const auto* source = state.cards().get(card->source);
            const auto* definition = source ? rules.card(source->definition) : nullptr;
            if (card->energy < 0 || !definition || !state.creature(card->target) || card->play >= state.combat().counters.started_plays || card->instruction >= definition->program(source->upgraded).size()) return false;
            if (!source->removed && std::find(state.zone(Zone::play).begin(), state.zone(Zone::play).end(), card->source) == state.zone(Zone::play).end()) return false;
        } else if (const auto* draw = std::get_if<DrawFrame>(&frame)) {
            if (draw->remaining < 0 || draw->phase > DrawPhase::normal_hooks || draw->listeners.count != 0 || draw->handler.stage != 0 ||
                !range_valid(draw->listeners, listeners.size(), captured_end)) return false;
            const bool card_phase = draw->phase == DrawPhase::early_hooks || draw->phase == DrawPhase::normal_hooks;
            if (card_phase ? !state.cards().get(draw->current_card) : draw->current_card.present()) return false;
        } else if (const auto* exhaust = std::get_if<ExhaustCardsFrame>(&frame)) {
            if (exhaust->phase > ExhaustPhase::finish_card || exhaust->source_zone >= Zone::count ||
                (!exhaust->charge_card_step && exhaust->cards.count != 1) ||
                !range_valid(exhaust->cards, cards.size(), cards_end) ||
                !range_valid(exhaust->listeners, listeners.size(), captured_end)) return false;
            if (exhaust->phase != ExhaustPhase::start_card && exhaust->cards.cursor == exhaust->cards.count) return false;
            if (exhaust->phase != ExhaustPhase::hooks && (exhaust->listeners.count != 0 || exhaust->handler.stage != 0)) return false;
            if (exhaust->handler.stage > 1) return false;
            if (exhaust->handler.stage != 0) {
                if (exhaust->listeners.cursor >= exhaust->listeners.count) return false;
                const auto handler = listeners[exhaust->listeners.begin + exhaust->listeners.cursor].handler;
                if (handler != HookHandler::dark_embrace && handler != HookHandler::joss_paper) return false;
            }
            for (std::uint32_t i = 0; i < exhaust->listeners.count; ++i)
                if (const auto handler = listeners[exhaust->listeners.begin + i].handler;
                    handler != HookHandler::dark_embrace && handler != HookHandler::charons_ashes && handler != HookHandler::joss_paper) return false;
        } else if (const auto* shuffle = std::get_if<ShuffleFrame>(&frame)) {
            if (shuffle->phase > ShufflePhase::hooks || shuffle->handler.stage > 1 ||
                !range_valid(shuffle->listeners, listeners.size(), captured_end)) return false;
            if (shuffle->phase == ShufflePhase::reorder && (shuffle->listeners.count != 0 || shuffle->handler.stage != 0)) return false;
            if (shuffle->handler.stage != 0 && shuffle->listeners.cursor >= shuffle->listeners.count) return false;
            if (shuffle->handler.stage == 1 && (&frame != &frames.back() ||
                !prompt)) return false;
            for (std::uint32_t i = 0; i < shuffle->listeners.count; ++i)
                if (listeners[shuffle->listeners.begin + i].handler != HookHandler::stratagem) return false;
        } else if (const auto* death = std::get_if<DeathFrame>(&frame)) {
            const auto* target = state.creature(death->target);
            if (!state.combat().gremlin_horn || !target || !state.is_enemy(death->target) || target->hp != 0 ||
                !range_valid(death->listeners, listeners.size(), captured_end)) return false;
            for (std::uint32_t i = 0; i < death->listeners.count; ++i)
                if (listeners[death->listeners.begin + i].handler != HookHandler::gremlin_horn) return false;
        } else if (const auto* turn = std::get_if<EnemyTurnFrame>(&frame)) {
            if (enemy_turn || &frame != &frames.front() || !state.combat().enemy_side ||
                turn->phase > EnemyTurnPhase::finish || turn->cursor > state.enemy_turn_order().size() ||
                (turn->phase == EnemyTurnPhase::after_move && turn->cursor == state.enemy_turn_order().size())) return false;
            enemy_turn = true;
        } else if (const auto* attack = std::get_if<EnemyAttackFrame>(&frame)) {
            if (enemy_attack || !enemy_turn || !attack->actor.present() || attack->actor == state.data.player ||
                attack->base_damage < 0 || attack->phase > EnemyAttackPhase::finish || attack->handler.stage > 3 ||
                attack->hit.blocked < 0 || attack->hit.unblocked < 0 || attack->hit.overkill < 0 ||
                !range_valid(attack->listeners, listeners.size(), captured_end)) return false;
            const auto* turn = std::get_if<EnemyTurnFrame>(&frames.front());
            if (!turn || turn->phase != EnemyTurnPhase::after_move || state.enemy_turn_order()[turn->cursor] != attack->actor) return false;
            if (attack->phase != EnemyAttackPhase::hooks && (attack->listeners.count != 0 || attack->handler.stage != 0)) return false;
            if (attack->handler.stage != 0 && attack->listeners.cursor == attack->listeners.count) return false;
            if (attack->handler.stage != 0 && (!state.combat().centennial_puzzle_used || attack->hit.unblocked == 0)) return false;
            for (std::uint32_t i = 0; i < attack->listeners.count; ++i)
                if (listeners[attack->listeners.begin + i].handler != HookHandler::centennial_puzzle) return false;
            enemy_attack = true;
        }
    }

    for (std::size_t i = 0; i < state.enemy_turn_order().size(); ++i) {
        const auto actor = state.enemy_turn_order()[i];
        if (!actor.present() || actor.slot >= state.creatures().slots().size() || actor.generation > state.creatures().data.highest_generation ||
            actor == state.data.player || std::find(state.enemy_turn_order().begin(), state.enemy_turn_order().begin() + i, actor) != state.enemy_turn_order().begin() + i) return false;
    }
    if (captured_end != listeners.size() || cards_end != cards.size()) return false;
    for (auto card : cards) if (!state.cards().get(card)) return false;
    for (const auto& listener : listeners) {
        if (!listener.owner.present() || listener.handler > HookHandler::gremlin_horn) return false;
        const bool power = listener.handler == HookHandler::dark_embrace || listener.handler == HookHandler::stratagem;
        if (power ? listener.power_instance >= state.combat().next_power_instance : listener.power_instance != UINT32_MAX) return false;
        if (listener.removed_power_amount < -999999999 || listener.removed_power_amount > 999999999) return false;
        if (const auto* owner = state.creature(listener.owner)) {
            if (const auto* instance = owner->find_power_instance(listener.power_instance, state.overflow)) {
                const auto definition = listener.handler == HookHandler::stratagem ? PowerId::stratagem : PowerId::dark_embrace;
                if (!power || instance->definition != definition) return false;
            }
        }
    }
    return true;
}
bool valid_detachment_boundaries(const RunState& state, std::span<const DetachmentBoundary> boundaries,
                      std::uint32_t base, std::size_t end) noexcept {
    std::uint32_t previous = base;
    for (auto boundary : boundaries) {
        if (boundary.depth < previous || boundary.depth >= end || boundary.join >= state.combat().next_frame_join) return false;
        previous = boundary.depth;
    }
    return true;
}
}
bool valid_continuations(const RunState& state, const Ruleset& rules) noexcept {
    if (state.combat().selected_cards != RangeProgress{} || state.combat().selection_requested ||
        state.combat().next_frame_join == 0) return false;
    const auto entries = state.pending_choices();
    const auto order = state.pending_choice_order();
    if (state.combat().active_choice > entries.size()) return false;
    // An ordinary root action may publish its own choice while detached hook
    // actions are pending. FIFO constrains activation of queued groups, not
    // the executing caller's ordinary choice.
    if (state.combat().active_choice) for (auto slot : order) {
        if (slot == state.combat().active_choice) break;
        if (slot != 0 && slot <= entries.size() && entries[slot - 1].phase == PendingChoicePhase::pending) return false;
    }
    for (std::size_t i = 0; i < order.size(); ++i) {
        if (order[i] == 0 || order[i] > entries.size() || entries[order[i] - 1].phase == PendingChoicePhase::free) return false;
        for (std::size_t j = 0; j < i; ++j) if (order[j] == order[i]) return false;
    }
    std::size_t turns = 0;
    const auto count_turns = [&](std::span<const ExecutionFrame> frames) {
        for (const auto& frame : frames) if (std::holds_alternative<EnemyTurnFrame>(frame)) ++turns;
    };
    count_turns(state.execution());
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto& entry = entries[i];
        if (entry.phase > PendingChoicePhase::active || entry.previous > entries.size() ||
            entry.join >= state.combat().next_frame_join || entry.waiting_join >= state.combat().next_frame_join) return false;
        const bool ordered = std::find(order.begin(), order.end(), i + 1) != order.end();
        if (ordered != (entry.phase != PendingChoicePhase::free)) return false;
        if (entry.phase == PendingChoicePhase::free) {
            if (!entry.frames.empty() || !entry.listeners.empty() || !entry.cards.empty() ||
                !entry.detachment_boundaries.empty() || entry.selection != CardSelection{} || entry.depth || entry.previous || entry.join || entry.waiting_join) return false;
        } else if (entry.phase == PendingChoicePhase::pending) {
            const auto frames = entry.frames.values(state.combat_overflow);
            const auto listeners = entry.listeners.values(state.combat_overflow);
            if (frames.empty() || entry.depth || entry.previous || entry.waiting_join ||
                !valid_frame_group(state, rules, frames, listeners, entry.cards.values(state.combat_overflow), true) ||
                !valid_frame_selection(state, rules, frames, listeners, entry.selection, {}, true) ||
                !valid_detachment_boundaries(state, entry.detachment_boundaries.values(state.combat_overflow), 0, frames.size())) return false;
            count_turns(frames);
        } else if (!entry.frames.empty() || entry.selection != CardSelection{}) return false;
    }
    if (turns != (state.combat().enemy_side ? 1U : 0U) || (!turns && !state.enemy_turn_order().empty())) return false;
    // Walk the active chain backwards. Each entry holds its parent's parked
    // arena; only the newest group's arena is in CombatData's hot fields.
    auto frames = std::span<const ExecutionFrame>(state.execution());
    auto listeners = std::span<const HookListener>(state.captured_hook_listeners());
    auto cards = std::span<const CardHandle>(state.continuation_cards());
    auto boundaries = std::span<const DetachmentBoundary>(state.detachment_boundaries());
    auto slot = state.combat().active_choice;
    std::size_t end = frames.size(), active_count = 0;
    bool top = true;
    while (true) {
        if (slot && (++active_count > entries.size() || entries[slot - 1].phase != PendingChoicePhase::active)) return false;
        const auto base = slot ? entries[slot - 1].depth : 0;
        if (base > end || !valid_frame_group(state, rules, frames.subspan(base, end - base), listeners, cards, top && state.combat().phase == CombatPhase::card_choice) ||
            !valid_detachment_boundaries(state, boundaries, base, end)) return false;
        if (top && state.combat().phase == CombatPhase::card_choice &&
            !valid_frame_selection(state, rules, frames.subspan(base, end - base), listeners, state.combat().selection, state.selection_candidates())) return false;
        if (!slot) break;
        const auto& entry = entries[slot - 1];
        end = base; listeners = entry.listeners.values(state.combat_overflow); cards = entry.cards.values(state.combat_overflow);
        boundaries = entry.detachment_boundaries.values(state.combat_overflow); slot = entry.previous; top = false;
    }
    for (const auto& entry : entries) if (entry.phase == PendingChoicePhase::active) {
        if (active_count == 0) return false;
        --active_count;
    }
    if (active_count || state.combat().waiting_join >= state.combat().next_frame_join) return false;
    if (state.combat().phase != CombatPhase::card_choice &&
        (!frames.empty() || !order.empty() || !state.zone(Zone::play).empty() ||
         state.combat().selection != CardSelection{} || !state.selection_candidates().empty() || state.combat().waiting_join)) return false;
    return true;
}
}
