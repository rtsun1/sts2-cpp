# Suspendable enemy turns

This records the original independent task. Its enemy scope and checkpoint version
are superseded by the [integration contract](enemy-combat-integration.md).

Scope: implement the supported beetle enemy turn with cloneable continuation
state, including actual Centennial Puzzle draws that can shuffle and invoke
Stratagem's selection. This branch does not add enemies or encounters. Review
baseline: `6005d64eacf5d9324d781975e2d723b15ad7b5c1`.

## Behavior and ownership

The enemy turn captures ordered creature handles once. Handles retain generations,
so removing an enemy and reusing its slot cannot replace a pending actor. New
creatures wait until a later turn. Power-turn callbacks recheck actor membership
and HP before running content.

`EnemyTurnFrame` owns the actor cursor and side phases in combat orchestration.
Compiled beetle content applies Shrink directly or inserts an `EnemyAttackFrame`.
That mechanics frame owns damage, its result, and received-damage listener
progress. It emits the attack-completed event after those listeners finish.
There is no generic scheduler or virtual gameplay hierarchy.

Hooks captures the supported received-damage listener and selects the named
Centennial handler. Content owns its conditions: unused relic, positive HP damage,
and a surviving player. It marks used before awaiting three separate one-card
draws. Each draw can insert a shuffle, which can publish Stratagem's choice.
The current damage dispatch, attack, and enemy turn remain pending until the
complete ordered selection is submitted. No effect is repeated on resume.

The normal common stack has at most four frames at that choice:
enemy turn, enemy attack, draw, shuffle. The existing inline execution capacity
therefore handles this scenario without promoting to heap storage. An inline
four-handle sequence captures enemy order, with existing independently owned
overflow for larger live populations.

Completed enemies retain a `performed` AI flag until their next move is selected
at the following normal player-side start. The selected move stays unchanged
while an enemy-side choice is pending. Side-end effects finish first. Ordinary
terminal checks preserve player death and combat victory behavior; terminal cleanup
releases captured actor order and resets Centennial's combat-used flag.

## Pinned native evidence

Assembly SHA-256:
`0861bfa1df347538d932f22d580e75420f08082792eb914e53b4882764acdbe9`.
The [pinned audit](enemy-generalization-pinned-audit.md) freshly decompiled this DLL and matched retained files. Paths
below abbreviate the common `MegaCrit.Sts2.Core.` decompilation directory prefix.

- `Combat/CombatManager.cs:1405–1437` captures enemies and awaits each TakeTurn.
- `Models/MonsterModel.cs:435–460` awaits content before recording completion.
- `Commands.Builders/AttackCommand.cs:669,672` uses BlockingPlayerChoiceContext
  for monster damage and attack hooks when no choice context is supplied.
- `Commands/CreatureCmd.cs:390–439` awaits received-damage hooks and normally
  skips that hook for lethal damage.
- `Models.Relics/CentennialPuzzle.cs:40–50` marks used before three separately
  awaited one-card draws; `:54–57` resets after combat.
- `Commands/CardPileCmd.cs:1007–1132` awaits shuffle before drawing and awaits
  shuffle listeners. `Models.Powers/StratagemPower.cs:17–27` awaits selection.
- `CombatManager.cs:739–744` and `Entities.Creatures/Creature.cs:547–559` advance
  AI at the following player-side start, rather than after the attack.

The prior native managed probe twice held a headless selector task during an
injected mixed-fight Centennial/Stratagem interaction. HP stayed at 73 after the
beetle attack while the crawler waited; answering continued to 69 and next moves.
It was a native task-order probe, not a Godot UI capture or complete parity fixture.
This implementation's two-beetle test instead expects HP 60 after 7 and 13 damage.

## Public APIs, persistence, and limits

Selection submission uses the existing `SelectionInput`, `selection_space`, and
`apply_action` interfaces. Enemy-side prompts expose their candidates while
ordinary card play and end turn remain illegal. Checked and trusted paths execute
the same transitions. Integrity and unsupported-content validation remain at
checked boundaries or behind `STS2_DEFENSIVE_VALIDATION`; operational budget,
capacity, allocation handling, and live gameplay conditions remain active.

CombatConfig can enable Centennial Puzzle for authored standalone scenarios;
run setup derives that capability from the real inventory. Combat state retains
its used flag. Clone equality and sequence ownership include the captured actors.
Run checkpoint revision 20 and embedded combat payload revision 3 encode the new
frames, actor identities, AI completion flags, and relic state; older revisions
are rejected. The native diagnostic exporter still explicitly rejects active
enemy-side and Stratagem prompts because their native continuation mapping has
not been established. It exports Centennial fields at supported ordinary states.

Supported attacks remain single-hit beetle attacks. This does not claim full
native damage-hook coverage, arbitrary new enemies/multi-hit effects, summons,
death-trigger choice policies, or unrelated relic support. Future interruptible
content must preserve its own unfinished work before scheduling children.

## Verification and layout

Tests use the agreed public combat/run action and checkpoint seams. The first
test failed on the unsupported Centennial inventory before implementation, then
passed with a real pending choice and equal clone/restored futures. Additional
cases cover the later enemy waiting, rejected-input atomicity, public choice
inspection, checked/trusted equality, blocked/lethal damage, and once-per-combat
activation. Existing mutation/reuse tests exercise captured creature generations.

With g++ on this host, layout changed from the baseline as follows:

| Type | Baseline bytes | This branch bytes |
| --- | --- | --- |
| ExecutionFrame | 32 | 48 |
| CombatData | 4,492 | 4,596 |
| CreatureState | 272 | 272 |
| RunData | 34,272 | 34,376 |
| RunState | 34,368 | 34,472 |

The pending-choice test checks empty combat overflow before and after a copy.
These are layout/allocation facts, not throughput or complete-correctness claims.
Build/test logs are retained under `/tmp/suspension-*.log`.

Validation on 2026-11-03:

- Debug configure/build succeeded; full `ctest --preset debug -j4` passed all
  318 checks, including 23 replay-parity checks (`/tmp/suspension-ctest-full.log`).
- Release and Debug with `STS2_DEFENSIVE_VALIDATION=ON` each passed all 22
  selected EnemyTurn, MultiCreature, and HookDispatch tests.
- Release `kernel_benchmark 200` retained zero warmed allocations and checksum
  19000. Independent parent kernel and architecture benchmark smoke runs also
  exited 0. Short timings are not used for a speed claim.
- `git diff --check` passed. Release retained the existing run-initialization
  maybe-uninitialized warnings; new continuation code compiled without warnings.

Critical review paths are `resume_enemy_turn`, `resume_enemy_attack`,
`centennial_puzzle_after_damage_received`, and the checkpoint frame decoder.
Review must establish actor identity across slot reuse, progress-before-child
scheduling, damage/event order, once-only relic activation, and independent
restored futures. Passing tests do not establish complete native correctness.
