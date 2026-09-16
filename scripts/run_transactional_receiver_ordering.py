"""QPC-04D：冻结接收排序对照，复用平台、追溯和独立质量入口。"""

import argparse
import time
from pathlib import Path

from run_transactional_platform import ROOT, identity, native_run, win, write
from run_transactional_boundary_integration import read, rows, compare, HISTORY
from experiment_infrastructure import runner
from experiment_infrastructure.quality import evaluate, pointwise_pair


CASES = ("peking547-b50000-transactional-t8", "dem-sierra-b50000-transactional-t8",
         "dem-canyon-b50000-transactional-t8")
FRAMES = {case: ((2, 15, 16, 23) if "peking" in case else (2, 48, 80, 95)) for case in CASES}
BOUNDARY = ROOT / "benchmark-output/cpu-refinement/qpc-04b/run-01"
SELECTION = ROOT / "docs/research/cpu_refinement/data/qpc_01_quality_recovery/selection.json"


def references(case):
    """复合排序沿用同一边界背景，原边界关闭与DOD仍是独立参照。"""
    boundary = "sierra" not in case
    return {
        "A": BOUNDARY / ("quality-" + case) if boundary else HISTORY / ("quality-" + case),
        "off": HISTORY / ("quality-" + case),
        "dod": HISTORY / ("quality-" + case.replace("transactional", "dod")),
    }


def historical(case, visual=False):
    if "sierra" in case:
        return HISTORY / (("visual-" if visual else "r1-") + case)
    return BOUNDARY / (case + ("-visual" if visual else "-timing"))


def limited(started):
    if time.monotonic() - started > 45 * 60:
        raise RuntimeError("采集评价达到45分钟边界，保留完成部分")


def freeze(output, app, probe):
    selection = read(SELECTION)["cases"]
    result = {"protocol": "qpc04d-receiver-error-first-v1", "app": identity(app), "probe": identity(probe), "cases": {}}
    for case in CASES:
        old = selection[case]
        resolved = Path(old["resolved"]["path"])
        if identity(resolved)["sha256"] != old["resolved"]["sha256"]:
            raise RuntimeError("旧输入身份改变")
        config = read(ROOT / "configs/experiments/formal/fer_02" / (case + ".json"))
        config["boundaryRefinement"] = "sierra" not in case
        for label, order in (("A", "composite"), ("B", "error-first")):
            write(output / (case + "-" + label + ".json"), {**config, "receiverOrder": order})
        witnesses = [dict(item) for item in old["witnesses"]]
        if "sierra" not in case:
            u, v = ((.86328125, .9990234375) if "canyon" in case else (.9404761904761905, .9981684981684982))
            witnesses.append({"frame": witnesses[0]["frame"], "returnFrame": witnesses[0]["returnFrame"],
                              "ordinal": 0, "u": u, "v": v, "roles": ["04B-adverse"]})
        table = output / (case + ".txt")
        table.write_text("".join(f"{w['frame']} {w['returnFrame']} {w['ordinal']} {w['u']:.17g} {w['v']:.17g}\n"
                                 for w in witnesses), encoding="utf-8")
        result["cases"][case] = {"oldResolved": identity(resolved), "witnesses": witnesses,
                                 "table": identity(table), "qualityFrames": list(FRAMES[case]),
                                 "configs": {label: identity(output / (case + "-" + label + ".json")) for label in ("A", "B")}}
    path = output / "freeze.json"
    if path.exists() and read(path) != result:
        raise RuntimeError("冻结输入或程序改变，拒绝覆盖")
    write(path, result)
    return result


def checked_run(config, destination, app, mode="timing"):
    if not (destination / "manifest.json").exists():
        runner.run(config, destination, app, mode=mode)
    manifest = read(destination / "manifest.json")
    if manifest["status"] != "ok" or manifest["binary"]["sha256"] != identity(app)["sha256"]:
        raise RuntimeError("运行未完成或程序身份改变：" + str(destination))


def collect(output, app, probe, quality_probe):
    started = time.monotonic()
    frozen = freeze(output, app, probe)
    baseline = read(output / "baseline.json")
    result = native_run(output, "platform-after-off", app, "peking", "transactional", 8, "timing",
                        arguments=["--experiment-run", win(Path(baseline["resolved"]["path"])), win(output / "platform-after-off")])
    if result["status"] != "ok":
        raise RuntimeError("默认政策平台运行失败")
    compare(rows(output / "platform-before/frames.csv"), rows(output / "platform-after-off/frames.csv"))
    for case in CASES:
        limited(started)
        for label in ("A", "B"):
            config = output / (case + "-" + label + ".json")
            destination = output / (case + "-" + label)
            checked_run(config, destination, app)
            old_manifest = read(historical(case) / "manifest.json")
            manifest = read(destination / "manifest.json")
            if manifest["workloadId"] != old_manifest["workloadId"]:
                raise RuntimeError("A/B 改变冻结 workload")
            if label == "A":
                compare(rows(destination / "run/frames.csv"), rows(historical(case) / "run/frames.csv"))
                if manifest["taskId"] != old_manifest["taskId"]:
                    raise RuntimeError("默认排序改变旧任务身份")
            else:
                if manifest["taskId"] == old_manifest["taskId"]:
                    raise RuntimeError("新政策未进入任务身份")
                visual = output / (case + "-B-visual")
                checked_run(config, visual, app, "visual")
                runner.compare_modes([destination, visual])
                name = case + "-B-trace"
                frames = FRAMES[case][1::2]
                record = native_run(output, name, probe, case, "transactional", 8, "audit", arguments=[
                    "--recovery-trace", win(destination / "inputs/resolved.json"), win(output / (case + ".txt")),
                    win(output / name), "--priority-frames", ",".join(map(str, frames))])
                if record["status"] != "ok":
                    raise RuntimeError("追溯未完成：" + case)
                compare(rows(destination / "run/frames.csv"), rows(output / name / "frames.csv"))
                for frame in frames:
                    if not (output / name / f"priority-{frame}.csv").exists():
                        raise RuntimeError("全根诊断缺失")
        limited(started)
        target = output / ("quality-" + case)
        if not target.exists():
            evaluate(output / (case + "-B-visual"), target, quality_probe, FRAMES[case])
        for label, reference in references(case).items():
            pair = output / (case + "-vs-" + label + ".json")
            if not pair.exists():
                pointwise_pair(target, reference, pair)
        print("quality completed", case, flush=True)
    limited(started)
    case = CASES[0]
    config = read(output / (case + "-B.json"))
    config["workers"] = 1
    serial_config = output / (case + "-B-t1.json")
    write(serial_config, config)
    checked_run(serial_config, output / (case + "-B-t1"), app)
    compare(rows(output / (case + "-B-t1/run/frames.csv")), rows(output / (case + "-B/run/frames.csv")))
    write(output / "collection.json", {"seconds": time.monotonic() - started,
        "scope": "six normal, three visual, three trace, one serial; historical A quality reused",
        "qualityProbe": identity(quality_probe), "defaultMeshWorkEqual": True, "samePolicyThreadsEqual": True})


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--quality-probe", type=Path, required=True)
    args = parser.parse_args()
    collect(args.output.resolve(), args.app.resolve(), args.probe.resolve(), args.quality_probe.resolve())
