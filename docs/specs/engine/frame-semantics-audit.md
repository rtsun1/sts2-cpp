# Native operation and hook continuation audit

## Result and scope

This is a static audit of the pinned reference game's draw, shuffle, exhaust,
selection, and relevant damage/death paths. It answers which operands, snapshots,
and progress must survive child work. It does not select a C++ representation or
measure performance. No simulator code was changed and no live gameplay scenario
was run for this audit.

The ordinary draw/exhaust paths inspected here do **not** require a separate C++
frame for every hook dispatch. A resumable operation could retain its current
hook phase, captured-listener range, cursor, and pending content completion.
Separate hook frames are also viable. Both must preserve the same native ordering
and capture timing; neither representation alone establishes parity.

An important qualification is that death hooks can detach unfinished work into a
queued action when a choice is requested. A universal rule that every child must
finish before its parent continues is therefore unsafe beyond the ordinary
await-to-completion paths. The exact runtime ordering of the complete
Speedster/Gremlin Horn/Stratagem chain remains unrecorded.

A subsequent [runtime scenario trace](../../planning/detachment-scenario-handoff.md)
now covers lethal Strike and Dagger Throw with Gremlin Horn/Stratagem. It confirms
death cleanup before Horn's choice and establishes ordinary caller-choice priority
before queued hook activation. Its local-only input seam and supported scope are
recorded there; the complete Speedster chain remains outside that evidence.

## Artifact identity and validation

- Artifact: installed `data_sts2_windows_x86_64/sts2.dll`.
- SHA-256: `0861bfa1df347538d932f22d580e75420f08082792eb914e53b4882764acdbe9`.
- Module MVID: `73b63ee0-6c0a-47bb-b0d1-b21f6d94222e`.
- REA artifact record: `ev_415a7d8dbc77d9fc2a9433793405db90303338277a3988cbf19f1b1724301485`;
  summary: `ev_a5a6ce511be906275188baed0d74fa5fa8f5c6487f6f7c5be62fc77a00344757`.
