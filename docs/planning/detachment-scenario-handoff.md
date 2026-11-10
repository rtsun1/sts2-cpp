# Gameplay detachment scenario handoff

Implemented and native-traced on `feat/detached-execution`: a lethal direct card
hit with a surviving enemy, Gremlin Horn, Stratagem, an empty draw pile and enough
discarded cards to require selection. Dagger Throw adds a second outstanding
choice and establishes the executing caller's priority over its queued hook.

## Native evidence

The pinned game is `0.111.0`, assembly SHA-256
`0861bfa1df347538d932f22d580e75420f08082792eb914e53b4882764acdbe9`,
MVID `73b63ee0-6c0a-47bb-b0d1-b21f6d94222e`. Both native scenarios ran twice in
independently initialized Godot 4.5.1 Mono processes with identical observations.

- [Retained trace](../../tests/fixtures/death-detachment-native-v1.json): ordered
  card identities, candidates, bounds, energy, enemy membership/HP/powers, caller
  completion and the retained native hook task/action at each boundary. Includes
  assembly, resource-pack, Godot, source and log hashes, plus .NET runtime identity.
- [Probe and reproduction instructions](../../tools/oracle/native_detachment_probe/README.md).
- Runner: `tools/oracle/check_native_detachment.py`. Raw logs and copied sources:
  `/tmp/detachment-native/final-trace-v2/`.
- Static frame semantics: [pinned audit](../specs/engine/frame-semantics-audit.md).
  Fresh focused reconstruction is retained in `/tmp/detachment-native/decompiled/`.

The trace uses actual native `PlayCardAction`, `ActionQueueSet`,
`HookPlayerChoiceContext`, and `GenericHookGameAction`. The local-only input
provider runs after native choice-begun signalling and live candidate capture;
choice-ended signalling and action resumption also execute. Native `TestMode` and
`AutoSlayerCheck` suppress presentation work; presentation nodes are absent. UI
callbacks, network `SyncLocalChoice`, wall-clock and whole-game startup are outside
this trace. The ordinary global test selector bypasses the required signalling
and is not used here.

## Observed caller and choice timing

Both scenarios use seed `ORACLECHECK01`, source card at deck index 0, nine Defends,
Gremlin Horn, Stratagem 1 and two beetles at HP 6/40 and 40/40. Strike starts alone
in hand with nine Defends discarded. Dagger Throw also retains Defend 1 in hand,
leaving eight discarded cards. Native initial shuffle/draw execute normally
before this declared pile setup.

| Native boundary | Caller complete | Hand | Draw | Discard | Source |
| --- | --- | ---: | ---: | ---: | --- |
| Strike: Horn/Stratagem choice | yes | 0 | 9 | 1 | discard |
| Strike: resumed completion | yes | 2 | 7 | 1 | discard |
| Dagger Throw: caller discard choice | no | 2 | 7 | 0 | play |
| Dagger Throw: Horn/Stratagem choice | yes | 1 | 7 | 2 | discard |
| Dagger Throw: resumed completion | yes | 3 | 5 | 2 | discard |

Energy is 3 throughout these observations: the attack pays once and Horn grants
once. The dead enemy has left native combat membership and has no powers at every
published choice. Its surviving teammate remains at HP 40. Stratagem's selected
card moves to hand before Horn's interrupted draw adds one more card.

Dagger Throw's ordinary discard choice belongs to the currently executing card
action. That caller keeps priority until it finishes; the queued Horn action then
presents its choice using the live draw pile. This runtime finding corrected the
previous assumption that any older queued hook should precede an ordinary caller
prompt. Each scenario enqueues exactly one native Horn action; its retained task
and action are unfinished at the choices and complete at the final observation.
Synthetic two-detached-group tests separately exercise queue storage and FIFO.

## Implementation and acceptance coverage

