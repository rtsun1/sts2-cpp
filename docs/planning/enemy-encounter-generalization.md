# Enemy and encounter generalization

## Scope

Implement A0 single-player Shrinker Beetle, Fuzzy Wurm Crawler, and Overgrowth
Crawlers through the existing combat/run APIs. Keep compiled content behavior,
add enemy identity and local move indices, rename content/monsters to enemies,
and place encounter composition functions in content/encounters. Composition uses
four inline entries plus a count, matching the pinned initial encounter inventory;
this does not cap the live creature pool.

Native evidence is the pinned v0.111.0 assembly with SHA-256
0861bfa1df347538d932f22d580e75420f08082792eb914e53b4882764acdbe9. See the [pinned audit report](enemy-generalization-pinned-audit.md) and
[encounter inventory](enemy-generalization-encounter-inventory.csv). This implementation
must preserve native ordering, HP RNG uniqueness, delayed next-move selection,
crawler's 4-damage attacks and +7 accumulating Strength, and beetle Shrink removal
when its applier dies.

## Execution boundary

This branch supports the existing synchronous enemy effects. Enemy-turn suspension
and Centennial Puzzle belong to a separate independently based worktree. No generic
interpreter, dynamic gameplay polymorphism, or repeated validation in trusted
execution is introduced. Checked boundaries validate state/content; trusted callers
retain their supported-content precondition. Existing unsupported relic/content
combinations remain rejected. Native diagnostics must reject unmapped new content
rather than fabricate native state history.

## Validation plan

Use the owner-confirmed seams: public combat/run action application and checkpoint
save/load. Tests compare audited native literals for crawler cycles and accumulated
Strength, mixed composition/order and targeting, post-death Shrink behavior, complete
checkpoint futures, and run encounter selection. Existing beetle and recording tests
remain regressions. New native full-state parity is not claimed without recordings.

## State and compatibility

EnemyId plus EnemyMove local indices replace the beetle-only move type.
EnemyObservation.definition identifies the owning enemy. A per-enemy performed
flag separates move completion from next-player-start advancement. Existing
creature membership flags share one byte so CreatureState remains 272 bytes.
RunData retains combat_encounter through cleanup, ensuring unmapped native JSON
exports fail even after the combat graph has been released.

Run checkpoint revision 20 serializes enemy identity, performed progress, and
encounter identity. Earlier revisions are rejected; combat payload layout stays
at revision 2, with a changed content fingerprint. On x86-64 GCC, RunData remains
34272 bytes, CombatData4492, ExecutionFrame32, CreatureState272, RunState34368.
No throughput conclusion follows from these layout measurements.

Targeted public-seam tests cover the ten new enemy cases plus twelve existing
multi-creature cases. Release allocation/layout smoke checks also passed: kernel 200 warmed battles
reported zero warmed allocations and checksum 19000, matching the baseline smoke.
Architecture 100 confirmed the unchanged layouts above. These small checks do
not establish a throughput improvement. Parent-retained logs are under
`/tmp/sts2-enemy-task-evidence/generalization-{build.log,kernel.txt,architecture.txt}`.
Final validation:

- Debug build exited 0; ten new Enemies tests and twelve MultiCreature tests pass.
- The first engine-label pass exposed a test-only checkpoint offset helper using
  the old creature width. Updating it for the two added serialized bytes made its
  focused corruption/identity regression pass.
- `ctest --preset debug -L replay_parity`: 23/23 pass.
- `ctest --preset debug -j4`: 325/325 pass, including all 239 engine-labelled tests.
- `git diff --check`: pass.

Full logs remain `/tmp/enemies-{full-suite,replay-checks,engine-checks}.log` and
focused `/tmp/enemies-*.log`. No new canonical fixtures or installed game files
were changed. Passing these tests establishes their covered cases, not complete
native parity for the newly supported content.
