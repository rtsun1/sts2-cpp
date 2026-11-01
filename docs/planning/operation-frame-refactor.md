# Operation-frame refactor: layout and implementation proposal

Status: implemented and technically verified; user learning review remains open.
The user subsequently chose direct `state.execution.push_back` instead of a
one-line `schedule_frame` wrapper, and operation resumers in mechanics. The
prototype measurements below preceded implementation; final measurements are
recorded with verification results.

## Agreed behavior

- Batch `DrawFrame`, batch `ExhaustCardsFrame`, and operation-level `ShuffleFrame`
  own their operation phases and embedded hook progress.
- A frame records progress before scheduling at most one child, then yields.
  A prompt can yield without a child. Existing tail delegation can replace a
  completed parent with one child; stack-depth equality alone cannot identify it.
- Handler completion belongs in its parent operation. Content interprets its own
  saved stage; the operation does not branch on a Joss Paper completion action.
- Synchronous handlers can ignore handler progress and return complete.
- Content and mechanics may construct typed frame payloads. They insert frames
  directly; insertion never executes the child inline.
- Remove `SelectionFrame` for current effects: confirmed exhaust operands go to
  `ExhaustCardsFrame`; other currently supported selected effects remain direct
  where they are synchronous. Remove `RelicDrawCompletionFrame` and the old
  exhaust-hook-only `ExhaustFrame`.
- Preserve supported ordering, prompts, captured identities, clone/restore,
  resource failures, and execution-budget behavior. No new Speedster, Fiend Fire,
  ethereal, damage/death queue, or pile-change listener implementation is included.

See the [native audit](../specs/engine/frame-semantics-audit.md) for evidence and
the boundaries that future content must preserve. In particular, a shuffle can
be requested outside drawing, and some native death hooks detach paused tasks.

## Recommended saved layouts

The common pieces are simple values:

```cpp
struct RangeProgress {
    std::uint32_t begin = 0, count = 0, cursor = 0;
};

struct HandlerProgress {
    std::uint8_t stage = 0;
};
```

The operation owns the captured range; content owns the meaning of its handler
stage. Stage zero is the common initial state. Currently supported post-child
content needs no additional saved operand beyond operation context: Joss Paper
reads the live counter after drawing. More complex future content may require
additional operands and a new layout review.

Proposed payload field order:

```cpp
struct DrawFrame {
    RangeProgress listeners;
    CardHandle current_card {};
    std::int32_t remaining = 0;
    HandlerProgress handler;
    DrawPhase phase = DrawPhase::start;
    bool from_hand_draw = true;
};

struct ExhaustCardsFrame {
    RangeProgress cards;
    RangeProgress listeners;
    HandlerProgress handler;
    ExhaustPhase phase = ExhaustPhase::start_card;
    Zone source_zone = Zone::hand;
    bool charge_card_step = true;
};

struct ShuffleFrame {
    RangeProgress listeners;
    std::uint32_t expected_selection_count = 0;
    HandlerProgress handler;
    ShufflePhase phase = ShufflePhase::reorder;
    bool charge_draw_step_if_hooks = false;
};
```

The phase enums have `std::uint8_t` representation. They distinguish operation
steps such as waiting for a shuffle, processing the current card's hooks, and
finishing a card. Exact phase labels should follow the implemented transitions;
unused full-game behavior is not added merely because the native audit found it.

`CardProgramFrame` stays unchanged. The variant becomes these four alternatives:
`CardProgramFrame`, `DrawFrame`, `ExhaustCardsFrame`, and `ShuffleFrame`.

### Meaning and lifetime

- Draw retains the current card while its hooks run. Remaining count is not a
  preselected card list; the next card is determined after earlier effects.
- Exhaust owns a captured card range in the existing shared card-operand storage.
  It retains the current card until its listeners and post-child work finish,
  then advances to the next captured identity. Single-card requests use the same
  range mechanism. No stored spans, vectors, or pointers into relocatable state.
- Hook progress retains the current listener until its handler completes. Saving
  a handler stage before yielding prevents repeating its pre-child mutation.
  Cursor meaning will be documented explicitly and reflected in validation.
- Shuffle's expected selection count preserves current prompt-validation state;
  it is not an instruction for generic operation code to implement Stratagem.
- Card and listener ranges are captured and released in stack order. Child
  operations own independently captured ranges. Snapshot identities/order are
  distinct from live content values.
- `charge_card_step` preserves the existing distinction between selected-card
  application work and an exhaust already covered by a card instruction's budget
  charge. This is compatibility bookkeeping, not a performance fast path. It fits
  existing padding. The implementation must compare low-budget boundaries rather
  than assume merging frames preserves them automatically.

### Handler resumption contract

`resume_listener` remains a hook-dispatch helper, called normally from an operation
handler. Content returns complete or yield. Yield can mean a scheduled child or
a prompt. Scheduling does not execute the child before the function returns.

Handlers record their stage before scheduling. After a child is pushed, the call
chain immediately returns without touching previous references into execution
storage. Completed synchronous handlers advance listener progress normally. On
resumption, the same content handler interprets its stage; simple tail-await
handlers can finish without reapplying their initial effect. Joss Paper performs
its modulo operation only after its child finishes.

Budget charges attach to existing work, not every helper return or phase
transition. In particular, a completion with no additional mutation must not gain
a charge simply because its handler is called again. Joss Paper's reset retains
the charge previously represented by its completion frame. Finished-loop checks
and existing error/resource outcomes must be covered by regression tests.

## Measured sizes

