# Enemy generalization: pinned assembly audit

Status: pinned static audit completed for the questions below, with a native
managed task probe. Real UI choice and combat parity recordings remain separate
verification work. No engine changes.

## Provenance and method

On 2026-11-02 the installed `sts2.dll` at
`/mnt/c/Program Files (x86)/Steam/steamapps/common/Slay the Spire 2/data_sts2_windows_x86_64/sts2.dll`
was hashed directly. SHA-256:
`0861bfa1df347538d932f22d580e75420f08082792eb914e53b4882764acdbe9`.
This matches the repository's v0.111.0 matching contract.

REA static managed inspection independently reported that hash, module MVID
`73b63ee0-6c0a-47bb-b0d1-b21f6d94222e`, and .NET 9 CIL metadata. Metadata evidence:
`ev_415a7d8dbc77d9fc2a9433793405db90303338277a3988cbf19f1b1724301485`;
summary `ev_a5a6ce511be906275188baed0d74fa5fa8f5c6487f6f7c5be62fc77a00344757`.
Metadata inspection itself did not decompile or execute the assembly.

A fresh ILSpy project decompilation was generated in
`/tmp/enemy-generalization-audit/decompiled/` (exit 0). Unlike the third-party
snapshot, it reconstructs complete async bodies. All 227 compared files were
byte-identical to retained sources: every encounter and monster file, plus the
15 central setup, command, hook, choice-context, and RNG files listed in
`/tmp/enemy-generalization-audit/source-comparison.json`.

Reproduction (existing ILSpy 9.1.0.7988 tool):

```sh
sha256sum '/mnt/c/Program Files (x86)/Steam/steamapps/common/Slay the Spire 2/data_sts2_windows_x86_64/sts2.dll'
DOTNET_ROLL_FORWARD=Major /tmp/sts2-audit-tools/ilspycmd --disable-updatecheck -p \
  -o /tmp/enemy-generalization-audit/decompiled \
  '/mnt/c/Program Files (x86)/Steam/steamapps/common/Slay the Spire 2/data_sts2_windows_x86_64/sts2.dll'
```

Below, source paths are relative to the retained
`build/combat-state-audit/decompiled/` tree. Line numbers refer to that ILSpy
output. Source tables abbreviate the common `MegaCrit.Sts2.Core.` directory
prefix. Game sources remain ignored; only findings and the inventory are retained
in this repository. Findings below are static evidence, not live observations.

## Conclusions for the design discussion

- Enemy attacks can suspend for player input. The native enemy turn awaits that
  work before continuing the move, the next enemy, or side-end processing.
- Compiled C++ enemy functions remain possible, but potentially interruptible
  callers need cloneable saved progress. This evidence does not prescribe a
  card interpreter, general scheduler, or particular frame layout.
- Selected moves advance at the next normal player-side start, after enemy-side
  effects, rather than immediately after each enemy move.
- Four bounds initial returned encounter composition in the pinned inventory.
  It does not bound the live creature pool after spawning/summoning.
- IDs and order suffice for the three target compositions. Other native encounters
  also specify slots, initial moves, or per-enemy starting adjustments.
- No simulator performance measurements were made. No throughput penalty or
  advantage is established for any proposed implementation.

## Encounter composition and setup

The [inventory](enemy-generalization-encounter-inventory.csv) covers all 90 concrete
`GenerateMonsters()` overrides. The 91st encounter file is an abstract superclass.
Each row records minimum/maximum returned length, reasoning, source location, and
source SHA-256. Nonliteral loops, additions, and conditional branches were read;
the count is not a count of possible monster types.

Maximum initial length is **4**, attained by exactly:

| Encounter | Evidence in `MegaCrit.Sts2.Core.Models.Encounters/` |
| --- | --- |
| DenseVegetationEventEncounter | `:13,19–28`, four named slots, one emitted creature per slot |
| ExoskeletonsNormal | `:28–37`, array of four |
| PhantasmalGardenersElite | `:36–45`, array of four |
| ScrollsOfBitingNormal | `:16–33`, array of four |
| SlimesNormal | `:33–45`, array of four |

