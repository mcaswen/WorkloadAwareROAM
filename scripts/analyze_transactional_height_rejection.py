"""归约高度拒绝消融，不在正常计时中重算被省去的认证。"""
from __future__ import annotations

import argparse
import csv
import json
import statistics
from collections import Counter, OrderedDict
from pathlib import Path


LOGICAL = (
    'frame', 'sample', 'event', 'poseHash', 'projectionHash', 'faces', 'budget',
    'workers', 'sequence', 'hash', 'status', 'updated', 'cold', 'seedFaces',
    'samples', 'raw', 'examined', 'receivers', 'need', 'feasible', 'exchanges',
    'free', 'pairs', 'conflicts', 'donorReuse', 'vertexWrites', 'indexWrites',
)
TIMES = ('cpuMs', 'viewMs', 'receiverMs', 'donorMs', 'reservationMs',
         'sampleRepairMs', 'topologyPrepareMs', 'topologyPublishMs', 'meshPrepareMs')
PHASES = {'moving': (3, 32), 'return': (32, 64), 'static': (64, 96)}


def read_rows(path: Path) -> list[dict]:
    with path.open(encoding='utf-8-sig', newline='') as stream:
        return list(csv.DictReader(stream))


def timing(rows: list[dict]) -> dict:
    result = {}
    for phase, (first, last) in PHASES.items():
        selected = [row for row in rows if first <= int(row['frame']) < last]
        values = {}
        for key in TIMES:
            samples = sorted(float(row[key]) for row in selected)
            if samples:
                # 帧分位只描述单进程轨迹，不将帧数当独立统计重复
                values[key] = {'mean': statistics.mean(samples), 'median': statistics.median(samples),
                               'p95': samples[min(len(samples)-1, int(.95 * len(samples)))],
                               'max': max(samples)}
        result[phase] = {'frames': len(selected), 'times': values}
    return result


def compare(before: list[dict], after: list[dict]) -> dict:
    differences = []
    for a, b in zip(before, after):
        for key in LOGICAL:
            if a[key] != b[key]:
                differences.append({'frame': a['frame'], 'key': key, 'before': a[key], 'after': b[key]})
    return {'equal': len(before) == len(after) == 96 and not differences,
            'frames': [len(before), len(after)], 'differences': differences[:20],
            'differenceCount': len(differences)}


def recurrence(path: Path) -> dict:
    table: OrderedDict[tuple, str] = OrderedDict()
    groups = {'all': {'failures': 0, 'keyHits': 0, 'sameSample': 0, 'intervalRepeat': 0},
              'moving': {'failures': 0, 'keyHits': 0, 'sameSample': 0, 'intervalRepeat': 0}}
    for row in read_rows(path):
        key = (row['root'], row['kind'], row['ordinal'])
        selected = [groups['all']]
        if 3 <= int(row['frame']) < 32:
            selected.append(groups['moving'])
        for counts in selected:
            counts['failures'] += 1
            if key in table:
                counts['keyHits'] += 1
                if table[key] == row['sample']:
                    counts['sameSample'] += 1
                    counts['intervalRepeat'] += int(row['interval'])
        # 更新不改变首次插入次序，对应固定FIFO；不以结果挑选容量
        if key not in table and len(table) == 2048:
            table.popitem(last=False)
        table[key] = row['sample']
    return {'capacity': 2048, 'counts': groups, 'finalEntries': len(table),
            'meaning': '同键同失败样本且当前区间可拒绝的复遇下界，不是实现后的提示命中率'}


def diagnostic(path: Path) -> dict:
    result = {}
    rows = read_rows(path / 'pointwise-work.csv')
    failures = read_rows(path / 'height-failures.csv')
    for phase, (first, last) in {'all': (0, 96), **PHASES}.items():
        counts, seconds, maxima = Counter(), Counter(), Counter()
        for row in rows:
            if not first <= int(row['frame']) < last:
                continue
            key, value = row['key'], float(row['value'])
            if row['type'] == 'count':
                if key == 'height_hint_entries':
                    maxima[key] = max(maxima[key], value)
                else:
                    counts[key] += int(value)
            elif row['type'] == 'seconds':
                seconds[key] += value
            else:
                maxima[key] = max(maxima[key], value)
        shadow = Counter(row['originalReason'] for row in failures
                         if first <= int(row['frame']) < last and row.get('originalReason'))
        result[phase] = {'counts': dict(counts), 'seconds': dict(seconds),
                         'maxima': dict(maxima), 'shadowReasons': dict(shadow)}
        if sum(shadow.values()) != counts['quality_hint_rejected']:
            raise ValueError('提前拒绝与原认证影子记录数量不同')
    return result


def profiles(path: Path, output: Path) -> dict:
    from analyze_transactional_current_profile import perf_details, tracy_details
    frames = read_rows(path / 'fp-timing/run/frames.csv')
    result = {'comparisons': {}, 'timing': {}}
    result['windowsComparison'] = compare(read_rows(path / 'timing/run/frames.csv'), frames)
    for mode in ('fp-timing', 'perf', 'tracy-timing', 'tracy'):
        current = read_rows(path / mode / 'run/frames.csv')
        result['comparisons'][mode] = compare(frames, current)
        result['timing'][mode] = timing(current)
    for mode, analyze in (('perf', perf_details), ('tracy', tracy_details)):
        destination = output / mode
        destination.mkdir(parents=True, exist_ok=True)
        result[mode] = analyze(path / mode, list(range(3, 32)), destination)
    return result


