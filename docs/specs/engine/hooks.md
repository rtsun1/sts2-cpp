# Combat hooks

This is the module-placement contract for gameplay hooks and their suspendable
work. It clarifies [ADR 0006](../../adr/0006-use-a-data-oriented-engine-with-selective-continuations.md).
Use the root [glossary](../../../GLOSSARY.md) for hook, listener, handler, dispatch,
and continuation terminology.

## Module ownership

| Module | Owns |
| --- | --- |
| `src/engine/content/` | Behavior specific to cards, powers, relics, monsters, and other game objects; trigger conditions, modifier formulas, and content-specific completion effects |
| `src/engine/hooks/` | Generic hook entry points; candidate capture, native order and eligibility; listener iteration and selecting named content handlers; no operation-frame push/pop |
| `src/engine/mechanics/` | Reusable mutations and draw/exhaust/shuffle operation resumption; owning operation phases, hook progress, and frame lifecycle |
| `src/engine/execution/` | Generic frame dispatch, card programs, prompts/answers, and saved-progress validation |
| `include/sts2/state/` | Authoritative values, identities, operands, and progress needed to clone or resume gameplay |

These are ownership boundaries, not a prohibition on calls between folders.
Use explicit compiled functions between modules, as ADR 0006 requires.

- Mechanics calls generic hook entry points. Hooks links those calls to named
  content handlers; individual handlers do not own generic listener dispatch.
- Content owns an object's conditions and effects. For example, Joss Paper's
  threshold and counter reset belong in content. Hook eligibility means native
  membership/activity checks, distinct from those content-specific conditions.
- Content and mechanics may construct typed child frames and insert them with
  `state.execution.push_back`. There is no one-line scheduling wrapper. Record
  progress first, push at most one child, then yield to the interpreter.
- Operation resumers inspect and complete their own frames. Content handlers do
  not inspect, copy, or pop caller frames; they interpret only their supplied
  progress and operands. Hooks advances supplied listener ranges without owning
  an operation frame.
- A content handler may request a prompt through execution. Execution owns the
  prompt lifecycle and answer capture; content specifies candidate eligibility and
  selection bounds, then consumes the answer and applies its own effect.
- Saved state uses copyable values and identities. Temporary local pointers or
  references are allowed, but must not be retained as saved progress or used
  after the owning storage may relocate.
- Capture candidates from existing game state in native order. Do not introduce
  a separate listener registration pool, maintained registration-order list,
  mutable callback-registration table, or fixture-only gameplay handlers.

## Naming and placement examples

Generic `before_*` and `after_*` functions are hook entry points. Content handler
names identify the owning object, such as `artifact_after_power_application`,
`stratagem_after_shuffle`, or `joss_paper_after_card_exhausted`. Use "handler"
consistently rather than adding "reaction" as another name for the same concept.

For power application, the call path is:

```text
mechanics/powers.cpp: apply_power
  -> hooks/dispatch.cpp: after_power_application
  -> content/powers.cpp: artifact_after_power_application
```

Putting Artifact's implementation in content is correct; putting the generic
`after_power_application` entry point there is not. The same distinction applies
to `before_power_application`. Damage/block modifier formulas remain in content.

`resume_frame` only dispatches. Card programs live in execution; `resume_draw`,
`resume_exhaust_cards`, and `resume_shuffle` live in mechanics. Hooks owns
`capture_*_listeners` and `resume_listeners`; content owns resumable named handlers.
Synchronous content handlers can ignore progress. A handler that schedules a child
records its stage and returns yield. The return chain must not touch old execution
references after insertion, which can relocate storage.

## Review checks

When adding or moving a hook or handler, verify:

1. The generic entry point and handler selection are in hooks; the named effect
   and its formulas are in the owning content module.
2. Operations remain in mechanics; prompts remain in execution. Child frame
   construction belongs to its requester and does not drive the child inline.
3. Candidate capture preserves native order and type-specific eligibility without
   another registration system. Changed candidate membership does not silently
   replace the identities of an existing dispatch.
