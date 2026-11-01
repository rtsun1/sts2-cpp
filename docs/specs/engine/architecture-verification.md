# Integrated architecture verification

This records #56/#62 architecture evidence against integration commit
`3b422bb975e55eab6dd52259585219c26d69a784` (runtime fixes) merged with
benchmark commit `72886eb` at measured/tested source revision
`04c79645029b570e00bc0878093fdd35ec9307da`. It covers supported gameplay only. No production
content, old content-ticket completion requirement, native fixture replacement or
binary-to-JSON checkpoint migration is introduced. The current binary revisions
are combat 18 and run 16 after card-store changes and the
[real-content hook refactor](hooks.md).
Earlier revisions are rejected; the measurements below describe the recorded
source revision before these storage changes.

## Behavioral evidence

The integrated `engine_test` public-interface tests in `tests/unit/run_test.cpp`
exercise initialization, starting decisions, map/rewards, consecutive scenario
encounters and complete binary checkpoints. Same and alternate cloned/restored
futures compare complete state, including all RNG words and counters. Existing
ordered card-choice and native replay suites preserve their supported comparison
surfaces and original fixture bytes.

| Guarantee | Regression evidence |
| --- | --- |
| Canonical creatures, retained operands and generation-safe reuse | `CreatureOverflowCloneAndRestoreKeepIndependentBranches`, `DeadCreatureRemainsUntilCombatResolutionReleasesItsOperands`, `RecycledGenerationsAndExhaustionRemainSafeAcrossCombats` |
| Permanent/combat card isolation and layered costs | `PermanentUpgradeTargetsOriginAcrossDeckReorderingAndBranches`, `CombatUpgradeDoesNotChangePersistentDeck`, `CostLayerOverflowClonesIndependently` |
| Ordered separate/per-applier powers and retained creature targets | `DistinctAndPerApplierPowersSurviveOverflowCloneAndRestore`, `PerApplierIdentityAndArtifactRemainOnRetainedCreature`, `CapturedCreatureTargetSurvivesMembershipReplacementAcrossNestedPrompt` |
| Captured listeners and nested return order | `NestedShuffleChoiceResumesDrawAndParentExactlyOnceAfterRestore`, `LargeStratagemPileChoiceKeepsIndependentSameAndAlternativeFutures`, `NestedRelicDrawDefersItsCounterResetUntilTheChildChoiceFinishes` |
| Parent combat/reward return and cleanup | `ParentInteractionReturnsAfterChildCombatAndRewardsExactlyOnce` |
| Transactional restore and resource failure | `OverflowingNestedCloneAndRestoreFailuresKeepDestinationTransactional`, `CorruptionCompatibilityAndMalformedPayloadsAreTransactional`, `CombinedCreatureAndPowerOverflowRestoreReacquiresCanonicalRecords` |
| Pure borrowed traversal/direct validation, including overflow | `DirectActionValidationDoesNotCloneOverflowState`, `BorrowedRunActionsTraverseOverflowWithoutAllocationAndExpireAfterMutation` |
| Hidden information, invalid/restricted/stale actions and operational failure | `HiddenQueuesAndDrawOrderDoNotAffectVisibleChoicesOrIds`, `HiddenEncounterSupportIsNotDisclosedByTravelQueries`, `MalformedUnsupportedAndStaleActionsArePureAndFailuresInvalidate` |
| Worker-local complete state/scratch with shared immutable rules | `WorkersShareOnlyImmutableRulesAndProduceIdenticalCompleteStates`, `ConcurrentOverflowBranchesOwnStateAndScratchIndependently` |

The historical controlled shuffle handlers described below have been replaced by
real power handlers; see [combat hooks](hooks.md). Parent interactions and extra
retained creatures remain architecture fixtures. Native diagnostic export still
rejects unmapped Stratagem pile prompts (`StratagemPilePromptRejectsUnimplementedNativeDiagnosticMapping`).
The benchmark results below describe the historical source and synthetic workload,
not the replacement. No benchmark or live oracle capture was performed for the
hook refactor.

## Reproducible release measurement

Run from the repository root:

```sh
cmake -S . -B /tmp/sts2-architecture-release \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/usr/bin/g++ \
  -DBUILD_TESTING=OFF -DSTS2_BUILD_JSON_IO=OFF \
  -DSTS2_BUILD_PARITY_TOOLS=OFF -DSTS2_BUILD_BENCHMARKS=ON
cmake --build /tmp/sts2-architecture-release -j4
/tmp/sts2-architecture-release/architecture_benchmark 10000
/tmp/sts2-architecture-release/kernel_benchmark 10000
```