def export(report: dict, root: Path, output: Path) -> None:
    import hashlib
    from analyze_transactional_current_profile import write_csv
    output.mkdir(parents=True, exist_ok=True)
    timings = []
    for scene, variants in report['cases'].items():
        for variant, data in variants.items():
            for phase, summary in data['timing'].items():
                for metric, values in summary['times'].items():
                    timings.append({'scene': scene, 'variant': variant, 'phase': phase,
                                    'metric': metric, **values})
    write_csv(output / 'timing.csv', timings)
    work = []
    for name, phases in report['diagnostic'].items():
        for phase, summary in phases.items():
            for kind in ('counts', 'seconds', 'maxima'):
                for key, value in summary[kind].items():
                    work.append({'variant': name, 'phase': phase, 'type': kind, 'key': key, 'value': value})
    write_csv(output / 'work.csv', work)
    (output / 'summary.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    names = {'frames.csv', 'pointwise-work.csv', 'height-failures.csv', 'manifest.json',
             'profile-result.json', 'freeze.json', 'S1-program.json', 'S2-program.json'}
    identities = {str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest()
                  for path in sorted(root.rglob('*')) if path.is_file() and path.name in names}
    (output / 'provenance.json').write_text(json.dumps(identities, indent=2) + '\n', encoding='utf-8')


def plot(report: dict, root: Path, output: Path) -> None:
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    figure, axes = plt.subplots(1, 2, figsize=(12, 4.5), layout='constrained')
    colors = {'S0': '#0072b2', 'S1': '#e69f00', 'S2': '#009e73', 'P0': '#cc79a7'}
    for axis, scene in zip(axes, ('dem-sierra', 'dem-canyon')):
        for variant in ('S0', 'S1', 'S2', 'P0'):
            path = root / f'{scene}-{variant}/timing/run/frames.csv'
            if not path.exists():
                continue
            data = read_rows(path)[3:]
            axis.plot([int(r['frame']) for r in data], [float(r['cpuMs']) for r in data],
                      label=variant, color=colors[variant], linewidth=1.2)
        for boundary in (32, 64):
            axis.axvline(boundary, color='gray', alpha=.35, linestyle='--')
        axis.set_title(scene + ': one process per variant')
        axis.set_xlabel('Opportunity (3-31 moving; 32-63 return; 64-95 static)')
        axis.set_ylabel('Native Windows CPU-ready (ms)')
        axis.legend()
        axis.grid(alpha=.2)
    figure.suptitle('QPC-06F: same SourceHeight output for S0/S1/S2; P0 is a different generator')
    figure.savefig(output / 'trajectory.png', dpi=150)
    plt.close(figure)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('root', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--details', type=Path)
    parser.add_argument('--plot', action='store_true')
    args = parser.parse_args()
    report = {'cases': {}, 'recurrence': {}, 'diagnostic': {}}
    for scene in ('dem-sierra', 'dem-canyon'):
        cases = {}
        baseline_path = args.root / f'{scene}-S0/timing/run/frames.csv'
        baseline = read_rows(baseline_path) if baseline_path.exists() else []
        for variant in ('S0', 'S1', 'S2', 'P0', 'S2-repeat'):
            path = args.root / f'{scene}-{variant}/timing/run/frames.csv'
            if path.exists():
                rows = read_rows(path)
                cases[variant] = {'timing': timing(rows), 'finalFaces': int(rows[-1]['faces']),
                                  'exchanges': sum(int(r['exchanges']) for r in rows),
                                  'free': sum(int(r['free']) for r in rows)}
                if variant.startswith('S') and baseline:
                    cases[variant]['comparison'] = compare(baseline, rows)
                if variant == 'P0':
                    old = args.root.parent.parent / f'qpc-04h/run-01/{scene}-P0/timing/run/frames.csv'
                    cases[variant]['comparisonTo04H'] = compare(read_rows(old), rows)
        report['cases'][scene] = cases
    for path in args.root.glob('**/height-failures.csv'):
        report['recurrence'][str(path.relative_to(args.root))] = recurrence(path)
        report['diagnostic'][path.parent.name] = diagnostic(path.parent)
    if args.details and (args.root / 'dem-sierra-S2/tracy/profile-result.json').exists():
        report['profile'] = profiles(args.root / 'dem-sierra-S2', args.details)
    output = args.output or args.root / 'analysis.json'
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    if args.details:
        export(report, args.root, args.details)
        if args.plot:
            plot(report, args.root, args.details)
    for scene, variants in report['cases'].items():
        print(scene, {name: round(data['timing']['moving']['times']['cpuMs']['mean'], 3)
                      for name, data in variants.items()})
    print('diagnostic:', list(report['diagnostic']))


if __name__ == '__main__':
    main()