4. Saved progress advances before invoking child work, and local references are
   not used after that work may relocate storage.
5. Tests check state at suspended choices and resumed futures. Equal eventual
   outcomes do not establish correct intermediate counter or continuation state.

## Native evidence and supported scope

The retained decompilation under `build/combat-state-audit/decompiled/` comes from
assembly SHA-256 `0861bfa1df347538d932f22d580e75420f08082792eb914e53b4882764acdbe9`.
The [operation and hook continuation audit](frame-semantics-audit.md) extends this
evidence to per-card draw phases, Fiend Fire, content completions, and death hooks
that can detach choice work into a queued action. Its findings constrain both
separate and combined hook-frame designs; they do not select a representation.

These observations are static evidence, not a new runtime parity recording:

- `MegaCrit.Sts2.Core.Combat/CombatState.cs:411`, `IterateHookListeners`, builds a
  candidate list before yielding. For each ally then enemy it captures powers,
  followed by player inventory/cards or the monster. Powers retain application
  order; relics retain inventory order. Eligibility is checked at each yield.
- The same file's `Contains` checks a power's owner combat/activity, rather than
  requiring the power still to be in its owner's active power array. Relics have
  their own removal/activity check. Captured identities must not bind to newly
  applied instances of the same definition.
- `MegaCrit.Sts2.Core.Models.Powers/DarkEmbracePower.cs:38` awaits a draw after
  ordinary exhaust. `StratagemPower.cs:17` selects from the draw pile and adds
  selected cards to hand. These additions are not card-draw events.
  `CardSelectCmd.cs:489` orders draw-pile candidates by rarity/model ID for the
  selector path; the implemented definitions use that ordering without changing
  the underlying draw pile.
- `MegaCrit.Sts2.Core.Commands/CardPileCmd.cs:1047` awaits shuffle before the
  interrupted draw, and `:1131` awaits after-shuffle dispatch.
- `MegaCrit.Sts2.Core.Models.Cards/BurningPact.cs:24` awaits exhaust before its
  own draw. Thus Dark Embrace's exhaust draw, including Stratagem's choice,
  completes before Burning Pact's draw begins.

Only supported single-player handlers are captured: player Dark Embrace for
ordinary card exhaust, player Stratagem for shuffling, Centennial Puzzle for
player damage received, and the existing exhaust relics. Their candidates are derived from actual power/relic state and filtered
before capture to omit unsupported/no-op handlers. This does not implement all
native listener categories. Dark Embrace and Stratagem are available as diagnostic
power applications; their playable card definitions are not added by this change.
Ethereal turn-end exhaustion remains unsupported by the engine; Dark Embrace's
native ethereal deferral is consequently outside this slice.

## Execution frames

The execution stack stores six alternatives: `CardProgramFrame`, `DrawFrame`,
`ExhaustCardsFrame`, `ShuffleFrame`, `EnemyTurnFrame`, and `EnemyAttackFrame`. Each owns the unfinished operation it
names. Operation frames own both batch progress and current hook progress.
`SelectionFrame`, the old exhaust-hook-only `ExhaustFrame`, and
`RelicDrawCompletionFrame` have been removed.

Enemy turns capture ordered creature handles once, including their generations.
The combat resumer owns actor traversal and side completion; the mechanics
resumer owns one enemy-to-player attack and its received-damage dispatch.
Centennial Puzzle owns its used flag and three individually awaited one-card
draws. A nested Stratagem selection blocks damage completion and later enemies.
Next moves advance at the following normal player-side start. See the
[enemy-turn implementation contract](../../planning/enemy-turn-suspension.md).

`resume_frame` delegates to operation-specific functions. Each processes work
until completion, a child request, input, or failure. A parent records progress
before scheduling at most one child and yielding. Finishing a card program may
tail-delegate its final exhaust to a replacement frame at the same depth, so
stack-depth equality is not an identity/completion test.

Exhaust requests capture card identities in shared operand storage. Exhausting
one card finishes its listeners and their child work before advancing the card
cursor. Captured cards stay owned until the batch finishes. Draws instead retain
a remaining count and determine each next card after previous effects. Early
and normal draw hooks have distinct capture phases, but no draw listeners are
implemented in the current supported content.

