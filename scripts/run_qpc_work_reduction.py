"""复用实验入口进行QPC有限工作削减对照，不扩展正式实验矩阵。"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

from experiment_infrastructure.runner import run
from analyze_transactional_height_rejection import compare, read_rows, timing


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--before', type=Path)
    parser.add_argument('--cases', nargs='+', default=['dem-sierra-S2', 'dem-canyon-S2', 'dem-sierra-P0'])
    args = parser.parse_args()
    frozen = Path('benchmark-output/cpu-refinement/qpc-06f/run-01')
    result = {}
    for case in args.cases:
        destination = args.output / case / 'timing'
        manifest_path = destination / 'manifest.json'
        if not manifest_path.exists():
            run(frozen / case / 'timing/inputs/case.json', destination,
                args.executable, mode='timing', backend='d3d12')
        # 恢复中断的归约时只读已有结果；绝不覆盖或重采同目录
        manifest = json.loads(manifest_path.read_text())
        if manifest['status'] != 'ok':
            raise RuntimeError(f'{case}: {manifest["status"]}')
        current = read_rows(destination / 'run/frames.csv')
        record = {'timing': timing(current), 'binary': manifest['binary'],
                  'workloadId': manifest['workloadId'], 'sourceId': manifest['sourceId']}
        if args.before:
            previous = read_rows(args.before / case / 'timing/run/frames.csv')
            record['comparison'] = compare(previous, current)
            if not record['comparison']['equal']:
                raise RuntimeError(f'{case}: 逻辑结果变化 {record["comparison"]}')
        result[case] = record
        args.output.mkdir(parents=True, exist_ok=True)
        (args.output / 'summary.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
        print(case, record['timing']['moving']['times']['cpuMs']['mean'], flush=True)


if __name__ == '__main__':
    main()
