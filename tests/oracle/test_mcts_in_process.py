"""Exercise cloned-root UCT against checked execution and replay golden results."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

executable = Path(sys.argv[1])
repo = Path(__file__).resolve().parents[2]
keys = ('action', 'iterations', 'transitions', 'nodes', 'best_visits', 'best_mean', 'terminal_rollouts', 'cutoffs')
with tempfile.TemporaryDirectory() as temporary:
    config_file = Path(temporary) / 'config.json'
    for scenario, iterations, transitions in [('shrinker.json', 100, 1986), ('shrinker-low-hp.json', 20, 248)]:
        config = dict(scenario=json.loads((repo / 'benchmarks/mcts' / scenario).read_text()),
                      iterations=iterations, horizon=64, seed=1)
        results = []
        for mode in ('trusted', 'checked', 'verify'):
            config.update(checked=mode == 'checked', verify=mode == 'verify')
            config_file.write_text(json.dumps(config))
            results.append(json.loads(subprocess.check_output([str(executable), str(config_file)], text=True)))
        for key in keys:
            assert all(result[key] == results[0][key] for result in results), key
        assert results[0]['transitions'] == transitions
        assert results[0]['terminal_rollouts'] == iterations
        assert results[0]['action'] == dict(kind='play_card', card_index=2, target_index=0)
        config['scenario']['setup']['character'] = 'Silent'
        config_file.write_text(json.dumps(config))
        assert subprocess.run([str(executable), str(config_file)], capture_output=True).returncode != 0
print('Cloned-root checked/trusted verification and replay golden results passed')