Variable/nonliteral cases include SlitheringStranglerNormal (2–3), BowlbugsNormal
(one rock plus two workers), RubyRaidersNormal (three selected raiders),
CorpseSlugsNormal/Weak (3/2 via helper), OvicopterNormal (one despite six slots),
and SlimesWeak (three insertions). DeprecatedEncounter returns zero. No inspected
composition length branches on player count or ascension; reachability and stat
scaling are distinct questions.

Target encounter definitions:

| Encounter | Composition in native order | Source |
| --- | --- | --- |
| ShrinkerBeetleWeak | Shrinker Beetle | `Models.Encounters/ShrinkerBeetleWeak.cs:18–21` |
| FuzzyWurmCrawlerWeak | Fuzzy Wurm Crawler | `Models.Encounters/FuzzyWurmCrawlerWeak.cs:18–21` |
| OvergrowthCrawlers | Shrinker Beetle, Fuzzy Wurm Crawler | `Models.Encounters/OvergrowthCrawlers.cs:24–31` |

All three use null slots and consume no composition RNG. Path prefixes in this
table abbreviate `MegaCrit.Sts2.Core.`.

`Models/EncounterModel.cs:254–271` generates once and initializes a separate
encounter RNG with the exact expression:
`(ulong)((long)runState.Rng.Seed + (long)runState.TotalFloor) + StringHelper.GetDeterministicHashCode(base.Id.Entry)`.
The third-party report's uint formulation should not be used as the pinned contract.

`Rooms/CombatRoom.cs:197–231` generates composition, optionally awaits asset loading,
then calls CreateCreature and immediately AddCreature for each entry in order.
It sets up combat, awaits AfterRoomEntered, and starts the combat lifecycle.
`Combat/CombatState.cs:233–249` assigns run RNG, creates the creature, rolls HP with
Niche, scales multiplayer HP, attaches identity, initializes monster-local RNG,
and invokes encounter OnCreatureSpawned.

`Entities.Creatures/Creature.cs:372–385` builds the inclusive HP candidate range,
excludes existing same-side creatures' max HP, and selects one candidate or falls
back to the original range when exhausted. Earlier entries are already added when
later entries roll. `Random/Rng.cs:95–104,289–299` supplies one indexed draw per
selection. The target mixed pair's HP ranges do not overlap, so each costs one
Niche draw without narrowing the other's range. Repeated beetles cannot generally
be modeled as independent unrestricted HP rolls.

Future composition inputs/outputs need further design:

- Encounter slot order controls enemy ordering (`CombatState.cs:496–502`).
- PunchOffEventEncounter sets RNG-dependent starting HP reductions (`:13–20`).
- DecimillipedeElite (`:35–50`), TwoTailedRatsNormal (`:28–42`), Scroll encounters,
  and CorpseSlug helper (`Models.Monsters/CorpseSlug.cs:135–145`) initialize moves.

Generic setup also awaits AfterCreatureAdded and BeforeCombatStart
(`CombatManager.cs:576–610`); Creature.AfterAddedToRoom awaits monster content
(`Creature.cs:415–421`). Do not infer that all future setup is prompt-free.

## Target enemy behavior

| Enemy | A0 HP | A8+ HP | Moves | A0 / A9+ attack |
| --- | --- | --- | --- | --- |
| Shrinker Beetle | 38–40 | 40–42 | SHRINKER_MOVE → CHOMP_MOVE → STOMP_MOVE → CHOMP_MOVE… | Chomp 7/8; Stomp 13/14 |
| Fuzzy Wurm Crawler | 55–57 | 58–59 | FIRST_ACID_GOOP → INHALE → ACID_GOOP → FIRST_ACID_GOOP… | Both Goops 4/6 |

Evidence: `Models.Monsters/ShrinkerBeetle.cs:22–70` and
`Models.Monsters/FuzzyWurmCrawler.cs:29–88`; ascension enum identifies ToughEnemies
as 8 and DeadlyEnemies as 9.

Crawler AcidGoop awaits one monster attack with a null choice context. Inhale
awaits animation then applies **7 Strength to itself**, using itself as applier
(`FuzzyWurmCrawler.cs:83–88`). Strength is a counter power adding its amount to
the owner's powered attacks (`Models.Powers/StrengthPower.cs:12–24`). Thus repeated
Inhales accumulate Strength; first/later Acid Goop remain distinct AI identities
despite identical attack implementations. `_isPuffed` controls visuals and is
already excluded by the pinned field audit.

