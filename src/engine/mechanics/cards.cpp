#include "cards.hpp"
#include "../combat/trace.hpp"
#include "../hooks/dispatch.hpp"
#include "../execution/interpreter.hpp"
#include "sts2/core/sort.hpp"
#include "sts2/engine/run.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace sts2::engine::gameplay {
namespace {
template<class View, class Handle> auto find_extra(View entries, Handle card) noexcept {
    using Pointer = decltype(&entries.front().data);
    for (auto& entry : entries) if (entry.card == card) return &entry.data;
    return Pointer{};
}
template<class View, class Handle> void set_extra(View entries, Handle card, CardExtraData data) {
    if (auto* existing = find_extra(entries, card)) *existing = data;
    else entries.push_back({card, data});
}
template<class View, class Pool> bool valid_extra(View entries, Pool pool) noexcept {
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (!pool.get(entries[i].card) || entries[i].data.valueless_by_exception()) return false;
        for (std::size_t j = 0; j < i; ++j) if (entries[i].card == entries[j].card) return false;
    }
    return true;
}
}

CardExtraData* find_card_extra(CombatView state, CardHandle card) noexcept { return state.cards.get(card) ? find_extra(state.card_extra, card) : nullptr; }
const CardExtraData* find_card_extra(ConstCombatView state, CardHandle card) noexcept { return state.cards.get(card) ? find_extra(state.card_extra, card) : nullptr; }
CardExtraData* find_card_extra(RunView state, PersistentCardHandle card) noexcept { return state.deck.get(card) ? find_extra(state.card_extra, card) : nullptr; }
const CardExtraData* find_card_extra(ConstRunView state, PersistentCardHandle card) noexcept { return state.deck.get(card) ? find_extra(state.card_extra, card) : nullptr; }
void set_card_extra(CombatView state, CardHandle card, CardExtraData data) {
    if (!state.cards.get(card)) throw std::invalid_argument("Stale combat card");
    set_extra(state.card_extra, card, data);
}
void set_card_extra(RunView state, PersistentCardHandle card, CardExtraData data) {
    if (!state.deck.get(card)) throw std::invalid_argument("Stale persistent card");
    set_extra(state.card_extra, card, data);
}
bool valid_card_extra(ConstCombatView state) noexcept { return valid_extra(state.card_extra, state.cards); }
bool valid_card_extra(ConstRunView state) noexcept { return valid_extra(state.card_extra, state.deck); }
bool valid_enchantment(CardEnchantment value) noexcept {
    return value.definition < EnchantmentId::count &&
        (value.definition != EnchantmentId::none || value.amount == 0);
}
namespace {
bool valid_runtime(EnchantmentId enchantment, const EnchantmentRuntimeData& data) noexcept {
    switch (enchantment) {
    case EnchantmentId::swift: case EnchantmentId::sown: case EnchantmentId::vigorous: case EnchantmentId::glam:
        return std::holds_alternative<EnchantmentUseState>(data);
    case EnchantmentId::momentum: return std::holds_alternative<MomentumState>(data);
    default: return false;
    }
}
}
const EnchantmentRuntimeData* find_enchantment_runtime(ConstCombatView state, CardHandle card) noexcept {
    return state.cards.get(card) ? find_extra(state.enchantment_runtime, card) : nullptr;
}
void set_enchantment_runtime(CombatView state, CardHandle card, EnchantmentRuntimeData data) {
    const auto* instance = state.cards.get(card);
    if (!instance || !valid_runtime(instance->enchantment.definition, data))
        throw std::invalid_argument("Invalid enchantment runtime attachment");
    if (auto* existing = find_extra(state.enchantment_runtime, card)) *existing = data;
    else state.enchantment_runtime.push_back({card, data});
}
bool valid_card_modifiers(ConstCombatView state) noexcept {
    for (const auto& slot : state.cards.slots()) {
        const auto& card = slot.value;
        if (!valid_enchantment(card.enchantment) || card.affliction.definition >= AfflictionId::count ||
            (card.affliction.definition == AfflictionId::none && card.affliction.amount != 0) ||
            (card.keyword_mask & ~127U) != 0 || card.base_replay_count < 0) return false;
    }
    for (std::size_t i = 0; i < state.enchantment_runtime.size(); ++i) {
        const auto& entry = state.enchantment_runtime[i];
        const auto* card = state.cards.get(entry.card);
        if (!card || !valid_runtime(card->enchantment.definition, entry.data)) return false;
        for (std::size_t j = 0; j < i; ++j)
            if (state.enchantment_runtime[j].card == entry.card) return false;
    }
    return true;
}
bool supported_card_modifiers(const CombatCard& card) noexcept {
    // Representation support does not imply implemented hook/replay mechanics.
    return (card.enchantment.definition == EnchantmentId::none || card.enchantment.definition == EnchantmentId::souls_power) &&
        card.affliction.definition == AfflictionId::none && card.base_replay_count == 0 &&
        (card.keyword_mask & ~3U) == 0 && !card.single_turn_sly;
}
void move_card(CombatView state, Zone from, Zone to, CardHandle card) {
    auto source = state.zone(from);
    source.erase(std::find(source.begin(), source.end(), card));
    state.zone(to).push_back(card);
}
void add_to_hand(CombatView state, Zone from, CardHandle card) {
    // Native CardPileCmd.Add redirects hand overflow into discard; this is not a draw.
    move_card(state, from, state.zone(Zone::hand).size() < 10 ? Zone::hand : Zone::discard, card);
}
void discard_card(CombatView state, Scratch& scratch, CardHandle card) {
    move_card(state, Zone::hand, Zone::discard, card);
    emit(state, scratch, CombatEventKind::discarded, false, false, card);
}
void discard_hand_at_turn_end(CombatView state, const Ruleset&) {
    auto hand = state.zone(Zone::hand); auto discard = state.zone(Zone::discard);
    for (auto card : hand) if (!state.cards.get(card)->single_turn_retain && !has_keyword(state.cards.get(card)->keyword_mask, CardKeyword::retain)) discard.push_back(card);
    hand.erase(std::remove_if(hand.begin(), hand.end(), [&](auto card) { return !state.cards.get(card)->single_turn_retain && !has_keyword(state.cards.get(card)->keyword_mask, CardKeyword::retain); }), hand.end());
    for (auto& slot : state.cards.slots()) {
        slot.value.single_turn_retain = false;
        slot.value.single_turn_sly = false;
    }
}
void exhaust(CombatView state, Scratch& scratch, Zone from, CardHandle card) {
    if (state.enemy.hp == 0 || state.player.hp == 0) return;
    move_card(state, from, Zone::exhaust, card);
    emit(state, scratch, CombatEventKind::exhausted, false, false, card);
    after_card_exhausted(state, scratch, card);
}