Measured 2026-10-29 using GCC 11.4.0, CMake Release defaults (`-O3 -DNDEBUG`),
x86-64 Linux 5.15/WSL2 on AMD Ryzen 7 3700X. Other integration tests ran
concurrently; these are one-run descriptive measurements, not an isolated CPU
comparison, performance gate, percentile estimate or capacity recommendation.

Each workload owns its `RunState` and `Scratch`; rules are shared immutable data.
The supported workload initializes seeded combat with default configuration:
seed `ORACLECHECK01`, `CHARACTER.IRONCLAD`, ascension 0, pinned build
`0.111.0+41cef1ea`, rules revision 1. The pinned assembly SHA-256 is
`0861bfa1df347538d932f22d580e75420f08082792eb914e53b4882764acdbe9`
(`game_digest` in `include/sts2/content/cards.hpp`). Rules source/provenance is
fixed by measured source revision `04c79645029b570e00bc0878093fdd35ec9307da`; the generated definition tables are
unchanged.
Creature overflow adds twelve controlled retained creatures. The 200-card workload
uses supported Strike/Defend definitions in a controlled large deck. Nested overflow uses
Burning Pact plus seven controlled shuffle listeners and suspends at the nested
choice. Controlled power overflow adds nine separate Strength instances,
two distinct per-applier instances (player and enemy appliers), and one default
by-type instance to the player: twelve ordered instances in total. No production
power policy or new content is introduced by this fixture. Initial setup is outside measurement. No observation text, JSON or codec
work is timed. The existing combat `kernel_benchmark` remains unchanged.

`query_validate` is one complete indexed action traversal plus validation of every
offered input (including restrictions), rather than one indexed lookup. `clone`
is complete owning `clone_into`, including destination overflow replacement.
`step` is one public `apply_action`: end turn from the seeded/large-deck/power opening or selection
of the first nested-choice candidate. Each iteration resets from its baseline
outside step timing/counting. It measures repeated representative transitions,
not complete-run throughput or naturally generated unsupported encounters.

Global ordinary, array and aligned C++ allocation operators count allocations in
each measured region, including engine default overflow allocation. The counter
does not interpose arbitrary C `malloc` calls; engine storage uses C++ allocation.
Retained bytes are `sizeof(RunState)` plus both owners' retained bytes, excluding
allocator bookkeeping, stack scratch, code and shared immutable rules.

| Workload | Root bytes | Retained bytes | Query ns / allocations total | Clone ns / allocations total | Step ns / allocations total | Steps/s |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Supported seeded combat | 33,472 | 33,472 | 56,207 / 0 | 511 / 0 | 1,007 / 0 | 993,527 |
| Controlled creature overflow | 33,472 | 38,112 | 57,901 / 0 | 715 / 30,000 | 1,716 / 0 | 582,705 |
| Supported cards, controlled 200-card deck | 33,472 | 60,864 | 1,622,030 / 0 | 1,832 / 120,000 | 2,394 / 0 | 417,631 |
| Controlled nested overflow | 33,472 | 35,456 | 25,845 / 0 | 883 / 60,000 | 5,170 / 0 | 193,415 |
| Controlled twelve-power overflow | 33,472 | 34,000 | 64,543 / 0 | 589 / 20,000 | 1,600 / 10,000 | 625,179 |

Every row uses 10,000 iterations per operation; checksum was 490,000.
`sizeof(RunData)` is 33,376, `CombatData` 4,000, `CombatCard` (then named `CardInstance`) 36,
`EffectFrame` 56, `CreatureState` 272 and `PowerInstance` 40 bytes on this compiler/ABI.
Common cloning uses the inline complete-record memcpy with no allocation. Overflow
cloning independently owns its retained buffers and has an explicit allocation
cost. Allocation-free traversal does not imply constant-time validation: complete
state validity checks remain observable in query timing. The twelve-power end turn
allocates once per transition when Shrinker's move adds
another power beyond the independently cloned overflow buffer's retained capacity.
The common, creature, large-deck and nested measured steps allocate nothing; this
is not a claim that every exceptional transition is allocation-free. Root size and query cost
are observations for future tuning; this report selects no alternative layout.