Beetle Shrink applies -1 Shrink to its targets with the beetle as applier.
`Models.Powers/ShrinkPower.cs:28–45,80–113` distinguishes infinite negative amounts,
decrements finite duration after owner turns, removes on actual applier death,
and multiplies owner powered attacks by 0.7. These mixed-fight death semantics
must remain correct when the crawler survives the beetle.

The deterministic FollowUpState graphs consume no AI RNG. The generic machine
does retain first-move/completion/log metadata, but that does not automatically
make all native history necessary simulator gameplay state. Preserve future
behavior and the declared comparison contract, rather than copying native objects.

## Enemy-turn choice: actual awaited chain

| Call edge | Pinned source |
| --- | --- |
| Enemy loop snapshots order, checks membership, awaits each actor, then cancellation/unpause/win checks | `Combat/CombatManager.cs:1405–1437` |
| Actor awaits monster move unless spawned this turn | `Entities.Creatures/Creature.cs:711–720` |
| Monster caches move/targets, awaits move before completion/history | `Models/MonsterModel.cs:435–460` |
| Move state awaits its content callback | `MonsterMoves.MonsterMoveStateMachine/MoveState.cs:57–62` |
| Monster attack with null context uses BlockingPlayerChoiceContext | `Commands.Builders/AttackCommand.cs:669,672` |
| Each attack hit awaits damage before subsequent hits and AfterAttack | `AttackCommand.cs:549–563,665–674` |
| Damage awaits HP and damage hooks, including AfterDamageReceived | `Commands/CreatureCmd.cs:390–439` |
| Received-damage listeners are awaited in order | `Hooks/Hook.cs:429–445` |
| Centennial sets used flag then awaits three individual draws | `Models.Relics/CentennialPuzzle.cs:40–50` |
| Draw awaits shuffle before taking its ordinary top card | `Commands/CardPileCmd.cs:1007–1065`, especially `:1047` |
| Necessary shuffle moves cards, then awaits shuffle hooks | `CardPileCmd.cs:1079–1132,1186–1195`; `Hook.cs:1142–1151` |
| Stratagem awaits pile selection, then moves selected cards to hand | `Models.Powers/StratagemPower.cs:17–27` |
| Pile selector awaits answer, unless its documented auto-selection cases apply | `Commands/CardSelectCmd.cs:449–528`, native CardsSelected await `:514` |
| Native screen resolves selection completion on answer | `Nodes.Screens.CardSelection/NCombatPileCardSelectScreen.cs:297–302` |

`GameActions.Multiplayer/BlockingPlayerChoiceContext.cs:7–17` explicitly documents
blocking the remaining enemy turn until choice completion, naming a Centennial
Puzzle draw interaction as an example. Its signal methods do not detach work.
This is corroboration of the actual awaited chain, not a conclusion drawn merely
from async method signatures.

Required scenario conditions: combat active; unused Centennial owned by the
damaged player; positive nonlethal unblocked damage; drawing allowed and room in
hand; empty draw pile with discard available; owner Stratagem active; sufficiently
many candidates to avoid automatic selection. Test selectors can bypass the UI
and must be distinguished from a real pending native screen.

At a Stratagem choice reached during Centennial's first draw:

1. Damage and HP/block mutations have occurred; Centennial is marked used.
2. Shuffle has moved discard into draw and advanced Shuffle RNG.
3. Stratagem has not added selected cards to hand; the ordinary draw has not
   taken its top card.
4. The current listener, remaining listeners, damage return, attack result
   insertion, later hits, AfterAttack, move completion, later enemies, and
   enemy-side end remain unfinished.
5. Answer resumes selection and additions, remaining shuffle hooks, ordinary
   drawing and remaining Centennial draws, damage/attack/move completion, and
   the enemy loop, subject to death/cancellation guards.

