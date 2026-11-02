#include "validation.hpp"
#include "selection.hpp"
#include <algorithm>

namespace sts2::engine::gameplay {
bool valid_continuations(const RunState& state, const Ruleset& rules) noexcept {
    std::size_t captured_end = 0, cards_end = 0;
    const auto range_valid = [](const RangeProgress& range, std::size_t size, std::size_t& end) {
        if (range.cursor > range.count || range.begin > size) return false;
        if (range.count == 0) return range.cursor == 0;
        if (range.begin != end || range.count > size - end) return false;
        end += range.count;
        return true;
    };
    for (const auto& frame : state.execution()) {
        if (const auto* card = std::get_if<CardProgramFrame>(&frame)) {
            const auto* source = state.cards().get(card->source);
            const auto* definition = source ? rules.card(source->definition) : nullptr;
            if (card->energy < 0 || !definition || !state.creature(card->target) || card->play >= state.combat().counters.started_plays || card->instruction >= definition->program(source->upgraded).size()) return false;
            if (!source->removed && std::find(state.zone(Zone::play).begin(), state.zone(Zone::play).end(), card->source) == state.zone(Zone::play).end()) return false;
        } else if (const auto* draw = std::get_if<DrawFrame>(&frame)) {
            if (draw->remaining < 0 || draw->phase > DrawPhase::normal_hooks || draw->listeners.count != 0 || draw->handler.stage != 0 ||
                !range_valid(draw->listeners, state.captured_hook_listeners().size(), captured_end)) return false;
            const bool card_phase = draw->phase == DrawPhase::early_hooks || draw->phase == DrawPhase::normal_hooks;
            if (card_phase ? !state.cards().get(draw->current_card) : draw->current_card.present()) return false;
        } else if (const auto* exhaust = std::get_if<ExhaustCardsFrame>(&frame)) {
            if (exhaust->phase > ExhaustPhase::finish_card || exhaust->source_zone >= Zone::count ||
                (!exhaust->charge_card_step && exhaust->cards.count != 1) ||
                !range_valid(exhaust->cards, state.continuation_cards().size(), cards_end) ||
                !range_valid(exhaust->listeners, state.captured_hook_listeners().size(), captured_end)) return false;
            if (exhaust->phase != ExhaustPhase::start_card && exhaust->cards.cursor == exhaust->cards.count) return false;
            if (exhaust->phase != ExhaustPhase::hooks && (exhaust->listeners.count != 0 || exhaust->handler.stage != 0)) return false;
            if (exhaust->handler.stage > 1) return false;
            if (exhaust->handler.stage != 0) {
                if (exhaust->listeners.cursor >= exhaust->listeners.count) return false;
                const auto handler = state.captured_hook_listeners()[exhaust->listeners.begin + exhaust->listeners.cursor].handler;
                if (handler != HookHandler::dark_embrace && handler != HookHandler::joss_paper) return false;
            }
            for (std::uint32_t i = 0; i < exhaust->listeners.count; ++i)
                if (state.captured_hook_listeners()[exhaust->listeners.begin + i].handler == HookHandler::stratagem) return false;
        } else if (const auto* shuffle = std::get_if<ShuffleFrame>(&frame)) {
            if (shuffle->phase > ShufflePhase::hooks || shuffle->handler.stage > 1 ||
                !range_valid(shuffle->listeners, state.captured_hook_listeners().size(), captured_end)) return false;
            if (shuffle->phase == ShufflePhase::reorder && (shuffle->listeners.count != 0 || shuffle->handler.stage != 0 || shuffle->expected_selection_count != 0)) return false;
            if (shuffle->handler.stage != 0 && shuffle->listeners.cursor >= shuffle->listeners.count) return false;
            if (shuffle->expected_selection_count != 0 && (shuffle->handler.stage != 1 || &frame != &state.execution().back() || state.combat().phase != CombatPhase::card_choice)) return false;
            for (std::uint32_t i = 0; i < shuffle->listeners.count; ++i)
                if (state.captured_hook_listeners()[shuffle->listeners.begin + i].handler != HookHandler::stratagem) return false;
        }
    }
    if (captured_end != state.captured_hook_listeners().size() || cards_end != state.continuation_cards().size()) return false;
    for (auto card : state.continuation_cards()) if (!state.cards().get(card)) return false;
    for (const auto& listener : state.captured_hook_listeners()) {
        if (!listener.owner.present() || listener.handler > HookHandler::joss_paper) return false;
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
    return valid_selection(state, rules);
}
}
