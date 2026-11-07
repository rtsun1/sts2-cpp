# Replay-based MCTS baseline

## Native search without protocol or startup overhead

```sh
cmake --build --preset release --target mcts_search_bridge mcts_in_process
python3 tools/oracle/run_native_mcts.py \
  --reference-submodule /path/to/prepared/sts2-cli \
  --iterations 100 --horizon 64 --warmups 1 --samples 3 --verify-cpp \
  --output-dir /tmp/mcts-native-in-process
```

This separate Linux x64 profile, `mcts-native-in-process-replay-v1`, invokes the
same C++ UCT implementation through a C ABI and managed delegates. Callbacks call
the unchanged public `RunSimulator.StartRun`, `ExecuteAction` and `CleanUp`
methods directly. Each sample runs entirely inside one .NET process. The timer
excludes process startup, configuration JSON parsing and final report JSON/IPC.
Managed stdout/stderr are suppressed during timed search. Root reconstruction,
public-method decision dictionary/description construction, polling waits,
tree work and managed/native callback costs are included. It still reconstructs
roots through setup and prefix replay; it does not snapshot native combat state.

The temporary stock build includes an alternate benchmark entry point, leaving
the upstream submodule and its gameplay/action implementation unchanged. Stock
startup assumes process-lifetime ownership of its card selector and event
handlers. Benchmark-only teardown captures and disposes selector scopes using a
Harmony postfix and removes the old simulator's own event handlers before cleanup.
This bookkeeping is necessary for process reuse; it adds no replacement action
scheduler or gameplay transitions. Public root values are checked after every
reconstruction, and measured sample result fields must remain identical.

`--verify-cpp` runs an untimed C++ checked/trusted state-verification search and
compares every native sample's action and search statistics against its result.
This is sampled equivalence, not proof of complete hidden native state isolation.
Reports retain the host source, assembly, prepared game DLL and bridge hashes.
The supported prefixes currently contain ordinary actions for the supplied
single-enemy starter-deck scenarios; other prefix operation kinds fail explicitly.
The stock JSON measurement remains available through `run_mcts.py`.

## In-process C++ search

Build `mcts_in_process` to run the same UCT implementation directly against typed
engine inputs, cloning a prepared root before every simulation. There is no
per-action JSON, process communication, projection or trace hashing. The root is
prepared through checked prefix actions and validated once. Normal search uses
trusted execution; clone failures and execution failures terminate the benchmark.

```sh
cmake --build --preset release --target mcts_in_process
python3 - <<'PY'
import json
from pathlib import Path
scenario = json.loads(Path('benchmarks/mcts/shrinker.json').read_text())
Path('/tmp/search.json').write_text(json.dumps(dict(
    scenario=scenario, iterations=100, horizon=64, seed=1, repeats=100)))
PY
./build/release/mcts_in_process /tmp/search.json
```

The config embeds a scenario plus optional search budgets and `repeats` (default
1). Each repeat builds a fresh tree with the same seed. Reported search statistics
describe one search; `seconds` covers all repeats, `seconds_per_search` is their
average, and throughput counts transitions across all repeats. Initial scenario
preparation and final JSON output are outside timing; root cloning is inside.

Set `checked: true` for checked execution. Set `verify: true` in a separate
correctness run to clone every pre-action state and compare the complete result
against checked execution. Verification timing includes those additional costs.
The initial prefix decoder supports choose-option, travel, end-turn, card play
and diagnostic encounter entry; other operations fail explicitly. The same bounded
content profile as the replay runner applies.

This profile measures a different integration and branching mechanism from the
replay baseline. Comparing it with the current CLI pipeline describes application
throughput with available interfaces, not an isolated simulator-kernel speedup.

This is a small, single-threaded UCT runner with uniform random rollouts. The
same C++ search executable drives `sts2_engine` and a disposable build of the stock
`sts2-cli` C# sources using its provided JSON interface. It does not modify the reference submodule.

Each simulation resets the worker, replays the scenario prefix, traverses the
tree, expands at most one node, rolls out to a terminal boundary or horizon,
and backs up the score. Nodes store actions and statistics, not game states.
The most visited root action is recommended. This runs one search at the root;
it does not repeatedly choose actions to play an entire run.

## Build and run

```sh
cmake --preset release
cmake --build --preset release --target mcts_runner public_benchmark_worker mcts_search_test mcts_combat_test
ctest --test-dir build/release -L benchmark --output-on-failure

python3 tools/oracle/run_mcts.py --backend cpp --output-dir /tmp/mcts-cpp

python3 tools/oracle/run_mcts.py --backend both \
  --reference-submodule /path/to/prepared/sts2-cli \
  --iterations 100 --horizon 64 --seed 1 \
  --output-dir /tmp/mcts-both
```

Use a new output directory per invocation. `--backend native` runs just the
reference backend. A worktree without an initialized reference submodule can
use `--reference-submodule /path/to/main/checkout/sts2-cli`. The native backend
requires .NET and an upstream installation prepared with its own `setup.sh`.
By default it copies the prepared `sts2-cli/lib` directory; `--native-lib DIR`
selects another prepared library directory. Setup's headless game-DLL patches
are retained, and their actual hash is recorded. The runner copies unmodified
C# sources into a temporary directory and builds them, without invoking oracle
injection. It records revision, source and assembly hashes and retains build logs.
Stock JSON has no reset/snapshot command. Each native reconstruction therefore
uses a fresh process; this prevents accumulated singleton callbacks or pending
rewards from contaminating later roots. Startup is included in native search time.
No unsupported repeated `start_run` reset behavior is assumed.

