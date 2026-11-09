# Multi-creature combat

The engine supports an authored scenario with one Ironclad and two Ascension 0
Shrinker Beetles, using the starting deck and existing monster behavior. This is
an engine test scenario; it does not establish that this roster is a native
encounter. Existing one-enemy recording parity remains a separate check.

```cpp
RunState state;
Scratch scratch;
CombatConfig config;
config.enemy_count = 2;
const auto result = initialize_combat(state, config, scratch);
```

Configured setup accepts one or two beetles, a Fuzzy Wurm Crawler, or Overgrowth
Crawlers (beetle then crawler). Native summoning encounters, multiplayer, and
allies remain outside this milestone.

## Ownership, positions and membership

`RunState` owns the complete simulation, including optional `CombatData` and
independent run/combat overflow storage. Authoritative records remain trivially
copyable; owning clones also copy any overflow storage independently, as required
by [ADR 0003](../../adr/0003-inline-state-with-correct-overflow.md).

The player occupies pool position 0. Encounter slot `i` maps to position `1 + i`;
slot numbers refer to positions in the encounter's ordered slot list. Living
creatures never move to compact holes. Removing a creature invalidates its handle
by advancing the slot generation; exhausted generation positions cannot be reused.

`CreatureState.enemy_member` records active enemy membership. `EnemyPoolRange`
traverses occupied members in increasing pool position and stores no roster.
A live pool record can remain outside active membership when suspended work still
references it. A dead member is excluded from new attack targets and enemy moves.
The existing `enemy()` and `enemy_handle()` accessors select the first member;
they are conveniences for one-enemy consumers, not a roster or living-target
selection rule.

Cards and creatures share `PoolData`/`PoolView` with a compile-time allocation
policy. Card pools retain their free list and ordinary `create()`. Creature pools
omit free-list storage and expose `available_at(index, limit)` and
`create_at(index, value, limit)`. An ungrown position can be available; insertion
grows storage and leaves intervening holes. Inline capacity is not a game limit:
larger collections use overflow.

Spawn rules choose positions outside the pool. For example, a last-empty-slot
rule scans the encounter's positions from highest to lowest and calls
`create_at` at the first available position. It must respect the encounter's slot
range rather than treating every ungrown position as a valid encounter slot.
The pool test covers this selection pattern; native rat content is not implemented.

## Targets and observations

`CombatInput.target` is a `CreatureHandle`, containing position and generation.
Use an empty handle for an action without an enemy target. Targeted cards offer
one action per living enemy; validation rejects dead, removed and stale-generation
targets. Execution captures the chosen handle in the card program. Removing a
creature from active membership does not redirect an existing program to another
creature, and slot reuse cannot make an old target identify the replacement.

`CombatObservationView.enemies` exposes each member's handle, HP, block, relevant
powers and intent. Its legacy scalar enemy fields summarize the first member.
`observe_card` and `inspect_card` accept an optional target handle for a
specific damage preview. Without a selected target, a multi-enemy damage preview
does not assume the first enemy's Vulnerable status. Borrowed observations must
be discarded after mutation, owner replacement or destruction.

JSON enemy labels and tool target indices resolve encounter positions to current
handles. Native diagnostic export and tracing reject multi-enemy states whose
native identity/history mapping has not been implemented; the custom scenario is
not exported as fabricated native parity evidence.

## Enemy turns and combat completion

Each creature owns its AI selection. Combat owns the turn number and enemy-side
flag. Traversal visits eligible occupied positions in order,
rechecking membership, HP and spawn eligibility. It checks the outcome after the
current enemy move completes, before another enemy acts.

`create_enemy_at` sets `skip_current_enemy_turn` when creation occurs during the
enemy side. Player-side creation remains eligible for the upcoming enemy turn.
Replacement generations start with their own AI and eligibility. Side-end power
work completes before skip flags are cleared. Enemy turns capture actor handles
once and store their cursor in `EnemyTurnFrame`. Each attack uses an
`EnemyAttackFrame`, allowing damage hooks and their nested choices to suspend
without repeating damage or advancing to later actors. Diagnostic callbacks
remain synchronous. See the [integration contract](../../planning/enemy-combat-integration.md).

Existing single-target effects use captured identities. Existing Shockwave and
Charon's Ashes effects traverse living enemies. Power application, snapshots and
expiry reacquire owners by handle after callbacks that can grow the creature pool;
local references must not survive storage relocation.

Victory requires the last living active enemy to die. Terminal handling resolves
Burning Blood and run rewards once. On victory, powers on retained live creatures
are cleared before their combat-card sources are released. Retained targets and
per-applier identities remain covered by the existing continuation regressions.

## Checkpoints and validation

The whole-run checkpoint uses magic `STS2RUN1`, version **19**. Its internal
combat payload uses magic `STS2CB02`, version **2**; it is not a standalone public
checkpoint API. Earlier checkpoint revisions are incompatible.

The run codec stores the creature pool once, including membership, per-creature
AI, generations and skip flags. The enemy-side flag is stored with combat data.
There is no serialized enemy roster or creature free list. Restore decodes and
validates a temporary run before publishing it. Semantic equality includes these
fields and ignores storage addresses and inline/overflow capacity.

[Multi-creature tests](../../../tests/unit/multi_creature_test.cpp) cover:

- Either target, target-specific previews and local Bash effects.
- Independent AI, slot order and stopping after lethal player damage.
- Either enemy kill order, continued survivor actions and final healing/rewards.
- Enemy-side spawns/replacements, player-side creation and side-end pool growth.
- Clone/restore of per-enemy AI, eligibility and suspended retained targets.
- Stale target rejection, terminal retained-power cleanup and unmapped diagnostics.

Existing recording and replay-parity checks remain part of the validation suite.
Passing tests establishes their tested scope, not full-game correctness. No
benchmarks were run for this change, and no performance improvement is claimed.

## Additional encounter compositions

`CombatConfig.encounter` and direct seeded combat setup now also accept
`ENCOUNTER.FUZZY_WURM_CRAWLER_WEAK` and `ENCOUNTER.OVERGROWTH_CRAWLERS`. The latter
creates Shrinker then Crawler without setting `enemy_count`. Natural run travel
uses the generated encounter queue. The authored two-beetle override remains
available only for Shrinker setup. See the
[enemy generalization scope](../../planning/enemy-encounter-generalization.md).
