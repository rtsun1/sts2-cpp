#include "sts2/observation/observe.hpp"
#include "../engine/content/monsters.hpp"
#include "../engine/content/powers.hpp"
#include "../engine/mechanics/cards.hpp"
#include <algorithm>

namespace sts2::engine {
namespace {
SelectionEffect selection_effect(const RunState& state, const Ruleset& rules) noexcept {
    if (state.execution().empty()) return SelectionEffect::none;
    if (std::holds_alternative<ShuffleFrame>(state.execution().back())) return SelectionEffect::add_to_hand;
    const auto* program = std::get_if<CardProgramFrame>(&state.execution().back());
    const auto* source = program ? state.cards().get(program->source) : nullptr;
    const auto* definition = source ? rules.card(source->definition) : nullptr;
    if (!definition || program->instruction >= definition->program(source->upgraded).size()) return SelectionEffect::none;
    switch (definition->program(source->upgraded)[program->instruction].opcode) {
    case Opcode::exhaust_choice: return SelectionEffect::exhaust;
    case Opcode::upgrade_choice: return SelectionEffect::upgrade;
    case Opcode::topdeck_choice: return SelectionEffect::topdeck;
    case Opcode::discard_choice: return SelectionEffect::discard;
    default: return SelectionEffect::none;
    }
}
}
std::size_t EnemyObservationRange::size() const noexcept { return state_ ? state_->enemies().size() : 0; }
std::optional<EnemyObservation> EnemyObservationRange::at(std::size_t index) const noexcept {
    if (!state_ || index >= size()) return std::nullopt;
    const auto handle = state_->enemies()[index];
    const auto& enemy = *state_->creature(handle);
    const auto intent = gameplay::visible_enemy_intent(*state_, handle);
    return EnemyObservation{handle, enemy.hp, enemy.max_hp, enemy.block,
        enemy.power_amount(PowerId::vulnerable, state_->overflow), enemy.power_amount(PowerId::weak, state_->overflow), intent.attack, intent.move};
}
CombatObservationView observe_combat(const RunState& s) noexcept {
    if (!s.valid_combat_storage()) { CombatObservationView invalid{}; invalid.phase = CombatPhase::invalid; return invalid; }
    const auto intent = gameplay::visible_enemy_intent(s, s.enemy_handle());
    CombatObservationView view{game_build, s.data.boundary, s.combat().phase, s.player().hp, s.player().max_hp, s.player().block, s.combat().energy,
        s.combat().turn, s.enemy().hp, s.enemy().max_hp, s.enemy().power_amount(PowerId::vulnerable, s.overflow), intent.attack, s.player().has_power(PowerId::shrink, s.overflow), intent.move,
        s.zone(Zone::hand), s.zone(Zone::draw).size(), s.zone(Zone::discard).size(), s.zone(Zone::exhaust).size(), std::nullopt, s.player().power_amount(PowerId::strength, s.overflow), s.player().power_amount(PowerId::dexterity, s.overflow), s.player().power_amount(PowerId::weak, s.overflow), s.enemy().power_amount(PowerId::weak, s.overflow), EnemyObservationRange(s)};
    if (s.combat().phase == CombatPhase::card_choice) view.selection = CardSelectionView{s.combat().selection.id, selection_effect(s, Ruleset::pinned()), s.selection_candidates(), s.combat().selection.minimum, s.combat().selection.maximum};
    return view;
}
std::optional<CardView> observe_card(const RunState& s, CardHandle h, const Ruleset& rules, CreatureHandle target) noexcept {
    if (!s.valid_combat_storage()) return std::nullopt;
    const auto& hand = s.zone(Zone::hand);
    if (std::find(hand.begin(), hand.end(), h) == hand.end()) return std::nullopt;
    return inspect_card(s, h, rules, target);
}
std::optional<CardView> inspect_card(const RunState& s, CardHandle h, const Ruleset& rules, CreatureHandle target) noexcept {
    if (!s.valid_combat_storage()) return std::nullopt;
    bool visible = false;
    for (const auto zone : {Zone::hand, Zone::draw, Zone::discard, Zone::exhaust}) {
        const auto& cards = s.zone(zone);
        visible = visible || std::find(cards.begin(), cards.end(), h) != cards.end();
    }
    if (!visible) return std::nullopt;
    const auto* instance = s.cards().get(h); if (!instance) return std::nullopt;
    const auto* def = rules.card(instance->definition); if (!def) return std::nullopt;
    if (target.present() && (!def->enemy_target || !s.is_enemy(target) || s.creature(target)->hp <= 0)) return std::nullopt;
    CardView view{h, def->model_id, def->name, effective_card_cost(s, h, rules), 0, 0, 0, instance->upgraded, def->enemy_target, def->rarity};
    // Native CardModel.UpdateDynamicVarPreview runs global combat hooks only
    // for Hand/Play or explicit combat upgrade-preview copies. inspect_card
    // exposes live pile cards, never upgrade-preview copies or the playing pile.
    const auto& hand = s.zone(Zone::hand);
    const bool active_preview = std::find(hand.begin(), hand.end(), h) != hand.end();
    for (const auto& op : def->program(instance->upgraded)) {
        const auto value = op.base;
        if (op.opcode == Opcode::damage) view.damage = active_preview && target.present() ? gameplay::modify_attack_damage(*s.creature(target), value, s.player(), s.overflow) : gameplay::preview_attack_damage(s, value, active_preview);
        else if (op.opcode == Opcode::block) view.block = gameplay::preview_block(s, value, active_preview);
        else if (op.opcode == Opcode::vulnerable) view.vulnerable = value;
        else if (op.opcode == Opcode::strength) view.strength = value;
        else if (op.opcode == Opcode::weak) view.weak = value;
    }
    view.keyword_mask = instance->keyword_mask;
    if (instance->single_turn_retain) view.keyword_mask |= static_cast<CardKeywordMask>(CardKeyword::retain);
    if (instance->single_turn_sly) view.keyword_mask |= static_cast<CardKeywordMask>(CardKeyword::sly);
    view.exhaust = instance->exhaust_on_next_play || has_keyword(view.keyword_mask, CardKeyword::exhaust);
    view.enchantment = instance->enchantment;
    if (const auto* runtime = gameplay::find_enchantment_runtime(s, h)) view.enchantment_runtime = *runtime;
    view.affliction = instance->affliction; view.base_replay_count = instance->base_replay_count;
    view.power = instance->definition == CardId::inflame;
    return view;
}
std::string describe(const CardView& card) {
    std::string text;
    if (card.damage) text += "Deal " + std::to_string(card.damage) + " damage.";
    if (card.block) text += "Gain " + std::to_string(card.block) + " Block.";
    if (card.vulnerable) text += " Apply " + std::to_string(card.vulnerable) + " Vulnerable.";
    if (card.strength) text += " Gain " + std::to_string(card.strength) + " Strength.";
    if (card.weak) text += " Apply " + std::to_string(card.weak) + " Weak.";
    if (has_keyword(card.keyword_mask, CardKeyword::retain)) text += " Retain.";
    if (card.exhaust) text += " Exhaust.";
    if (card.model_id == "CARD.SURVIVOR") text += " Discard 1 card.";
    if (card.model_id == "CARD.PURITY") text += std::string(" Exhaust up to ") + (card.upgraded ? "5" : "3") + " cards in your hand.";
    if (card.model_id == "CARD.ARMAMENTS") text += card.upgraded ? " Upgrade all cards in your hand." : " Upgrade a card in your hand.";
    if (card.model_id == "CARD.HEADBUTT") text += " Put a card from your discard pile on top of your draw pile.";
    if (card.model_id == "CARD.BURNING_PACT") text += std::string("Exhaust 1 card. Draw ") + (card.upgraded ? "3" : "2") + " cards.";
    if (card.model_id == "CARD.TRUE_GRIT") text += card.upgraded ? " Exhaust a card from your hand." : " Exhaust a random card from your hand.";
    return text;
}
} // namespace sts2::engine