The unchanged isolated combat benchmark completed 10,000 battles / 130,000
transitions in 0.219658 seconds (591,828 transitions/s), including clone reset,
with a 5,160-byte root/retained state, zero warmed allocations and checksum 950,000.
It is a separate standalone-combat workload, not the public complete-run root.

Logs: `/tmp/sts2-architecture-release-{configure,build,results}.log` and
`/tmp/sts2-architecture-kernel-results.log`.

## Deferred choices and scope

Dense/sparse powers, exceptional-payload placement and inline capacities, dispatch
specialization and partial-run cloning remain deferred. Representative larger
content distributions, cache behavior, multithread throughput and comparison to
`sts2-cli` have not been measured here. No unmeasured winner is selected.

The earlier full-game file/class sketch is historical design guidance wherever it
conflicts with ADR 0006 or the implemented ownership/execution contract.
`implementation-status.md`, `inline-storage.md` and the actual public APIs describe
the supported implementation. Full-game content and victory acceptance remain
separate; architecture completion does not close or rewrite their tickets.

## Verification execution

Both final Release and Debug builds passed with no compiler warnings in their
captured logs. The final registered suite and benchmark results refer to the
measured/tested revision above.

`ctest --test-dir /tmp/sts2-final-debug -j4 --output-on-failure` passed all
243 registered tests in 166.57 seconds, including 155 engine checks and 23
replay-parity checks. Complete output is retained in
`/tmp/sts2-final-debug-tests.log`; build logs are
`/tmp/sts2-final-debug-{configure,build}.log`. The Release architecture benchmark
completed all five workloads at 10,000 iterations, and the unchanged kernel
benchmark completed 10,000 battles, both with exit status zero.

The preliminary full-suite attempt exposed a worktree setup issue: the pinned
`sts2-cli` submodule had not been populated. It was restored from the existing local
pinned checkout without changing fixture bytes before final verification.

Initial standards review reported no findings. Spec review identified power
identity and captured-creature-target gaps, fixed in `13f0a38`. Final correctness
review identified the combined creature/power overflow restore lifetime bug, fixed
in `3b422bb` with a deterministic poisoned-retired-buffer regression demonstrated
failing before the fix and passing afterward. Reviewers confirmed no remaining
blockers in their focused rechecks.


## Explicit execution frames and batching (2026-10-31)

These measurements precede the subsequent always-scheduled draw/exhaust policy.
They establish results for batching with the former synchronous fast paths, not
the performance of the current scheduling policy.

The baseline for this change is the working tree with the real hook implementation
already present, before the frame refactor. The structural-only stage replaces
`EffectFrame` and its nested payload with a direct `ExecutionFrame` variant and
separate resume handlers. The batched stage loops inside each handler while
preserving per-work-unit budget charges and yielding to child work or input.
No new frame nesting is introduced.

On GCC 11.4.0/x86-64, frame size changed from 48 to 32 bytes, `CombatData` from
4,712 to 4,648 bytes, `RunData` from 34,576 to 34,512 bytes, and standalone
`CombatState` from 5,872 to 5,808 bytes. These are ABI-specific measurements.
Combat checkpoint revision 19 stores only the active frame alternative and
rejects revision 18; the enclosing run format remains revision 16.

Both release and debug builds succeeded. `ctest --preset release` and
`ctest --preset debug` each passed all 272 tests. Five new `ExecutionFrames`
checks cover batched card completion, draw/completion budget charges, budget
exhaustion, yielding to a shuffle prompt, and a final selection replacing its
own frame with a child at the same depth. Existing tests cover nested effects,
clone/restore, retained identities, allocation failures, and recorded parity.
`git diff --check` passed. These checks do not establish correctness for content
outside the supported scope.

Performance runs used GCC 11.4.0, `-O3 -DNDEBUG -ffp-contract=off`, with separate
`-flto -fno-fat-lto-objects` builds for the structural and batched stages. No
builds or tests ran alongside the final timing samples. Kernel results use five
samples of 100,000 battles each, alternating variant order. Every kernel sample
had 1,300,000 transitions, checksum 9,500,000, and zero warmed allocations.
The original baseline was not rebuilt with LTO; the LTO comparison isolates
batching after the structural refactor.

