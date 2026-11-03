#include "enemies.hpp"
#include "powers.hpp"
#include "../mechanics/damage.hpp"
#include "../mechanics/powers.hpp"
#include "../combat/trace.hpp"
#include <stdexcept>

namespace sts2::engine::gameplay {
namespace {
int attack_base(const CreatureState& enemy) noexcept {
    switch (enemy.enemy_type) {
    case EnemyId::shrinker_beetle:
        return enemy.enemy_ai.selected == EnemyMove::second ? 7 : enemy.enemy_ai.selected == EnemyMove::third ? 13 : 0;
    case EnemyId::fuzzy_wurm_crawler:
        return enemy.enemy_ai.selected == EnemyMove::second ? 0 : 4;
    case EnemyId::count: break;
    }
    return 0;
}
}
void execute_enemy_move(RunState& state, Scratch& scratch, CreatureHandle actor) {
    const auto& enemy = *state.creature(actor);
    const auto base = attack_base(enemy);
    if (base != 0) {
        const auto hit = damage(state.player(), base, enemy, state.overflow);
        emit(state, scratch, CombatEventKind::damaged, true, false, {}, 0, 0, hit, true, true, actor, state.data.player);
        emit(state, scratch, CombatEventKind::attacked, true, true, {}, 0, 0, hit, true, true, actor, state.data.player);
    } else if (enemy.enemy_type == EnemyId::fuzzy_wurm_crawler) {
        (void)apply_power(state, scratch, PowerId::strength, 7, true, true, {}, true, true, PowerInstance::Stacking::by_type, actor, actor);
    } else if (enemy.enemy_type == EnemyId::shrinker_beetle) {
        (void)apply_power(state, scratch, PowerId::shrink, -1, false, true, {}, true, true, PowerInstance::Stacking::by_type, actor);
    } else {
        throw std::invalid_argument("Unsupported enemy move");
    }
    if (auto* completed = state.creature(actor)) completed->enemy_ai.performed = true;
}
void advance_enemy_move(CreatureState& enemy) noexcept {
    if (!enemy.enemy_ai.performed) return;
    switch (enemy.enemy_type) {
    case EnemyId::fuzzy_wurm_crawler:
        enemy.enemy_ai.selected = enemy.enemy_ai.selected == EnemyMove::first ? EnemyMove::second : enemy.enemy_ai.selected == EnemyMove::second ? EnemyMove::third : EnemyMove::first;
        break;
    case EnemyId::shrinker_beetle:
        enemy.enemy_ai.selected = enemy.enemy_ai.selected == EnemyMove::second ? EnemyMove::third : EnemyMove::second;
        break;
    case EnemyId::count: return;
    }
    enemy.enemy_ai.performed = false;
}
EnemyIntent visible_enemy_intent(const RunState& state, CreatureHandle actor) noexcept {
    const auto& enemy = *state.creature(actor);
    const auto base = attack_base(enemy);
    return {enemy.enemy_ai.selected, base == 0 ? 0 : modify_attack_damage(state.player(), base, enemy, state.overflow)};
}
int roll_enemy_hp(const RunState& state, EnemyId type, GameRng& rng) {
    int minimum = 0, maximum = 0;
    switch (type) {
    case EnemyId::shrinker_beetle: minimum = 38; maximum = 40; break;
    case EnemyId::fuzzy_wurm_crawler: minimum = 55; maximum = 57; break;
    case EnemyId::count: throw std::invalid_argument("Unsupported enemy HP range");
    }
    // Native HashSet insertion order is ascending for these fresh integer ranges.
    std::array<int, 3> candidates {}; int count = 0;
    for (int hp = minimum; hp <= maximum; ++hp) {
        bool used = false;
        for (auto handle : state.enemies()) used |= state.creature(handle)->max_hp == hp;
        if (!used) candidates[count++] = hp;
    }
    int index = 0;
    if (count != 0) { (void)rng.next_int(0, count, index); return candidates[index]; }
    (void)rng.next_int(minimum, maximum + 1, index); return index;
}
}
