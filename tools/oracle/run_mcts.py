#!/usr/bin/env python3
"""Run shared C++ UCT against a C++ worker and stock sts2-cli JSON."""
import argparse
import contextlib
import hashlib
import json
from pathlib import Path
import time

from reference_runtime import JsonProcess, require, wire_json

from mcts_stock import stock_build, StockBackend

REPO = Path(__file__).resolve().parents[2]


def combat_observation(response):
    """Workload policy: stop at death or leaving combat; score survival and HP.

    Replace this function for a run-segment workload. Keep protocol translation
    in ReplayBackend, rather than teaching UCT about game phases or card indices.
    """
    require(response.get('ok'), f'Worker failed: {response}')
    view = response['observation']
    hp, max_hp = view['hp'], view['max_hp']
    require(max_hp > 0, 'Invalid max HP')
    terminal = hp <= 0 or view['prompt'] != 'combat'
    if hp <= 0:
        value = 0.0
    elif terminal:
        value = 0.5 + 0.5 * hp / max_hp
    else:
        # Cutoffs rank below victories. This is a baseline policy, not a strong player.
        value = 0.5 * hp / max_hp
    actions = []
    choices, capabilities = view['choices'], response['capabilities']
    require(len(choices) == len(capabilities), 'Missing capability diagnostics')
    if not terminal:
        for choice, capability in zip(choices, capabilities):
            if not choice['enabled']:
                continue
            require(capability == 0, f'Unsupported legal action: {choice}')
            kind = choice['kind']
            action = {'kind': kind}
            if kind == 'play_card':
                action['card_index'] = choice['index']
                if choice['target_enemy']:
                    action['target_index'] = choice.get('target_index', 0)
            else:
                require(kind == 'end_turn', f'Unsupported combat action: {choice}')
            actions.append(action)
        require(actions, 'Combat has no supported actions')
    # Include public combat details in the comparison digest, even though UCT
    # only consumes actions, terminal, and value. This is not a full-state audit.
    combat = view.get('combat')
    state = dict(hp=hp, max_hp=max_hp, prompt=view['prompt'])
    if combat is not None:
        state['combat'] = {key: combat[key] for key in
                           ('hp', 'max_hp', 'block', 'energy', 'enemy_hp', 'enemy_max_hp', 'draw', 'discard')}
        state['hand'] = [{key: card[key] for key in ('id', 'cost')}
                         for card in combat['hand']]
    return dict(terminal=terminal, value=value, actions=actions, state=state)


class ReplayBackend:
    """One live backend; each restore resets and replays the scenario prefix."""
    def __init__(self, host, scenario, project=combat_observation, cpp_execution='trusted'):
        self.host, self.scenario = host, scenario
        self.project = project
        self.cpp_execution = cpp_execution
        self.requests = 0
        self.root = None
        self.last_response = None

    def timing(self, enabled=None):
        operation = dict(cmd='timing')
        if enabled is not None:
            operation['enabled'] = enabled
        request = operation
        return self.host.send(request)['timing']

    def operation(self, operation):
        request = operation
        self.requests += 1
        response = self.host.send(request)
        require(response.get('ok'), f'Backend operation failed: {response}')
        self.last_response = response
        return response

    def restore(self):
        if self.root is not None:
            # Discard outstanding rewards before resetting the C++ worker.
            if self.last_response['observation']['prompt'] == 'rewards':
                self.operation(dict(cmd='action', action=dict(kind='skip_rewards')))
            self.requests += 1
            reset = self.host.send(dict(cmd='reset'))
            require(reset.get('reset'), 'Backend reset failed')
        response = self.operation(dict(cmd='start', setup=self.scenario['setup']))
        for operation in self.scenario['operations']:
            response = self.operation(operation)
        response = self.operation(dict(cmd='mcts_root', execution=self.cpp_execution))
        observation = self.project(response)
        require(not observation['terminal'], 'Scenario must end at a decision within the workload')
        # Check the complete worker root projection on every reconstruction.
        if self.root is None:
            self.root = response['observation']
        require(self.root == response['observation'], 'Root replay drifted')
        return observation

    def step(self, action):
        actions = self.project(self.last_response)['actions']
        return self.project(self.operation(dict(cmd='mcts_step', index=actions.index(action))))