| Version | Median kernel transitions/s | Sample range |
| --- | ---: | ---: |
| before | 562,540 | 529,974–589,231 |
| structural | 547,765 | 517,963–570,737 |
| batched | 559,146 | 543,802–580,325 |
| structural-lto | 646,961 | 637,046–656,981 |
| batched-lto | 686,475 | 666,245–689,932 |

Architecture benchmark median step times (ns); 30 iterations per sample, three samples:

| Workload | Before | Structural | Batched | Structural LTO | Batched LTO |
| --- | ---: | ---: | ---: | ---: | ---: |
| supported_seeded_combat | 1,347 | 1,288 | 1,569 | 1,084 | 1,125 |
| controlled_creature_overflow | 1,981 | 1,842 | 1,952 | 1,778 | 1,782 |
| supported_cards_200_overflow | 2,484 | 2,440 | 2,515 | 2,057 | 2,167 |
| stratagem_pile_choice_overflow | 27,064 | 29,321 | 27,897 | 30,529 | 23,375 |
| controlled_power_instances_12_overflow | 1,846 | 1,824 | 1,767 | 1,661 | 1,596 |

The final non-LTO kernel median is 0.6% below the original, with overlapping
sample ranges; this does not establish a regression or improvement. Batching
increased the LTO kernel median throughput by 6.1% relative to the structural-only
stage, with nonoverlapping observed sample ranges. Short architecture samples
are noisier (including cold samples), although the nested Stratagem step was
consistently faster with LTO batching in these runs. The other architecture end-turn
workloads do not enter suspendable frame processing; their timing differences
should not be attributed to batching without further investigation. These results
support retaining batching, not a universal performance claim or a guarantee
against regressions on other workloads or compilers.

Raw logs, stage source snapshots, saved benchmark executables, scripts, and JSON
samples are under `/tmp/frame-refactor/`. The final samples are
`final-kernel-samples.json` and `final-architecture-samples.json`; test logs are
`release-tests.log` and `debug-tests.log`. Reproduce the normal workloads with
`kernel_benchmark 100000` and `architecture_benchmark 30` from a release build.
The isolated LTO build used `CMAKE_INTERPROCEDURAL_OPTIMIZATION=ON`; no repository
build preset was changed.


## Operation-owned hook progress (2026-11-01)

The completed refactor uses four operation alternatives: card program, draw,
exhaust cards, and shuffle. Hook ranges and resumable handler stages live inside
operations; selection-resolution and relic-completion alternatives are removed.
Content constructs child frames directly. Operation resumers live in mechanics,
card programs and prompts in execution, and listener capture/dispatch in hooks.
The [implementation record](../../planning/operation-frame-refactor.md) describes
ownership and compatibility choices.

Actual GCC 11.4.0/x86-64 sizes match the approved prototype: payloads 28/28/28/20
bytes, `ExecutionFrame` 32, `CombatData` 4,648, `RunData` 34,512, standalone
`CombatState` 5,808, and `RunState` 34,608 bytes. All remain unchanged in size from
the six-alternative baseline; saved data is trivially copyable. Binary combat
checkpoints are now revision 20 and reject revision 19. Fixture/reference versions
were not changed.

Validation:

- Release build and all 279 CTest checks passed after the final code change.
- Focused regressions cover per-card listener recapture after child work,
  operation/handler corruption, last-operand ownership, budget accounting, and
  child insertion relocating the execution stack.
- A differential probe ran eleven scenarios at budgets 0–40 (451 setups), yielding
  1,229 action observations. Baseline and refactor output matched exactly for
  statuses/reasons, charged steps, trace hashes, selected counters, zones, and RNG.
  The cases include nested draws/choices and Joss Paper reaching its threshold
  after a lethal exhaust listener, when its draw child is omitted.
- The same probe and a forced execution-storage relocation probe passed with
  AddressSanitizer and UndefinedBehaviorSanitizer. Leak detection was disabled;
  this was not a sanitizer run of the entire CTest suite.
- `git diff --check` passed. These are bounded checks, not proof of full-game parity.

The comparison baseline was the staged, tested six-frame implementation on top of
`a30387f`, extracted through the Git index before this refactor. Its staged patch
SHA-256 is `b833855f1544ef0941279bb35a588ec6fab99ad8cedadcb6e554d91f63b5b9a1`.
Differential probe source SHA-256: `6b4985ba2a2a8f5c778a35b26920d9956edba856989f186d7c5af77961e7c397`.
Matching output SHA-256: `c5bf004061bff339818edde8ee985658600b7268ec54b1e3eca82de917eb7a36`.

