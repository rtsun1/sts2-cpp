"""Stock JSON support, with an optional separate in-process benchmark entry point."""
import contextlib
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from reference_runtime import JsonProcess, require


@contextlib.contextmanager
def stock_build(submodule, lib, logs, *, benchmark_host=None):
    submodule, lib = submodule.resolve(), lib.resolve()
    require((lib / 'sts2.dll').is_file(), 'Run upstream sts2-cli setup.sh first, or supply --native-lib with prepared headless DLLs')
    require(not subprocess.check_output(['git', '-C', str(submodule), 'status', '--porcelain', '--untracked-files=normal'], text=True).strip(), 'Stock submodule must have clean sources')
    with tempfile.TemporaryDirectory(prefix='mcts-stock-') as temporary:
        root = Path(temporary)
        shutil.copytree(submodule, root, dirs_exist_ok=True,
                        ignore=shutil.ignore_patterns('.git', 'bin', 'obj', 'lib', 'logs', 'saves', '__pycache__'))
        shutil.copytree(lib, root / 'lib')
        env = dict(os.environ, STS2_LIB=str(root / 'lib'), STS2_GAME_DIR=str(root / 'lib'),
                   DOTNET_CLI_HOME=str(logs / 'dotnet-home'), DOTNET_CLI_TELEMETRY_OPTOUT='1')
        project = root / 'src/Sts2Headless/Sts2Headless.csproj'
        if benchmark_host is not None:
            shutil.copy2(benchmark_host, project.parent / 'BenchmarkMain.cs')
            project.write_text(project.read_text().replace('<OutputType>Exe</OutputType>',
                '<OutputType>Exe</OutputType><StartupObject>BenchmarkMain</StartupObject>'))
        result = subprocess.run(['dotnet', 'build', str(project), '-c', 'Release', '--nologo'],
                                env=env, capture_output=True, text=True, timeout=120)
        (logs / 'stock-build.log').write_text(result.stdout + result.stderr)
        require(result.returncode == 0, 'Stock build failed; see stock-build.log')
        assembly = project.parent / 'bin/Release/net9.0/Sts2Headless.dll'
        source_hash = hashlib.sha256()
        for source in sorted((root / 'src').rglob('*')):
            if source.suffix in ('.cs', '.csproj'):
                source_hash.update(source.relative_to(root).as_posix().encode() + b'\0' + source.read_bytes())
        yield ['dotnet', str(assembly)], env, dict(
            source_sha256=source_hash.hexdigest(),
            integration='public-api-in-process-host' if benchmark_host else 'stock-json-no-injected-adapter',
            revision=subprocess.check_output(['git', '-C', str(submodule), 'rev-parse', 'HEAD'], text=True).strip(),
            game_dll_sha256=hashlib.sha256((lib / 'sts2.dll').read_bytes()).hexdigest(),
            assembly_sha256=hashlib.sha256(assembly.read_bytes()).hexdigest())


def project_stock(reply):
    require(reply.get('type') == 'decision', f'Unexpected stock response: {reply}')
    require(reply.get('decision') in ('combat_play', 'game_over', 'card_reward', 'combat_reward', 'rewards', 'map_select'),
            f'Unsupported stock decision: {reply.get("decision")}')
    player = reply['player']
    combat = reply['decision'] == 'combat_play'
    hp, max_hp = player['hp'], player['max_hp']
    state = dict(hp=hp, max_hp=max_hp, prompt='dead' if hp <= 0 else 'combat' if combat else 'rewards')
    actions = []
    if combat:
        enemies = reply['enemies']
        require(len(enemies) == 1, 'Stock MCTS supports one enemy')
        state['combat'] = dict(hp=hp, max_hp=max_hp, block=player['block'], energy=reply['energy'],
                               enemy_hp=enemies[0]['hp'], enemy_max_hp=enemies[0]['max_hp'],
                               draw=reply['draw_pile_count'], discard=reply['discard_pile_count'])
        state['hand'] = [dict(id=c['id'], cost=c['cost']) for c in reply['hand']]
        for card in reply['hand']:
            if not card['can_play']: continue
            action = dict(kind='play_card', card_index=card['index'])
            if card['target_type'] == 'AnyEnemy': action['target_index'] = enemies[0]['index']
            else: require(card['target_type'] in ('Self', 'None'), 'Unsupported target type')
            actions.append(action)
        actions.append(dict(kind='end_turn'))
    terminal = hp <= 0 or not combat
    return dict(terminal=terminal, value=0.0 if hp <= 0 else (0.5 if terminal else 0) + 0.5 * hp / max_hp,
                actions=actions, state=state)


class StockBackend:
    """A fresh stock process per reconstructed root avoids unsupported singleton reset."""
    def __init__(self, command, env, logs, timeout, scenario):
        self.command, self.env, self.logs, self.timeout, self.scenario = command, env, logs, timeout, scenario
        self.host = None
        self.requests = 0
        self.generation = 0
        self.root = None

    def close(self):
        if self.host is not None:
            self.host.close()
            self.host = None

    def timing(self, enabled=None):
        require(not enabled, 'Stock sts2-cli exposes no worker timers; use --instrument only with --backend cpp')
        return {}

    def send(self, request):
        self.requests += 1
        return self.host.send(request)

    def action(self, action):
        return self.send(dict(cmd='action', action=action['kind'], args={k:v for k,v in action.items() if k != 'kind'}))

    def restore(self):
        self.close()
        self.generation += 1
        self.host = JsonProcess(self.command, self.env, self.logs / f'stock-{self.generation}', self.timeout, False)
        require(self.host.receive().get('type') == 'ready', 'Missing stock handshake')
        setup = self.scenario['setup']
        require(set(setup) <= {'kind', 'character', 'ascension', 'seed', 'clock'}, 'Unsupported stock setup fields')
        require(setup.get('kind') == 'seeded_run', 'Stock profile requires a seeded run')
        reply = self.send(dict(cmd='start_run', character=setup['character'].removeprefix('CHARACTER.').title(),
                               ascension=setup['ascension'], seed=setup['seed'], lang='en'))
        for operation in self.scenario['operations']:
            if operation['cmd'] == 'action': reply = self.action(operation['action'])
            elif operation['cmd'] == 'scenario_enter_combat':
                reply = self.send(dict(cmd='enter_room', type='combat', encounter=operation['encounter'].removeprefix('ENCOUNTER.')))
            else: raise RuntimeError('Unsupported stock prefix operation')
        result = project_stock(reply)
        require(not result['terminal'], 'Stock prefix did not reach combat')
        if self.root is None: self.root = result
        require(self.root == result, 'Stock reconstructed root drifted')
        return result

    def step(self, action):
        return project_stock(self.action(action))
