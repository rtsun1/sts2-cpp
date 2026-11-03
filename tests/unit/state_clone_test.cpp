#include "../support/selection_input.hpp"
#include "sts2/engine/action.hpp"
#include "sts2/observation/run.hpp"
#include "sts2/persistence/checkpoint.hpp"
#include "storage_allocator.hpp"
#include "../support/checkpoint_fixture.hpp"
#include "../../src/engine/mechanics/powers.hpp"
#include <gtest/gtest.h>

namespace sts2::engine {
namespace {
using test::AllocationCounter;
static_assert(std::is_trivially_copyable_v<CombatData>);
static_assert(std::is_trivially_copyable_v<RunData>);

TEST(CreaturePool, ExplicitPositionsPreserveOccupiedSlotsAndReuseGenerations) {
    AllocationCounter allocation;
    CreaturePool data;
    OverflowStorage owner(allocation.allocator());
    CreaturePoolView<> pool{data, owner};
    const auto player = pool.create_at(0, {80, 80}, 8);
    const auto last = pool.create_at(4, {40, 40}, 8);
    const auto middle = pool.create_at(2, {30, 30}, 8);
    ASSERT_TRUE(player.present()); ASSERT_TRUE(last.present()); ASSERT_TRUE(middle.present());
    EXPECT_TRUE(pool.valid());
    EXPECT_EQ(allocation.allocations, 0u);
    EXPECT_EQ(pool.get(last)->hp, 40);
    EXPECT_FALSE(pool.create_at(2, {99, 99}, 8).present());
    EXPECT_FALSE(pool.create_at(8, {}, 8).present());
    EXPECT_FALSE(pool.create_at(1, {}, 8, 0).present());
    EXPECT_EQ(pool.get(middle)->hp, 30);
    ASSERT_TRUE(pool.erase(middle));
    const auto replacement = pool.create_at(2, {35, 35}, 8);
    ASSERT_TRUE(replacement.present());
    EXPECT_EQ(replacement.slot, middle.slot);
    EXPECT_GT(replacement.generation, middle.generation);
    EXPECT_EQ(pool.get(middle), nullptr);
    EXPECT_EQ(pool.get(replacement)->hp, 35);
    EXPECT_EQ(pool.get(player)->hp, 80);
    EXPECT_EQ(pool.get(last)->hp, 40);
    EXPECT_TRUE(pool.valid());
    const auto exhausted = pool.create_at(6, {10, 10}, 8, UINT32_MAX);
    ASSERT_TRUE(exhausted.present());
    ASSERT_TRUE(pool.erase(exhausted));
    EXPECT_FALSE(pool.create_at(6, {10, 10}, 8).present());
    EXPECT_TRUE(pool.valid());
    EXPECT_TRUE(pool.create_at(5, {10, 10}, 8).present());
}

TEST(CreaturePool, ExplicitPositionGrowthSurvivesAllocationFailureAndPreservesHoles) {
    AllocationCounter allocation;
    PoolData<CreatureState, CreatureTag, 2, PoolAllocation::specified_slot> data;
    OverflowStorage owner(allocation.allocator());
    PoolView<CreatureState, CreatureTag, 2, false, PoolAllocation::specified_slot> pool{data, owner};
    const auto original = pool.create_at(0, {80, 80}, 16);
    allocation.fail_after = allocation.allocations;
    EXPECT_THROW((void)pool.create_at(8, {40, 40}, 16), std::bad_alloc);
    EXPECT_TRUE(pool.valid());
    EXPECT_EQ(pool.slots().size(), 1u);
    EXPECT_EQ(pool.get(original)->hp, 80);
    allocation.fail_after = SIZE_MAX;
    const auto last = pool.create_at(8, {40, 40}, 16);
    ASSERT_TRUE(last.present());
    EXPECT_TRUE(pool.valid());
    for (std::uint32_t i = 1; i < 8; ++i) {
        EXPECT_TRUE(pool.available_at(i, 16));
        ASSERT_TRUE(pool.create_at(i, {20, 20}, 16).present());
    }
    const auto allocations = allocation.allocations;
    allocation.fail_after = allocations;
    EXPECT_TRUE(pool.erase(last));
    EXPECT_TRUE(pool.create_at(last.slot, {30, 30}, 16).present());
    EXPECT_EQ(allocation.allocations, allocations);
    EXPECT_TRUE(pool.valid());
}

TEST(CreaturePool, ReverseEncounterSearchIncludesUngrownSlotsAndSkipsUnavailablePositions) {
    CreaturePool data;
    OverflowStorage owner;
    CreaturePoolView<> pool{data, owner};
    const auto player = pool.create_at(0, {80, 80}, 16);
    ASSERT_TRUE(player.present());
    // The encounter owns positions [1, 5); selection remains outside the pool.
    const auto last_available = [&]() -> std::uint32_t {
        for (std::uint32_t index = 5; index > 1;) {
            --index;
            if (pool.available_at(index, 5)) return index;
        }
        return UINT32_MAX;
    };
    EXPECT_EQ(pool.slots().size(), 1u);
    EXPECT_EQ(last_available(), 4u);
    const auto exhausted = pool.create_at(4, {10, 10}, 5, UINT32_MAX);
    ASSERT_TRUE(exhausted.present());
    ASSERT_TRUE(pool.erase(exhausted));
    EXPECT_EQ(last_available(), 3u);
    const auto occupied = pool.create_at(3, {10, 10}, 5);
    ASSERT_TRUE(occupied.present());
    EXPECT_EQ(last_available(), 2u);
    ASSERT_TRUE(pool.create_at(2, {10, 10}, 5).present());
    ASSERT_TRUE(pool.create_at(1, {10, 10}, 5).present());
    EXPECT_EQ(last_available(), UINT32_MAX);
    ASSERT_TRUE(pool.erase(occupied));
    EXPECT_EQ(last_available(), 3u);
    EXPECT_FALSE(pool.available_at(5, 5));
    EXPECT_FALSE(pool.available_at(UINT32_MAX, SIZE_MAX));
    EXPECT_TRUE(pool.valid());
}

TEST(CardPool, DefaultAllocationReusesFreedSlotWithNewGeneration) {
    PoolData<std::uint32_t, CombatCardTag, 2> data;
    OverflowStorage owner;
    PoolView<std::uint32_t, CombatCardTag, 2> pool{data, owner};
    const auto first = pool.create(10, 2);
    const auto second = pool.create(20, 2);
    ASSERT_TRUE(first.present()); ASSERT_TRUE(second.present());
    ASSERT_TRUE(pool.erase(first));
    ASSERT_EQ(pool.free_slots().size(), 1u);
    EXPECT_EQ(pool.free_slots().front(), first.slot);
    const auto reused = pool.create(30, 2);
    EXPECT_EQ(reused.slot, first.slot);
    EXPECT_NE(reused.generation, first.generation);
    EXPECT_EQ(pool.get(first), nullptr);
    ASSERT_NE(pool.get(reused), nullptr);
    EXPECT_EQ(*pool.get(reused), 30u);
    EXPECT_TRUE(pool.free_slots().empty());
    EXPECT_TRUE(pool.valid());
}

TEST(CompleteStateClone, SpecifiedCreaturePositionsPreserveHolesAndRecycledGenerations) {
    RunState state; Scratch scratch;
    ASSERT_EQ(initialize_combat_scenario(state, {}, {}).status, StepStatus::awaiting_input);
    const auto retained = state.creatures().create_at(9, {30, 30}, 16);
    const auto removed = state.creatures().create_at(10, {40, 40}, 16);
    ASSERT_TRUE(retained.present()); ASSERT_TRUE(removed.present());
    ASSERT_TRUE(state.creatures().erase(removed));
    ASSERT_TRUE(valid_run(state));
    RunState clone = state, restored;
    ASSERT_EQ(restore(restored, checkpoint(state).bytes, scratch), RestoreStatus::restored);
    EXPECT_EQ(state, clone); EXPECT_EQ(state, restored);
    for (auto* branch : {&clone, &restored}) {
        EXPECT_TRUE(branch->creatures().available_at(4, 16));
        EXPECT_TRUE(branch->creatures().available_at(10, 16));
        EXPECT_FALSE(branch->creatures().available_at(9, 16));
        const auto replacement = branch->creatures().create_at(10, {50, 50}, 16);
        ASSERT_TRUE(replacement.present());
        EXPECT_EQ(replacement.generation, removed.generation + 1);
        EXPECT_EQ(branch->creatures().get(removed), nullptr);
        ASSERT_NE(branch->creatures().get(retained), nullptr);
        EXPECT_EQ(branch->creatures().get(retained)->hp, 30);
        EXPECT_TRUE(valid_run(*branch));
    }
    EXPECT_EQ(clone, restored);
}

TEST(CompleteStateClone, PreviousCreatureFreeListCheckpointVersionIsIncompatible) {
    RunState state; Scratch scratch;
    ASSERT_EQ(initialize_combat_scenario(state, {}, {}).status, StepStatus::awaiting_input);
    const auto saved = checkpoint(state).bytes;
    auto previous = saved;
    test::checkpoint_u32(previous, 8, 17);
    test::checksum_checkpoint(previous);
    EXPECT_EQ(restore(state, previous, scratch), RestoreStatus::incompatible);
    EXPECT_EQ(checkpoint(state).bytes, saved);
}

TEST(CompleteStateClone, CombatSetupPreservesPlayerAndCreatesEnemyOnlyOnce) {
    RunState state;
    const auto player = state.data.player;
    ASSERT_EQ(initialize_combat_scenario(state, {}, {}).status, StepStatus::awaiting_input);
    EXPECT_EQ(state.data.player, player);
    EXPECT_EQ(state.enemy_handle(), (CreatureHandle{1, 1}));
    EXPECT_EQ(state.creatures().data.highest_generation, 1u);
    EXPECT_TRUE(valid_run(state));
}

TEST(CompleteStateClone, CommonRunClonesAllocateNothing) {
    AllocationCounter allocation;
    {
        RunState source(allocation.allocator()), run_copy(allocation.allocator());
        ASSERT_EQ(initialize_seeded_combat(source, {}).status, StepStatus::awaiting_input);
        ASSERT_TRUE(source.overflow.empty());
        ASSERT_TRUE(source.combat_overflow.empty());
        RunState combat(std::as_const(source)), combat_copy(allocation.allocator());
        ASSERT_TRUE(combat.combat_overflow.empty());
        const auto allocations = allocation.allocations;
        allocation.fail_after = allocations;
        EXPECT_EQ(clone_into(run_copy, source), CloneStatus::cloned);
        EXPECT_EQ(clone_into(combat_copy, combat), CloneStatus::cloned);
        EXPECT_EQ(allocation.allocations, allocations);
        EXPECT_EQ(source, run_copy); EXPECT_EQ(combat, combat_copy);
        RunState relocated(allocation.allocator());
        std::memcpy(&relocated.data, &combat.data, sizeof(RunData));
        EXPECT_TRUE(valid_combat(relocated)); EXPECT_EQ(relocated, combat);
        Scratch scratch;
        const CombatInput input{run_copy.data.boundary, CombatInputKind::end_turn};
        EXPECT_EQ(apply_action(run_copy, input, scratch).status, StepStatus::awaiting_input);
        EXPECT_NE(run_copy, source);
        EXPECT_EQ(combat, combat_copy);
    }
    EXPECT_EQ(allocation.outstanding, 0u);
}

TEST(CompleteStateClone, OrderedPowerInstancesCloneWithoutAllocating) {
    AllocationCounter allocation;
    RunState source(allocation.allocator()), branch(allocation.allocator()); Scratch scratch;
    ASSERT_EQ(initialize_combat(source, {}, scratch).status, StepStatus::awaiting_input);
    auto powers = source.player().power_instances(source.overflow);
    PowerInstance vulnerable{0, PowerId::vulnerable, 2, 1, false, true, true}; vulnerable.applier = {0, 1};
    PowerInstance shrink{1, PowerId::shrink, -1, -1, false, true, true}; shrink.applier = {0, 1};
    powers.push_back(vulnerable); powers.push_back(shrink); source.combat().next_power_instance = 2;
    ASSERT_TRUE(valid_combat(source));
    const auto allocations = allocation.allocations; allocation.fail_after = allocations;
    ASSERT_EQ(clone_into(branch, source), CloneStatus::cloned);
    EXPECT_EQ(allocation.allocations, allocations);
    EXPECT_EQ(branch.player().power_instances(branch.overflow).front().definition, PowerId::vulnerable);
    branch.player().find_power(PowerId::vulnerable, branch.overflow)->amount = 3;
    EXPECT_EQ(source.player().power_amount(PowerId::vulnerable, source.overflow), 2);
    RunState restored;
    ASSERT_EQ(restore(restored, checkpoint(source).bytes, scratch), RestoreStatus::restored);
    EXPECT_EQ(restored, source);
}
TEST(CompleteStateClone, InvalidPowerIdentityAndStackingAreRejected) {
    RunState source; Scratch scratch;
    ASSERT_EQ(initialize_combat(source, {}, scratch).status, StepStatus::awaiting_input);
    PowerInstance power{0, PowerId::shrink, -1, 0, false, true, true}; power.applier = {0, 1};
    source.player().power_instances(source.overflow).push_back(power); source.combat().next_power_instance = 1;
    ASSERT_TRUE(valid_combat(source));
    auto invalid = source;
    invalid.player().powers.count = 7;
    EXPECT_FALSE(valid_combat(invalid));
    invalid = source; invalid.player().power_instances(invalid.overflow)[0].definition = PowerId::count;
    EXPECT_FALSE(valid_combat(invalid));
    invalid = source; invalid.player().power_instances(invalid.overflow).push_back(power);
    EXPECT_FALSE(valid_combat(invalid));
    invalid = source; invalid.player().power_instances(invalid.overflow)[0].enemy_owner = true;
    EXPECT_FALSE(valid_combat(invalid));
    invalid = source; invalid.player().power_instances(invalid.overflow)[0].applier.generation++;
    EXPECT_FALSE(valid_combat(invalid));
}

TEST(CompleteStateClone, LargePublicRunBranchesAndRestoresWithIndependentOverflow) {
    Scratch transition_scratch;
    RunState source;
    SeededConfig config; config.seed = "KERNELRUN14";
    ASSERT_EQ(initialize_run(source, config).status, StepStatus::awaiting_input);
    // An authored large inventory exercises the same public entry/transition
    // boundaries; queue generation and map topology remain untouched.
    auto& setup = source;
    while (setup.deck().size() < 200) setup.deck().push_back({SetupRules::find("CARD.STRIKE_IRONCLAD"), static_cast<std::uint32_t>(setup.deck().size()), 0, 1});
    ASSERT_TRUE(valid_run(source));
    ASSERT_EQ(apply_input(source, RunInput{source.data.boundary, RunInputKind::choose_option, 0}, transition_scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(apply_input(source, RunInput{source.data.boundary, RunInputKind::proceed}, transition_scratch).status, StepStatus::awaiting_input);
    const auto& map = *source.data.maps[0];
    MapNodeId destination = no_node;
    for (auto node : map.nodes[map.start].children.values()) if (map.nodes[node].kind == MapKind::monster) { destination = node; break; }
    ASSERT_NE(destination, no_node);
    ASSERT_EQ(apply_input(source, RunInput{source.data.boundary, RunInputKind::travel, 0, map.nodes[destination].col, map.nodes[destination].row}, transition_scratch).status, StepStatus::awaiting_input);
    ASSERT_FALSE(source.overflow.empty()); ASSERT_FALSE(source.combat_overflow.empty());
    ASSERT_EQ(source.cards().slots().size(), 200u);
    RunState cloned, restored; Scratch scratch;
    clone_into(cloned, source);
    const auto encoded = checkpoint(source).bytes;
    ASSERT_EQ(restore(restored, encoded, scratch), RestoreStatus::restored);
    EXPECT_EQ(checkpoint(restored).bytes, encoded);
    const CombatInput input{source.data.boundary, CombatInputKind::end_turn};
    ASSERT_EQ(apply_action(source, input, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(apply_action(cloned, input, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(apply_action(restored, input, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(source, cloned); EXPECT_EQ(source, restored);
    const auto unchanged = checkpoint(source).bytes;
    cloned.deck()[199].upgrade = 1;
    cloned.cards().get({199, cloned.data.combat_generation})->upgraded = true;
    EXPECT_EQ(checkpoint(source).bytes, unchanged);
    EXPECT_NE(source, cloned); EXPECT_EQ(source, restored);
    clone_into(cloned, source); // Reuse a destination that already owns overflow.
    EXPECT_EQ(cloned, source);
    RunState common;
    ASSERT_EQ(initialize_run(common, {}).status, StepStatus::awaiting_input);
    clone_into(cloned, common);
    EXPECT_EQ(cloned, common); EXPECT_TRUE(cloned.combat_overflow.empty());
}

TEST(CompleteStateClone, CheckpointsIgnoreStorageModeAndRetainedCapacity) {
    RunState source; Scratch scratch;
    ASSERT_EQ(initialize_combat(source, {}, scratch).status, StepStatus::awaiting_input);
    auto other = source;
    other.execution().reserve(256); other.seed().reserve(100);
    ASSERT_FALSE(other.combat_overflow.empty());
    EXPECT_TRUE(valid_combat(other)); EXPECT_EQ(source, other);
    EXPECT_EQ(checkpoint(source).bytes, checkpoint(other).bytes);
    source.reset();
    EXPECT_TRUE(source.combat_overflow.empty()); EXPECT_EQ(source.combat().phase, CombatPhase::uninitialized);
}

TEST(CompleteStateClone, FailedPublicInitializationAndCloneKeepDestinationUnchanged) {
    const std::array<CardSpec, 200> deck{};
    CombatConfig config; config.deck = deck;
    AllocationCounter allocation;
    {
        RunState destination(allocation.allocator()), source;
        Scratch scratch;
        ASSERT_EQ(initialize_combat(destination, {}, scratch).status, StepStatus::awaiting_input);
        ASSERT_EQ(initialize_combat(source, config, scratch).status, StepStatus::awaiting_input);
        const auto saved = checkpoint(destination).bytes;
        allocation.fail_after = allocation.allocations;
        EXPECT_EQ(initialize_combat(destination, config, scratch).status, StepStatus::resource_limit);
        EXPECT_EQ(checkpoint(destination).bytes, saved);
        EXPECT_EQ(clone_into(destination, source), CloneStatus::resource_limit);
        EXPECT_EQ(checkpoint(destination).bytes, saved);
        EXPECT_EQ(restore(destination, checkpoint(source).bytes, scratch), RestoreStatus::resource_limit);
        EXPECT_EQ(checkpoint(destination).bytes, saved);
    }
    EXPECT_EQ(allocation.outstanding, 0u);
}

TEST(CompleteStateClone, CheckpointRejectsAbsentOrEnemyPlayerIdentity) {
    RunState run; Scratch scratch;
    ASSERT_EQ(initialize_combat_scenario(run, {}, {}).status, StepStatus::awaiting_input);
    const auto retained = run.creatures().create_at(2, {5, 5}, 4096);
    const auto saved = checkpoint(run).bytes;
    const auto offset = test::player_identity_offset(saved);
    for (auto player : {CreatureHandle{}, run.enemy_handle()}) {
        auto bytes = saved;
        test::checkpoint_u32(bytes, offset, player.slot);
        test::checkpoint_u32(bytes, offset + 4, player.generation);
        test::checksum_checkpoint(bytes);
        EXPECT_EQ(restore(run, bytes, scratch), RestoreStatus::malformed);
        EXPECT_EQ(checkpoint(run).bytes, saved);
    }
    RunState restored;
    ASSERT_EQ(restore(restored, saved, scratch), RestoreStatus::restored);
    ASSERT_NE(restored.creature(retained), nullptr);
    EXPECT_EQ(restored.creature(retained)->hp, 5);
    EXPECT_EQ(restored, run);
}

TEST(CompleteStateClone, OverflowPowerRestorePublishesBothOwnersTransactionally) {
    RunState source; Scratch scratch;
    ASSERT_EQ(initialize_combat_scenario(source, {}, {}).status, StepStatus::awaiting_input);
    for (int i = 0; i < 9; ++i)
        gameplay::apply_power(source, scratch, PowerId::strength, 1, false, false, {}, true, false, PowerInstance::Stacking::separate);
    const auto bytes = checkpoint(source).bytes;
    AllocationCounter allocation;
    {
        RunState destination(allocation.allocator());
        ASSERT_EQ(initialize_combat_scenario(destination, {}, {}).status, StepStatus::awaiting_input);
        const auto unchanged = checkpoint(destination).bytes;
        bool completed = false;
        for (std::size_t allowed = 0; allowed < 128; ++allowed) {
            allocation.fail_after = allocation.allocations + allowed;
            const auto result = restore(destination, bytes, scratch);
            if (result == RestoreStatus::restored) { completed = true; break; }
            ASSERT_EQ(result, RestoreStatus::resource_limit);
            EXPECT_EQ(checkpoint(destination).bytes, unchanged);
        }
        ASSERT_TRUE(completed);
        EXPECT_TRUE(valid_run(destination));
        EXPECT_EQ(destination, source);
    }
    EXPECT_EQ(allocation.outstanding, 0u);
}

TEST(CompleteStateClone, RecycledCreatureRetainsIndependentOverflowPowerStorage) {
    RunState state; Scratch scratch;
    ASSERT_EQ(initialize_combat_scenario(state, {}, {}).status, StepStatus::awaiting_input);
    for (int i = 0; i < 9; ++i)
        gameplay::apply_power(state, scratch, PowerId::strength, 1, true, false, {}, true, false, PowerInstance::Stacking::separate);
    const auto old = state.enemies().front();
    const auto replacement = state.creatures().create_at(2, {40, 40}, 4096);
    state.set_enemy_membership(state.enemy_handle(), false);
    state.set_enemy_membership(replacement, true);
    state.creatures().get(old)->clear();
    ASSERT_TRUE(state.creatures().erase(old));
    const auto reused = state.creatures().create_at(old.slot, {5, 5}, 4096);
    EXPECT_EQ(reused.slot, old.slot); EXPECT_NE(reused.generation, old.generation);
    ASSERT_TRUE(valid_run(state));
    EXPECT_TRUE(state.creatures().get(reused)->powers.overflowed());
    EXPECT_TRUE(state.creatures().get(reused)->powers.empty());
    RunState clone = state, restored;
    ASSERT_EQ(restore(restored, checkpoint(state).bytes, scratch), RestoreStatus::restored);
    EXPECT_EQ(clone, state); EXPECT_EQ(restored, state);
}

TEST(CompleteStateClone, MultiDomainCloneFailureNeverPublishesPartialOwnership) {
    RunState source;
    ASSERT_EQ(initialize_seeded_combat(source, {}).status, StepStatus::awaiting_input);
    source.visited().reserve(100);
    source.deck().reserve(100);
    source.execution().reserve(200);
    ASSERT_TRUE(valid_run(source));
    for (std::size_t allowed = 0; allowed <= 7; ++allowed) {
        AllocationCounter allocation;
        {
            RunState destination(allocation.allocator());
            ASSERT_EQ(initialize_run(destination, {}).status, StepStatus::awaiting_input);
            const auto unchanged = checkpoint(destination).bytes;
            allocation.fail_after = allocation.allocations + allowed;
            if (allowed < 7) {
                EXPECT_EQ(clone_into(destination, source), CloneStatus::resource_limit);
                EXPECT_EQ(checkpoint(destination).bytes, unchanged);
                EXPECT_TRUE(destination.overflow.empty());
                EXPECT_TRUE(destination.combat_overflow.empty());
            } else {
                EXPECT_EQ(clone_into(destination, source), CloneStatus::cloned); EXPECT_EQ(destination, source);
                destination.deck()[0].upgrade = 1;
                EXPECT_EQ(source.deck()[0].upgrade, 0u);
            }
        }
        EXPECT_EQ(allocation.outstanding, 0u);
    }
}

TEST(CompleteStateClone, PendingChoicePreservesSourceAfterUnreferencedCardRemoval) {
    const std::array deck{CardSpec{CardId::strike}, CardSpec{CardId::survivor}, CardSpec{CardId::defend}, CardSpec{CardId::bash}};
    CombatConfig config; config.deck = deck; config.enemy_hp = 40;
    RunState source; Scratch scratch;
    ASSERT_EQ(initialize_combat(source, config, scratch).status, StepStatus::awaiting_input);
    const auto find = [&](CardId id) {
        for (const auto card : source.zone(Zone::hand)) if (source.cards().get(card)->definition == id) return card;
        return CardHandle{};
    };
    const auto strike = find(CardId::strike), survivor = find(CardId::survivor), defend = find(CardId::defend), bash = find(CardId::bash);
    ASSERT_EQ(apply_combat(source, {source.data.boundary, CombatInputKind::play_card, strike, source.enemy_handle()}, scratch).status, StepStatus::awaiting_input);
    auto discard = source.zone(Zone::discard);
    discard.erase(std::find(discard.begin(), discard.end(), strike));
    ASSERT_TRUE(source.cards().erase(strike));
    ASSERT_TRUE(valid_combat(source)); // Export-only history no longer roots this slot.
    ASSERT_EQ(apply_combat(source, {source.data.boundary, CombatInputKind::play_card, survivor}, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(source.combat().phase, CombatPhase::discard_choice);
    source.execution().reserve(200);
    const auto encoded = checkpoint(source).bytes;
    RunState cloned, restored; clone_into(cloned, source);
    ASSERT_EQ(restore(restored, encoded, scratch), RestoreStatus::restored);
    const auto select = test::selection_input(source, defend);
    ASSERT_EQ(apply_selection(cloned, select, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(apply_selection(restored, select, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(cloned, restored); EXPECT_EQ(cloned.data.rng, restored.data.rng);
    EXPECT_FALSE(cloned.cards().get(strike)); EXPECT_EQ(checkpoint(source).bytes, encoded);
    ASSERT_EQ(apply_selection(source, test::selection_input(source, bash, {}), scratch).status, StepStatus::awaiting_input);
    EXPECT_NE(source, cloned);
}

TEST(CompleteStateClone, VictoryReleasesCompletedOverflowGraphButPreservesIdentityProgress) {
    const std::array<CardSpec, 200> deck{};
    CombatConfig config; config.deck = deck; config.enemy_hp = 1;
    RunState state; Scratch scratch;
    ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
    ASSERT_FALSE(state.combat_overflow.empty());
    const auto card = state.zone(Zone::hand).front();
    ASSERT_EQ(apply_combat(state, {state.data.boundary, CombatInputKind::play_card, card, state.enemy_handle()}, scratch).status, StepStatus::combat_won);
    EXPECT_TRUE(state.combat_overflow.empty()); EXPECT_TRUE(state.cards().slots().empty());
    EXPECT_EQ(state.cards().data.highest_generation, 1u);
    ASSERT_TRUE(valid_combat(state));
    RunState restored; ASSERT_EQ(restore(restored, checkpoint(state).bytes, scratch), RestoreStatus::restored);
    EXPECT_EQ(restored, state); EXPECT_TRUE(restored.combat_overflow.empty());
}

TEST(CompleteStateClone, SuspendedCountersAndCapturedOperandsSurviveIndependentFutures) {
    const std::array deck{CardSpec{CardId::survivor}, CardSpec{CardId::strike}, CardSpec{CardId::defend}};
    CombatConfig config; config.deck = deck; config.enemy_hp = 40;
    RunState source; Scratch scratch;
    ASSERT_EQ(initialize_combat(source, config, scratch).status, StepStatus::awaiting_input);
    const auto find = [&](CardId id) {
        for (auto card : source.zone(Zone::hand)) if (source.cards().get(card)->definition == id) return card;
        return CardHandle{};
    };
    const auto survivor = find(CardId::survivor), strike = find(CardId::strike), defend = find(CardId::defend);
    ASSERT_EQ(apply_combat(source, {source.data.boundary, CombatInputKind::play_card, survivor}, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(source.combat().counters.started_plays, 1u);
    EXPECT_EQ(source.combat().counters.finished_plays, 0u);
    ASSERT_EQ(source.execution().size(), 1u);
    EXPECT_EQ(std::get<CardProgramFrame>(source.execution().back()).energy, 1);
    EXPECT_EQ(std::get<CardProgramFrame>(source.execution().back()).target, source.data.player);
    const auto saved = checkpoint(source).bytes;
    auto branch = source; RunState restored;
    ASSERT_EQ(restore(restored, saved, scratch), RestoreStatus::restored);
    for (auto* state : {&branch, &restored}) {
        ASSERT_EQ(apply_selection(*state, test::selection_input(*state, defend), scratch).status, StepStatus::awaiting_input);
        EXPECT_TRUE(state->execution().empty());
        EXPECT_EQ(state->combat().counters.finished_plays, 1u);
        EXPECT_EQ(state->cards().get(survivor)->last_finished_player_turn, 1);
        ASSERT_EQ(apply_combat(*state, {state->data.boundary, CombatInputKind::play_card, strike, state->enemy_handle()}, scratch).status, StepStatus::awaiting_input);
        EXPECT_EQ(state->enemy().hp, 34);
        EXPECT_EQ(state->enemy().positive_unblocked_hits, 1u);
        EXPECT_EQ(state->combat().counters.started_this_turn, 2u);
        EXPECT_EQ(state->combat().counters.attacks_this_turn, 1u);
        ASSERT_EQ(apply_combat(*state, {state->data.boundary, CombatInputKind::end_turn}, scratch).status, StepStatus::awaiting_input);
        EXPECT_EQ(state->combat().counters.started_plays, 2u);
        EXPECT_EQ(state->combat().counters.finished_plays, 2u);
        EXPECT_EQ(state->combat().counters.started_this_turn, 0u);
        EXPECT_EQ(state->combat().counters.attacks_this_turn, 0u);
        EXPECT_EQ(state->cards().get(strike)->last_finished_player_turn, 1);
    }
    EXPECT_EQ(branch, restored);
    EXPECT_EQ(checkpoint(source).bytes, saved);
    ASSERT_EQ(apply_selection(source, test::selection_input(source, strike, {}), scratch).status, StepStatus::awaiting_input);
    EXPECT_NE(source, branch);
}

TEST(CompleteStateClone, LongCombatDoesNotAccumulateDiagnosticStorage) {
    const std::array deck{CardSpec{CardId::defend, true}, CardSpec{CardId::defend, true}, CardSpec{CardId::defend, true}};
    CombatConfig config; config.deck = deck;
    AllocationCounter allocation; RunState state(allocation.allocator()); Scratch scratch;
    ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
    const auto allocations = allocation.allocations;
    allocation.fail_after = allocations;
    const auto bytes = state.retained_bytes();
    for (unsigned turn = 0; turn < 100; ++turn) {
        while (!state.zone(Zone::hand).empty()) {
            const auto card = state.zone(Zone::hand).front();
            ASSERT_EQ(apply_combat(state, {state.data.boundary, CombatInputKind::play_card, card}, scratch).status, StepStatus::awaiting_input);
        }
        ASSERT_EQ(apply_combat(state, {state.data.boundary, CombatInputKind::end_turn}, scratch).status, StepStatus::awaiting_input);
        ASSERT_TRUE(valid_combat(state));
        EXPECT_EQ(state.retained_bytes(), bytes);
        EXPECT_TRUE(state.combat_overflow.empty());
        EXPECT_EQ(state.player().powers.size() + state.enemy().powers.size(), 1u);
    }
    EXPECT_EQ(state.combat().counters.finished_plays, 300u);
    EXPECT_EQ(allocation.allocations, allocations);
}

TEST(CompleteStateClone, InvalidStorageIdsCountsAndAliasingAreRejectedBeforeAccess) {
    RunState source; Scratch scratch;
    ASSERT_EQ(initialize_combat(source, {}, scratch).status, StepStatus::awaiting_input);
    auto invalid = source;
    invalid.combat().execution.overflow_id = 123;
    EXPECT_FALSE(valid_combat(invalid));
    invalid = source; invalid.combat().execution.count = UINT32_MAX;
    EXPECT_FALSE(valid_combat(invalid));
    invalid = source; invalid.execution().reserve(100);
    invalid.combat().continuation_cards.overflow_id = invalid.combat().execution.overflow_id;
    EXPECT_FALSE(valid_combat(invalid));
}
} // namespace
} // namespace sts2::engine
