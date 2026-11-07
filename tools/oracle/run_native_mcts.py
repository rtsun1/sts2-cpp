#!/usr/bin/env python3
"""Time shared UCT inside one .NET process, using stock public RunSimulator calls."""
import argparse
import json
import hashlib
import os
import subprocess
from pathlib import Path
from mcts_stock import stock_build
from reference_runtime import JsonProcess, require, wire_json, strict_json

REPO = Path(__file__).resolve().parents[2]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference-submodule', type=Path, default=REPO / 'sts2-cli')
    parser.add_argument('--native-lib', type=Path)
    parser.add_argument('--bridge', type=Path, default=REPO / 'build/release/libmcts_search_bridge.so')
    parser.add_argument('--verify-cpp', action='store_true', help='Compare every result field with an untimed checked/trusted C++ verification search')
    parser.add_argument('--cpp-runner', type=Path, default=REPO / 'build/release/mcts_in_process')
    parser.add_argument('--scenario', type=Path, default=REPO / 'benchmarks/mcts/shrinker.json')
    parser.add_argument('--iterations', type=int, default=100)
    parser.add_argument('--horizon', type=int, default=64)
    parser.add_argument('--seed', type=int, default=1)
    parser.add_argument('--samples', type=int, default=3)
    parser.add_argument('--warmups', type=int, default=1)
    parser.add_argument('--timeout', type=float, default=600)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=False)
    scenario = json.loads(args.scenario.read_text())
    config = dict(scenario=scenario, iterations=args.iterations, horizon=args.horizon,
                  seed=args.seed, samples=args.samples, warmups=args.warmups)
    path = args.output_dir / 'config.json'
    path.write_text(json.dumps(config))
    report = dict(status='running', config=config,
                  timing='Includes root teardown/reconstruction, stock public method output construction, polling waits, C++ UCT and managed/native callbacks; excludes process startup, configuration parsing, final JSON and IPC.')
    try:
        require(args.bridge.is_file(), 'Build mcts_search_bridge first')
        report['bridge_sha256'] = hashlib.sha256(args.bridge.read_bytes()).hexdigest()
        report['lifecycle'] = 'Benchmark-only selector-scope capture/disposal and simulator event-handler removal; public gameplay methods unchanged'
        report['logging'] = 'Managed stdout/stderr suppressed during timed search'
        expected = None
        if args.verify_cpp:
            cpp_config = args.output_dir / 'cpp-verification.json'
            cpp_config.write_text(json.dumps(dict(scenario=scenario, iterations=args.iterations, horizon=args.horizon, seed=args.seed, verify=True)))
            cpp = subprocess.run([str(args.cpp_runner.resolve()), str(cpp_config.resolve())], capture_output=True, text=True, timeout=args.timeout)
            (args.output_dir / 'cpp-verification.log').write_text(cpp.stdout + cpp.stderr)
            require(cpp.returncode == 0, 'C++ verification failed; see cpp-verification.log')
            expected = strict_json(cpp.stdout)
        with stock_build(args.reference_submodule, args.native_lib or args.reference_submodule / 'lib',
                         args.output_dir, benchmark_host=REPO / 'benchmarks/mcts/native/BenchmarkMain.cs') as (command, env, provenance):
            report['reference'] = provenance
            env['LD_LIBRARY_PATH'] = str(args.bridge.resolve().parent) + os.pathsep + env.get('LD_LIBRARY_PATH', '')
            host = JsonProcess(command + [str(path.resolve())], env, args.output_dir / 'native-in-process', args.timeout, False)
            try:
                result = host.receive()
                require(host.process.wait(timeout=30) == 0, 'Native benchmark exited with error')
                require(result.get('profile') == 'mcts-native-in-process-replay-v1', 'Unexpected benchmark result')
                report['measurement'] = result
                rows = result['samples']
                require(rows, 'No measured samples')
                fields = ('action', 'iterations', 'transitions', 'nodes', 'best_visits', 'best_mean', 'terminal_rollouts', 'cutoffs')
                require(all(all(row[key] == rows[0][key] for key in fields) for row in rows), 'Native sample results drifted')
                if expected is not None:
                    require(all(row[key] == expected[key] for row in rows for key in fields), 'Native and C++ search results differ')
                    report['cpp_equivalence'] = 'passed'
                report['status'] = 'passed'
            finally: host.close()
    except Exception as error:
        report.update(status='failed', error=str(error))
        raise
    finally: (args.output_dir / 'report.json').write_text(wire_json(report) + '\n')
    print(wire_json(report))

if __name__ == '__main__': main()