Performance samples used GCC 11.4.0 release builds with `-O3 -DNDEBUG` and
`-ffp-contract=off`; separate LTO builds used `CMAKE_INTERPROCEDURAL_OPTIMIZATION=ON`.
No builds/tests ran alongside final measurements. Variant order alternated.
Kernel results use five samples of 100,000 battles / 1,300,000 transitions each;
all had checksum 9,500,000 and zero warmed allocations. Architecture results use
three samples of 30 iterations. Joss Paper results use five samples of 10,000
iterations, with clone reset excluded from prompt-execution timing.

| Build | Median kernel transitions/s | Range |
| --- | ---: | ---: |
| before | 572,150 | 549,147–614,947 |
| after | 565,192 | 533,678–578,643 |
| before_lto | 727,962 | 709,107–737,774 |
| after_lto | 724,839 | 713,608–745,353 |

| Standalone Joss Paper choice workload | Before | After | Before LTO | After LTO |
| --- | ---: | ---: | ---: | ---: |
| reach_joss_choice ns | 2,450.8 | 2,307.1 | 2,005.0 | 2,003.8 |
| reach_joss_choice allocations/iteration | 2.0 | 0.0 | 2.0 | 0.0 |
| clone_joss_choice ns | 132.2 | 102.5 | 135.6 | 102.7 |
| clone_joss_choice allocations/iteration | 2.0 | 0.0 | 2.0 | 0.0 |

| Architecture step median, ns | Before | After | Before LTO | After LTO |
| --- | ---: | ---: | ---: | ---: |
| supported_seeded_combat | 1,491 | 1,613 | 1,179 | 1,185 |
| controlled_creature_overflow | 2,346 | 2,390 | 2,013 | 2,066 |
| supported_cards_200_overflow | 3,116 | 3,459 | 2,763 | 3,240 |
| stratagem_pile_choice_overflow | 30,493 | 28,894 | 24,417 | 21,219 |
| controlled_power_instances_12_overflow | 2,372 | 2,224 | 1,782 | 1,804 |

The isolated kernel medians are 1.2% lower without LTO and 0.4% lower with LTO,
with overlapping observed ranges. These samples do not establish a kernel
speedup or a repeatable regression. Empty draw-hook phases advance within the
same iteration; repeatedly dispatching empty phases had caused an earlier loss.

The short architecture samples included a slower large-deck LTO step median.
A follow-up removed the preceding query/clone timing sections and collected five
samples of 10,000 step iterations per workload, still excluding clone-reset time:

| Step-only median, ns (10,000 iterations/sample) | Before | After | Before LTO | After LTO |
| --- | ---: | ---: | ---: | ---: |
| supported_seeded_combat | 1,316.7 | 1,436.8 | 1,152.3 | 1,059.5 |
| controlled_creature_overflow | 2,148.3 | 2,325.6 | 1,959.2 | 1,876.3 |
| supported_cards_200_overflow | 2,773.6 | 3,098.1 | 2,479.3 | 2,399.1 |
| stratagem_pile_choice_overflow | 28,733.6 | 28,840.4 | 24,797.2 | 21,861.1 |
| controlled_power_instances_12_overflow | 2,007.5 | 2,084.1 | 1,708.6 | 1,688.8 |

The slower large-deck LTO result did not persist in that longer step-only check.
LTO step-only medians were lower across the five controlled cases. Non-LTO
step-only medians were higher by roughly 0.4–12%; this refactor is not a universal
performance win, and default release presets have not been changed to enable LTO.

At the standalone Joss Paper/Stratagem choice, live frames fell from five to four,
so the operation fits the existing four inline slots. Measured retained state fell
from 6,112 to 5,808 bytes. Both reaching that prompt and cloning it changed from
two allocation calls per iteration to zero. LTO prompt-execution time was nearly
unchanged; standalone prompt-clone median fell from about 136 to 103 ns. This
bounded standalone case is not a full-run MCTS workload or a general clone-speed
claim.

Raw samples, comparison source, build/test logs, sanitizer logs, and reproduction
scripts are under `/tmp/sts2-operation-refactor/`: `performance-samples.json`,
`step-only-samples.json`, `budget_probe.cpp`, `budget-{before,after}.txt`,
`final-release-tests.log`, and `final-sanitized-*`. Future optimization decisions
still need representative MCTS operation/choice/cloning distributions.