[Module ownership](../specs/engine/hooks.md#module-ownership) and
[validation ownership](../specs/engine/public-run-loop.md#validation-ownership)
remain in force.

- Content owns Horn's opposing-side condition, energy and draw. Hooks owns captured
  listeners, eligibility, order and named handler selection. Mechanics owns
  `DeathFrame`, cleanup and the detachment boundary. The listener cursor advances
  before opening that boundary. Horn has no post-draw effect, so the draw owns all
  remaining child work; no extra Horn frame, generic hook frame or join is needed.
- Public actions drive the Strike and Dagger Throw scenarios without fixture
  handlers or test-only boundary marking. `PublicStatesMatchThePinnedNativeQueueTrace`
  compares every declared native boundary, ordered pile/candidate identities,
  selection bounds, energy, membership, HP, powers and completion state.
- Both Dagger Throw choices are cloned/checkpointed. Every legal answer is resumed
  through checked and trusted execution. Completing the caller preserves Horn's
  saved draw/shuffle operands; later child completion repeats no caller effect.
- Budget sweeps before and after selection, large-pile allocation failures,
  energy-capacity failure and inline-storage allocation checks pass. The common
  Strike action, clone and answer require no overflow allocation.
- A final lethal Dagger Throw ends combat without drawing or presenting a hand
  prompt. Cancellation of an already active production-created group is also
  tested at the lifecycle seam; no supported independent single-player effect
  currently ends combat while that prompt is open.
- The active queued entry retains its position, base depth and previous active
  entry until completion. Ordinary caller choices keep their executing group's
  priority; FIFO governs activation of queued groups after caller completion or
  an explicit join. Generic two-detached-group tests mark both boundaries
  explicitly and preserve independent arenas through clone/checkpoint.

## Scope and compatibility

Horn with Charon's Ashes is explicitly rejected at checked setup/support
boundaries. Speedster, multi-target and simultaneous-death scheduling remain
outside this slice. Existing no-Horn death behavior is unchanged. Dead enemies
removed by cleanup cannot receive post-hit debuffs; living retained identities
keep their existing targeting behavior. The last dead enemy remains the terminal
representation's identity anchor until teardown.

Native diagnostic JSON still rejects detached groups. The retained scheduling
trace supplies the requested evidence without a full-state JSON parity claim.
After integration with generalized enemies, run checkpoint revision is 25 and
combat payload revision is 8; older revisions
are rejected. The content fingerprint also records ordinary caller priority.
No rollout-throughput measurement has been established.

## Validation

Native: Strike and Dagger Throw each passed two independent runs; build completed
without warnings and runtime logs contain no gameplay/runtime errors. Both actual
queued Horn actions and retained tasks completed once. Baseline real hand UI
callbacks also passed independently in `/tmp/detachment-native/baseline-callback/`;
those UI checks are separate from this trace's local-only input seam.

Feature-branch C++ verification passed:

- `cmake --build --preset debug --parallel 2`: successful, no compiler warnings.
- `ctest --preset debug --parallel 2`: all **355 checks** passed, including
  269 engine checks and 23 replay-parity checks.
- The defensive build passed all **103 selected checks**, covering the original
  handoff's card choices, operation frames, hooks, trusted actions and enemy turns,
  plus multi-creature, retained-target and new detachment scenarios.
- All 39 focused queue/hook/detachment checks passed; the pinned native comparison
  checks every declared boundary from both traces.
- `python3.12 -m py_compile tools/oracle/check_native_detachment.py` and
  `git diff --check` passed.

Logs are retained under `/tmp/detachment-native/`:
`final-build.log`, `final-full.log`, `final-defensive-build.log`,
`final-defensive-expanded.log`, and `final-focused.log`.

## Main integration verification

Merged with the generalized-enemy and MCTS work already on `main`. Both enemy
content and detached-group tests remain registered. Shrink's synchronous applier-
death handler is preserved before Horn's detachable listener; an integration
regression checks that Shrink is gone at Horn's published choice and that checked
and trusted restored futures agree.

The combined layout uses run revision 25 / combat revision 8 and a content
fingerprint containing both enemy and detachment semantics. This rejects the
incompatible layouts from either pre-merge branch.

The merged Debug build passed all **380 checks**, including 281 engine checks,
23 replay-parity checks and 13 MCTS/benchmark-label compatibility checks. All
**115 selected defensive checks** passed. The 42 focused enemy/queue/detachment
checks passed. Both builds completed without compiler warnings, and
`git diff --check` passed. The layout probe remains `CombatData=5136`,
`RunState=35016`, `ExecutionFrame=48`; no throughput claim follows.

Integration logs: `/tmp/detachment-merge-{build,mcts-build,full-tests,mcts-tests,focused,defensive-build,defensive-tests}.log`.