Next-move selection occurs later: `CombatManager.cs:739–744` calls
PrepareForNextTurn at the next normal player-side start; `Creature.cs:547–559`
rolls the move and refreshes intents. Extra player turns skip that advancement.
The current simulator advances selected moves immediately, which must not be
carried into pending-enemy-work state without reviewing this difference.

Lethal damage normally skips AfterDamageReceived (`CreatureCmd.cs:414–423`).
An attacker killed during nested work fails the next-hit check; no live targets
also ends the hit loop (`AttackCommand.cs:553–563`). The outer enemy loop checks
combat completion after the actor returns. Other death-hook contexts may defer
work differently; this blocking-chain finding is not a universal hook policy.

## Native managed task probe

A disposable reference adapter under `/tmp/enemy-choice-task-probe/` exercised
native PlayerCmd.EndTurn and the native combat turn loop. Its `result.json`
contains build provenance and two independent before/pending/after runs.
`EnemyChoiceProbe.cs`, `run.py`, `run.log`, and `logs/` retain the diagnostic source,
execution, and build logs. The reference builder checked the pinned DLL and wrapper
revision `084d1aa3d8e118ca7ce8d8774ad16d6be9c92367`.

This is an injected diagnostic scenario: seed ENEMYCHOICE, Overgrowth Crawlers,
Centennial Puzzle plus Stratagem, cards moved to discard, and beetle forced to
Chomp. It uses the existing headless ICardSelector branch, holding its task pending
until the answer is explicitly supplied. It does not exercise the real Godot UI,
network choice synchronization, or a naturally traversed run.

Both independent runs observed:

| Capture | HP | Side / round | Centennial | Beetle / crawler selected moves | Selector / native loop |
| --- | --- | --- | --- | --- | --- |
| Before end turn | 80 | Player / 1 | unused | CHOMP_MOVE / FIRST_ACID_GOOP | no choice / unfinished |
| Pending Stratagem | 73 | Enemy / 1 | used | CHOMP_MOVE / FIRST_ACID_GOOP | pending / unfinished |
| After answer | 69 | Player / 2 | used | STOMP_MOVE / INHALE | no choice / no fault |

The held choice therefore blocked the crawler's 4-damage attack and next-move
advancement. Releasing it allowed both to proceed. The native turn-loop task
normally remains unfinished while waiting for another player turn; unfinished
alone is not the proof of the pause. HP, side, move IDs, and the held selector
together establish the observed ordering.

The final probe build and execution both exited 0 (12 pre-existing build warnings,
no build errors). The two interruption results compare equal as parsed JSON.
Both output files record adapter SHA-256
`de7973ec9ae81b4c2af8f4aad4f66aba8ca0bb23c1bb39b584fc7f3ab104af09`.
Pile counts changed from hand/draw/discard 0/0/10 before the attack to 0/10/0
at the held choice, then 9/1/0 after completion and next-turn draw.

A second scenario, `/tmp/enemy-choice-task-probe/crawler.json`, exercised native
standalone crawler through two Inhales (seed CRAWLERAUDIT; initial enemy HP 55):

| Player-side round | Player HP | Selected crawler move | Strength |
| --- | --- | --- | --- |
| 1 | 80 | FIRST_ACID_GOOP | 0 |
| 2 | 76 | INHALE | 0 |
| 3 | 76 | ACID_GOOP | 7 |
| 4 | 65 | FIRST_ACID_GOOP | 7 |
| 5 | 54 | INHALE | 7 |
| 6 | 54 | ACID_GOOP | 14 |

No native task fault was reported. This corroborates the cycle, base attack, and
accumulating Strength. These snapshots are bounded diagnostics, not complete
canonical state recordings or simulator replay comparisons.

## Remaining verification and limits

Static pinned code and the managed probe establish suspension well enough to reject
an assumption that enemy turns are inherently synchronous. It does not establish
measured throughput, a completed native parity fixture, or complete continuation
coverage in the current exporter.

Still needed for implementation verification: canonical crawler and pending-choice
recordings with resumed-state comparison; mixed-fight targeting and either enemy
dying first; and a multi-hit interruption probe. These should
retain setup declarations, seed/RNG, tool revisions, and repeat/replay evidence.
Natural traversal and injected/direct scenarios must remain distinguished.

No C++ implementation or design approval is implied by this audit.
