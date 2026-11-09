# Combined enemy content and turn suspension

This merges `feat/enemy-encounter-generalization` at `f0efd89` and
`feat/enemy-turn-suspension` at `4979f4f`, both based on the owner's action refactor
at `6005d64`. The original branches remain available independently.

Compiled content in `content/enemies` selects beetle or crawler effects. Both
single-hit attacks insert `EnemyAttackFrame`; the enclosing `EnemyTurnFrame`
marks a move performed only after that frame and its children finish. Inhale and
Shrink remain synchronous supported power applications. Next moves advance at
the following player-side start, after enemy-side cleanup.

Encounter composition, native HP selection, enemy identity, and mixed-fight Shrink
removal remain from the generalization task. Centennial Puzzle, its three
individually awaited draws, ordered actor capture, and nested Stratagem choices
remain from the suspension task. Complete ordered selection submission and
checked/trusted validation ownership remain the owner's existing contracts.

Run checkpoint revision 22 combines enemy/encounter identities with captured
actor order, pending attack/listener progress, and relic state. Embedded combat
payload revision is 4; the content fingerprint includes both rule changes.
Earlier standalone branch checkpoints are rejected rather than misinterpreted.

The new public-API regression covers standalone crawler and the mixed encounter:
nonlethal damage triggers Centennial, shuffle opens Stratagem, current move and
later actors remain pending, and copies/restored checkpoints produce equal
futures after a complete ordered answer. Expected mixed HP is 73 while waiting
on the beetle's attack and 69 after the crawler also attacks, matching the pinned
native managed probe. The standalone crawler stays at HP 76 through its answer.
The common pending case remains wholly inline. Full native UI/parity recordings
for these new interactions remain outside the established evidence.

## Verification

The new regression first failed against the mechanical merge, then passed after
compiled enemy attacks were connected to the shared mechanics frame. The initial
targeted run passed all 33 Enemies, EnemyTurn, MultiCreature, and HookDispatch tests.

- Debug configure and full build exited 0.
- The full Debug CTest suite passed 329/329 tests, including the original replay
  checks and the new combined crawler/choice regression.
- A separate Debug build with `STS2_DEFENSIVE_VALIDATION=ON` passed all 33 focused
  enemy, turn, multi-creature, and hook tests.
- The host layout probe reports RunState 34,472 bytes, RunData 34,376,
  CombatData 4,596, CreatureState 272, and ExecutionFrame 48. These match the
  suspension branch; no additional state growth or throughput claim follows.
- Logs: `/tmp/enemy-integration-{configure,build-all,ctest-full,defensive-build,defensive-targeted}.log`.

Independent Standards and Spec reviews of merge `79e4e5a` found no actionable
issues. Both reviewed the conflict resolutions and combined continuation,
checkpoint, and public selection behavior. The original task branches remain
unchanged. Passing checks and static review do not establish full native parity.

## Merge into main

The main merge also preserves `e872071`, where selection requesters own answer
execution and retained operands. The generic hook API keeps that refactor’s
signature while exposing the Shrink death dispatch. Run revision 22 distinguishes
the combined enemy identity and selection-handoff layout from both prior revision
21 formats; the embedded combat payload follows main’s revision 4.

Main merge verification (2026-11-09): the Debug configure and build passed without
compiler warnings. All 55 focused selection/enemy tests passed; the checkpoint
regression additionally covers atomic rejection of both prior run revisions 20
and 21. The full CTest suite passed 332/332 checks, including all 23 replay-parity
checks. Logs are `/tmp/enemy-main-{build,targeted,checkpoint-test,ctest-full}.log`.
