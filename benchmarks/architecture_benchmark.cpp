#include "sts2/engine/action.hpp"
#include "../tests/support/continuation_fixture.hpp"
#include "../src/engine/mechanics/powers.hpp"
#include <stdexcept>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <new>

namespace { bool counting = false; std::uint64_t allocations = 0, checksum = 0; }
void* operator new(std::size_t n) {
    if (auto* p = std::malloc(n ? n : 1)) { if (counting) ++allocations; return p; }
    throw std::bad_alloc{};
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void* operator new(std::size_t n, std::align_val_t alignment) {
    void* p = nullptr;
    if (posix_memalign(&p, static_cast<std::size_t>(alignment), n ? n : 1)) throw std::bad_alloc{};
    if (counting) ++allocations;
    return p;
}
void* operator new[](std::size_t n, std::align_val_t a) { return ::operator new(n, a); }
void operator delete(void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }

using namespace sts2::engine;
void require(bool ok) { if (!ok) throw std::runtime_error("benchmark scenario failed"); }
void nested(RunState& state) {
    Scratch scratch;
    scenario::prepare_exhaust_shuffle(state, true);
    for (auto definition : {CardId::defend, CardId::burning_pact}) {
        CardHandle selected{};
        for (auto card : state.zone(Zone::hand))
            if (state.cards().get(card)->definition == definition) { selected = card; break; }
        require(apply_action(state, CombatInput{state.data.boundary, CombatInputKind::play_card, selected}, scratch).status == StepStatus::awaiting_input);
    }
    auto& combat = state;
    require(apply_action(state, CombatInput{state.data.boundary, CombatInputKind::select_card, combat.selection_candidates().front(), {}, combat.combat().selection.id}, scratch).status == StepStatus::awaiting_input);
    require(apply_action(state, CombatInput{state.data.boundary, CombatInputKind::confirm_selection, {}, {}, combat.combat().selection.id}, scratch).status == StepStatus::awaiting_input);
    require(!state.combat_overflow.empty());
}

// Legacy restrictions also describe simulator capability and operational limits.
// Keep these legal branches so both branching timings execute the same set.
bool offered_legal(Restriction restriction) {
    return restriction == Restriction::none || restriction == Restriction::unsupported_setup ||
        restriction == Restriction::capacity || restriction == Restriction::clock_exhausted;
}

// Consume action identities so enumeration timing includes constructing operands.
std::uint64_t action_checksum(const PlayerInput& input) {
    if (const auto* run = std::get_if<RunInput>(&input))
        return run->expected_boundary.value + static_cast<unsigned>(run->kind) + run->option + run->col + run->row;
    const auto& combat = std::get<CombatInput>(input);
    return combat.expected_boundary.value + static_cast<unsigned>(combat.kind) + combat.card.slot + combat.card.generation +
        combat.target.slot + combat.target.generation + combat.prompt.value;
}

template<class Function> void measure(const char* operation, std::uint64_t iterations, Function function) {
    allocations = 0;
    const auto start = std::chrono::steady_clock::now(); counting = true;
    for (std::uint64_t i = 0; i < iterations; ++i) function();
    counting = false;
    const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cout << " operation=" << operation << " iterations=" << iterations << " allocations=" << allocations
              << " ns_per_operation=" << seconds * 1e9 / iterations << " operations_per_second=" << iterations / seconds << '\n';
}
int main(int argc, char** argv) try {
    std::uint64_t iterations = 100000;
    if (argc == 2) { char* end = nullptr; iterations = std::strtoull(argv[1], &end, 10); if (!end || *end || !iterations || iterations > 100000000) return 1; }
    else if (argc > 2) return 1;
    std::cout << "run_data_bytes=" << sizeof(RunData) << " combat_data_bytes=" << sizeof(CombatData)
              << " card_instance_bytes=" << sizeof(CombatCard) << " execution_frame_bytes=" << sizeof(ExecutionFrame)
              << " creature_state_bytes=" << sizeof(CreatureState) << " power_instance_bytes=" << sizeof(PowerInstance) << '\n';
    for (int workload = 0; workload < 8; ++workload) {
        RunState baseline, branch; Scratch scratch;
        if (workload >= 5) {
            SeededConfig run_config; run_config.seed = "KERNELRUN14";
            require(initialize_run(baseline, run_config).status == StepStatus::awaiting_input);
            if (workload >= 6) {
                require(apply_action(baseline, RunInput{baseline.data.boundary, RunInputKind::choose_option, 0}, scratch).status == StepStatus::awaiting_input);
                if (workload == 7) {
                    std::vector<PlayerInput> travel;
                    for_each_legal_action(baseline, [&](const PlayerInput& input) { travel.push_back(input); });
                    require(!travel.empty());
                    require(apply_action(baseline, travel.front(), scratch).status == StepStatus::awaiting_input);
                    while (baseline.data.phase == RunPhase::combat) {
                        PlayerInput chosen = CombatInput{baseline.data.boundary, CombatInputKind::end_turn};
                        bool found = false;
                        for_each_legal_action(baseline, [&](const PlayerInput& input) {
                            if (!found && std::get<CombatInput>(input).kind == CombatInputKind::play_card && std::get<CombatInput>(input).target.present()) { chosen = input; found = true; }
                        });
                        require(apply_action_trusted(baseline, chosen, scratch).status == StepStatus::awaiting_input);
                    }
                    require(baseline.data.phase == RunPhase::rewards);
                }
            }
        }
        else if (workload == 4) {
            require(initialize_seeded_combat(baseline, {}).status == StepStatus::awaiting_input);
            auto& combat = baseline;
            using Stacking = PowerInstance::Stacking;
            for (int i = 0; i < 9; ++i)
                gameplay::apply_power(combat, scratch, PowerId::strength, 1, false, false, {}, true, false, Stacking::separate, combat.data.player);
            gameplay::apply_power(combat, scratch, PowerId::strength, 2, false, false, {}, true, false, Stacking::by_applier, combat.data.player);
            gameplay::apply_power(combat, scratch, PowerId::strength, 3, false, false, {}, true, true, Stacking::by_applier, combat.enemy_handle());
            gameplay::apply_power(combat, scratch, PowerId::strength, 4, false, false);
            require(combat.player().powers.overflowed());
            require(combat.player().power_instances(combat.overflow).size() == 12);
        }
        else if (workload == 3) nested(baseline);
        else if (workload == 2) {
            std::array<CardSpec, 200> deck;
            for (std::size_t i = 0; i < deck.size(); ++i) deck[i] = CardSpec{i % 2 ? CardId::strike : CardId::defend};
            require(initialize_combat_scenario(baseline, {}, deck).status == StepStatus::awaiting_input);
        }
        else {
            require(initialize_seeded_combat(baseline, {}).status == StepStatus::awaiting_input);
            if (workload == 1) for (int i = 0; i < 12; ++i) require(baseline.creatures().create_at(2 + i, {3, 3}, 4096).present());
        }
        require(valid_run(baseline));
        require(clone_into(branch, baseline) == CloneStatus::cloned);
        std::cout << "workload=" << (workload == 0 ? "supported_seeded_combat" : workload == 1 ? "controlled_creature_overflow" : workload == 2 ? "supported_cards_200_overflow" : workload == 3 ? "stratagem_pile_choice_overflow" : workload == 4 ? "controlled_power_instances_12_overflow" : workload == 5 ? "run_neow" : workload == 6 ? "run_travel" : "run_rewards")
                  << " player_power_instances=" << baseline.player().power_instances(baseline.overflow).size()
                  << " root_bytes=" << sizeof(RunState) << " retained_bytes=" << sizeof(RunState) + baseline.overflow.retained_bytes() + baseline.combat_overflow.retained_bytes() << '\n';
        measure("query_validate", iterations, [&] {
            RunActionSpaceView actions(baseline);
            for (std::size_t i = 0; i < actions.size(); ++i) {
                auto action = actions.at(i); require(action.has_value());
                checksum += static_cast<unsigned>(validate_action(baseline, action->input)) + 1;
            }
        });
        measure("integrity", iterations, [&] { require(valid_run(baseline)); checksum += baseline.data.boundary.value; });
        measure("legal_enumeration", iterations, [&] {
            for_each_legal_action(baseline, [&](const PlayerInput& input) { checksum += action_checksum(input); });
        });
        // Reusable consumer storage is search-owned and allocated before timing.
        std::vector<PlayerInput> legal;
        for_each_legal_action(baseline, [&](const PlayerInput& input) { legal.push_back(input); });
        require(!legal.empty());
        std::size_t supported = 0, legacy_legal = 0;
        const auto offered = get_actions(baseline);
        for (const auto& action : offered) {
            supported += action.restriction == Restriction::none;
            legacy_legal += offered_legal(action.restriction);
        }
        require(legacy_legal == legal.size());
        std::cout << " legal_actions=" << legal.size() << " offered_unrestricted_actions=" << supported << '\n';
        const PlayerInput step_input = workload < 5
            ? PlayerInput{workload == 3 ? CombatInput{baseline.data.boundary, CombatInputKind::select_card, baseline.selection_candidates().front(), {}, baseline.combat().selection.id}
                : CombatInput{baseline.data.boundary, CombatInputKind::end_turn}}
            : legal.front();
        for (bool trusted : {false, true}) {
            double seconds = 0; std::uint64_t total_allocations = 0;
            for (std::uint64_t i = 0; i < iterations; ++i) {
                require(clone_into(branch, baseline) == CloneStatus::cloned);
                allocations = 0; counting = true; const auto start = std::chrono::steady_clock::now();
                const auto result = trusted ? apply_action_trusted(branch, step_input, scratch) : apply_action(branch, step_input, scratch);
                seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(); counting = false;
                total_allocations += allocations;
                require(result.status == StepStatus::awaiting_input || result.status == StepStatus::unsupported_content);
                checksum += static_cast<unsigned>(result.status) + branch.data.boundary.value;
            }
            std::cout << " operation=" << (trusted ? "apply_trusted" : "apply_checked") << " iterations=" << iterations << " allocations=" << total_allocations << " ns_per_operation=" << seconds * 1e9 / iterations << '\n';
        }
        for (bool trusted : {false, true}) {
            measure(trusted ? "branching_trusted" : "branching_checked", iterations, [&] {
                const auto apply = [&](const PlayerInput& input) {
                    require(clone_into(branch, baseline) == CloneStatus::cloned);
                    const auto result = trusted ? apply_action_trusted(branch, input, scratch) : apply_action(branch, input, scratch);
                    require(result.status == StepStatus::awaiting_input || result.status == StepStatus::unsupported_content || result.reason == Restriction::unsupported_setup);
                    checksum += static_cast<unsigned>(result.status) + branch.data.boundary.value;
                };
                if (trusted) for_each_legal_action(baseline, apply);
                else {
                    const RunActionSpaceView offered(baseline);
                    for (std::size_t i = 0; i < offered.size(); ++i) {
                        const auto action = offered.at(i);
                        if (action && offered_legal(action->restriction)) apply(action->input);
                    }
                }
            });
        }
        measure("clone", iterations, [&] { require(clone_into(branch, baseline) == CloneStatus::cloned); checksum += branch.data.boundary.value; });

    }
    std::cout << "checksum=" << checksum << '\n';
} catch (const std::exception& error) { counting = false; std::cerr << error.what() << '\n'; return 2; }
