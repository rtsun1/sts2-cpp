# Combat hooks

This is the module-placement contract for gameplay hooks and their suspendable
work. It clarifies [ADR 0006](../../adr/0006-use-a-data-oriented-engine-with-selective-continuations.md).
Use the root [glossary](../../../GLOSSARY.md) for hook, listener, handler, dispatch,
and continuation terminology.

## Module ownership

| Module | Owns |
| --- | --- |
| `src/engine/content/` | Behavior specific to cards, powers, relics, monsters, and other game objects; trigger conditions, modifier formulas, and content-specific completion effects |
| `src/engine/hooks/` | Generic hook entry points; candidate capture, native order and eligibility; selecting named content handlers; creating, advancing, and releasing hook-dispatch continuations |
| `src/engine/mechanics/` | Reusable state-changing operations such as drawing, moving cards, damage, and power application; invoking hooks at the relevant transition points |
| `src/engine/execution/` | Instructions, prompts, deferred-operation scheduling, and execution-frame lifecycle; driving saved work until it finishes or needs input |
| `include/sts2/state/` | Authoritative values, identities, operands, and progress needed to clone or resume gameplay |

These are ownership boundaries, not a prohibition on calls between folders.
Use explicit compiled functions between modules, as ADR 0006 requires.

- Mechanics calls generic hook entry points. Hooks links those calls to named
  content handlers; individual handlers do not own generic listener dispatch.
- Content owns an object's conditions and effects. For example, Joss Paper's
  threshold and counter reset belong in content. Hook eligibility means native
  membership/activity checks, distinct from those content-specific conditions.
- Content and mechanics request deferred operations through execution functions.
  They must not inspect, copy, push, or pop execution frames themselves. Hooks
  manages its own dispatch frames; execution drives them through `resume_*`.
- A content handler may request a prompt through execution. Execution owns the
  prompt lifecycle and reusable selection resolution; content specifies the
  requested operation and constraints.
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

`resume_*` advances saved hook dispatch by at most one candidate. `defer_*`
functions in execution schedule unfinished work. A synchronous content completion
such as `joss_paper_after_draw` updates the object's state; execution then removes
the completion frame. A content callback must not pop its caller's frame.

## Review checks

When adding or moving a hook or handler, verify:

1. The generic entry point and handler selection are in hooks; the named effect
   and its formulas are in the owning content module.
2. Reusable mutations remain in mechanics; prompt and deferred-operation frame
   management are requested through execution-owned functions.
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
ordinary card exhaust, player Stratagem for shuffling, and the existing exhaust
relics. Their candidates are derived from actual power/relic state and filtered
before capture to omit unsupported/no-op handlers. This does not implement all
native listener categories. Dark Embrace and Stratagem are available as diagnostic
power applications; their playable card definitions are not added by this change.
Ethereal turn-end exhaustion remains unsupported by the engine; Dark Embrace's
native ethereal deferral is consequently outside this slice.

## Execution frames

The execution stack stores `ExecutionFrame`, a variant of `CardProgramFrame`,
`DrawFrame`, `ShuffleFrame`, `SelectionFrame`, `ExhaustFrame`, and
`RelicDrawCompletionFrame`. Each alternative owns its operation's operands and
progress. Only `CardProgramFrame` stores the source card, play identity, energy,
target, and instruction position; deferred operations do not copy this context.

`resume_frame` dispatches to separate operation handlers. A handler processes
work until its frame finishes, schedules child work, needs input, or fails.
Handlers charge the execution budget per instruction, drawn card, selection
operand, or hook-listener iteration, including the existing completion checks.
Batching does not change stack nesting or child-before-parent ordering.
Progress advances before an effect can schedule children; frame references must
not be used after storage may relocate. A final selection operand releases its
frame before running the effect, so child work can replace it at the same depth.

## Default frame scheduling and deferred optimization

Drawing always schedules a `DrawFrame` when there is draw work. Exhaust-hook
notifications always use an `ExhaustFrame` when captured listeners exist. Joss
Paper always schedules its completion after its child draw. There is no separate
synchronous path selected by inspecting active powers or predicting trigger chains.
Zero-work requests and empty listener lists need no frame. Primitive mutations
such as moving a card, dealing damage, or gaining energy remain direct calls.
The initial hand draw is drained through the execution driver before initialization
publishes its first decision boundary; it also consumes the execution budget.
End-turn and card-effect draws use the same scheduled path.

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

Verification of the always-scheduled baseline: release build succeeded; all 32
focused frame/hook/card-choice checks and all 275 release CTest checks passed.
Three added regressions cover no-Stratagem scheduling, Joss Paper completion
ordering, and transactional initial-draw budget failure. Logs are under
`/tmp/always-frame-baseline/`. This policy has not been measured on MCTS workloads.

## Saved dispatch progress

`captured_hook_listeners` is shared inline storage for dispatch candidates.
Each shuffle/exhaust continuation owns a contiguous range (`begin`, `count`) and
its next-listener `cursor`. Nested ranges append after parent ranges and are
released in stack order. Candidates contain content identity and an owner handle,
not pointers or callable objects. A captured power uses its live amount while
present. Before erasure, mechanics retain its final amount in any captured
candidate so a replacement power cannot change the old candidate's behavior.

`after_*` starts dispatch by pushing a frame when candidates exist. `resume_*`
processes candidates until completion, child work, input, or failure, copying each
candidate and advancing the saved cursor before content can grow the stack.
Stratagem's handler constructs a draw-pile selection. The dispatch saves the required selection count and marks itself
waiting; the interpreter returns when phase becomes `card_choice`. Selecting the
required cards moves them to hand (or discard if hand is full) synchronously,
then resumes dispatch and the interrupted draw. A C++ call stack is never saved.

Content and mechanics request deferred draw work through execution-owned
functions. Joss Paper requests a completion before starting its child draw;
execution creates the frame, later calls the content-owned counter reset, and
removes the frame. Relic content does not inspect or mutate the execution stack.

## Compatibility and verification

Binary combat checkpoint revision 19 encodes only the active frame alternative;
revision 18 checkpoints are rejected. Run revision 16 embeds the combat format
and rejects older embedded combat checkpoints. Revision 18 originally replaced
fixture-only listener storage with real captured candidates.
The new powers and Stratagem's combat-pile prompt have no complete native diagnostic
JSON mapping yet and are explicitly rejected by the exporter. Existing native recordings are unchanged.

`HookDispatch` tests cover order, stacked choices, repeat shuffles, final captured
power values, and full-hand movement. Public run tests exercise the real
Burning Pact/Dark Embrace/Stratagem path (or Joss Paper) across clone/restore,
alternative choices, retained operands, allocation failures, and execution budgets.
Large-pile variants exercise actual card/selection storage overflow instead of
inventing arbitrary nested shuffle handlers. Allocation tests are correctness
checks, not throughput measurements. No benchmark was run for this refactor.

Final verification: `cmake --build --preset debug --parallel 2` completed with no
compiler warnings; `ctest --preset debug --parallel 2` passed all 267 tests,
including 181 engine checks and 23 replay-parity checks. `git diff --check` passed.
The layout-only compiler probe reported `CombatData` at 4,712 bytes (previously
4,800), `EffectFrame` unchanged at 48 bytes, and `HookListener` at 20 bytes.
These sizes and allocation regressions do not establish a throughput improvement.
