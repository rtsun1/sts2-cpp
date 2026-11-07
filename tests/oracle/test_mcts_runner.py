"""Replay and workload contracts, independent of a game installation."""
import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools/oracle'))
from run_mcts import ReplayBackend, combat_observation


def response():
    return dict(ok=True, observation=dict(prompt='combat', hp=80, max_hp=80,
        choices=[dict(kind='play_card', index=2, enabled=True, target_enemy=True),
                 dict(kind='play_card', index=1, enabled=False, target_enemy=False),
                 dict(kind='end_turn', enabled=True)],
        combat=dict(hp=80, max_hp=80, block=0, energy=3, turn=1, enemy_hp=40,
                    enemy_max_hp=40, draw=5, discard=0, exhaust=0, hand=[])), capabilities=[0, 6, 0])


class MctsRunnerTests(unittest.TestCase):
    def test_translates_only_legal_actions_and_target_indices(self):
        projected = combat_observation(response())
        self.assertEqual(projected['actions'], [dict(kind='play_card', card_index=2, target_index=0),
                                               dict(kind='end_turn')])
        self.assertFalse(projected['terminal'])
        self.assertEqual(projected['value'], 0.5)

    def test_unsupported_actions_and_backend_errors_fail(self):
        data = response()
        data['capabilities'][0] = 10
        with self.assertRaisesRegex(RuntimeError, 'Unsupported legal action'):
            combat_observation(data)
        with self.assertRaisesRegex(RuntimeError, 'Worker failed'):
            combat_observation(dict(ok=False, error='unsupported content'))

    def test_victory_death_and_cutoff_have_distinct_scores(self):
        data = response()
        data['observation'].update(prompt='rewards', combat=None, hp=40)
        victory = combat_observation(data)
        self.assertTrue(victory['terminal'])
        self.assertEqual(victory['value'], 0.75)
        self.assertEqual(victory['actions'], [])
        data['observation'].update(prompt='dead', hp=0)
        self.assertEqual(combat_observation(data)['value'], 0.0)

    def test_restore_replays_prefix_checks_root_and_resets_only_after_start(self):
        class Host:
            requests = []
            result = response()
            def send(self, request):
                self.requests.append(request)
                return dict(reset=True) if request['cmd'] == 'oracle_reset' else copy.deepcopy(self.result)
        host = Host()
        backend = ReplayBackend(host, True, dict(setup={}, operations=[dict(cmd='action', action={})]))
        backend.restore()
        self.assertEqual(host.requests[0]['cmd'], 'benchmark_operation')
        backend.restore()
        self.assertEqual(host.requests[2], dict(cmd='oracle_reset', audit=False))
        self.assertEqual(backend.requests, 5)
        host.result['observation']['hp'] = 79
        with self.assertRaisesRegex(RuntimeError, 'Root replay drifted'):
            backend.restore()

    def test_restore_settles_rewards_before_reset(self):
        class Host:
            def __init__(self):
                self.requests = []
            def send(self, request):
                self.requests.append(request)
                return dict(reset=True) if request['cmd'] == 'reset' else response()
        host = Host()
        backend = ReplayBackend(host, False, dict(setup={}, operations=[]))
        backend.restore()
        backend.last_response = response()
        backend.last_response['observation']['prompt'] = 'rewards'
        backend.restore()
        self.assertEqual(host.requests[1], dict(cmd='action', action=dict(kind='skip_rewards')))
        self.assertEqual(host.requests[2], dict(cmd='reset'))


if __name__ == '__main__':
    unittest.main()
