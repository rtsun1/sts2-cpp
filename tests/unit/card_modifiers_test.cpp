#include "sts2/engine/combat.hpp"
#include "sts2/json_io/native_run.hpp"
#include "sts2/observation/observe.hpp"
#include "sts2/persistence/checkpoint.hpp"
#include "../../src/engine/mechanics/cards.hpp"
#include "storage_allocator.hpp"
#include <gtest/gtest.h>
#include <array>
#include <algorithm>

namespace sts2::engine {
namespace {
CardHandle first_card(ConstCombatView state) {
    return {0, state.cards.slots()[0].generation};
}
void initialize_one(CombatState& state, CardId id = CardId::defend, CardEnchantment enchantment = {}) {
    const std::array deck{CardSpec{id, false, {}, enchantment}};
    CombatConfig config; config.deck = deck; config.enemy_hp = 40;
    Scratch scratch;
    ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
}
StepResult play_card(CombatState& state, CardHandle card, Scratch& scratch) {
    return apply_combat(state, {state.view().boundary, CombatInputKind::play_card, card,
        Ruleset::pinned().card(state.view().cards.get(card)->definition)->enemy_target}, scratch);
}
TEST(CardModifiers, ExhaustMutationChangesExecutionAndObservation) {
    CombatState state; initialize_one(state); Scratch scratch;
    const auto card = first_card(state);
    state.view().cards.get(card)->keyword_mask |= static_cast<CardKeywordMask>(CardKeyword::exhaust);
    ASSERT_TRUE(observe_card(state, card)->exhaust);
    ASSERT_EQ(play_card(state, card, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(state.zone(Zone::exhaust).size(), 1u);
    EXPECT_EQ(state.zone(Zone::exhaust).front(), card);
    EXPECT_TRUE(valid_combat(state));

    CombatState other; initialize_one(other, CardId::tremble);
    const auto other_card = first_card(other);
    other.view().cards.get(other_card)->keyword_mask &= ~static_cast<CardKeywordMask>(CardKeyword::exhaust);
    EXPECT_FALSE(observe_card(other, other_card)->exhaust);
    ASSERT_EQ(play_card(other, other_card, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(other.zone(Zone::discard).size(), 1u);
    EXPECT_TRUE(other.zone(Zone::exhaust).empty());
}
TEST(CardModifiers, TemporaryRetainExpiresWithoutRemovingLocalRetain) {
    CombatState state; initialize_one(state); Scratch scratch;
    const auto card = first_card(state);
    state.view().cards.get(card)->single_turn_retain = true;
    EXPECT_TRUE(has_keyword(observe_card(state, card)->keyword_mask, CardKeyword::retain));
    gameplay::discard_hand_at_turn_end(state, Ruleset::pinned());
    ASSERT_EQ(state.zone(Zone::hand).size(), 1u);
    EXPECT_FALSE(state.view().cards.get(card)->single_turn_retain);
    EXPECT_FALSE(has_keyword(state.view().cards.get(card)->keyword_mask, CardKeyword::retain));
    gameplay::discard_hand_at_turn_end(state, Ruleset::pinned());
    EXPECT_TRUE(state.zone(Zone::hand).empty());

    CombatState permanent; initialize_one(permanent, CardId::purity);
    const auto retained = first_card(permanent);
    permanent.view().cards.get(retained)->single_turn_retain = true;
    gameplay::discard_hand_at_turn_end(permanent, Ruleset::pinned());
    EXPECT_TRUE(has_keyword(permanent.view().cards.get(retained)->keyword_mask, CardKeyword::retain));
}
TEST(CardModifiers, SoulsPowerRemovesExhaustDuringInitialization) {
    CombatState state; initialize_one(state, CardId::tremble, {EnchantmentId::souls_power, 1});
    const auto card = first_card(state); Scratch scratch;
    EXPECT_FALSE(state.view().cards.get(card)->origin.present());
    EXPECT_FALSE(has_keyword(state.view().cards.get(card)->keyword_mask, CardKeyword::exhaust));
    EXPECT_FALSE(observe_card(state, card)->exhaust);
    ASSERT_EQ(play_card(state, card, scratch).status, StepStatus::awaiting_input);
    EXPECT_TRUE(state.zone(Zone::exhaust).empty());
    EXPECT_EQ(state.zone(Zone::discard).front(), card);
}
TEST(CardModifiers, ExhaustOnNextPlayIsConsumedIndependentlyOfSoulsPower) {
    CombatState state; initialize_one(state, CardId::defend, {EnchantmentId::souls_power, 1});
    const auto card = first_card(state); Scratch scratch;
    state.view().cards.get(card)->exhaust_on_next_play = true;
    ASSERT_EQ(play_card(state, card, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(state.zone(Zone::exhaust).front(), card);
    EXPECT_FALSE(state.view().cards.get(card)->exhaust_on_next_play);
}
TEST(CardModifiers, PowerTypeRemovesCardEvenWhenExhaustWasAdded) {
    CombatState state; initialize_one(state, CardId::inflame); Scratch scratch;
    const auto card = first_card(state);
    state.view().cards.get(card)->keyword_mask |= static_cast<CardKeywordMask>(CardKeyword::exhaust);
    ASSERT_EQ(play_card(state, card, scratch).status, StepStatus::awaiting_input);
    EXPECT_TRUE(state.view().cards.get(card)->removed);
    EXPECT_TRUE(state.zone(Zone::play).empty());
    EXPECT_TRUE(state.zone(Zone::discard).empty());
    EXPECT_TRUE(state.zone(Zone::exhaust).empty());
    EXPECT_TRUE(valid_combat(state));
}
TEST(CardModifiers, CombatInitializationCopiesSavedEnchantmentAndExportKeepsLocalState) {
    RunState state; NativeRunExporter exporter; Scratch scratch;
    const std::array deck{CardSpec{CardId::tremble, false, {}, {EnchantmentId::souls_power, 1}}};
    ASSERT_EQ(initialize_combat_scenario(state, {}, deck, exporter.trace()).status, StepStatus::awaiting_input);
    auto combat = *state.view().combat;
    const auto card = first_card(combat);
    EXPECT_EQ(combat.cards.get(card)->enchantment, state.view().deck[0].enchantment);
    combat.cards.get(card)->enchantment.amount = 2;
    EXPECT_EQ(state.view().deck[0].enchantment, (CardEnchantment{EnchantmentId::souls_power, 1}));
    const auto snapshot = exporter.snapshot(state);
    EXPECT_EQ(snapshot["players"][0]["deck"][0]["enchantment"]["id"], "ENCHANTMENT.SOULS_POWER");
    const auto& exported = snapshot["combat"]["cards"]["card_0"];
    EXPECT_EQ(exported["local_keywords"], nlohmann::json::array());
    EXPECT_EQ(exported["enchantment_runtime"]["status"], "Normal");
    RunState restored;
    ASSERT_EQ(restore(restored, checkpoint(state).bytes, scratch), RestoreStatus::restored);
    EXPECT_EQ(restored.view().deck[0].enchantment, state.view().deck[0].enchantment);
    EXPECT_EQ(*restored.view().combat->cards.get(card), *combat.cards.get(card));
}
TEST(CardModifiers, RuntimeAfflictionAndReplaySurviveAllocationFreeCloneAndCheckpoint) {
    test::AllocationCounter allocation;
    CombatState state(allocation.allocator()), branch(allocation.allocator()); initialize_one(state);
    const auto card = first_card(state); auto& value = *state.view().cards.get(card);
    value.enchantment = {EnchantmentId::glam, 1};
    gameplay::set_enchantment_runtime(state, card, EnchantmentUseState{true});
    value.affliction = {AfflictionId::bound, 2}; value.base_replay_count = 3;
    value.keyword_mask |= static_cast<CardKeywordMask>(CardKeyword::sly);
    value.single_turn_retain = true; value.single_turn_sly = true;
    const auto before = allocation.allocations;
    allocation.fail_after = before;
    ASSERT_EQ(clone_into(branch, state), CloneStatus::cloned);
    EXPECT_EQ(allocation.allocations, before);
    EXPECT_EQ(*branch.view().cards.get(card), value);
    gameplay::set_enchantment_runtime(branch, card, EnchantmentUseState{false});
    EXPECT_TRUE(std::get<EnchantmentUseState>(*gameplay::find_enchantment_runtime(state, card)).consumed);
    Scratch scratch; CombatState restored;
    ASSERT_EQ(restore(restored, checkpoint(state).bytes, scratch), RestoreStatus::restored);
    EXPECT_EQ(*restored.view().cards.get(card), value);
    ASSERT_NE(gameplay::find_enchantment_runtime(restored, card), nullptr);
    EXPECT_EQ(*gameplay::find_enchantment_runtime(restored, card), *gameplay::find_enchantment_runtime(state, card));
    // State transport is supported; unimplemented effects cannot silently execute.
    const auto saved = checkpoint(state).bytes;
    EXPECT_EQ(play_card(state, card, scratch).reason, Restriction::unsupported_setup);
    EXPECT_EQ(checkpoint(state).bytes, saved);
}
TEST(CardModifiers, SparseRuntimeOverflowClonesIndependentlyAndFailedPromotionIsAtomic) {
    test::AllocationCounter allocation;
    CombatState state(allocation.allocator()), branch; Scratch scratch;
    const std::array deck{CardSpec{CardId::defend}, CardSpec{CardId::defend}, CardSpec{CardId::defend}};
    CombatConfig config; config.deck = deck;
    ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
    ASSERT_TRUE(state.view().enchantment_runtime.empty());
    const CardHandle a{0, state.view().cards.slots()[0].generation};
    const CardHandle b{1, state.view().cards.slots()[1].generation};
    const CardHandle c{2, state.view().cards.slots()[2].generation};
    state.view().cards.get(a)->enchantment = {EnchantmentId::glam, 1};
    state.view().cards.get(b)->enchantment = {EnchantmentId::swift, 2};
    state.view().cards.get(c)->enchantment = {EnchantmentId::momentum, 3};
    const auto before = allocation.allocations;
    allocation.fail_after = before;
    gameplay::set_enchantment_runtime(state, a, EnchantmentUseState{true});
    gameplay::set_enchantment_runtime(state, b, EnchantmentUseState{false});
    EXPECT_EQ(allocation.allocations, before);
    EXPECT_FALSE(state.data.enchantment_runtime.overflowed());
    const auto saved = checkpoint(state).bytes;
    EXPECT_THROW(gameplay::set_enchantment_runtime(state, c, MomentumState{7}), std::bad_alloc);
    EXPECT_EQ(checkpoint(state).bytes, saved);
    allocation.fail_after = SIZE_MAX;
    gameplay::set_enchantment_runtime(state, c, MomentumState{7});
    ASSERT_TRUE(state.data.enchantment_runtime.overflowed());
    ASSERT_EQ(clone_into(branch, state), CloneStatus::cloned);
    gameplay::set_enchantment_runtime(branch, c, MomentumState{19});
    EXPECT_EQ(std::get<MomentumState>(*gameplay::find_enchantment_runtime(state, c)).extra_damage, 7);
    CombatState restored;
    ASSERT_EQ(restore(restored, checkpoint(state).bytes, scratch), RestoreStatus::restored);
    EXPECT_EQ(restored.view().enchantment_runtime, state.view().enchantment_runtime);
    EXPECT_THROW(gameplay::set_enchantment_runtime(state, c, EnchantmentUseState{true}), std::invalid_argument);
    EXPECT_THROW(gameplay::set_enchantment_runtime(state, CardHandle{}, EnchantmentUseState{true}), std::invalid_argument);
    state.view().enchantment_runtime.push_back(state.view().enchantment_runtime.front());
    EXPECT_FALSE(valid_combat(state));
    state.view().enchantment_runtime.pop_back();
    ++state.view().enchantment_runtime.front().card.generation;
    EXPECT_FALSE(valid_combat(state));
}
TEST(CardModifiers, UnknownRuntimePayloadRestoreIsTransactional) {
    CombatState state; initialize_one(state); Scratch scratch;
    const auto card = first_card(state);
    state.view().cards.get(card)->enchantment = {EnchantmentId::momentum, 1};
    gameplay::set_enchantment_runtime(state, card, MomentumState{0x12345678});
    const auto saved = checkpoint(state).bytes;
    auto corrupt = saved;
    constexpr std::array signature{std::byte{0x78}, std::byte{0x56}, std::byte{0x34}, std::byte{0x12}};
    const auto found = std::search(corrupt.begin(), corrupt.end(), signature.begin(), signature.end());
    ASSERT_NE(found, corrupt.end()); ASSERT_NE(found, corrupt.begin());
    *(found - 1) = std::byte{255};
    const auto hash = game_hash({reinterpret_cast<const char*>(corrupt.data()), corrupt.size() - 8});
    for (unsigned i = 0; i < 8; ++i) corrupt[corrupt.size() - 8 + i] = std::byte(hash >> (8 * i));
    EXPECT_EQ(restore(state, corrupt, scratch), RestoreStatus::malformed);
    EXPECT_EQ(checkpoint(state).bytes, saved);
}
TEST(CardModifiers, InvalidRuntimeFieldsAndUnknownIdsAreRejected) {
    CombatState state; initialize_one(state);
    auto& card = *state.view().cards.get(first_card(state));
    state.view().enchantment_runtime.push_back({first_card(state), EnchantmentUseState{true}});
    EXPECT_FALSE(valid_combat(state));
    state.view().enchantment_runtime.clear(); card.affliction.amount = 1;
    EXPECT_FALSE(valid_combat(state));
    card.affliction = {}; card.enchantment.definition = static_cast<EnchantmentId>(255);
    EXPECT_FALSE(valid_combat(state));
    card.enchantment = {}; card.keyword_mask = 128;
    EXPECT_FALSE(valid_combat(state));
    EXPECT_THROW(checkpoint(state), std::invalid_argument);
}
} // namespace
} // namespace sts2::engine
