"""冻结四条原轨迹，旁路捕获有限批次；不让新条件改变原 Apply。"""

import argparse
import csv
import json
from pathlib import Path
import time

from run_transactional_platform import ROOT, identity, native_run, win, write
from transactional_exchange_quality import OPERATING_POINTS


PRIOR = ROOT / "benchmark-output/cpu-refinement/qpc-04d/run-01"
BOUNDARY = ROOT / "benchmark-output/cpu-refinement/qpc-04b/run-01"
CASES = (("peking", "peking547", (15, 23)), ("sierra", "dem-sierra", (48, 95)),
         ("canyon", "dem-canyon", (24, 95)), ("canyon-composite", "dem-canyon", (8,)))
WORK_FIELDS = ("frame", "hash", "faces", "raw", "examined", "receivers", "need", "feasible", "exchanges",
               "free", "pairs", "conflicts", "donorReuse", "touches")


def read(path):
    return json.loads(path.read_text(encoding="utf-8"))


def rows(path):
    with path.open(newline="", encoding="utf-8-sig") as stream:
        return list(csv.DictReader(stream))


def compare(left, right):
    a, b = rows(left), rows(right)
    if len(a) != len(b):
        raise RuntimeError("轨迹长度不同")
    for x, y in zip(a, b):
        for key in WORK_FIELDS:
            if x[key] != y[key]:
                raise RuntimeError(f"轨迹分叉 frame={x['frame']} field={key}: {x[key]} != {y[key]}")
    return len(a)


def collect(output, probe):
    started = time.monotonic()
    cases = []
    for name, stem, frames in CASES:
        case = stem + "-b50000-transactional-t8"
        base = BOUNDARY if name == "canyon-composite" else PRIOR
        run = base / (case + ("-timing" if base == BOUNDARY else "-B"))
        trace = base / (case + ("-trace-on" if base == BOUNDARY else "-B-trace"))
        cases.append(dict(name=name, frames=frames, resolved=identity(run / "inputs/resolved.json"),
                          witnesses=identity(base / (case + ".txt")), history=identity(trace / "frames.csv"),
                          timing=identity(run / "run/frames.csv")))
    frozen = dict(protocol="qpc04e-readonly-v1", probe=identity(probe), cases=cases,
                  operatingPoints=[dict(e=str(e), heightRatio=str(h)) for e, h in OPERATING_POINTS],
                  processSeconds=180, processBytes=8*1024**3, batchSampleRecords=2000000,
                  analysisSeconds=1200, totalSeconds=1800, sumFractionBits=128,
                  sourceFiles={str(p.relative_to(ROOT)): identity(p) for p in (
                      ROOT / "src/experiment/greedy_transactional_lod/TransactionalExchangeQualityAudit.cpp",
                      ROOT / "src/benchmark/experiment/TransactionalRecoveryTrace.cpp",
                      ROOT / "scripts/transactional_exchange_quality.py")})
    path = output / "freeze.json"
    if path.exists() and read(path) != json.loads(json.dumps(frozen)):
        raise RuntimeError("冻结身份发生变化；保留已有证据")
    write(path, frozen)
    baseline = read(output / "baseline.json")
    normal = native_run(output, "normal-after", probe, "peking", "transactional", 8, "timing",
                        arguments=["--experiment-cpu", win(Path(baseline["resolved"]["path"])), win(output / "normal-after")])
    if normal["status"] != "ok":
        raise RuntimeError("关闭审计回放失败")
    normal_frames = compare(output / "normal-before/frames.csv", output / "normal-after/frames.csv")
    checked = {}
    for case in cases:
        if time.monotonic()-started > 600:
            raise RuntimeError("采集超过预留十分钟，保留完成项")
        name = case["name"]
        result = native_run(output, name, probe, name, "transactional", 8, "audit", arguments=[
            "--recovery-trace", win(Path(case["resolved"]["path"])), win(Path(case["witnesses"]["path"])),
            win(output / name), "--exchange-quality-frames", ",".join(map(str, case["frames"]))])
        if result["status"] != "ok":
            checked[name] = dict(status=result["status"])
            continue
        count = compare(output / name / "frames.csv", Path(case["history"]["path"]))
        checked[name] = dict(status="ok", sameFrames=count,
                             captureFiles=[identity(output / name / f"exchange-quality-{frame}.json") for frame in case["frames"]])
    write(output / "collection.json", dict(seconds=time.monotonic()-started, normalSameFrames=normal_frames, cases=checked))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--probe", type=Path, required=True)
    args = parser.parse_args()
    collect(args.output.resolve(), args.probe.resolve())