`ShuffleFrame` owns rearrangement and its after-shuffle dispatch. Draw waits for
this child before trying the next card. No future Reboot/auto-play content is
implemented merely by making shuffle independently resumable.

`HandlerProgress` contains a byte-sized stage interpreted by content. Joss Paper
records its post-draw stage, requests one draw child, and performs its live-counter
modulo only when resumed. No separate completion entry is pushed. Stateless
handlers remain ordinary calls. More complicated future content can require saved
operands beyond this stage; such a layout change needs review.

Operation phase changes do not each consume a work unit. Selected exhausts are
charged per operand; a direct random-exhaust instruction covers its own single
mutation. Selection and effect instructions each consume a work unit, including
forced choices; their split can increase budgets relative to the prior combined
instructions. Shuffle retains the
former interrupted-draw charge only when listeners actually interrupted the old
path. A resumed tail-await handler adds no charge solely for returning again;
Joss Paper's actual reset retains its former completion charge.

The implemented ABI measurements are recorded in the
[layout and migration record](../../planning/operation-frame-refactor.md).

## Default frame scheduling and deferred optimization

Draw and exhaust operations use their frame path regardless of whether active
content could request input. There is no prediction-based synchronous fast path.
Primitive mutations such as moving a card, dealing damage, and gaining energy
remain compiled calls. Initial drawing is drained before initialization publishes
its first boundary and consumes the execution budget transactionally.

Effect summaries are a potential optimization lever, not an implemented feature.
A conservative summary could certify that an operation and every reachable trigger
will finish synchronously, allowing a direct path. A draw-count/pile-size check
alone is insufficient: draw-triggered damage can kill an enemy, triggering more
draws and eventually a shuffle choice. False "cannot suspend" results would be
correctness bugs; false "may suspend" results only retain frame bookkeeping.

Retain the single frame path until representative MCTS measurements justify an
alternative. Compare always scheduling frames against summary-guided direct
execution, including flag checks, summary maintenance, state size/copying cost,
frame dispatch, overflow allocations, and total rollout throughput. Workloads must
represent actual draw/exhaust frequencies, active trigger combinations, nesting,
choices, and cloning rates. Existing batching microbenchmarks do not establish
whether these summaries would pay for themselves.

The earlier always-scheduled six-frame baseline passed 275 release checks. Its
logs remain in `/tmp/always-frame-baseline/`; those are historical results rather
than verification of the current four-frame refactor.

## Saved dispatch progress

`captured_hook_listeners` is shared inline storage for dispatch candidates.
Each shuffle/exhaust continuation owns a contiguous range (`begin`, `count`) and
its next-listener `cursor`. Nested ranges append after parent ranges and are
released in stack order. Candidates contain content identity and an owner handle,
not pointers or callable objects. A captured power uses its live amount while
present. Before erasure, mechanics retain its final amount in any captured
candidate so a replacement power cannot change the old candidate's behavior.

`capture_*_listeners` returns a captured range to its operation owner. Generic
`resume_listeners` advances the active handler, yields to its child or prompt, and
only advances the listener cursor after completion. A stage is saved before child
work, so revisiting the same listener does not repeat its initial effect. Finishing
one dispatch releases its captured range before the next card's fresh capture.

Selection prompts store only an identity and bounds; candidates live in shared
selection storage. `record_selection` copies an ordered answer into continuation
operand storage and publishes its range in `CombatData::selected_cards`. The
requester immediately takes that range and clears the answer slot. Synchronous
consumers release the operands after applying their effect; a suspending consumer
transfers the range into its own frame before scheduling child work. Answers must
not remain in the shared slot at any published boundary, including nested prompts.

Card programs use a `request_choice` instruction followed by an effect instruction.
The request advances the instruction cursor before yielding; resumption starts at
the effect instruction. Empty and forced single-card choices use the same handoff
without publishing a prompt. Optional choices still require input. Each instruction
is atomic; an exhaust effect transfers its operands to `ExhaustCardsFrame`, which
owns hook suspension and cleanup. Both instructions consume execution budget; the
exhaust operation retains its per-card charges. No throughput claim is made.

