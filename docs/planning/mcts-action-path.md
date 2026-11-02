# MCTS action execution and enumeration

Status: design interview complete; requirements and implementation direction agreed.
Implementation is not approved.

## Agreed requirements

- For supported legal actions, execution without validation must produce the
  same resulting complete state as checked execution, including prompts and
  suspended effects.
- MCTS starts from a validated root, enumerates actions at each node, and applies
  an action only to its originating state or an exact copy. Reuse across
  different states is deferred until profiling justifies it.
- The design scope includes combat, prompts, and run decisions.
- Enumeration for MCTS returns legal actions, evaluating gameplay restrictions
  there rather than repeating them during trusted application. MCTS owns any
  retained action lists; the simulator does not cache them solely for search.
- Keep existing checked APIs and provide an explicitly selected trusted API.
  Share transition logic to minimize duplication.
- Remove redundant state-integrity and action-legality checks from the trusted
  path, while retaining operational limits, allocation-failure handling, and
  unsupported-content results discovered during execution.
- Do not filter legal travel actions by hidden destination capability.
- Measure enumeration, application, and MCTS-style branching separately before
  setting a numerical speedup target. Use scenarios the simulator supports today;
  compare supported run decisions such as travel and rewards and explicitly list
  unmeasured decisions. Do not expand gameplay support for these measurements.
- There is no MCTS runner. Any branching workload added for measurement is
  synthetic and does not establish actual MCTS throughput.
- Enumeration need not preserve existing action order. Choose an efficient
  order considering both action generation and consumption by the simulator,
  rather than optimizing enumeration alone. Avoid workarounds to retain
  historical ordering; no ordering performance claim is established yet.
- Correctness tests enumerate legal actions, apply them to independent copies
  through checked and trusted APIs, and compare results and complete states.
  Cover prompts, overflow, run transitions, and operational failures. Production
  trusted execution performs no full-state validation.
- Enumeration emits legal actions sequentially to a caller-controlled consumer;
  it does not require an engine-owned list or indexed access.
- Invalid states, illegal actions, and actions reused on a different state violate
  trusted API preconditions. Rejection and recovery are not guaranteed for misuse.
  Checked APIs remain available when callers cannot establish the preconditions.
- Execution need not preserve its input state on operational or unsupported-content
  failure. The state may be partially modified; the caller must discard it rather
  than resume simulation. Keep explicit failure reporting without rollback copies.
- This explicitly includes the checked API: unsupported-content and execution-budget
  failures do not require rollback. This does not authorize changing the checked
  API's behavior for rejected invalid inputs. Full content support is the long-term
  target, at which point valid inputs should not encounter unsupported content
  within the simulator's supported game scope. The execution budget may eventually
  be removed; it remains in force until that separate decision is made.
- Both APIs eliminate redundant downstream validation and share one execution
  implementation. Compile-time defensive validation can enable diagnostic checks
  at selected internal boundaries using reusable validators; it must not duplicate
  transitions or affect gameplay behavior. Performance builds omit those checks.
- Generate legal actions directly, evaluating shared conditions before expanding
  candidates where possible. Do not implement enumeration by constructing every
  conceivable action and invoking the general action validator for each one.

## Agreed implementation direction

The user accepted this document at the close of the design interview.

1. Separate state-integrity validation, action legality, and operational/capability
   handling. Reuse gameplay legality predicates between checked validation and
   legal-action enumeration without invoking full-state validation per candidate.
2. Keep checked entry points and add explicitly named trusted entry points. Route
   both through shared transition bodies, including prompt resumption and automatic
   run advancement. The trusted path must also bypass redundant validation in
   downstream execution; skipping only the public entry check is insufficient.
   Consolidate validation at checked boundaries and retain optional compile-time
   internal diagnostics in the shared execution implementation.
3. Provide sequential legal-action enumeration for trusted states through a
   caller-controlled consumer. Avoid repeated indexed scans, engine-owned action
   caches, and constraints to preserve historical ordering. Assess ordering and
   action representation together against generation and simulator application
   costs. Choose the concrete C++ interface after inspecting dispatch and
   compilation costs.
4. Preserve operational and unsupported-content handling. Run planning currently
   mixes legality, capability, and capacity responsibilities; do not bypass the
   whole planner under the assumption that every check is redundant validation.
   Prepare transitions directly in the caller's state where staging exists solely
   to preserve that state on failure. In particular, investigate removing the extra
   run copy for combat entry. Failure reporting remains required; rollback does not.
5. Add checked/trusted differential tests and isolated release measurements using
   supported combat, overflow, prompt, and run scenarios. Compare both action sets
   without assuming order. Report enumeration, application, and synthetic branching
   separately, including allocation counts and the contribution of clone cost.
6. The user decides implementation slices and learning checkpoints. After the
   grilling session, wait for the user to request slice proposals, then present
   them for approval or feedback before implementation. No slices or learning
   checkpoints are established by this document.

## Evidence and open questions

The architecture benchmark's `query_validate` workload constructs an action
view, traverses it, and explicitly validates each offered action again.
Combat view indexing also invokes validation and rescans the hand and targets.
These are concrete sources of repeated work, but their individual runtime
contributions have not been measured for this proposal. A reported roughly 30×
large-deck query cost does not establish a 30× whole-simulation slowdown.

Existing action views include restricted actions. Run travel previews deliberately
avoid revealing hidden room capability. The design must distinguish action
legality, supported content, and operational failures before promising that
enumerated actions always execute successfully.

The requirements interview is complete. Exact C++ signatures and numerical
performance targets remain deferred to implementation design review and baseline
measurement respectively. Wait for the user to request slice proposals; engine
implementation is not approved.
