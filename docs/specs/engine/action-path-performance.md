# Action-path measurements and verification

Implementation evidence for [issue #63](https://github.com/rtsun1/sts2-cpp/issues/63),
measured 2026-11-02 at commit `3fcfc7f`, before the follow-up that omits trusted
support guards in performance builds. These are historical measurements, not
measurements of the updated mode. No benchmarks were run for that follow-up.

This report measures supported examples and synthetic branching;
there is no MCTS runner and no whole-simulation throughput claim.

## Configuration and method

GNU g++ 11.4.0, release `-O3 -DNDEBUG -ffp-contract=off -std=c++23`, no LTO,
`STS2_DEFENSIVE_VALIDATION=OFF`. AMD Ryzen 7 3700X, 16 logical CPUs, Linux x86_64
under Microsoft virtualization. One benchmark thread; no affinity, cache flushing,
or power-policy controls, and background load is uncontrolled. Three independent
process samples, each with 100 iterations per scenario; values below are medians
of sample means. These small samples are diagnostic evidence, not confidence intervals.
Final comparison timing followed builds/tests, rather than running alongside them.

The baseline is commit `70b269f795218e45e305f00c72ad3ba058ee5840`'s
`architecture_benchmark` (standalone target built before engine changes). Its
`query_validate` walks indexed offered actions and explicitly calls `validate_action`
for each. `step` applies end-turn, or a prompt selection, after an untimed reset clone.
The updated harness retains those query measurements, and separately measures one
`valid_run`, direct enumeration consuming action identities, checked/trusted
application, clone reset, and enumerate/clone/apply branching. Branching uses the same
legal identities in both paths, including unsupported Neow options; legacy restrictions
are separated into gameplay versus capability/resource reasons. Printed branch counts
make the distinction from unrestricted legacy offers explicit. No historical action
ordering change or representation-specific speedup is claimed.

Allocation counters cover ordinary and aligned global `new`/`new[]`, including overflow
owners. Application timing and allocations exclude reset clones. Branching includes
clones, query allocations and transitions. Clone timings and counts are separate.
Setup and output are outside timers. Repeated clones target reusable destination
storage; retained overflow still requires independently owned copies. Some run travel
allocations construct the temporary combat deck and are not rollback copies.

After the initial baseline, provisional measurement goals were zero allocations for
direct enumeration in the supported scenarios and at least a 10x reduction from
query-plus-validation to direct enumeration for the 200-card example. Both hold here.
Those operations deliberately do different validation work; these goals do not imply
an end-to-end speedup target. Per-application and branching results are reported
separately to avoid transferring cost between operations unnoticed.

Raw per-sample timings/allocation counts, source fingerprints and binary hashes are
retained in [report.json](../../planning/action-path-measurements/report.json).
Reproduce the current measurements with `cmake --build --preset release` followed by
three separate invocations of `./build/release/architecture_benchmark 100`. Build the
baseline standalone target from the pinned parent commit in a separate checkout
using matching compiler/diagnostic settings to reproduce the before measurement.

## Median results

Times below are microseconds per operation. Application uses one identical action
on independent reset copies; branching is one full legal expansion, not one branch.

| Scenario | Before query + validation | Direct legal enumeration | Before apply | Checked apply | Trusted apply |
| --- | ---: | ---: | ---: | ---: | ---: |
| `supported_seeded_combat` | 51.384 | 0.053 | 0.883 | 0.588 | 0.329 |
| `controlled_creature_overflow` | 61.676 | 0.070 | 1.881 | 1.065 | 0.379 |
| `supported_cards_200_overflow` | 1714.220 | 0.054 | 2.876 | 1.495 | 0.698 |
| `stratagem_pile_choice_overflow` | 59832.200 | 0.273 | 23.480 | 20.622 | 0.670 |
| `controlled_power_instances_12_overflow` | 54.711 | 0.055 | 1.533 | 1.000 | 0.540 |
| `run_neow` | — | 0.005 | — | 0.144 | 0.074 |
| `run_travel` | — | 0.009 | — | 2.228 | 2.163 |
| `run_rewards` | — | 0.007 | — | 0.115 | 0.048 |

Run scenarios use `KERNELRUN14`: Neow choice, legal first travel, and gold reward
following the supported first combat. Combat scenarios retain the existing
`ORACLECHECK01` seed. The large prompt uses real Burning Pact / Dark Embrace /
Stratagem continuations over 200 cards; selected state and suspended work are
included. Controlled creature/power overflow examples do not add gameplay support.
Before run timings are unavailable in the old harness; their columns remain blank.

| Scenario | Integrity validation | Clone | Checked branching | Trusted branching |
| --- | ---: | ---: | ---: | ---: |
| `supported_seeded_combat` | 5.971 | 0.499 | 14.420 | 4.093 |
| `controlled_creature_overflow` | 6.538 | 0.638 | 22.540 | 5.142 |
| `supported_cards_200_overflow` | 220.739 | 1.502 | 247.133 | 13.235 |
| `stratagem_pile_choice_overflow` | 247.794 | 1.654 | 8805.010 | 482.773 |
| `controlled_power_instances_12_overflow` | 6.488 | 0.548 | 18.649 | 4.994 |
| `run_neow` | 5.211 | 0.503 | 7.075 | 1.608 |
| `run_travel` | 5.260 | 0.503 | 11.326 | 5.299 |
| `run_rewards` | 5.291 | 0.547 | 7.924 | 2.430 |

Direct enumeration allocated zero times in all eight scenarios. Complete-state
layout remains unchanged (`RunState` 34,456 bytes, `RunData` 34,360 bytes). Ordinary
cloning and combat application were allocation-free in these samples; overflow
clones retain their ownership costs. The twelve-power application scenario still
allocates for existing power-turn snapshot work. Travel still allocates during
combat setup; the report does not claim allocation-free run execution. Some
per-operation timings are tens of nanoseconds and sensitive to timer/compiler effects.

Enumeration omits repeated indexed scans and state-integrity validation by contract.
The isolated integrity measurement helps attribute that difference; it is not an
additive decomposition of every old query or every simulator step. Checked execution
also removes repeated continuation/storage validation, while trusted execution
retains capability, clock, capacity and work-budget checks. Ordinary application
gains are much smaller than query gains. Passing equivalence tests does not establish
all-game correctness, and no full native parity coverage was added by this optimization.

## Verification

- Final release build: exit 0; `ctest --preset release --parallel 2`: **307/307 passed**.
- Earlier debug engine label: **216/216 passed**, before the last five regressions.
- Defensive release engine executable: **197/197 passed** with internal diagnostics
  enabled, followed by **71/71 relevant tests passed** after the final initialization
  validation change. The nine public-boundary regressions pass with diagnostics both
  enabled and disabled. Existing deterministic replay expectations pass in each build.
- The first new equivalence test failed to link before trusted execution existed.
  Later capability-status and invalid-phase regressions failed before their fixes.
- `git diff --check`: exit 0. Release warnings about uninitialized `index` in existing
  run RNG setup also appeared in the baseline build; this change adds no warning.

The public tests compare unordered action identities, detect duplicates, and compare
complete successful states after checked/trusted application on independent copies.
They cover ordinary/overflow combat, ordinary/nested/large prompts, discard and
automatic-selection prompts, confirmation/deselection, run Neow/travel/rewards,
hidden unsupported destinations, unsupported transported combat modifiers,
allocation failure and execution budgets. Invalid-input purity tests remain in place.
Failed execution states are discarded rather than compared for rollback contents.

## Standards

No documented standard violations were found in the independent fresh-context review.
One nonblocking heuristic finding remains: direct legal generation and legacy offered
traversal require coordinated maintenance. Action-set comparisons across supported
phases guard that risk; their different legality/capability contracts remain explicit.

## Spec

The independent fresh-context review found two issues: a dropped checked invalid-phase
guard, and unequal Neow branching sets. Both were fixed with a regression or explicit
matched-set measurement, and the reviewer rechecked the changes with no remaining
actionable spec concern. Unsupported combat modifiers also now produce the explicit
`unsupported_content` status through both APIs.

Review totals: Standards 0 rule violations / 1 low maintenance concern; Spec 2 findings
resolved / 0 remaining. The learning checkpoint remains for the owner to complete.

## Support-assuming mode follow-up

The owner subsequently approved omitting redundant trusted support guards from
performance builds, retaining them with `STS2_DEFENSIVE_VALIDATION=ON`, and keeping
execution limits and checked-API support handling. This supersedes the earlier
contract that trusted performance execution always reports unsupported content.
Unsupported content or reachable effects now violate its caller preconditions.
No benchmarks were run for this follow-up, at the owner's request; all measurements
above and the raw JSON describe `3fcfc7f` and must not be presented as updated results.

The performance path omits the combat card-modifier scan and compile-time-specializes
run preparation, combat configuration checks, and reward capability guards. Checked
automatic transitions explicitly retain support guards without repeating input
legality validation. Definition/pool lookup and operand construction remain necessary;
missing definitions can still fail explicitly. Execution budget, capacity, clock,
and allocation-failure handling remain active. The benchmark consumer is restricted
to implemented seeded Neow branches so future runs satisfy the stronger preconditions.
The indexed `.at()` interfaces were retained.

Verification: release build and **308/308 CTest checks passed**; defensive engine
executable **198/198 passed**, including ten public action-path tests in each mode.
The additional regression verifies checked automatic reward support rejection.
Compiled-object inspection found zero `supported_card_modifiers` call references in
the performance `apply_combat_trusted` entry and one in its defensive counterpart.
`git diff --check` passed. Independent fresh-context Standards and Spec reviews
reported no actionable findings for this follow-up.
