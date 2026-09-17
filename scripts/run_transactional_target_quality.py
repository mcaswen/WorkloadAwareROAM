"""冻结显式质量目标的三条持续轨迹，正常计时、视觉和精确审计分离。"""

import argparse
import json
import shutil
import time
from pathlib import Path

from run_transactional_platform import ROOT, identity, native_run, win, write
from run_transactional_receiver_ordering import CASES, FRAMES, checked_run
from run_transactional_boundary_integration import rows, compare
from experiment_infrastructure import runner
from experiment_infrastructure.quality import evaluate, pointwise_pair


PRIOR = ROOT / "benchmark-output/cpu-refinement/qpc-04d/run-01"
AUDIT_FRAMES = ((15, 23), (48, 95), (24, 95))


def read(path):
    return json.loads(path.read_text(encoding="utf-8"))


def freeze(output):
    import jsonschema
    schema = read(ROOT / "configs/experiments/schema/case.schema.json")
    cases = []
    for case, audit_frames in zip(CASES, AUDIT_FRAMES):
        original = PRIOR / (case + "-B.json")
        config = read(original)
        paths = {}
        for label in ("A", "B"):
            variant = dict(config)
            if label == "B":
                variant.update(qualityPolicy="pointwise-target", qualityTargetPixels=.5, qualityHeightRatio=1/256)
            jsonschema.validate(variant, schema)
            path = output / (case + "-" + label + ".json")
            if path.exists() and read(path) != variant:
                raise RuntimeError("冻结配置发生变化")
            write(path, variant)
            paths[label] = identity(path)
        witnesses = output / (case + ".txt")
        shutil.copy2(PRIOR / (case + ".txt"), witnesses)
        # 04D新增返回见证事先固定，不从新结果中重新挑选
        if "canyon" in case:
            with witnesses.open("a") as stream:
                stream.write("24 95 0 0.748046875 0.876953125\n")
        cases.append(dict(case=case, configs=paths, witnesses=identity(witnesses),
                          qualityFrames=FRAMES[case], auditFrames=audit_frames, source=identity(original)))
    frozen = dict(protocol="qpc04f-pointwise-target-v1", cases=cases,
                  targetPixels=.5, heightRatio="1/256", receiverOwnProgress=True,
                  processSeconds=180, processBytes=8*1024**3, totalSeconds=2700,
                  retainedFit=True, runtimeAndPublishedGeometry=True)
    path = output / "freeze.json"
    frozen = json.loads(json.dumps(frozen))
    if path.exists() and read(path) != frozen:
        raise RuntimeError("冻结协议发生变化")
    write(path, frozen)
    return frozen


def collect(output, app, probe, quality_probe):
    started = time.monotonic()
    frozen = freeze(output)
    write(output / "binaries.json", {"app": identity(app), "probe": identity(probe), "qualityProbe": identity(quality_probe)})
    baseline = read(output / "baseline.json")
    record = native_run(output, "normal-after", app, "peking", "transactional", 8, "timing",
                        arguments=["--experiment-run", win(Path(baseline["resolved"]["path"])), win(output / "normal-after")])
    if record["status"] != "ok":
        raise RuntimeError("默认模式回放失败")
    compare(rows(output / "normal-before/frames.csv"), rows(output / "normal-after/frames.csv"))
    outcomes = []
    for item in frozen["cases"]:
        if time.monotonic() - started > 2700:
            break
        case = item["case"]
        result = dict(case=case, status="started")
        outcomes.append(result)
        try:
            for label in ("A", "B"):
                config = Path(item["configs"][label]["path"])
                checked_run(config, output / (case + "-" + label), app)
            # 旧质量仅在相同逐帧输出核对后复用，成本仍用当前进程值
            compare(rows(output / (case + "-A/run/frames.csv")), rows(PRIOR / (case + "-B/run/frames.csv")))
            config = Path(item["configs"]["B"]["path"])
            visual = output / (case + "-B-visual")
            checked_run(config, visual, app, "visual")
            runner.compare_modes([output / (case + "-B"), visual])
            trace = output / (case + "-B-trace")
            record = native_run(output, case + "-B-trace", probe, case, "transactional", 8, "audit", arguments=[
                "--recovery-trace", win(output / (case + "-B/inputs/resolved.json")),
                win(Path(item["witnesses"]["path"])), win(trace),
                "--exchange-quality-frames", ",".join(map(str, item["auditFrames"]))])
            if record["status"] != "ok":
                raise RuntimeError("有限追溯未完成")
            compare(rows(output / (case + "-B/run/frames.csv")), rows(trace / "frames.csv"))
            quality = output / ("quality-" + case)
            if not quality.exists():
                evaluate(visual, quality, quality_probe, item["qualityFrames"])
            if not (output / (case + "-vs-A.json")).exists():
                pointwise_pair(quality, PRIOR / ("quality-" + case), output / (case + "-vs-A.json"))
            seed_trace = output / (case + "-A-seed-trace")
            record = native_run(output, case + "-A-seed-trace", probe, case, "transactional", 8, "audit", arguments=[
                "--recovery-trace", win(output / (case + "-A/inputs/resolved.json")),
                win(Path(item["witnesses"]["path"])), win(seed_trace)])
            if record["status"] != "ok":
                raise RuntimeError("旧政策种子身份导出失败")
            if read(seed_trace / "seed-identity.json") != read(trace / "seed-identity.json"):
                raise RuntimeError("A/B初始种子不一致")
            result["status"] = "complete"
        except Exception as error:
            result.update(status="failed-or-censored", error=str(error))
        write(output / "collection.json", dict(seconds=time.monotonic()-started, cases=outcomes))
    if outcomes and outcomes[0]["status"] == "complete":
        config = read(Path(frozen["cases"][0]["configs"]["B"]["path"]))
        config["workers"] = 1
        path = output / "peking-B-t1.json"
        write(path, config)
        checked_run(path, output / "peking-B-t1", app)
        compare(rows(output / "peking-B-t1/run/frames.csv"), rows(output / (CASES[0] + "-B/run/frames.csv")))
    write(output / "collection.json", dict(seconds=time.monotonic()-started, cases=outcomes))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--freeze-only", action="store_true")
    parser.add_argument("--app", type=Path)
    parser.add_argument("--probe", type=Path)
    parser.add_argument("--quality-probe", type=Path)
    args = parser.parse_args()
    if args.freeze_only:
        freeze(args.output.resolve())
    else:
        collect(args.output.resolve(), args.app.resolve(), args.probe.resolve(), args.quality_probe.resolve())