Stratagem consumes the answer in its resumed content handler. Accepted cards move
to hand (or discard if full) synchronously in supported content, then the handler
finishes and the interrupted draw resumes. Validation derives selection bounds
from the waiting request instruction or captured Stratagem listener. Observation
similarly derives effect descriptions from the requester rather than saved prompt
metadata. Full native pile-change hooks remain outside this slice.

## Detached frame groups

`DetachmentBoundary` records an explicit pause-or-completion boundary through
`begin_detachable_frames`. The caller records its post-child progress first,
then pushes the complete child frame group. That group must own all of its
unfinished handler work; a boundary cannot leave the child's saved handler stage
in the caller. Hooks still do not push or pop operation frames. Existing supported
draw/exhaust/shuffle/damage-received hooks retain full-await semantics. No existing
hook has been relabeled detachable without native evidence. Death-hook content,
including Gremlin Horn and Speedster, remains outside the implemented content.

A selection request does not publish `card_choice`. At an explicit boundary,
execution parks the unfinished suffix in `pending_choices`, advances the remaining
frames, and presents queued work in FIFO order at an input boundary or join. An
ordinary blocking choice behind older queued work parks its entire active group.
The active queue entry remains allocated until its frames finish. Further choices
from that entry retain its queue position. Each restored entry records its own
base depth and previous active slot. Completion returns to the queue decision
before running frames below the base; equal-depth tail replacement within the
group does not count as completion.

Each group owns a LIFO operand arena. Parking copies the suffix's cards/listeners
and rebases frame ranges, leaving the caller's ranges in place. Activation swaps
arenas into the existing hot fields; finishing swaps the parent's arena back.
No per-access arena lookup or general range allocator is added. Captured power
removal updates visit queued and parked arenas too. Boundaries require disjoint
parent/child range ownership. Crossing a boundary with a retained reference into
another group's operand range is invalid.

`create_frame_join` supplies an identity stored by the owning operation;
`begin_detachable_frames` associates children with it, and `wait_for_frame_join`
marks the owner's synchronization checkpoint. The owner records its next phase
before waiting. Join lookup includes active entries as well as parked ones.
Older queued choices are processed before joined children, while unrelated later
entries do not block a completed join. There is no global join registry or general
task scheduler. Native action eligibility distinctions are not implemented by
this machinery.

### Candidate timing and native evidence

The retained pinned decompilation for assembly SHA-256
`0861bfa1df347538d932f22d580e75420f08082792eb914e53b4882764acdbe9`
shows `CardSelectCmd.FromCombatPile` awaiting `SignalPlayerChoiceBegun` before
reading `pile.Cards` (lines 453–478), and `FromHand` capturing its hand list after
the same await (lines 817–834). `HookPlayerChoiceContext` awaits its generated
action's `ExecutionStartedTask` before returning from that signal (lines 177–205).
These are static findings, not a live multi-choice recording.

Selection requests retain their zone, `SelectionEligibility`, bounds and prompt
identity as copyable values. `request_selection` only records this description;
it never collects candidates or resolves automatic selections. `present_selection`
is the sole owner of those tasks for both ordinary and queued requests. It uses
the recorded zone and shared eligibility predicate directly, with no refresh flag,
saved callback or inference from the active frame. Queued entries retain that
request and their frames, not candidate snapshots. The bounds used by Stratagem are captured before the pause,
so later power changes do not alter an outstanding request. Empty selections and
candidate counts at or below the minimum resolve automatically without publishing
another input boundary or advancing the action clock. Ordinary automatic selections also resolve through presentation before the next
effect instruction runs. New eligibility rules extend the stored eligibility
representation and its shared predicate; checked boundaries verify that the
request descriptor agrees with its owning content/program.

### Storage and validation

