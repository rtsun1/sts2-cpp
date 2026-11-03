# Ordered combat card choices

Scope: [#18](https://github.com/TonyinBio/sts2-cpp/issues/18), pinned build
`0.111.0+41cef1ea`. This sheet separates implemented behavior from native evidence.
The complete evidence ledger is maintained below; a source audit or atomic model
comparison does not establish native scene initialization or partial callbacks.

## Public gameplay

`apply_selection` and `apply_action` accept a `SelectionInput`: current boundary,
typed `PromptId`, and a complete ordered `std::span<const CardHandle>`. Survivor's
single-card discard uses the same input with an empty prompt ID. Checked submission
validates count, distinctness, candidate membership, prompt and boundary before mutation.
Inspection and rejected inputs leave gameplay, RNG and clocks unchanged.

The caller owns proposed selections and any editing history. The engine borrows the
span only during submission and copies accepted handles once into continuation operand
storage before effects execute. Synchronous effects release their operands immediately;
exhaust frames retain them through nested prompts, cloning and checkpoint restoration,
then release them on completion. Actions and state have no separate selection-editing buffer.
Prompt candidates and limits accompany the suspended source frame in byte-copyable state,
with independent overflow where needed. Checkpoint revision 9's former partial-selection
count is reserved as zero; checkpoints containing partial edits are rejected as malformed.
The source card remains in Play; its energy cost and play hooks run once.

`selection_space(state)` exposes borrowed candidates and count limits.
`for_each_selection(space, workspace, consumer)` emits complete ordered selections
without replacement, using caller-owned workspace. Returning false stops traversal;
emitted spans are valid only during the callback. An eager consumer can copy every
result into its own storage. A future generator can replace traversal at this seam
without changing selection submission or effect execution. Discrete action views,
`get_actions` and `for_each_legal_action` omit selection decisions. Callers must inspect
`selection_space` before interpreting an empty discrete action set as terminal.
Never retain callback spans; use caller-owned handles with offset/count records (or
other storage with suitable lifetimes), and rebuild spans after storage relocation.

| Card | Suspended effect and constraints |
| --- | --- |
| Purity / upgraded | Retain; select zero through three/five hand cards, then submit. Exhaust them in selection order; finish and exhaust Purity. |
| Armaments | Gain five Block, then select an eligible unupgraded hand card and submit. Zero/one eligible candidates resolve automatically. Only the combat copy upgrades. |
| Armaments upgraded | Gain five Block and upgrade all eligible hand cards automatically. |
| Headbutt / upgraded | Deal nine/twelve damage, then choose a discard card for the draw-pile top. Zero/one candidates resolve automatically; a multi-candidate prompt accepts one complete submission. |
| Burning Pact / upgraded | Exhaust one hand card, then draw two/three. Zero/one candidates resolve automatically; a multi-candidate hand prompt accepts one complete submission. |
| True Grit | Gain seven Block, then exhaust a random hand card using the owned combat-card-selection RNG. |
| True Grit upgraded | Gain nine Block, then select one hand card to exhaust. Zero/one candidates resolve automatically; a multi-candidate hand prompt accepts one complete submission. |

Selection editing has no gameplay effects under the accepted simulator model.
Complete submissions preserve supplied order. Engine-level select/deselect/confirm
operations are removed; optional editing belongs to callers. Native automatic/manual
confirmation policy remains prompt metadata for historical recording adapters, not an
engine editing protocol.

The engine remains independent of JSON, filesystem access and game assemblies.
The declared custom-deck scenario builder initializes from native setup definitions
and a supplied deck, without changing generated pools or consuming expected states.
Purity's additional immutable saved-card default is audited separately because the
original setup audit covered character card pools. It adds no pool membership.

## Action and clock correspondence

One accepted complete submission consumes one boundary and configured clock step.
Rejected submissions and caller-side edits consume neither.

Historical recordings and evidence retain their original provenance and action intervals.
`sts2_run_scenario` adapts legacy select/deselect/confirm operations using caller-owned
editing storage. For that transport only, partial edits reproduce historical clock and
boundary increments and the old selection projection. Gameplay effects still enter the
engine as one complete submission. `tools/oracle/check_ordered_choices.py` retains its
zero-step comparison policy for the historical atomic model corpus. This compatibility
does not establish new native evidence for the bulk interface.

## Evidence ledger

Native rows below describe preserved historical captures, including the former incremental engine interface. They do not certify the new bulk submission interface.

| Surface | Current evidence |
| --- | --- |
| C++ gameplay and ownership | Current tests cover complete ordered submissions, invalid-input atomicity, independent enumeration outcomes, eager collection/early stop, checked/trusted agreement, owned suspended operands and clone/checkpoint futures. |
| Source contracts | Pinned `CardSelectCmd.FromHand`, `FromHandForUpgrade`, `FromCombatPile`; `NPlayerHand.SelectCards`, `SelectCardInSimpleMode`, `SelectCardInUpgradeMode`, `DeselectCard`; `NCombatPileCardSelectScreen.OnCardClicked`; each of the five native card classes. |
| Immutable Purity default | `selection-defaults.json`, captured from actual `ModelDb.Card<Purity>().ToMutable().ToSerializable()` with pinned assembly provenance. |
| Native atomic model comparison | All 26 cases / 62 boundaries captured, independently self-replayed and compared exactly against C++ public inputs. Preserved in `tests/parity/fixtures/reference/ordered-card-choices.json`, with per-case native build provenance, component hashes and explicit public action intervals. `ordered_card_choice_scenarios` verifies hashes and replays the corpus offline. |
| Historical native partial callbacks | [Preserved native selection-policy evidence](../../../data/evidence/native-selection-policies-18/README.md): six actual scene/callback scenarios passed in two independently initialized processes each. Eleven partial Purity/Armaments states match the C++ public prompt projection, followed by two completed confirmations; accepted C++ inputs each advance one boundary and one configured two-second clock step. Native callbacks do not advance the recorder transport clock; their logical-input correspondence is recorded separately. Native read purity covers the explicitly enumerated fingerprint surface. Headbutt's actual pile callback and command consumer run with pile visuals disabled; full presentation startup is outside this seam. Exact native resource-cache/renderer teardown diagnostics remain preserved and do not certify a leak-free application lifecycle. |
| Historical nested effect and hook interactions | Public clone/restore tests resume partial Purity choices into ordered Charon's Ashes/Joss Paper effects and threshold draws, with independent alternative selections. Six new native cases in `ordered-powers/` independently self-replay and compare exactly, including lethal first/later exhausts, blocked damage and downstream callbacks during combat completion. No supported callback creates a second simultaneous prompt; deeper input stops remain conditional on an actual supported native caller. |

Exact English, other characters and broader unsupported content remain outside
this slice. A missing native callback or interaction result remains an evidence gap
and prevents closing #18.
