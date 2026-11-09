# Potential refactor: compiled card handlers

Status: discussion note, 2026-11-09. This is an option to revisit, not an approved
implementation plan or a measured optimization. The current selection refactor
continues to use card instruction arrays.

Replace card instruction arrays and the card-bytecode execution loop with compiled
C++ handlers selected through explicit `CardId` dispatch. Keep the execution-frame
dispatcher: draws, exhausts, shuffles, enemy turns, and suspendable card handlers
still need saved progress and operands. This retains the explicit compiled calls
and copyable continuations required by ADR 0006.

A handler supplies selection zones and eligibility directly to `request_selection`,
rather than encoding them in `Instruction`. Simple synchronous cards can execute
straight through; suspendable cards explicitly record stages before yielding.

## Illustrative Burning Pact handler

This sketch is not complete implementation code. `progress`, `upgraded`, and the
shared card-play completion operation would need concrete interfaces. It assumes
`request_selection` handles empty and forced single-card choices immediately.

```cpp
switch (progress.stage) {
case 0:
    progress.stage = 1;
    request_selection(state, Zone::hand, 1, 1,
        [](const RunState&, CardHandle) { return true; });

    if (state.combat().phase == CombatPhase::card_choice)
        return result(state);
    [[fallthrough]];

case 1: {
    const auto cards = take_selection(state);
    progress.stage = 2;

    if (cards.count != 0) {
        state.execution().push_back(ExhaustCardsFrame{
            .cards = cards,
            .source_zone = Zone::hand,
        });
        return result(state);
    }
    [[fallthrough]];
}

case 2:
    progress.stage = 3;
    state.execution().push_back(DrawFrame{
        .remaining = upgraded ? 3 : 2,
        .from_hand_draw = false,
    });
    return result(state);

case 3:
    // Finish the card play using the shared completion logic.
}
```

The requester takes the selected operand range before scheduling exhaust. The
exhaust frame owns those operands while nested hook work runs, and releases them
when complete. No callback or C++ stack reference is saved across suspension.

## Tradeoffs and questions to revisit

- Removing `Opcode`, `Instruction`, and card program arrays replaces interpreted
  card programs with compiled state machines; it does not eliminate suspension
  stages or the generic frame dispatcher.
- A byte-sized stage could replace `CardProgramFrame::instruction`. Source card,
  play identity, energy spent, and target may still be required. More complex
  cards may require additional saved operands.
- Padding and the largest execution-frame alternative determine actual size.
  Measure `CardProgramFrame`, `ExecutionFrame`, and complete state layouts before
  claiming smaller states, and benchmark representative rollouts before claiming
  higher throughput.
- Establish how content-owned handlers share card-play completion, retain
  energy/target operands, and validate saved stages without inspecting or popping
  another operation's frame.
- Preserve execution budgets, ordered selections, hook ordering, and cloning and
  checkpoint behavior across suspended choices. Treat this as a separate refactor
  after the selection design is reviewed.
