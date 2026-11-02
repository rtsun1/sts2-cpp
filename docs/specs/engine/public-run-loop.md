# Public C++ access to the bounded run loop

Issue [#16](https://github.com/TonyinBio/sts2-cpp/issues/16) exposes the existing
Neow / Golden Pearl / first Shrinker / reward / map-return mechanics. This adds no
second gameplay implementation. `include/sts2/observation/run.hpp` supplies typed
observations. `include/sts2/engine/action.hpp` supplies action enumeration, validation,
application and automatic progression to the next decision or terminal result.
`sts2_play [seed]` is an example consumer (default seed `KERNELRUN14`). Enter an
advertised action number, or `quit`. Unsupported visible choices remain explicit.

```cpp
#include "sts2/engine/action.hpp"
#include "sts2/observation/run.hpp"

RunState run;
Scratch scratch;
initialize_run(run, config);
auto view = observe(run);
auto offered = get_actions(run);
auto result = apply_action(run, offered[index].input, scratch);
```

Observations own their projected vectors. Seed strings and combat hand spans borrow
run storage and expire on mutation. Immutable model strings live in the rules tables.
The build and semantic contract revision are exposed; this makes no binary ABI or
wire compatibility promise. Public observations contain no RNG, future rooms, future
maps, actual draw sequence, weighted pools, hidden AI or execution frames. Native JSON
export and historical `Session` replay remain separate privileged diagnostics.

Permanent cards retain their existing instance IDs; combat cards retain handles
across hand/pile movement. These identities refer to different domains. Map topology
and route coordinates are stable for this generated map. Options, reward entries and
potion slots retain their native offered indices, scoped by the expected boundary.
They do not identify hidden pool entries. Inventory IDs are bounded-loop identities;
this API does not certify replacement/removal identity policies for future mechanics.

Combat observations include card costs, upgrades, previews, targets (through typed
inputs), HP, energy, visible Shrink/Vulnerable and intent values. Discard/exhaust
card descriptors preserve visible pile order. Passive draw inspection uses the native
rarity/model comparator and existing .NET sort implementation, with no backing draw
sequence exposed. All four executable combat definitions currently have Basic rarity.
Differently modified equal-key ties retain the native comparator behavior; unrestricted
permutation equivalence is not claimed. Permanent deck metadata and reward model descriptors are
available even when acquisition or later play is unsupported. Exact English and
broader model preview support are not certified. Text is generated on demand. Starter/reward cards, Golden Pearl, Burning Blood and
Potion of Binding have source-backed effect summaries; other definitions return an
explicit unsupported-description status independently of acquisition/play support.

Reward cards are revealed only at the card-reward prompt. The prompt declares an
exact-one immediate submission with a separate Skip alternative; Skip returns to
the reward list, while leaving rewards discards unclaimed rewards. These semantics
are distinct from cancellation or zero-card selection. This slice introduces no new
multi-select/confirmation prompt. Existing atomic Survivor selection remains bounded
support; it is not certification of native partial hand-selection callbacks.

## Historical correspondence and clocks

`apply_input(RunState&, const PlayerInput&, Scratch&)` dispatches run and combat
inputs. It and historical replay retain their original boundaries and bytes.
`apply_action` executes the submitted input, then calls the same `proceed` transitions
for finished Neow, finished combat, and resolved rewards until the next real choice.
Each underlying transition retains its existing action-clock increment and boundary
increment. Thus one public input can span two historical actions; boundary numbers
can jump. Intermediate historical diagnostic boundaries remain replayable.
`advance_to_decision` at an input boundary is pure. Repeated queries and validation use
no authoritative RNG or clock. Travel validation checks visible adjacency only; unrevealed encounter capability is
checked by execution before mutation, so querying routes cannot identify hidden rooms.
Run preparation separates gameplay legality from capability checks and execution operands.
Both execution paths use the same transitions. Unsupported content and operational
failure may leave partially modified state; discard that branch. Checked rejection
of invalid inputs remains before mutation.

The public replay regression runs all nine existing dense/sparse loop fixtures and the three fresh native captures.
Recording aliases are related to independently generated card identities without
reading expected checkpoints. Every input is validated by the same kernel. Historical
checkpoints use the existing exact comparator, including intermediate forced stops;
public execution is compared at corresponding completed decisions, with complete
state and clock equality. Every public input is also resumed from an independent
clone and binary restore. Expected snapshots never supply transition data.

## Evidence and limits

The [model/case ledger](../../../data/evidence/public-run-loop.json) assigns the five
audited roots to #16 and retains implementation, reference and interaction status
separately, including pending wider branches. Its source inventory is the published
[#13 acceptance inventory](https://github.com/TonyinBio/sts2-cpp/tree/8baebc33321681bc170ebf9aef2e51bdfed043e0/docs/planning/ironclad-acceptance),
read with [#8's milestone clarification](https://github.com/TonyinBio/sts2-cpp/issues/8#issuecomment-6389882951).
It is an ownership/evidence ledger, not certification of every authored power caller.

Projection assertions use independent literal/source relationships: Neow offer order,
base/upgraded starter values, damage before Vulnerable, multiplicative Shrink and
Vulnerable, reveal timing, and paired hidden queues/draw orders. Relevant native
anchors are `Neow.GenerateInitialOptions`, `NPlayerHand` settled hand order,
`NCardPileScreen.OnPileContentsChanged`, `Bash.OnPlay/OnUpgrade`,
`DefendIronclad.OnPlay/OnUpgrade`, `StrikeIronclad.OnPlay/OnUpgrade`,
`ShrinkPower.ModifyDamageMultiplicative`, `VulnerablePower.ModifyDamageMultiplicative`
and the reward consumers indexed by the published visibility audit
[#11](https://github.com/TonyinBio/sts2-cpp/issues/11#issuecomment-6388814691).
The native DLL hash is recorded in the ledger. Static mappings and whole-state replay
are separate evidence; no filtered checkpoint is described as an independent public
capture. There are no new native input shapes requiring a partial-selector probe in
this slice. Native partial/multi-selection instrumentation remains required with its
first implementing feature.

Unsupported Neow effects and unsupported reward card play remain explicit. Complete
native reward pools are unchanged. [Consecutive combat ownership](consecutive-run-loop.md)
now retains prior encounter graphs and verifies a second supported fight through a
declared native scenario operation. Natural travel still checks the generated queue;
other encounters remain unsupported. This does not establish natural victory,
finite-duration Shrink callers, general listener ordering, or the remainder of #15. Known unsupported input is rejected
before mutation. Failure after an accepted input invalidates the simulation rather
than publishing a valid partial result. Restore/reinitialize is required to recover.

Fresh native evidence from `check_run_combat.py` independently self-replayed victory
(23 actions / 24 boundaries), death (12 / 13), and interface skip (19 / 20).
The three `*-public` fixture directories retain current-toolchain provenance separately
from the unchanged historical fixture bytes. [Verification metadata](../../../data/evidence/public-run-loop-native.json)
records their hashes. Direct live replay of the historical fixture correctly stopped
at the adapter provenance guard; its expected adapter hash differs from today's tool.
No provenance guard was relaxed, and fresh captures were not substituted for historical
expectations. Both generations are exercised by C++ replay.

Validation for this change: `cmake --build --preset debug -j 4` succeeded;
`ctest --preset debug -j 4 --output-on-failure` passed all 158 tests. The earlier
focused engine/replay run passed all 68 checks before the additional native corpus
and review regressions were added. `sts2_play` was smoke-tested through Neow to map.
Standards review found no rule violations and one optional base-preview deduplication
suggestion; spec review reported no remaining findings after the inspection, text,
ledger, reward-cost and hidden-travel fixes.

## Legal enumeration and trusted execution

Issue [#63](https://github.com/rtsun1/sts2-cpp/issues/63) adds
`for_each_legal_action(state, consumer)` and `apply_action_trusted(state, input, scratch)`.
The consumer is a statically dispatched callable receiving each `PlayerInput` in one
sequential traversal. It controls storage and must not mutate the source during
traversal. There is no action cache in complete state. Enumeration order is not a
compatibility guarantee.

Validate the root with `valid_run`, then enumerate each node's actions. Retain an
action only for that exact state or an independent exact copy. Successful trusted
actions preserve the state needed for later trusted decisions; a failed branch
must be discarded. Reusing inputs across different states, invalid complete state,
or illegal inputs violates the trusted contract; rejection is not guaranteed.

```cpp
if (!valid_run(root)) throw std::invalid_argument("invalid search root");
std::vector<PlayerInput> actions; // Search-owned storage; any consumer is allowed.
for_each_legal_action(root, [&](const PlayerInput& input) { actions.push_back(input); });
for (const auto& input : actions) {
    RunState branch = root; // Independent overflow ownership when needed.
    Scratch scratch;
    const auto result = apply_action_trusted(branch, input, scratch);
    // Retain successful decisions/terminal results; discard execution failures.
}
```

Gameplay legality does not imply implemented content or available resources.
Enumeration includes legal Neow and reward choices even when acquisition is
unsupported, and legal travel regardless of hidden encounter capability. Checked
execution reports unsupported content, clock/capacity limits and budget failures.
Trusted performance execution additionally requires the caller to guarantee that
content and reachable effects are implemented. Unsupported behavior violates that
contract; rejection or a correct result is not guaranteed. Use a defensive build
when that guarantee has not been established.
The legacy offered views retain their restrictions and may therefore expose a
smaller unrestricted set than legal enumeration in capability-limited scenarios.
They remain useful for checked external consumers.

Configure `-DSTS2_DEFENSIVE_VALIDATION=ON` to compile internal invariant diagnostics.
Invariant checks reuse validators, print the failed condition and source location,
and abort on an internal invariant failure. Defensive builds also retain trusted
content-support guards, which report `unsupported_content` rather than executing
unimplemented effects. They do not depend on `NDEBUG`; checked input
validation remains active with the option off. The performance configuration omits
internal diagnostic evaluation and repeated support guards, using the same
transitions. Budget, capacity, clock, and allocation-failure handling remain active.
Checked APIs retain support guards in both builds, including their automatic
transitions. Required definition/pool lookups and failures to construct or dispatch
missing behavior remain explicit; omitting support guards does not implement it. Diagnostic checks at
continuation entry use storage/power/modifier invariants, since an unfinished
operation is not a published decision state.

Measurements and verification for this change are recorded in
[the action-path report](action-path-performance.md). These are supported-scenario
microbenchmarks and synthetic branching, not actual MCTS throughput or proof of
full-game correctness. Those measurements predate the support-assuming trusted
performance mode; no benchmarks were run for that follow-up.