void reshuffle(CombatView state, const Ruleset& rules) {
    auto pile = state.zone(Zone::draw); auto discard = state.zone(Zone::discard);
    pile.insert(pile.end(), discard.begin(), discard.end()); discard.clear();
    game_sort<CardHandle>(pile, [&](CardHandle a, CardHandle b) {
        const auto& ca = *state.cards.get(a); const auto& cb = *state.cards.get(b);
        const auto ida = rules.card(ca.definition)->model_id, idb = rules.card(cb.definition)->model_id;
        return ida == idb ? ca.upgraded < cb.upgraded : ida < idb;
    });
    state.rng.get(RunStream::shuffle).shuffle<CardHandle>(pile);
}
void draw_one(CombatView state, Scratch& scratch, bool from_hand_draw) {
    auto pile = state.zone(Zone::draw); auto hand = state.zone(Zone::hand);
    auto card = pile.front(); pile.erase(pile.begin()); hand.push_back(card);
    if (!from_hand_draw) {
        if (state.counters.non_hand_draws_this_turn == UINT32_MAX) throw std::length_error("draw count exhausted");
        ++state.counters.non_hand_draws_this_turn;
    }
    if (scratch.trace.receive) {
        CombatEvent event{CombatEventKind::drawn, state.turn, false, false, card};
        event.from_hand_draw = from_hand_draw; scratch.trace.emit(state, event);
    }
}
void draw(CombatView state, Scratch&, int count, const Ruleset&, bool from_hand_draw) {
    if (state.enemy.hp == 0 || state.player.hp == 0 || count <= 0) return;
    defer_draw(state, count, from_hand_draw);
}
}

namespace sts2::engine {
std::int32_t effective_card_cost(ConstCombatView state, CardHandle handle, const Ruleset& rules) noexcept {
    const auto* card = state.cards.get(handle);
    if (!card) return 0;
    const auto* definition = rules.card(card->definition);
    if (!definition) return 0;
    std::int64_t cost = definition->cost;
    for (const auto& modifier : state.cost_modifiers) {
        if (modifier.card != handle) continue;
        const auto previous = cost;
        cost = modifier.kind == CostModifierKind::absolute ? modifier.amount : cost + modifier.amount;
        cost = std::clamp<std::int64_t>(cost, std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max());
        if (modifier.reduce_only) cost = std::min(cost, previous);
    }
    return static_cast<std::int32_t>(std::max<std::int64_t>(0, cost));
}
Restriction detail::upgrade_card_permanently(RunState& state, CardHandle handle) noexcept {
    if (!valid_run(state)) return Restriction::invalid_state;
    auto run = state.view();
    if (run.phase != RunPhase::combat || !run.combat) return Restriction::wrong_phase;
    auto* instance = run.combat->cards.get(handle);
    if (!instance || !instance->origin.present()) return Restriction::stale_card;
    auto* permanent = run.deck.get(instance->origin);
    if (!permanent) return Restriction::stale_card;
    permanent->upgrade = 1;
    instance->upgraded = true;
    return Restriction::none;
}
}
