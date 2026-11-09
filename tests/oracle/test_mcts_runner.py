"""Replay and workload contracts, independent of a game installation."""
import copy
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools/oracle'))
from run_mcts import ReplayBackend, combat_observation
from mcts_stock import project_stock, StockBackend


def response():
    return dict(ok=True, observation=dict(prompt='combat', hp=80, max_hp=80,
        choices=[dict(kind='play_card', index=2, enabled=True, target_enemy=True),
                 dict(kind='play_card', index=1, enabled=False, target_enemy=False),
                 dict(kind='end_turn', enabled=True)],
        combat=dict(hp=80, max_hp=80, block=0, energy=3, turn=1, enemy_hp=40,
                    enemy_max_hp=40, draw=5, discard=0, exhaust=0, hand=[])), capabilities=[0, 6, 0])


class MctsRunnerTests(unittest.TestCase):
    def test_timing_controls_use_one_request_without_changing_search_boundary(self):
        class Host:
            def send(self, request):
                self.request = request
                return dict(timing={})
        host = Host()
        backend = ReplayBackend(host, dict(setup={}, operations=[]))
        self.assertEqual(backend.timing(True), {})
        self.assertEqual(host.request, dict(cmd='timing', enabled=True))
        self.assertEqual(backend.requests, 0)
        self.assertIsNone(backend.last_response)

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
            def __init__(self):
                self.requests = []
                self.result = response()
            def send(self, request):
                self.requests.append(request)
                return dict(reset=True) if request['cmd'] == 'reset' else copy.deepcopy(self.result)
        host = Host()
        backend = ReplayBackend(host, dict(setup={}, operations=[dict(cmd='action', action={})]))
        backend.restore()
        self.assertEqual(host.requests[0]['cmd'], 'start')
        backend.restore()
        self.assertEqual(host.requests[3], dict(cmd='reset'))
        self.assertEqual(backend.requests, 7)
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
        backend = ReplayBackend(host, dict(setup={}, operations=[]))
        backend.restore()
        backend.last_response = response()
        backend.last_response['observation']['prompt'] = 'rewards'
        backend.restore()
        self.assertEqual(host.requests[2], dict(cmd='action', action=dict(kind='skip_rewards')))
        self.assertEqual(host.requests[3], dict(cmd='reset'))

    def test_cpp_prepares_root_and_submits_cached_legal_action_index(self):
        class Host:
            def __init__(self):
                self.requests = []
            def send(self, request):
                self.requests.append(request)
                data = response()
                # Search projection contains legal actions only.
                del data['observation']['choices'][1]
                del data['capabilities'][1]
                return data
        host = Host()
        backend = ReplayBackend(host, dict(setup={}, operations=[]))
        backend.restore()
        self.assertEqual(host.requests[-1], dict(cmd='mcts_root', execution='trusted'))
        backend.step(dict(kind='end_turn'))
        self.assertEqual(host.requests[-1], dict(cmd='mcts_step', index=1))


class StockProtocolTests(unittest.TestCase):
    def stock_response(self):
        return dict(type='decision', decision='combat_play', player=dict(hp=80, max_hp=80, block=0),
                    energy=3, draw_pile_count=5, discard_pile_count=0,
                    enemies=[dict(index=0, hp=40, max_hp=40)], hand=[
                        dict(index=0, id='CARD.STRIKE_IRONCLAD', cost=1, can_play=True, target_type='AnyEnemy'),
                        dict(index=1, id='CARD.DEFEND_IRONCLAD', cost=1, can_play=False, target_type='Self')])

    def test_stock_projection_matches_shared_fields_and_action_order(self):
        projected = project_stock(self.stock_response())
        self.assertEqual(projected['actions'], [dict(kind='play_card', card_index=0, target_index=0), dict(kind='end_turn')])
        self.assertEqual(projected['state']['combat']['enemy_hp'], 40)
        self.assertFalse(projected['terminal'])

    def test_stock_terminals_and_unsupported_content(self):
        reply = self.stock_response()
        reply.update(decision='game_over', player=dict(hp=0, max_hp=80))
        self.assertEqual(project_stock(reply)['value'], 0)
        reply.update(decision='card_reward', player=dict(hp=40, max_hp=80))
        self.assertEqual(project_stock(reply)['value'], 0.75)
        with self.assertRaises(RuntimeError): project_stock(dict(type='error'))

    def test_stock_restore_starts_fresh_processes_and_checks_root_drift(self):
        replies = self.stock_response()
        hosts = []
        class Host:
            def __init__(self, *args):
                self.closed = False
                self.requests = []
                hosts.append(self)
            def receive(self): return dict(type='ready')
            def send(self, request):
                self.requests.append(request)
                return copy.deepcopy(replies)
            def close(self): self.closed = True
        setup = dict(kind='seeded_run', character='Ironclad', ascension=0, seed='TEST')
        backend = StockBackend(['stock'], None, Path('/tmp'), 60,
                               dict(setup=setup, operations=[dict(cmd='action', action=dict(kind='end_turn'))]))
        with patch('mcts_stock.JsonProcess', Host):
            backend.restore()
            backend.restore()
            self.assertEqual(len(hosts), 2)
            self.assertTrue(hosts[0].closed)
            self.assertEqual([request['cmd'] for request in hosts[1].requests], ['start_run', 'action'])
            replies['player']['hp'] = 79
            with self.assertRaisesRegex(RuntimeError, 'root drifted'):
                backend.restore()
            backend.close()
            self.assertTrue(hosts[-1].closed)

    def test_stock_action_uses_only_public_protocol(self):
        class Host:
            def send(self, request): self.request = request; return {}
        backend = StockBackend([], None, Path('/tmp'), 60, {})
        backend.host = Host()
        backend.action(dict(kind='play_card', card_index=2, target_index=0))
        self.assertEqual(backend.host.request, dict(cmd='action', action='play_card', args=dict(card_index=2, target_index=0)))
        with self.assertRaises(RuntimeError): backend.timing(True)


if __name__ == '__main__':
    unittest.main()
