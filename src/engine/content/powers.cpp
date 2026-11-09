#include "powers.hpp"
#include "../mechanics/powers.hpp"
#include "../mechanics/cards.hpp"
#include "../execution/selection.hpp"
#include "../hooks/dispatch.hpp"
#include <algorithm>

namespace sts2::engine::gameplay {
HandlerResult dark_embrace_after_card_exhausted(RunState& state, HandlerProgress& progress, int amount) {
    if (progress.stage != 0) return HandlerResult::complete;
    if (amount <= 0 || !state.has_living_enemies() || state.player().hp == 0) return HandlerResult::complete;
    progress.stage = 1;
    state.execution().push_back(DrawFrame{.remaining = amount, .from_hand_draw = false});
    return HandlerResult::yield;
}
HandlerResult stratagem_after_shuffle(RunState& state, HandlerProgress& progress, int amount) {
    switch (progress.stage) {
    case 0: {
        if (amount <= 0) return HandlerResult::complete;
        const auto count = static_cast<std::uint32_t>(amount);
        progress.stage = 1;
        request_selection(state, Zone::draw, count, count);
        return HandlerResult::yield;
    }
    case 1: {
        const auto selected = take_selection(state);
        for (std::uint32_t i = 0; i < selected.count; ++i) {
            const auto card = state.continuation_cards()[selected.begin + i];
            add_to_hand(state, Zone::draw, card);
        }
        release_selection_cards(state, selected);
        return HandlerResult::complete;
    }
    default: throw std::logic_error("Invalid stratagem handler stage");
    }
}
PowerModifier artifact_before_power_application(const CreatureState& creature, const OverflowStorage& owner, PowerId id, int amount) noexcept {
    const bool debuff = (id == PowerId::strength || id == PowerId::dexterity) ? amount < 0 :
        id == PowerId::shrink || ((id == PowerId::weak || id == PowerId::vulnerable) && amount >= 0);
    const auto artifact = creature.has_power(PowerId::artifact, owner) && debuff ? creature.find_power(PowerId::artifact, owner)->instance : UINT32_MAX;
    return {artifact == UINT32_MAX ? amount : 0, artifact};
}
void artifact_after_power_application(RunState& state, Scratch& scratch, CreatureState& creature, PowerModifier modifier, bool enemy, bool enemy_side, CreatureHandle target) {
    if (modifier.artifact != UINT32_MAX && creature.has_power(PowerId::artifact, state.overflow) && creature.find_power(PowerId::artifact, state.overflow)->instance == modifier.artifact)
        (void)apply_power(state, scratch, PowerId::artifact, -1, enemy, enemy_side, {}, false, false, PowerInstance::Stacking::by_type, {}, target, modifier.artifact);
}
int modify_attack_damage(const CreatureState& target, int base, const CreatureState& source, const OverflowStorage& owner) noexcept {
    // Native decimal modifiers combine before one floor operation.
    const std::int64_t numerator = std::max<std::int64_t>(0, std::int64_t(base) + source.power_amount(PowerId::strength, owner)) *
        (source.has_power(PowerId::shrink, owner) ? 70 : 100) * (source.power_amount(PowerId::weak, owner) > 0 ? 75 : 100) *
        (target.power_amount(PowerId::vulnerable, owner) > 0 ? 150 : 100);
    return static_cast<int>(numerator / 1000000);
}
std::int64_t modify_block(const CreatureState& creature, const OverflowStorage& owner, int amount) noexcept {
    return std::max<std::int64_t>(0, std::int64_t(amount) + creature.power_amount(PowerId::dexterity, owner));
}
int preview_attack_damage(const RunState& s, int value, bool active_preview) noexcept {
    return static_cast<int>(std::max<std::int64_t>(0, std::int64_t(value) + (active_preview ? s.player().power_amount(PowerId::strength, s.overflow) : 0)) * (active_preview && s.player().has_power(PowerId::shrink, s.overflow) ? 70 : 100) * (active_preview && s.player().power_amount(PowerId::weak, s.overflow) > 0 ? 75 : 100) * (active_preview && s.enemies().size() == 1 && s.enemy().power_amount(PowerId::vulnerable, s.overflow) ? 150 : 100) / 1000000);
}
int preview_block(const RunState& s, int value, bool active_preview) noexcept {
    return static_cast<int>(std::max<std::int64_t>(0, std::int64_t(value) + (active_preview ? s.player().power_amount(PowerId::dexterity, s.overflow) : 0)));
}
bool skip_initial_power_tick(PowerId id, bool enemy_owner) noexcept {
    return !enemy_owner && (id == PowerId::weak || id == PowerId::vulnerable || id == PowerId::shrink);
}
bool power_is_depleted(PowerId id, int amount) noexcept {
    return (id == PowerId::strength || id == PowerId::dexterity || id == PowerId::shrink) ? amount == 0 : amount <= 0;
}
bool valid_power_amount(PowerId id, int amount) noexcept {
    return !(((id == PowerId::vulnerable || id == PowerId::weak || id == PowerId::artifact || id == PowerId::dark_embrace || id == PowerId::stratagem) && amount <= 0) ||
        (id == PowerId::shrink && amount >= 0) ||
        ((id == PowerId::strength || id == PowerId::dexterity) && amount == 0));
}
bool power_ticks_after_enemy_turn(PowerId id) noexcept {
    return id == PowerId::weak || id == PowerId::vulnerable;
}
}
