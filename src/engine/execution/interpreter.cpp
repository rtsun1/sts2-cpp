#include "interpreter.hpp"
#include "card_program.hpp"
#include "../mechanics/draw.hpp"
#include "../mechanics/death.hpp"
#include "../mechanics/exhaust.hpp"
#include "../mechanics/shuffle.hpp"
#include "../mechanics/enemy_attack.hpp"
#include "../combat/enemy_turn.hpp"

namespace sts2::engine::gameplay {
StepResult resume_frame(RunState& state, Scratch& scratch, const Ruleset& rules) {
    return std::visit([&](const auto& frame) -> StepResult {
        using Frame = std::decay_t<decltype(frame)>;
        if constexpr (std::is_same_v<Frame, CardProgramFrame>) return resume_card_program(state, scratch, rules);
        else if constexpr (std::is_same_v<Frame, DrawFrame>) return resume_draw(state, scratch, rules);
        else if constexpr (std::is_same_v<Frame, ExhaustCardsFrame>) return resume_exhaust_cards(state, scratch, rules);
        else if constexpr (std::is_same_v<Frame, ShuffleFrame>) return resume_shuffle(state, scratch, rules);
        else if constexpr (std::is_same_v<Frame, DeathFrame>) return resume_death(state, scratch);
        else if constexpr (std::is_same_v<Frame, EnemyTurnFrame>) return resume_enemy_turn(state, scratch, rules);
        else {
            static_assert(std::is_same_v<Frame, EnemyAttackFrame>);
            return resume_enemy_attack(state, scratch, rules);
        }
    }, state.execution().back());
}
}