def run_search(args, backend, log_prefix):
    search = JsonProcess([str(args.runner.resolve())], None, log_prefix, args.timeout, False)
    digest = hashlib.sha256()
    instrument = getattr(args, "instrument", False)
    backend.timing(instrument)
    started = time.perf_counter()
    try:
        message = search.send(dict(iterations=args.iterations, horizon=args.horizon, seed=args.seed))
        while message['cmd'] != 'result':
            if message['cmd'] == 'restore':
                reply = backend.restore()
            elif message['cmd'] == 'step':
                reply = backend.step(message['action'])
            else:
                raise RuntimeError(f'Unknown search request: {message}')
            digest.update(json.dumps([message, reply], sort_keys=True, separators=(',', ':')).encode())
            message = search.send(reply)
        elapsed = time.perf_counter() - started
        require(search.process.wait(timeout=args.timeout) == 0, 'Search process failed')
        worker_timing = backend.timing() if instrument else None
        worker_seconds = sum(float(value['seconds']) for value in worker_timing.values()) if instrument else None
        return dict(worker_timing=worker_timing, worker_seconds=worker_seconds,
                    unattributed_seconds=elapsed - worker_seconds if instrument else None,
                    result=message, seconds=elapsed, transitions_per_second=message['transitions'] / elapsed,
                    backend_requests=backend.requests, trace_sha256=digest.hexdigest())
    finally:
        search.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--backend', choices=('cpp', 'native', 'both'), default='both')
    parser.add_argument('--instrument', action='store_true', help='Accumulate exclusive worker timings')
    parser.add_argument('--cpp-execution', choices=('trusted', 'checked'), default='trusted')
    parser.add_argument('--runner', type=Path, default=REPO / 'build/release/mcts_runner')
    parser.add_argument('--worker', type=Path, default=REPO / 'build/release/public_benchmark_worker')
    parser.add_argument('--native-lib', type=Path, help='Prepared upstream headless lib directory')
    parser.add_argument('--reference-submodule', type=Path, default=REPO / 'sts2-cli')
    parser.add_argument('--scenario', type=Path, default=REPO / 'benchmarks/mcts/shrinker.json')
    parser.add_argument('--iterations', type=int, default=100)
    parser.add_argument('--horizon', type=int, default=32)
    parser.add_argument('--seed', type=int, default=1)
    parser.add_argument('--timeout', type=float, default=60)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    require(0 < args.iterations <= 10000000 and 0 < args.horizon <= 100000, 'Invalid search budget')
    require(0 <= args.seed < 2**64 and args.timeout > 0, 'Invalid seed or timeout')
    require(not args.instrument or args.backend == 'cpp', 'Stock native JSON has no internal timing API')
    args.output_dir.mkdir(parents=True, exist_ok=False)
    scenario_bytes = args.scenario.read_bytes()
    scenario = json.loads(scenario_bytes)
    report = dict(status='running', profile='mcts-stock-json-v3', iterations=args.iterations,
                  cpp_execution=args.cpp_execution, instrumented=args.instrument,
                  horizon=args.horizon, seed=args.seed, scenario_sha256=hashlib.sha256(scenario_bytes).hexdigest(),
                  runner_sha256=hashlib.sha256(args.runner.read_bytes()).hexdigest(),
                  timing='Includes search, checked root setup/prefix replay, root validation, selected C++ execution mode, JSON and IPC; native uses stock output and includes fresh process startup per simulation; excludes initial preparation/build.',
                  backends={})
    try:
        with contextlib.ExitStack() as stack:
            for name in ('cpp', 'native') if args.backend == 'both' else (args.backend,):
                if name == 'native':
                    command, env, provenance = stack.enter_context(stock_build(
                        args.reference_submodule, args.native_lib or args.reference_submodule / 'lib', args.output_dir))
                    report['reference'] = provenance
                    backend = StockBackend(command, env, args.output_dir, args.timeout, scenario)
                    try:
                        backend.restore()
                        backend.requests = 0
                        report['backends'][name] = run_search(args, backend, args.output_dir / f'{name}-search')
                    finally:
                        backend.close()
                    continue
                else:
                    command, env = [str(args.worker.resolve())], None
                    report['cpp_worker_sha256'] = hashlib.sha256(args.worker.read_bytes()).hexdigest()
                host = JsonProcess(command, env, args.output_dir / name, args.timeout, False)
                try:
                    backend = ReplayBackend(host, scenario, cpp_execution=args.cpp_execution)
                    # Untimed initialization makes setup errors explicit before measurement.
                    backend.restore()
                    backend.requests = 0
                    report['backends'][name] = run_search(args, backend, args.output_dir / f'{name}-search')
                finally:
                    host.close()
        if args.backend == 'both':
            cpp, native = (report['backends'][name] for name in ('cpp', 'native'))
            require(cpp['trace_sha256'] == native['trace_sha256'], 'Backend search traces differ; timings are not comparable')
            require(cpp['result'] == native['result'], 'Backend search results differ')
        report['status'] = 'passed'
    except Exception as error:
        report.update(status='failed', error=str(error))
        raise
    finally:
        (args.output_dir / 'report.json').write_text(wire_json(report) + '\n')
    print(wire_json(report))


if __name__ == '__main__':
    main()
