#!/usr/bin/env python3
"""Run the same C++ UCT search against replay-backed C++ and native workers."""
import argparse
import contextlib
import hashlib
import json
import os
from pathlib import Path
import time

from reference_runtime import JsonProcess, reference_build, require, wire_json

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
                           ('hp', 'max_hp', 'block', 'energy', 'turn', 'enemy_hp', 'enemy_max_hp',
                            'draw', 'discard', 'exhaust')}
        state['hand'] = [{key: card[key] for key in ('id', 'cost', 'upgraded')}
                         for card in combat['hand']]
    return dict(terminal=terminal, value=value, actions=actions, state=state)


class ReplayBackend:
    """One live backend; each restore resets and replays the scenario prefix."""
    def __init__(self, host, native, scenario, project=combat_observation):
        self.host, self.native, self.scenario = host, native, scenario
        self.project = project
        self.requests = 0
        self.root = None
        self.last_response = None

    def operation(self, operation):
        request = dict(cmd='benchmark_operation', operation=operation, stop_on_death=True) if self.native else operation
        self.requests += 1
        response = self.host.send(request)
        require(response.get('ok'), f'Backend operation failed: {response}')
        self.last_response = response
        return response

    def restore(self):
        if self.root is not None:
            # Native reset requires outstanding reward tasks to settle. Use the
            # same explicit cleanup action in both backends; it is timed but is
            # not part of the searched combat or its score.
            if self.last_response['observation']['prompt'] == 'rewards':
                self.operation(dict(cmd='action', action=dict(kind='skip_rewards')))
            self.requests += 1
            reset = self.host.send(dict(cmd='oracle_reset', audit=False) if self.native else dict(cmd='reset'))
            require(reset.get('reset'), 'Backend reset failed')
        response = self.operation(dict(cmd='start', setup=self.scenario['setup']))
        for operation in self.scenario['operations']:
            response = self.operation(operation)
        observation = self.project(response)
        require(not observation['terminal'], 'Scenario must end at a decision within the workload')
        # Check the complete public response, not only the search projection.
        if self.root is None:
            self.root = response['observation']
        require(self.root == response['observation'], 'Root replay drifted')
        return observation

    def step(self, action):
        return self.project(self.operation(dict(cmd='action', action=action)))


def run_search(args, backend, log_prefix):
    search = JsonProcess([str(args.runner.resolve())], None, log_prefix, args.timeout, False)
    digest = hashlib.sha256()
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
        return dict(result=message, seconds=elapsed, transitions_per_second=message['transitions'] / elapsed,
                    backend_requests=backend.requests, trace_sha256=digest.hexdigest())
    finally:
        search.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--backend', choices=('cpp', 'native', 'both'), default='both')
    parser.add_argument('--runner', type=Path, default=REPO / 'build/release/mcts_runner')
    parser.add_argument('--worker', type=Path, default=REPO / 'build/release/public_benchmark_worker')
    parser.add_argument('--game-dir', type=Path)
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
    require(args.backend == 'cpp' or args.game_dir is not None, '--game-dir required for native backend')
    args.output_dir.mkdir(parents=True, exist_ok=False)
    scenario_bytes = args.scenario.read_bytes()
    scenario = json.loads(scenario_bytes)
    report = dict(status='running', profile='mcts-replay-combat-v1', iterations=args.iterations,
                  horizon=args.horizon, seed=args.seed, scenario_sha256=hashlib.sha256(scenario_bytes).hexdigest(),
                  runner_sha256=hashlib.sha256(args.runner.read_bytes()).hexdigest(),
                  timing='Includes search, root reset/setup/prefix replay, checked actions, projection, JSON and IPC; excludes worker startup/build.',
                  backends={})
    try:
        with contextlib.ExitStack() as stack:
            for name in ('cpp', 'native') if args.backend == 'both' else (args.backend,):
                if name == 'native':
                    os.environ['STS2_REFERENCE_SUBMODULE'] = str(args.reference_submodule.resolve())
                    command, env, provenance = stack.enter_context(reference_build(
                        args.game_dir, args.output_dir, extra_sources=(Path(__file__),), configuration='Release'))
                    report['reference'] = provenance
                else:
                    command, env = [str(args.worker.resolve())], None
                    report['cpp_worker_sha256'] = hashlib.sha256(args.worker.read_bytes()).hexdigest()
                host = JsonProcess(command, env, args.output_dir / name, args.timeout, False)
                try:
                    if name == 'native':
                        require(host.receive().get('type') == 'ready', 'Missing native handshake')
                    backend = ReplayBackend(host, name == 'native', scenario)
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
