---
status: accepted
---

# Use a data-oriented engine with selective continuations

Build the full single-player simulator as a data-oriented C++ kernel: one canonical
run-owned creature pool, separate persistent and combat card instances, immutable
shared definitions, procedural mechanics and room handlers, and typed hooks.
Sequence simple effects with lightweight instructions and preserve unfinished work
in selective continuation frames whenever an operation can suspend, including
through nested hooks; ordinary synchronous mechanics remain direct compiled calls.
This supports deterministic, independent full-run cloning for MCTS without a
general effect scheduler, event bus, virtual gameplay hierarchy, or search policy
inside the engine.

The decision comes from the [architecture conversation](https://chatgpt.com/share/6ae309c7-03e8-83e8-9f6f-ec79dbee927f),
especially its final decision on selective continuations. The user explicitly made
that conversation authoritative over earlier architecture decisions when requesting
this ADR. The detailed specification and migration acceptance criteria are in the
[engine architecture spec](../planning/engine-architecture-spec.md).
The published implementation specification is
[issue #56](https://github.com/TonyinBio/sts2-cpp/issues/56).

## Complete ordered selection submissions

The later accepted selection decision supplements this architecture: the engine
accepts a complete ordered selection in one submission. Callers own proposed
selections and optional select/deselect/confirm editing. The simulator assumes
that editing before submission has no gameplay effects; this is an accepted
modeling assumption, not a new native-audit finding.

Submission borrows a `std::span<const CardHandle>` for the call. Before applying
effects, the engine copies accepted handles once into existing continuation
operand storage. Both synchronous completion and nested suspension use that
ownership model; operands remain cloneable and restorable until execution
finishes, then are released. Gameplay state retains the prompt and its constraints,
but no partial selection or separate selection-editing buffer.

Selection enumeration is separate from submission and effect execution. It uses
caller-owned workspace and a consumer interface, allowing early stopping or eager
collection without changing gameplay execution. Incremental editing was previously
preferred to avoid eagerly materializing permutations, but complete submissions
do not require eager enumeration. This choice removes editing state from gameplay
clones while leaving enumeration and retained-action storage with callers. No
comparative throughput improvement has been established.

This replaces earlier requirements to retain partial selections in authoritative
state. Detailed input, ownership, clock and historical-recording compatibility
contracts live in [ordered combat card choices](../specs/engine/ordered-card-choices.md).

## Consequences

- Complete state owns every mutable gameplay value, RNG stream, pending prompt,
  suspended operand and identity needed to resume. Player HP and maximum HP have
  one authoritative creature record; combat does not synchronize a duplicate.
- Continue ADR 0003's common-case memcpy cloning and independently owned overflow.
  Initially clone the complete run, while keeping interfaces open to a later
  measured reduction in copying. Do not retain completed encounter graphs solely
  for diagnostics.
- A synchronous function must never suspend. A potentially suspending call path
  must preserve its callers' progress in cloneable state before yielding; a C++
  stack frame or worker scratch cannot be a continuation.
- Source modules share one optimized engine library. Mechanics, hooks and compiled
  content may call each other through explicit internal interfaces. Logical
  separation does not require a library, object hierarchy or scheduler per module.
- Room state uses compact tagged alternatives where mutually exclusive; an event
  suspended around combat retains its required context alongside the active fight.
- Dense versus sparse power storage is deferred to representative benchmarks.
  This supersedes ADR 0005's fixed power-type slot commitment and its decision to
  defer storage evaluation until the full game is implemented. Native ordering,
  distinct instance identity and per-applier behavior remain requirements, but
  application-order tracking is not prescribed as a permanent representation.
- Exact capacities, card-specific inline versus sparse payloads, switch versus
  immutable function-table dispatch, and instruction specialization remain tuning
  choices. Do not claim they are settled or that one is faster without measurements.
- ADR 0001's observation boundary and ADR 0004's JSON checkpoint decision continue
  to apply. The conversation does not replace their contracts.

## Considered options

A generic effect scheduler or event bus would represent and route ordinary work
that can execute directly. Pure synchronous C++ or ad hoc card choices would fail
to preserve nested suspensions. Framing every primitive would add bookkeeping even
for operations guaranteed to finish immediately. Selective continuations preserve
the needed control flow while keeping those operations direct.
