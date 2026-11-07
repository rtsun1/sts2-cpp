# Replay-based MCTS baseline

This is a small, single-threaded UCT runner with uniform random rollouts. The
same C++ search executable drives `sts2_engine` and a disposable, instrumented
build of the pinned `sts2-cli`. It does not modify the reference submodule.

Each simulation resets the worker, replays the scenario prefix, traverses the
tree, expands at most one node, rolls out to a terminal boundary or horizon,
and backs up the score. Nodes store actions and statistics, not game states.
The most visited root action is recommended. This runs one search at the root;
it does not repeatedly choose actions to play an entire run.

## Build and run

```sh
cmake --preset release
cmake --build --preset release --target mcts_runner public_benchmark_worker mcts_search_test
ctest --test-dir build/release -L benchmark --output-on-failure

python3 tools/oracle/run_mcts.py --backend cpp --output-dir /tmp/mcts-cpp

python3 tools/oracle/run_mcts.py --backend both \
  --game-dir '/path/to/Slay the Spire 2' \
  --iterations 100 --horizon 64 --seed 1 \
  --output-dir /tmp/mcts-both
```

Use a new output directory per invocation. `--backend native` runs just the
reference backend. A worktree without an initialized reference submodule can
use `--reference-submodule /path/to/main/checkout/sts2-cli`. The native backend
requires the pinned game DLLs and .NET SDK; the existing reference builder checks
their versions and retains build logs. See [tools](../../tools/README.md).

The supplied scenario uses the existing `KERNELRUN14` fixture's first two
actions to reach the first Shrinker fight with the Ironclad starter deck.
`--scenario benchmarks/mcts/shrinker-low-hp.json` adds six empty turns to test
search from a damaged state and exercise deaths as well as victories.
`--scenario FILE` accepts `setup` plus `operations` in the same form as the
existing public worker scenarios. Every prefix is actually executed: a fixture
does not supply simulated outcomes. The prefix must end at a combat decision.

## Results and timing

`report.json` contains the recommended action, visits and mean value, node and
simulation counts, terminal/cutoff counts, transitions, backend request counts,
elapsed seconds, transitions per second, and trace hashes. Process stdout/stderr
and reference build logs remain alongside it. Failed invocations exit nonzero
and retain a report with `status: failed`.

Timing includes C++ search, root reset and setup, prefix replay, checked gameplay
calls, public projection, JSON, pipes, Python dispatch and trace hashing. Root
reconstruction first skips outstanding combat rewards in both backends so the
native reward tasks settle before reset; that cleanup is timed but not scored.
Worker startup and reference compilation are outside timing. The transition count is
search actions only; prefix actions are included in time and backend requests.
There is one untimed root preparation, but no JIT stabilization or statistical
sampling protocol. These are exploratory end-to-end search measurements, not
raw simulator throughput or a defensible speedup estimate. Both backends use
reconstruction even though the C++ engine can clone directly.

In `both` mode, the runner requires identical search results and digests of every
search request and projected response. Those responses include actions, score,
HP, energy, turn, enemy HP, pile sizes and hand IDs/costs/upgrades. This catches
divergence along the sampled workload; it does not establish full hidden-state
parity. Each backend also checks that every reconstructed root has the same
complete public observation as its initial root.

## Workload and extension points

- [search.hpp](search.hpp) owns UCT, sampling, budgets and backup. Its template
  adapter provides `restore_root`, `actions`, `apply`, `terminal` and `evaluate`.
  Actions must be owning values; failures throw. Search storage may allocate;
  no engine state representation or engine allocation policy is changed.
- [main.cpp](main.cpp) implements that adapter over a minimal JSON request/reply
  protocol. It has no encounter, card or run-phase logic.
- [run_mcts.py](../../tools/oracle/run_mcts.py) owns process orchestration and
  replay reconstruction. `ReplayBackend` accepts a projection function; replace
  `combat_observation` to define run-segment boundaries, actions and evaluation.
  The search algorithm and transport do not need to change.
- [shrinker.json](shrinker.json) owns the initial setup and prefix. Change it to
  choose another supported starting point. Diagnostic encounter entry is an
  explicit prefix operation, not a legal player action discovered by MCTS.

The initial workload ends on leaving combat or dying. Death scores 0; victory
scores `0.5 + 0.5 * hp/max_hp`; a horizon cutoff scores `0.5 * hp/max_hp`.
The horizon counts tree and rollout actions together. This basic heuristic
ignores enemy damage at cutoffs and is not intended as a strong playing policy.
The full deterministic simulator state is reused on every reconstruction; there
is no hidden-state sampling or chance-node model.

The reused native public projection currently supports one Shrinker enemy and
starter-card combat, and rejects selections and other unsupported prompts.
Different enemy types or multiple targets require extending that projection;
changing a scenario file alone cannot add backend support. Run segments also
need reward/route action translation and a different stopping/evaluation policy.
Unsupported legal actions and execution failures stop the benchmark rather than
silently biasing search. Potions and selections are outside the initial workload.

Future native clone support can replace root reconstruction behind the same
search adapter. Treat that as a separate timing profile. Paired repetitions,
warmup stabilization, richer workloads and play strength are subsequent work.
