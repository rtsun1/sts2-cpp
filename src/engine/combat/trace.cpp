#include "trace.hpp"

namespace sts2::engine::gameplay {
void emit(RunState& s, Scratch& scratch, CombatEventKind kind, bool enemy_side, bool actor_enemy,
             CardHandle card, std::uint32_t object, int amount, HitResult hit, bool has_applier, bool applier_enemy, CreatureHandle actor, CreatureHandle receiver) {
    if (!scratch.trace.receive) return;
    CombatEvent event{kind, s.combat().turn, enemy_side, actor_enemy, card, object, amount, hit};
    event.actor = actor.present() ? actor : (actor_enemy ? s.enemy_handle() : s.data.player);
    event.receiver = receiver;
    event.has_applier = has_applier;
    event.applier_enemy = applier_enemy;
    scratch.trace.emit(s, event);
}
}
