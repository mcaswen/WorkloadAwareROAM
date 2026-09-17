"""最终两个冻结输入的有限热点核查，复用FPR采集与QPC归约。"""
from __future__ import annotations

import json
import argparse
import subprocess
import sys
from pathlib import Path

from analyze_transactional_current_profile import perf_details, tracy_details
from analyze_transactional_height_rejection import compare, diagnostic, read_rows, timing


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--analyze-only', action='store_true')
    options = parser.parse_args()
    root = Path('benchmark-output/cpu-refinement/qpc-06j')
    destination = Path('docs/research/cpu_refinement/data/qpc_06j_remaining_cost')
    destination.mkdir(parents=True, exist_ok=True)
    frozen = Path('benchmark-output/cpu-refinement/qpc-06f/run-01')
    build = Path('/home/mcaswen/.cache/roam-profiling/build')
    binary = {mode: build / f'cpu-profile-{mode}/tests/parallel_roam_experiment_cpu'
              for mode in ('fp', 'tracy')}
    tools = Path('/home/mcaswen/.cache/roam-profiling')
    commands = []
    result = {}

    def execute(args):
        if options.analyze_only:
            return
        commands.append(args)
        (root / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        with (root / 'capture.log').open('a') as log:
            subprocess.run(args, stdout=log, stderr=subprocess.STDOUT, timeout=180, check=True)

    for scene in ('dem-sierra', 'dem-canyon'):
        case = frozen / f'{scene}-S2/timing/inputs/case.json'
        common = ['--case', str(case)]
        modes = ['fp-timing', 'perf']
        if scene == 'dem-sierra':
            modes += ['tracy-timing', 'tracy']
        for mode in modes:
            path = root / scene / mode
            kind = 'tracy' if mode.startswith('tracy') else 'fp'
            args = [sys.executable, 'scripts/run_experiment.py']
            if mode.endswith('timing'):
                args += ['run', *common, '--output', str(path), '--executable', str(binary[kind]),
                         '--mode', 'timing', '--cpu']
            else:
                args += ['profile', mode, *common, '--output', str(path), '--executable', str(binary[kind])]
                if mode == 'perf':
                    args += ['--perf', str(tools / 'bin/perf')]
                else:
                    args += ['--capture', str(tools / 'tracy-linux/tracy-capture'),
                             '--csvexport', str(tools / 'tracy-linux/tracy-csvexport')]
            execute(args)
            print(scene, mode, flush=True)

        entry = {'timing': {}, 'comparison': {}, 'identities': {}}
        native = read_rows(Path('benchmark-output/cpu-refinement/qpc-06i/after') /
                           f'{scene}-S2/timing/run/frames.csv')
        for mode in modes:
            path = root / scene / mode
            manifest = json.loads((path / 'manifest.json').read_text())
            entry['identities'][mode] = {
                key: manifest[key] for key in
                ('binary', 'commit', 'sourceId', 'executionId', 'workloadId', 'taskId', 'case')}
            rows = read_rows(path / 'run/frames.csv')
            entry['comparison'][mode] = compare(native, rows)
            if not entry['comparison'][mode]['equal']:
                raise RuntimeError(f'{scene}/{mode}: 捕获或跨平台结果不一致')
            entry['timing'][mode] = timing(rows)
            if mode in ('perf', 'tracy'):
                output = destination / scene / mode
                output.mkdir(parents=True, exist_ok=True)
                analyze = perf_details if mode == 'perf' else tracy_details
                entry[mode] = analyze(path, list(range(3, 32)), output)

        trace = root / scene / 'diagnostic'
        trace_command = [str(binary['fp']), '--recovery-trace',
                         str((root / scene / 'fp-timing/inputs/resolved.json').resolve()),
                         str(Path(f'benchmark-output/cpu-refinement/qpc-04f/run-01/{scene}-b50000-transactional-t8.txt').resolve()),
                         str(trace.resolve())]
        execute(trace_command)
        entry['diagnostic'] = diagnostic(trace)
        # 诊断CSV字段较少，仍核对全部机会的实际曲面与主要决定
        traced = read_rows(trace / 'frames.csv')
        keys = ('frame', 'hash', 'faces', 'raw', 'receivers', 'need', 'feasible', 'exchanges', 'free')
        entry['traceEqual'] = len(traced) == len(native) == 96 and all(
            all(a[key] == b[key] for key in keys) for a, b in zip(native, traced))
        if not entry['traceEqual']:
            raise RuntimeError(f'{scene}: 诊断改变实际结果')
        # 恢复诊断直接Plan；正常路径会复用完整空批，静止配对工作量不能要求相等
        entry['tracePairDifferences'] = [
            {'frame': a['frame'], 'normal': a['pairs'], 'diagnostic': b['pairs']}
            for a, b in zip(native, traced) if a['pairs'] != b['pairs']]
        for difference in entry['tracePairDifferences']:
            frame = int(difference['frame'])
            current, previous = native[frame], native[frame - 1]
            if (frame < 64 or current['pairs'] != '0' or current['exchanges'] != '0' or
                    current['free'] != '0' or current['hash'] != previous['hash'] or
                    current['projectionHash'] != previous['projectionHash']):
                raise RuntimeError(f'{scene}: 非静止空批出现配对工作差异')
        result[scene] = entry
        (destination / 'summary.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
        print(scene, 'analysis complete', flush=True)


if __name__ == '__main__':
    main()
