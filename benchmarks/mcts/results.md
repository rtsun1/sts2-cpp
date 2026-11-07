# Preliminary checked/trusted comparison

Current native MCTS uses stock JSON (`mcts-stock-json-v3`). All native v1/v2
timings below are historical measurements of our injected oracle adapter, not
measurements of stock `sts2-cli`. They are retained for provenance.

## Stock JSON smoke comparison

Ten simulations, seed 1, horizon 64: both backends matched all normalized search
traces and results over 191 transitions. C++ replay took 0.574 s; stock JSON
took 19.527 s. Native uses a fresh process per root because stock JSON provides
no reset command; startup/handshake/teardown are included. This is one diagnostic
sample, with no confidence interval, and is not a kernel speed comparison.
A separate low-HP comparison also matched all traces and results over five
simulations and 64 transitions, exercising death handling. Its raw artifacts are
in `/tmp/mcts-stock-low-hp/`.
Raw report and logs for the initial-root sample: `/tmp/mcts-stock-full/`. The stock build used revision
`084d1aa3d8e118ca7ce8d8774ad16d6be9c92367` and upstream-prepared game DLL hash
`14b57dbaee1b58cb919625a662831054fd714b3b75b13b7449eac0e95b98a3e4`.

## In-process cloned-root search

The subsequent `mcts_in_process` adapter runs the shared UCT search using typed
engine inputs and `clone_into` for independent roots. Seven samples, each with
100 fresh searches of 100 simulations, horizon 64 and seed 1, produced median
sample elapsed time 0.175053 seconds: **1.75053 milliseconds per search**, or
**1.135 million transitions/second**. One complete sample warmed the process
before sampling. Each search completed the same 1,986 transitions as the replay
baseline. Build work had completed before sampling; no affinity controls apply.

Initial scenario setup is excluded. State cloning, enumeration, trusted action
execution, rollout policy and all tree allocation/traversal are included.
There is no per-action JSON, IPC, public projection or trace hashing. Root and
low-HP scenario search results match their replay golden results; verification
runs additionally compared complete checked/trusted engine states after every
searched action. The registered `mcts_in_process_parity` test preserves these checks.

Raw samples: `/tmp/mcts-clone-benchmark-results.json`; config:
`/tmp/mcts-clone-benchmark.json`. The older replay timings below use different
branch-reconstruction and transport mechanisms and should retain their separate
profile labels. These numbers do not establish a pure C++ versus native kernel ratio.

Measured 2026-11-07 in the `feat/basic-mcts` worktree, using the compact
`mcts-replay-combat-v2` projection in both simulator workers. Release build,
`STS2_DEFENSIVE_VALIDATION=OFF`, no affinity or background-load controls.

Each search uses the supplied `shrinker.json`, seed 1, 100 simulations and a
64-action horizon. Every search completed 1,986 transitions, with 100 terminal
rollouts and no horizon cutoffs. All modes produced identical search results and
trace digest `af38beb0d4f12997b4b2143549979db8d91fb8c53374d73335bd0fc2e9d4e980`.

One full search per mode warmed persistent workers. Three measured searches per
mode followed, in orders checked/trusted/native, native/trusted/checked, and
trusted/checked/native. Builds and other benchmark processes had completed before
measured sampling. There is no JIT stabilization guarantee or confidence interval.

| Mode | Sample 1 (s) | Sample 2 (s) | Sample 3 (s) | Median (s) | Transitions/s at median |
| --- | ---: | ---: | ---: | ---: | ---: |
| C++ checked | 5.143 | 5.151 | 5.266 | 5.151 | 385.6 |
| C++ trusted | 5.112 | 5.108 | 5.122 | 5.112 | 388.5 |
| sts2-cli | 16.701 | 16.734 | 16.406 | 16.701 | 118.9 |

The ratio of native to trusted median elapsed time is 3.27. The ratio of checked
to trusted median elapsed time is 1.008, a difference below 1%; this small sample
does not establish a meaningful checked/trusted speedup in this workload.

These are end-to-end search measurements including replay, root validation,
projection, JSON, IPC and Python dispatch. They do not isolate simulator kernel
or API execution costs. Both C++ modes use the same projection and enumeration;
their execution API differs. Earlier v1 measurements used a larger projection
and redundant validation in the general worker, so improvements over v1 cannot
be attributed solely to trusted execution.

The local raw report, build provenance and logs are in
`/tmp/mcts-api-benchmark-v2/`; the sampling script is
`/tmp/mcts-api-bench-v2.py`. The reference revision is
`084d1aa3d8e118ca7ce8d8774ad16d6be9c92367`, with pinned game DLL SHA-256
`0861bfa1df347538d932f22d580e75420f08082792eb914e53b4882764acdbe9`.

## Worker timing breakdown

Subsequent instrumented measurements use the same seed, scenario, 100 simulations
and 1,986 search transitions. Persistent workers received one full instrumented
warmup, then three paired instrumentation on/off searches per backend, alternating
the on/off order. All traces and search results matched. Build work had completed
before sampling. Raw data and logs: `/tmp/mcts-timing-overhead/`; sampling script:
`/tmp/mcts-timing-overhead.py`.

| Exclusive worker wall time | C++ trusted median (s) | sts2-cli median (s) |
| --- | ---: | ---: |
| Root reconstruction, prefix and cleanup | 3.498 | 12.383 |
| Search-action execution | 0.00464 | 2.372 |
| Legal enumeration / choice construction | 0.00546 | 0.0642 |
| Projection excluding enumeration | 0.0937 | 0.270 |
| Root validation / profile check | 0.00130 | Included in native adapter work |
| Unattributed elapsed time | 1.323 | 1.306 |
| Total instrumented search elapsed | 4.963 | 16.394 |

Each cell is an independent median, so category medians need not sum to the
median total. Reconstruction accounts for about 70% of C++ elapsed time and
76% of native elapsed time. The current reset/replay workload spends little of
its C++ time executing searched actions. Native execution includes action-adapter
work and boundary waits, so dividing these execution times does not measure a
pure simulator-kernel speedup. Unattributed time includes serialization, IPC,
Python dispatch, search and other uninstrumented work.

Instrumentation-disabled medians were 4.999 s for C++ and 16.324 s for native.
The observed enabled/disabled differences were -0.7% and +0.4%, respectively;
these samples show no consistent material penalty, not proof of zero overhead.
