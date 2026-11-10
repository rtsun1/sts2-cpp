#include "sts2/content/cards.hpp"

namespace sts2::engine {
namespace {
constexpr auto exhaust = static_cast<CardKeywordMask>(CardKeyword::exhaust);
constexpr auto retain = static_cast<CardKeywordMask>(CardKeyword::retain);
constexpr std::array strike = {Instruction{Opcode::damage, 6}, Instruction{Opcode::finish}};
constexpr std::array strike_upgraded = {Instruction{Opcode::damage, 9}, Instruction{Opcode::finish}};
constexpr std::array defend = {Instruction{Opcode::block, 5}, Instruction{Opcode::finish}};
constexpr std::array defend_upgraded = {Instruction{Opcode::block, 8}, Instruction{Opcode::finish}};
constexpr std::array bash = {Instruction{Opcode::damage, 8}, Instruction{Opcode::vulnerable, 2}, Instruction{Opcode::finish}};
constexpr std::array bash_upgraded = {Instruction{Opcode::damage, 10}, Instruction{Opcode::vulnerable, 3}, Instruction{Opcode::finish}};
constexpr std::array survivor = {Instruction{Opcode::block, 8}, Instruction{Opcode::request_choice, 1}, Instruction{Opcode::discard_choice}, Instruction{Opcode::finish}};
constexpr std::array survivor_upgraded = {Instruction{Opcode::block, 11}, Instruction{Opcode::request_choice, 1}, Instruction{Opcode::discard_choice}, Instruction{Opcode::finish}};
constexpr std::array armaments = {Instruction{Opcode::block, 5}, Instruction{Opcode::request_choice, 1, CardChoice::upgradable_hand}, Instruction{Opcode::upgrade_choice}, Instruction{Opcode::finish}};
constexpr std::array armaments_upgraded = {Instruction{Opcode::block, 5}, Instruction{Opcode::upgrade_all}, Instruction{Opcode::finish}};
constexpr std::array headbutt = {Instruction{Opcode::damage, 9}, Instruction{Opcode::request_choice, 1, CardChoice::discard_one}, Instruction{Opcode::topdeck_choice}, Instruction{Opcode::finish}};
constexpr std::array headbutt_upgraded = {Instruction{Opcode::damage, 12}, Instruction{Opcode::request_choice, 1, CardChoice::discard_one}, Instruction{Opcode::topdeck_choice}, Instruction{Opcode::finish}};
constexpr std::array burning_pact = {Instruction{Opcode::request_choice, 1}, Instruction{Opcode::exhaust_choice}, Instruction{Opcode::draw, 2}, Instruction{Opcode::finish}};
constexpr std::array burning_pact_upgraded = {Instruction{Opcode::request_choice, 1}, Instruction{Opcode::exhaust_choice}, Instruction{Opcode::draw, 3}, Instruction{Opcode::finish}};
constexpr std::array true_grit = {Instruction{Opcode::block, 7}, Instruction{Opcode::exhaust_random}, Instruction{Opcode::finish}};
constexpr std::array true_grit_upgraded = {Instruction{Opcode::block, 9}, Instruction{Opcode::request_choice, 1}, Instruction{Opcode::exhaust_choice}, Instruction{Opcode::finish}};
constexpr std::array purity = {Instruction{Opcode::request_choice, 3, CardChoice::hand_optional}, Instruction{Opcode::exhaust_choice}, Instruction{Opcode::finish}};
constexpr std::array purity_upgraded = {Instruction{Opcode::request_choice, 5, CardChoice::hand_optional}, Instruction{Opcode::exhaust_choice}, Instruction{Opcode::finish}};
constexpr std::array inflame = {Instruction{Opcode::strength, 2}, Instruction{Opcode::finish}};
constexpr std::array inflame_upgraded = {Instruction{Opcode::strength, 3}, Instruction{Opcode::finish}};
constexpr std::array shockwave = {Instruction{Opcode::weak, 3}, Instruction{Opcode::vulnerable, 3}, Instruction{Opcode::finish}};
constexpr std::array shockwave_upgraded = {Instruction{Opcode::weak, 5}, Instruction{Opcode::vulnerable, 5}, Instruction{Opcode::finish}};
constexpr std::array tremble = {Instruction{Opcode::vulnerable, 3}, Instruction{Opcode::finish}};
constexpr std::array tremble_upgraded = {Instruction{Opcode::vulnerable, 4}, Instruction{Opcode::finish}};
constexpr std::array uppercut = {Instruction{Opcode::damage, 13}, Instruction{Opcode::weak, 1}, Instruction{Opcode::vulnerable, 1}, Instruction{Opcode::finish}};
constexpr std::array uppercut_upgraded = {Instruction{Opcode::damage, 13}, Instruction{Opcode::weak, 2}, Instruction{Opcode::vulnerable, 2}, Instruction{Opcode::finish}};
constexpr std::array dagger_throw = {Instruction{Opcode::damage, 9}, Instruction{Opcode::draw, 1}, Instruction{Opcode::request_choice, 1}, Instruction{Opcode::discard_choice}, Instruction{Opcode::finish}};
constexpr std::array dagger_throw_upgraded = {Instruction{Opcode::damage, 12}, Instruction{Opcode::draw, 1}, Instruction{Opcode::request_choice, 1}, Instruction{Opcode::discard_choice}, Instruction{Opcode::finish}};
constexpr std::array definitions = {
    CardDefinition{"CARD.STRIKE_IRONCLAD", "Strike", 1, true, {strike, strike_upgraded}, CardRarity::basic, CardKind::attack},
    CardDefinition{"CARD.DEFEND_IRONCLAD", "Defend", 1, false, {defend, defend_upgraded}},
    CardDefinition{"CARD.BASH", "Bash", 2, true, {bash, bash_upgraded}, CardRarity::basic, CardKind::attack},
    CardDefinition{"CARD.SURVIVOR", "Survivor", 1, false, {survivor, survivor_upgraded}},
    CardDefinition{"CARD.PURITY", "Purity", 0, false, {purity, purity_upgraded}, CardRarity::uncommon, CardKind::skill, exhaust | retain},
    CardDefinition{"CARD.ARMAMENTS", "Armaments", 1, false, {armaments, armaments_upgraded}, CardRarity::common},
    CardDefinition{"CARD.HEADBUTT", "Headbutt", 1, true, {headbutt, headbutt_upgraded}, CardRarity::common, CardKind::attack},
    CardDefinition{"CARD.BURNING_PACT", "Burning Pact", 1, false, {burning_pact, burning_pact_upgraded}, CardRarity::uncommon},
    CardDefinition{"CARD.TRUE_GRIT", "True Grit", 1, false, {true_grit, true_grit_upgraded}, CardRarity::common},
    CardDefinition{"CARD.INFLAME", "Inflame", 1, false, {inflame, inflame_upgraded}, CardRarity::uncommon, CardKind::power},
    CardDefinition{"CARD.SHOCKWAVE", "Shockwave", 2, false, {shockwave, shockwave_upgraded}, CardRarity::uncommon, CardKind::skill, exhaust},
    CardDefinition{"CARD.TREMBLE", "Tremble", 1, true, {tremble, tremble_upgraded}, CardRarity::common, CardKind::skill, exhaust},
    CardDefinition{"CARD.UPPERCUT", "Uppercut", 2, true, {uppercut, uppercut_upgraded}, CardRarity::uncommon, CardKind::attack},
    CardDefinition{"CARD.DAGGER_THROW", "Dagger Throw", 1, true, {dagger_throw, dagger_throw_upgraded}, CardRarity::common, CardKind::attack}};
}
const CardDefinition* Ruleset::card(CardId id) const noexcept {
    const auto index = static_cast<std::size_t>(id);
    return index < definitions.size() ? &definitions[index] : nullptr;
}
std::span<const CardDefinition> Ruleset::cards() const noexcept { return definitions; }
const Ruleset& Ruleset::pinned() noexcept { static constexpr Ruleset rules; return rules; }
} // namespace sts2::engine