- Sources below are retained decompilation under
  `build/combat-state-audit/decompiled/`, abbreviated **D/**.

Fresh ILSpy 9.1.0.7988 decompilation of 27 relevant types was compared with those
retained sources: ten were text-identical; all 27 were identical after the sole
normalization of `_003C_003Ez__` to `<>z__` in compiler-generated identifiers.
Source line numbering is unaffected. The tool ran with the installed .NET 9
runtime using `DOTNET_ROLL_FORWARD=Major`; it did not execute game code.
Raw source hashes, comparisons, and fresh output are in
`/tmp/sts2-frame-audit/source-validation.json` and `fresh/`.

REA's managed-member inspection hit its 64 MiB representation budget and returned
unavailable member coverage (`ev_0d33c43316f52bc1e29a3213a38fbc0c070b8213705d9ff573b0a4eb6aea15ef`).
No member/CIL completeness claim is based on that result. Method-body observations
below instead use the freshly cross-checked ILSpy reconstruction. The REA bundle
is retained at `/tmp/sts2-frame-audit/rea-evidence.json`.

## 1. What listener capture actually means

**Observed:** `CombatState.IterateHookListeners` builds a concrete list before
yielding its first combat listener. It captures allies then enemies, powers in
their collection order, player inventory and combat-card content (or the monster),
then modifiers, badges, and scaling. It checks type-specific `Contains` eligibility
when yielding each captured object. Mod subscribers are appended through a
separate iterator; this audit does not extend the snapshot claim to that path.
See [CombatState.cs](../../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Combat/CombatState.cs),
lines 411–493 and 550–604.

Capture freezes candidate identities/order, **not every value on those objects**.
For powers, `Contains` checks the owner's combat/activity rather than requiring
the same power instance to remain in the active power collection. Relics and
cards have their own removal/activity checks. A generic rule that a removed
listener is always skipped would be incorrect. Replacing a captured object with
a newly created instance of the same definition would also change identity.

The combat-ending guard is evaluated when each guarded enumeration starts, not
again between every listener. A dispatch already started can finish its remaining
eligible candidates after a listener kills an enemy. A later enumeration evaluates
the guard anew. See [Hook.cs](../../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Hooks/Hook.cs),
lines 35–62.

Run-scoped enumeration has a different boundary: it captures/yields its run list
before beginning the optional child-combat enumeration. Eagerly capturing one
combined run/combat list would change timing. See
[RunState.cs](../../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Runs/RunState.cs),
lines 545–595.

**Representation constraint:** preserve capture boundaries and independent
snapshots for simultaneously unfinished invocations. A live traversal of the
mutating source collection is not equivalent to the native captured list.

## 2. Draw batches, individual cards, and shuffle

**Observed:** `DrawInternal` owns a requested draw count, loop position, result
collection, and pile/capacity context. For each card it checks draw viability,
awaits a necessary shuffle, rechecks viability and capacity, selects the top card,
awaits moving it to hand, records draw history, awaits draw hooks, invokes the
card's drawn notification, then updates available hand capacity. Only afterward
does it attempt the next card. See
[CardPileCmd.cs](../../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Commands/CardPileCmd.cs),
lines 1007–1065.

Moving to hand is itself an awaited operation. Successful pile changes invoke
normal and late `AfterCardChangedPiles` dispatches before `Add` returns. Thus the
draw operation's full phase structure includes more than its final draw hook.
See CardPileCmd.cs, lines 324–356 and 478–545; Hook.cs, lines 167–179.

Each card calls `Hook.AfterCardDrawn` independently. That hook performs two
**separate** listener enumerations: `AfterCardDrawnEarly`, then ordinary
`AfterCardDrawn`. Each handler is awaited before execution-finished notification,
context pop, and advancement. The normal-pass snapshot is created after the early
pass finishes. See Hook.cs, lines 202–218.

Consequently:

- There is no one snapshot for a whole requested draw batch.
- A listener added during a pass does not enter that captured combat list, but
  may enter the next pass/card's list if eligible then.
- One card can change the hand, powers, or other listener membership before the
  next card's capture.
- A frame for multiple draws can still capture listeners at these precise times.
  Single-card frames are not necessary merely to obtain per-card snapshots.

The early pass can perform substantial child work: Hellraiser awaits auto-playing
a drawn Strike and removes it from its tracking set afterward. See
[HellraiserPower.cs](../../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/HellraiserPower.cs),
lines 37–68. The normal pass is therefore not safely captured before the early
pass, even if both would share a reusable progress slot in C++.

**Shuffle:** `DrawInternal` awaits `ShuffleIfNecessary` before choosing its next
card. Shuffle awaits card movement and then `AfterShuffle`; that hook captures
its own listeners and awaits them serially. Stratagem awaits a draw-pile selection
and adding the chosen cards to hand. The interrupted draw is still owed and must
recheck conditions when shuffle returns. See CardPileCmd.cs, lines 1047–1055,
1079–1132, and 1186–1194; Hook.cs, lines 1142–1151;
[StratagemPower.cs](../../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/StratagemPower.cs),
lines 17–27.

Shuffle is **not exclusively a draw child**. Reboot first moves its hand into
draw, explicitly awaits `Shuffle`, and only then starts drawing. Also,
`AutoPlayFromDrawPile` awaits necessary shuffles before moving cards into Play;
it is not a draw operation. See
[Reboot.cs](../../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Cards/Reboot.cs),
lines 23–30, and CardPileCmd.cs, lines 1143–1175. Putting all shuffle progress only
in `DrawFrame` would therefore not cover the native callers. A combined design
would need an appropriate owner for those other paths as well.

**Frame implication:** a combined draw frame needs operation-phase progress and
the current dispatch's progress; it must refresh the snapshot for each hook pass.
On the ordinary path these phases are sequential. No inspected method establishes
a requirement for several simultaneous same-level dispatch cursors inside one
draw invocation. Child draws, auto-plays, and damage invocations nevertheless need
their own independent progress. Preserve the native draw result identities too
where a caller uses its returned collection.

## 3. Exhaust is one card at a time; the caller owns the batch

**Observed:** native `CardCmd.Exhaust` takes one card, awaits its pile movement,
records history, and awaits its exhaust listeners before returning. The retained
method's documentation explicitly warns against a bulk exhaust implementation
that would interleave several cards before completing their hooks. See
[CardCmd.cs](../../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Commands/CardCmd.cs),
lines 225–250. The executable method ordering also establishes this sequence,
independently of its documentation. Hook.cs, lines 237–246, awaits each captured
exhaust listener serially.

A C++ batch coordinator remains possible: it must await each individual exhaust's
movement, hooks, and content completions before beginning the next exhaust. The
native warning rules out changing that order, not a particular C++ frame name.

Exhaust movement is not simply a guaranteed hand removal. `Add` checks removal
and owner state, reads the card's current pile, and can report failure; `Exhaust`
does not inspect that success flag before its history/hook calls. A captured card
leaving hand is not by itself grounds to silently drop it from a pending batch.
See CardPileCmd.cs, lines 373–404 and 478–506.

### Fiend Fire

**Observed:** Fiend Fire copies the hand's card identities with `ToList()`, saves
the original count, sequentially awaits each captured card's exhaust, then starts
its attack with that **original** hit count. See
[FiendFire.cs](../../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Cards/FiendFire.cs),
lines 30–49. The played card was moved to Play before `OnPlay`; its own keyword
exhaust is handled later by ordinary play cleanup if applicable. See
[CardModel.cs](../../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models/CardModel.cs),
lines 1870, 1933, 1988–2000, and 2085–2096.

**Inference:** cards drawn or generated during the exhaust hooks are not appended
to Fiend Fire's captured batch. Iterating the current hand until empty, or deriving
the hit count from successful exhausts afterward, would not implement this code.
The captured objects remain mutable: snapshotting identities does not snapshot
all their fields or guarantee that every later exhaust call moves a card.

### Selected exhausts and other selected effects

Purity awaits selection and then awaits each returned card's exhaust. Burning
Pact takes its selected card, awaits exhausting it, and only then performs its own
draw. The exhaustion's ordinary nested draw/choice path finishes before Burning
Pact's own draw begins. See
[Purity.cs](../../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Cards/Purity.cs),
lines 26–31, and
[BurningPact.cs](../../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Cards/BurningPact.cs),
lines 24–32.

`FromHand` captures candidate cards with `ToList()`; ordinary local UI confirmation
returns selected identities with another `ToList()`, in selection order. Custom
selector results are returned as `IEnumerable` without unconditional materializing
at that boundary, so the local-UI observation is not a claim about all providers.
See [CardSelectCmd.cs](../../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Commands/CardSelectCmd.cs),
lines 817–867, and
[NPlayerHand.cs](../../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Nodes.Combat/NPlayerHand.cs),
lines 986, 1165–1180, and 1246–1255.

This supports passing chosen operands to an exhaust operation instead of using a
generic selection-resolution frame. It does not establish that every other
selected effect is globally synchronous: native pile movement awaits its own
hooks. Native `CardCmd.Upgrade` is synchronous at its command boundary
(CardCmd.cs, lines 255–280), whereas `Add` is not. Audit each operation before
replacing its resumable resolution with a direct loop.

## 4. Content needs post-child progress beyond a listener cursor

| Native handler | Work after its awaited child | Evidence under D/ |
| --- | --- | --- |
| Joss Paper | Reduce the **live** exhaust counter modulo its threshold after drawing returns | `MegaCrit.Sts2.Core.Models.Relics/JossPaper.cs:126–134` |
| Dark Embrace, side-turn-end ethereal path | Clear the captured internal data object's ethereal count after drawing | `MegaCrit.Sts2.Core.Models.Powers/DarkEmbracePower.cs:53–61` |
| Burning Sticks | Mark the relic used only after adding its generated clone | `MegaCrit.Sts2.Core.Models.Relics/BurningSticks.cs:44–54` |
| Hellraiser | Remove the card from its auto-playing tracking set after auto-play | `MegaCrit.Sts2.Core.Models.Powers/HellraiserPower.cs:62–68` |
| Toasty Mittens | Apply Strength after the selected cards finish exhausting | `MegaCrit.Sts2.Core.Models.Relics/ToastyMittens.cs:38–42` |

For Joss Paper, a threshold count of five remains five while its draw is unfinished;
the modulo operation rereads the count after child work rather than assigning a
precomputed remainder. Its activation animation is separately fire-and-forget.
Ordinary Dark Embrace simply awaits drawing, but that does not eliminate the
dispatcher's own remaining-listener progress. Joss Paper and Dark Embrace also
have ethereal handling outside the current C++ supported slice.

**Inference:** a single “which listener is next?” cursor is insufficient to
describe every handler's unfinished work. A pending action enum can identify
simple completions, but future handlers may require operands such as a card
identity or retained content-instance data. These must live somewhere cloneable
in either design. The existence of that work does not require one new C++ frame
type per content object.

## 5. Speedster, multi-target damage, and a non-stack-like pause

**Observed:** Speedster is real in this build. On a non-hand-draw by its owner,
during that owner's side, it awaits unpowered damage to all hittable enemies.
The amount comes from the power's amount, not a fixed literal two. Gremlin Horn
handles an opposing-side death by awaiting energy gain and then a draw. See
[SpeedsterPower.cs](../../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/SpeedsterPower.cs),
lines 15–21, and
[GremlinHorn.cs](../../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/GremlinHorn.cs),
lines 24–30.

Damage snapshots targets, applies per-target processing with intervening awaited
hooks, then processes accumulated results and invokes death handling for killed
creatures. It is not simply “damage enemy A, fully resolve A's death, then damage
enemy B.” Target/index/result progress can itself need preservation around child
hooks. See [CreatureCmd.cs](../../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Commands/CreatureCmd.cs),
lines 258–285 and 392–433. This audit does not map every damage/redirection branch.

At death, `Hook.AfterDeath` starts each handler with a `HookPlayerChoiceContext`
and awaits **pause or completion**, then advances its enumeration. The context
can queue a `GenericHookGameAction`, signal its pause, and wait for that action to
start. Thus the unfinished handler can survive after the dispatch has advanced;
its queued action later awaits the retained task. See Hook.cs, lines 462–475;
[HookPlayerChoiceContext.cs](../../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.GameActions.Multiplayer/HookPlayerChoiceContext.cs),
lines 124–136 and 177–213; and
[GenericHookGameAction.cs](../../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.GameActions/GenericHookGameAction.cs),
lines 56–66. CreatureCmd.cs, lines 536–569, performs subsequent death cleanup
after `AfterDeath` returns, which need not mean every detached task has completed.
`TaskHelper.WhenAny` really awaits the first completing task, not all tasks; see
[TaskHelper.cs](../../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Helpers/TaskHelper.cs),
lines 44–53.

**Correction to the design discussion:** ordinary draw/exhaust hooks await their
handler tasks to completion, but this is not true of every native hook boundary.
The full Speedster → death → Gremlin Horn → Stratagem-choice example cannot be
specified as an unconditional LIFO chain merely from those effect descriptions.
The queued action/context behavior is static evidence; its precise observable
single-player ordering in that complete scenario still needs a runtime recording.
This is a requirement for future supported content, not a claim that current C++
recordings already fail on an implemented Speedster/Gremlin Horn path.

## 6. Consequences for separate versus combined hook frames

| Question | What the audited evidence permits or requires |
| --- | --- |
| Must draw/exhaust hook progress have its own frame? | No. Its range/cursor/phase can be stored in the owning operation if lifetime and capture timing are preserved. |
| Can one listener list cover a draw or exhaust batch? | No. Draw captures per card and separately per early/normal pass; each exhaust invokes a fresh dispatch. |
| Can sequential hook phases reuse one progress slot? | Plausibly yes on the inspected ordinary paths. Finish/release the previous phase before capturing the next. This is a representation inference. |
| Can shuffle progress always be owned by a draw frame? | No. Reboot and auto-play-from-draw-pile invoke shuffling outside a draw operation. |
| Does reuse remove nested snapshots? | No. Child draws, card plays, damage, and other live invocations need their own retained state. |
| Can an exhaust operation replace selected-exhaust resolution? | Yes in principle, provided it owns the selected identities/order and awaits each exhaust fully; Fiend Fire additionally needs fixed original membership/count. |
| Can Joss Paper's dedicated completion frame disappear? | Yes, if its exact post-draw work and required operands remain represented elsewhere. Moving the reset earlier is not equivalent. |
| Do separate frames solve all scheduling? | No. Native pause-to-queued-action boundaries additionally require appropriate task/action lifetime semantics. |

For a concrete comparison, derive two layouts from these constraints:

- **Separate:** an operation retains batch/phase progress and delegates the
  current captured range/cursor to a hook frame.
- **Combined:** the operation retains its current hook phase/range/cursor and
  whatever content-completion state the active handler needs.

Then measure the largest variant alternative, total inline state size, bytes
retained/copied in nested scenarios, actual simultaneously live frame counts,
and overflow allocations. Separate frames may use more stack slots; combining
may enlarge every slot if its payload becomes the variant maximum. Neither
direction follows from the native code alone. Native heap objects/async state
machines are evidence for lifetime and ordering, not a performance design to copy.

## Remaining verification

1. Runtime trace the complete Speedster/Gremlin Horn/Stratagem choice chain,
   including death cleanup, next listeners, and queued hook action execution.
2. Exercise listener addition/removal and replacements across early/normal passes
   and consecutive cards. Static capture/eligibility rules are established here;
   suspended-state/runtime regression fixtures have not been added.
3. Before supporting the corresponding effects, audit the remaining damage,
   generated-card, ethereal, cancellation, and combat-ending paths in their own
   scope. Mod subscribers and full multiplayer behavior are not covered here.
4. Compare actual layouts and representative MCTS workloads before choosing a
   representation for performance. This audit supplies constraints, not timings.

The engine and its tests were not modified. Validation for this report consists
of artifact identity inspection, the 27-type fresh-source comparison, source-link
checks, and `git diff --check`; no new gameplay parity or benchmark result is claimed.