All group state is trivially copyable, with independently owned overflow following
existing `InlineSequence` patterns. The common operand accessors are unchanged.
Checked boundaries validate queued requests, frame ranges, active chains and
nested storage; trusted execution adds no state/legality validator. Existing
budget, capacity, identity-exhaustion and allocation-failure handling remain.
Combat completion cancels queued/active work. Native diagnostic JSON export
explicitly rejects detached groups until their mapping is implemented.

The layout probe with GCC/C++23 reports `CombatData` at 5,132 bytes (previously
4,596), `RunState` at 35,008 bytes (previously 34,472), and `ExecutionFrame`
unchanged at 48 bytes. These measurements describe state footprint, not throughput.

## Compatibility and verification

Run checkpoint revision 23 embeds combat payload revision 6. They encode all six
frame alternatives, the captured enemy order, AI completion flags, Centennial
Puzzle state, embedded listener ranges, and handler stages. Earlier run/payload
revisions are rejected. Historical revision numbers below describe prior layouts.
The new powers and Stratagem's combat-pile prompt have no complete native diagnostic
JSON mapping yet and are explicitly rejected by the exporter. Existing native recordings are unchanged.

`HookDispatch` tests cover order, stacked choices, repeat shuffles, final captured
power values, and full-hand movement. Public run tests exercise the real
Burning Pact/Dark Embrace/Stratagem path (or Joss Paper) across clone/restore,
alternative choices, retained operands, allocation failures, and execution budgets.
Large-pile variants exercise actual card/selection storage overflow instead of
inventing arbitrary nested shuffle handlers. Allocation tests are correctness
checks, not throughput measurements. No benchmark was run for this refactor.

Historical hook-extraction verification: `cmake --build --preset debug --parallel 2` completed with no
compiler warnings; `ctest --preset debug --parallel 2` passed all 267 tests,
including 181 engine checks and 23 replay-parity checks. `git diff --check` passed.
The layout-only compiler probe reported `CombatData` at 4,712 bytes (previously
4,800), `EffectFrame` unchanged at 48 bytes, and `HookListener` at 20 bytes.
These sizes and allocation regressions do not establish a throughput improvement.


Historical four-operation verification: all 279 release checks pass, and differential
budget/trace and sanitizer probes pass. See the
[verification report](architecture-verification.md#operation-owned-hook-progress-2026-11-01)
for normal/LTO timing results, allocation observations, and scope limits.

Selection handoff verification (2026-11-07): the Debug build completed without
compiler warnings. All 321 registered checks passed across the full-suite run and
a targeted recorder rerun. The full run initially passed 320 checks; the recorder
check needed the pinned `sts2-cli` submodule initialized in the worktree, then
passed without source changes. All 235 engine checks and 23 replay-parity checks
passed in the full run. `git diff --check` passed. No benchmark was run.

Detached-frame verification (2026-11-09): Debug and defensive-validation builds
completed without compiler warnings. The full Debug suite passed all 337 checks,
including 251 engine checks and 23 replay-parity checks. A final checked/trusted
queued-answer equivalence test was then added; the rebuilt focused suite passed
all 17 pending-choice checks. The defensive build passed all 73 selected
pending-choice, hook, operation-frame, card-choice, trusted-action and enemy-turn
checks. `git diff --check` passed. Logs are retained at
`/tmp/detached-full-tests.log`, `/tmp/detached-pending-final.log`, and
`/tmp/detached-defensive-final.log`. The tests exercise explicit boundaries around
real supported operations; they do not establish native death-hook scheduling
parity. No rollout-throughput benchmark was run.

Selection presentation ownership verification (2026-11-09): the Debug full suite
passed all 341 checks (255 engine and 23 replay-parity checks). All 76 selected
checks passed with defensive validation enabled. The focused selection/queue/hook/
frame run passed all 61 checks, including regressions for request-only recording,
live eligibility at presentation without frame inspection, automatic resolution,
and descriptor validation/checkpoint roundtrip. Both builds completed without
compiler warnings; `git diff --check` passed. Logs are retained under
`/tmp/selection-ownership-{build,full,focused,defensive-build,defensive}.log`.
