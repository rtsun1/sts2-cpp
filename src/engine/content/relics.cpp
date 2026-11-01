#include "relics.hpp"
#include "../mechanics/damage.hpp"
#include "../mechanics/cards.hpp"
#include "../execution/interpreter.hpp"
#include "../combat/trace.hpp"

namespace sts2::engine::gameplay {
void joss_paper_after_draw(CombatView state) {
    state.joss_paper_counter %= 5;
}
void charons_ashes_after_card_exhausted(CombatView state, Scratch& scratch) {
    if (state.enemy.hp <= 0) return;
    const auto hit = receive_damage(state.enemy, 3);
    emit(state, scratch, CombatEventKind::damaged, false, true, {}, 4, 0, hit);
}
void joss_paper_after_card_exhausted(CombatView state, Scratch& scratch, const Ruleset& rules) {
    ++state.joss_paper_counter;
    if (state.joss_paper_counter < 5) return;
    defer_joss_paper_draw_completion(state);
    draw(state, scratch, static_cast<int>(state.joss_paper_counter / 5), rules, false);
}
}
