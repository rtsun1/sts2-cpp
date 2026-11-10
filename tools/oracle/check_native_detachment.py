#!/usr/bin/env python3
"""Trace native death detachment through the real action queue and local input seam."""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
from xml.sax.saxutils import escape

from reference_runtime import GAME_SHA256, require


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def run(command, env, cwd, log):
    with log.open('w') as stream:
        result = subprocess.run(command, env=env, cwd=cwd, stdout=stream,
                                stderr=subprocess.STDOUT, timeout=120)
    require(result.returncode == 0, f'Command failed ({result.returncode}); see {log}')


def check(game_dir, godot, output, only=None, fixture=None):
    game_dir, godot, output = game_dir.resolve(), godot.resolve(), output.resolve()
    dlls = list(game_dir.glob('data_*/sts2.dll'))
    require(len(dlls) == 1, 'Pass the installed game directory containing its .pck')
    dll, pack = dlls[0], game_dir / 'SlayTheSpire2.pck'
    require(digest(dll) == GAME_SHA256, 'Unsupported game DLL')
    require(pack.is_file(), 'Missing installed game resource pack')
    require(godot.is_file(), 'Missing Godot executable')
    packages = godot.parent / 'GodotSharp/Tools/nupkgs'
    require(packages.is_dir(), 'Pass a Godot 4.5.1 Mono executable with bundled SDK packages')
    output.mkdir(parents=True, exist_ok=False)
    project = output / 'project'
    shutil.copytree(Path(__file__).with_name('native_detachment_probe'), project)
    (project / 'Probe.csproj').write_text(
        '<Project Sdk="Godot.NET.Sdk/4.5.1"><PropertyGroup>'
        '<TargetFramework>net9.0</TargetFramework><Nullable>enable</Nullable>'
        '<EnableDynamicLoading>true</EnableDynamicLoading></PropertyGroup>'
        '<ItemGroup><Reference Include="sts2"><HintPath>' + escape(str(dll)) +
        '</HintPath><Private>false</Private></Reference></ItemGroup></Project>\n')
    (project / 'NuGet.Config').write_text(
        '<configuration><packageSources><clear/><add key="bundled" value="' +
        escape(str(packages), {'"': '&quot;'}) + '"/></packageSources></configuration>\n')
    env = dict(os.environ, DOTNET_CLI_HOME=str(output / 'dotnet-home'),
               NUGET_PACKAGES=str(output / 'packages'), DOTNET_CLI_TELEMETRY_OPTOUT='1',
               XDG_DATA_HOME=str(output / 'userdata'),
               STS2_CHOICE_GAME_DLL_DIR=str(dll.parent), STS2_CHOICE_GAME_PACK=str(pack))
    run(['dotnet', 'build', str(project / 'Probe.csproj')], env, project, output / 'build.log')
    results = []
    teardown = {}
    for case in ('strike', 'dagger-throw'):
        if only and case not in only:
            continue
        repeated = []
        for attempt in range(2):
            log = output / f'{case}-{attempt}.log'
            run([str(godot), '--headless', '--path', str(project)],
                dict(env, STS2_DETACHMENT_CASE=case), project, log)
            lines = log.read_text().splitlines()
            require('PROBE CLEANUP COMPLETE' in lines, f'Native cleanup did not complete; see {log}')
            cleanup = lines.index('PROBE CLEANUP COMPLETE')
            classified = []
            for index, line in enumerate(lines):
                require('PROBE ERROR' not in line, f'Native probe failed; see {log}')
                if line.startswith(('ERROR:', '[ERROR]')):
                    shutdown_allocation = re.fullmatch(
                        r"ERROR: (?:\d+ RID allocations of type '[^']+' were leaked at exit\.|\d+ resources still in use at exit \(run with --verbose for details\)\.)", line)
                    require(index > cleanup and shutdown_allocation,
                            f'Native runtime error; see {log}')
                    classified.append(line)
            teardown[log.name] = classified
            records = [json.loads(line.removeprefix('DETACHMENT_RESULT ')) for line in lines
                       if line.startswith('DETACHMENT_RESULT ')]
            require(len(records) == 1, f'Missing unique successful trace result; see {log}')
            repeated.append(records[0])
        require(repeated[0] == repeated[1], f'Independent native trace repeat differs: {case}')
        results.append(repeated[0])
    require(results, 'No selected native trace cases')
    report = {'profile': 'native-death-detachment-v1', 'cases': results,
              'independent_native_repeat': 'passed',
              'game_dll_sha256': digest(dll), 'game_pack_sha256': digest(pack),
              'godot_sha256': digest(godot), 'runner_sha256': digest(Path(__file__)),
              'logs_sha256': {p.name: digest(p) for p in sorted(output.glob('*.log'))},
              'probe_sources_sha256': {p.name: digest(p) for p in sorted(project.iterdir()) if p.is_file()},
              'shutdown_allocation_diagnostics': teardown,
              'input_provider': 'CardSelectCmd.UseSelector(input, localOnly:true)',
              'setup': {'seed': 'ORACLECHECK01', 'deck': 'source card then nine DefendIronclad',
                        'enemies': [{'max_hp': 40, 'hp': 6}, {'max_hp': 40, 'hp': 40}],
                        'relic': 'GremlinHorn', 'power': 'StratagemPower:1'},
              'limits': ['Runs the pinned native models and real PlayCardAction, ActionQueueSet, HookPlayerChoiceContext and GenericHookGameAction in Godot with native TestMode setup.',
                         'The local-only input provider preserves native choice signalling and queued execution. It bypasses UI callbacks and network SyncLocalChoice; no UI or network parity is claimed.',
                         'Game presentation nodes are absent. Native TestMode and AutoSlayerCheck suppress presentation waits.',
                         'This is an explicit scheduling trace, not a canonical native JSON export or whole-state parity recording.']}
    (output / 'verification.json').write_text(json.dumps(report, indent=2) + '\n')
    if fixture:
        fixture.parent.mkdir(parents=True, exist_ok=True)
        fixture.write_text(json.dumps(report, indent=2) + '\n')
    print(f'Native death detachment: {len(results)} cases, independent repeat passed; {output}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-dir', type=Path, required=True)
    parser.add_argument('--godot', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--only', action='append')
    parser.add_argument('--fixture', type=Path)
    args = parser.parse_args()
    check(args.game_dir, args.godot, args.output_dir, args.only, args.fixture.resolve() if args.fixture else None)