Measured with GCC 11.4.0 on the current x86-64 ABI. The stand-alone prototype uses
the actual repository headers and reproduces the enclosing data-field lists.
Static assertions verify that the baseline replicas match the actual current
types before substituting proposed frames. Saved data remains trivially copyable.
There are no packing pragmas, narrowed card/range indices, or heap members.

| Layout | Variant slot | Four-slot execution storage | CombatData | RunData | CombatState root | RunState root |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Current | 32 | 136 | 4,648 | 34,512 | 5,808 | 34,608 |
| Recommended: range and stage fields placed directly | 32 | 136 | 4,648 | 34,512 | 5,808 | 34,608 |
| Nested `HookProgress` containing range and stage | 36 | 152 | 4,664 | 34,528 | 5,824 | 34,624 |
| Uniform handler stage plus reserved 32-bit operand | 40 | 168 | 4,680 | 34,544 | 5,840 | 34,640 |

Recommended payload sizes: card program 28, draw 28, exhaust 28, shuffle 20 bytes.
The variant adds its tag/padding to reach 32. The stage and operation flags fit
without enlarging the maximum alternative. Shuffle's full-width selection count
is retained; a speculative operand is not reserved in every other handler.

This comparison demonstrates a padding cost for the nested grouping, not a
runtime advantage for the recommended representation. LTO does not change these
required saved-object sizes. No new transition-throughput measurements were run.
The prototype does not execute the proposed engine or establish its correctness.

Sources and output: `/tmp/sts2-operation-layout/generate.py`, `layout.cpp`,
`build.log`, and `results.txt`. Build command:

```sh
/usr/bin/g++ -std=c++23 -O3 -Wall -Wextra -Wpedantic -I include \
  /tmp/sts2-operation-layout/layout.cpp -o /tmp/sts2-operation-layout/layout
```

## Module placement

The user approved putting operation resumers in mechanics. The implemented
placement is below.

| Module | Responsibility |
| --- | --- |
| `execution/interpreter.cpp` | Small frame dispatcher |
| `execution/card_program.cpp` | Card instruction program execution |
| `execution/selection.cpp` | Prompts, input validation, and transferring answered operands |
| `execution/validation.cpp` | Saved-frame/operand validation coordination |
| `mechanics/draw.cpp` | Draw operation resumption and primitive drawing |
| `mechanics/exhaust.cpp` | Exhaust-batch resumption and primitive exhausting |
| `mechanics/shuffle.cpp` | Shuffle operation resumption and primitive rearrangement |
| `hooks/dispatch.cpp` | Candidate capture/order/eligibility, listener iteration, content dispatch |
| `content/*` | Content-specific conditions, mutations, and resumable handler stages |

Putting operation resumers in mechanics is an ownership choice, not a technical
requirement. Internal headers expose explicit compiled interfaces. Operations own
their frame lifecycle; hook dispatch no longer pushes/pops separate hook frames.
Update `hooks.md` to allow content/mechanics to construct typed child payloads
and insert directly. Callers must not manipulate other frames.

## Implementation sequence

1. The earlier commit did not complete. The user explicitly deferred committing
   until the refactor finishes. Keep unrelated `AGENTS.md` and `todos.md` edits out.
2. Separate interpreter dispatch, validation, card-program execution, operation
   mechanics, while preserving behavior. Remove redundant
   `draw`/`defer_draw` scheduling wrappers; request a typed draw frame directly.
3. Introduce the four-alternative saved model and resumable content interface;
   migrate draw, exhaust, and shuffle together with their shared operand ownership.
   Remove selection-resolution and relic-completion frames. Keep each coherent
   change buildable; do not leave old/new snapshots mixed at a public boundary.
4. Migrate selection confirmation, direct/single-card exhaust callers, initial and
   end-turn drawing, prompt validation, standalone cloning, native export, and
   checkpoint encoding together. Bump binary combat checkpoints from 19 to 20;
   reject old embedded combat encoding explicitly. Keep native recordings and
   supported content unchanged.
5. Add/adjust regressions for one-child yields, Joss Paper post-draw completion,
   per-card capture, selected and direct exhaustion, shuffle prompts, last-card
   tail delegation, low-budget boundaries, transactional initialization, overflow,
   clone/restore at prompts, and unsupported content rejection. Tests that assert
   old internal frame types must instead assert the new representation and the
   required externally observable order.
6. Run focused engine tests, engine/replay labels, then the release suite; save
   verbose output. Compare optimized standalone and nested supported workloads
   against the saved baseline, with LTO comparisons when practical. Preserve the
   distinction between these measurements and representative MCTS evidence.
7. Review the changed control flow with the user before calling the learning
   checkpoint complete. Critical paths are scheduling/yield, handler resumption,
   captured-range lifetime, selection-to-exhaust transfer, and budget preservation.

The user approved implementation, including direct insertion without a scheduling wrapper.
The learning checkpoint remains pending the user's code review and explanation.


## Implementation verification

The implemented payload/variant and enclosing state sizes match the recommended
row above, including the shuffle budget flag. All 279 release checks pass. A
451-setup differential budget/trace probe produced 1,229 identical observations;
address/undefined-behavior and forced-relocation probes passed. Normal and LTO
performance results and their limits are recorded in
[architecture verification](../specs/engine/architecture-verification.md#operation-owned-hook-progress-2026-11-01).

This scope retains the known source zone for each exhaust batch. Before adding
handlers that move unexhausted batch members between piles, extend the primitive
and its tests to honor the native current-pile semantics identified by the audit.
No such handler or Fiend Fire implementation is added here.
