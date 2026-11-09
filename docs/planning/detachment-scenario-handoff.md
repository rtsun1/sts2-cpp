# Next slice: a real gameplay detachment scenario

The frame infrastructure is implemented on `feat/detached-execution`. No supported
production hook currently opens a detachment boundary. Existing tests explicitly
mark boundaries around real draw/shuffle/exhaust operations; they establish frame
machinery behavior, not native gameplay scheduling parity.

Start with [module ownership and detached groups](../specs/engine/hooks.md#detached-frame-groups),
[validation ownership](../specs/engine/public-run-loop.md#validation-ownership), and
the [pinned frame semantics audit](../specs/engine/frame-semantics-audit.md).

## Smallest useful scenario

Audit and implement a lethal hit against one enemy while another enemy survives,
with Gremlin Horn, Stratagem, an empty draw pile and enough discarded cards to
require a selection. Gremlin Horn's draw should reach Stratagem through shuffle.
Use the existing two-enemy setup patterns in `tests/unit/multi_creature_test.cpp`.
Try this before adding Speedster or multiple simultaneous deaths.

Confirm the exact caller progress and choice timing in the pinned build
`0.111.0`, assembly SHA-256
`0861bfa1df347538d932f22d580e75420f08082792eb914e53b4882764acdbe9`.
The audit identifies `Hook.AfterDeath` as pause-or-completion and Gremlin Horn as
an awaited draw inside that listener. Capture a native trace/recording showing
which death cleanup or caller effects occur before the choice is presented.
Do not assume an exact event order from the illustrative scenario alone.

## Integration points

- Content owns Gremlin Horn's conditions and effects; hooks owns native listener
  order and eligibility. Mechanics owns resumable damage/death processing.
- Record caller progress before `begin_detachable_frames`, then push the complete
  child group. All unfinished handler work must belong to those child frames;
  leaving its saved stage in the continuing caller is unsafe. Existing operation
  frames embed handler progress, so identify that ownership before choosing the
  death-frame layout. Do not add a generic hook-frame abstraction or scheduler.
- `pending_choices.cpp` parks/restores frames and operand arenas. An active entry
  retains its FIFO position, base depth and previous active entry until completion.
- `request_selection` only saves zone, eligibility, bounds and identity.
  `present_selection` builds candidates from the live state and handles automatic
  answers. Effects remain with the requester, which consumes the answer before
  yielding or transfers its operands to an owning frame.
- Add join bookkeeping only at a native-proven join. Keep integrity/legality checks
  at checked boundaries or behind `STS2_DEFENSIVE_VALIDATION`; retain resource
  handling and gameplay conditions on trusted execution.

## Acceptance evidence

Drive the scenario through public actions, without test-only boundary marking or
fixture handlers. Check state at the published choice: caller progress, energy,
enemy membership, card piles, candidate order and saved frame progress. Compare
those observations with the pinned native evidence. Then resolve the choice and
verify the child resumes exactly once, with no repeated caller effects.

Clone and checkpoint at that choice; compare resumed checked/trusted futures and
alternative legal answers. Cover combat-end cancellation and the relevant budget
and allocation failures. Add a second queued choice after the first scenario
works, checking that one group's completion preserves the other's operands.
Native JSON export currently rejects detached groups; extend its mapping if the
chosen parity-recording route needs it, or retain explicit trace evidence and
state that limitation.

Baseline: 341 Debug checks and 76 selected defensive checks passed. Run checkpoint
revision is 23; combat payload revision is 6. Validation details and local log paths
are recorded in `docs/specs/engine/hooks.md`. No rollout-throughput measurement or
live detachment parity claim has been established.