C++ search defaults to `--cpp-execution trusted`. Use `--cpp-execution checked`
for a control run with the same legal actions and compact search projection.
Their trace digests and search results should match for identical budgets and
seeds. Setup, prefix replay and reward cleanup continue to use checked execution.
At each reconstructed root, the worker calls `valid_run` and establishes the
bounded content profile. It then caches `for_each_legal_action` inputs and submits
the selected cached input to `apply_action_trusted`, without per-action state or
legality validation. Operational failures still stop the run. Defensive engine
builds retain their diagnostic checks as usual.

The supplied scenario uses the existing `KERNELRUN14` fixture's first two
actions to reach the first Shrinker fight with the Ironclad starter deck.
`--scenario benchmarks/mcts/shrinker-low-hp.json` adds six empty turns to test
search from a damaged state and exercise deaths as well as victories.
`--scenario FILE` accepts `setup` plus `operations` in the same form as the
existing public worker scenarios. Every prefix is actually executed: a fixture
does not supply simulated outcomes. The prefix must end at a combat decision.

## Results and timing

Use `--backend cpp --instrument` to accumulate C++ worker wall times. Stock JSON
has no internal timing API, so native/both modes reject `--instrument`. Totals are reset before
search and fetched once afterward, outside the measured search interval; no
extra per-action messages are sent. `worker_timing` reports seconds and call
counts for execution, enumeration, projection, reconstruction, and C++ root
validation. Nested scopes are exclusive: projection excludes enumeration.
Enumeration includes constructing offered choices. Reconstruction includes
reset/teardown, initialization, prefix actions and reward cleanup.

`worker_seconds` sums those scopes. `unattributed_seconds` is elapsed search time
minus that sum: it includes uninstrumented worker work, response serialization,
IPC, launcher processing and search. It is not a direct IPC measurement.
These timings do not isolate the simulator CPU kernel. The disabled mode skips clocks
and counter updates; the timer scaffolding itself still has some overhead.

See the [preliminary checked/trusted comparison](results.md) for measured samples
and their limitations.

`report.json` contains the recommended action, visits and mean value, node and
simulation counts, terminal/cutoff counts, transitions, backend request counts,
elapsed seconds, transitions per second, and trace hashes. Process stdout/stderr
and reference build logs remain alongside it. Failed invocations exit nonzero
and retain a report with `status: failed`.

Timing includes search, root reconstruction, prefix replay, C++ root validation,
projection, JSON, pipes, Python dispatch and trace hashing. Native timing also
includes process startup/handshake and teardown for each simulated root. Build
and the first untimed preparation are excluded. Transition counts are search
actions only; prefix actions count toward elapsed time and backend requests.
This is `mcts-stock-json-v3`; earlier oracle-adapter v1/v2 measurements describe
another implementation and must not be labelled stock CLI measurements.

In `both` mode, results and digests of all search requests and normalized
responses must match. Shared fields are legal card/end-turn actions, HP/max HP,
block, energy, enemy HP/max HP, draw/discard counts and hand model IDs/costs.
Stock combat output does not expose all earlier projected fields, so turn,
exhaust count and upgrades are omitted from this comparison. Its game clock is
not controlled by the oracle action-clock setup field. This verifies sampled
observable behavior, not hidden-state parity. Each backend also checks repeated
root projections for drift. These exploratory timings include different public
interfaces and restoration mechanisms; they do not isolate simulator kernels.

## Workload and extension points

- [search.hpp](search.hpp) owns UCT, sampling, budgets and backup. Its template
  adapter provides `restore_root`, `actions`, `apply`, `terminal` and `evaluate`.
  Actions must be owning values; failures throw. Search storage may allocate;
  no engine state representation or engine allocation policy is changed.
- [main.cpp](main.cpp) implements that adapter over a minimal JSON request/reply
  protocol. It has no encounter, card or run-phase logic.
- [combat.hpp](combat.hpp) owns the C++ combat root preconditions, compact
  projection, legal action cache and checked/trusted execution selection. It
  accepts one Shrinker with plain Strike/Defend/Bash cards and the starting
  Burning Blood/Golden Pearl relic inventory. Extend the content profile here
  when adding another supported workload.
- [run_mcts.py](../../tools/oracle/run_mcts.py) owns process orchestration and
  replay reconstruction. `ReplayBackend` accepts a projection function; replace
  `combat_observation` to define run-segment boundaries, actions and evaluation.
  The search algorithm and transport do not need to change.
- [mcts_stock.py](../../tools/oracle/mcts_stock.py) builds unmodified upstream
  sources, translates stock JSON commands/responses and restarts native processes.
- [shrinker.json](shrinker.json) owns the initial setup and prefix. Change it to
  choose another supported starting point. Diagnostic encounter entry is an
  explicit prefix operation, not a legal player action discovered by MCTS.

The initial workload ends on leaving combat or dying. Death scores 0; victory
scores `0.5 + 0.5 * hp/max_hp`; a horizon cutoff scores `0.5 * hp/max_hp`.
The horizon counts tree and rollout actions together. This basic heuristic
ignores enemy damage at cutoffs and is not intended as a strong playing policy.
The full deterministic simulator state is reused on every reconstruction; there
is no hidden-state sampling or chance-node model.

The stock-response normalization currently supports one enemy and
starter-card combat, and rejects selections and other unsupported prompts.
Different enemy types or multiple targets require extending the workload profile;
changing a scenario file alone cannot add backend support. Run segments also
need reward/route action translation and a different stopping/evaluation policy.
Unsupported legal actions and execution failures stop the benchmark rather than
silently biasing search. Potions and selections are outside the initial workload.

Future native clone support can replace root reconstruction behind the same
search adapter. Treat that as a separate timing profile. Paired repetitions,
warmup stabilization, richer workloads and play strength are subsequent work.
