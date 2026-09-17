"""QPC-06等价降本：复用冻结平台回放，正常计时与有限提案审计分离。"""

import argparse
import json
from pathlib import Path

from run_transactional_platform import ROOT, identity, native_run, win, write
from run_transactional_receiver_ordering import CASES, checked_run
from run_transactional_boundary_integration import rows, compare

PRIOR = ROOT / "benchmark-output/cpu-refinement/qpc-04f/run-01"
FEASIBILITY = ROOT / "benchmark-output/cpu-refinement/qpc-04g/run-01"


def read(path):
    return json.loads(path.read_text(encoding="utf-8"))


def collect(output, arm, app, probe, audit=False, audit_only=False, policy="pointwise-target", reuse_work=False):
    target = output / arm
    target.mkdir(parents=True, exist_ok=True)
    program = target / ("audit-program.json" if audit_only else "program.json")
    current = dict(app=identity(app))
    if audit or audit_only:
        current["probe"] = identity(probe)
    if program.exists() and read(program) != current:
        raise RuntimeError("同一实验臂禁止混用程序")
    write(program, current)
    variant = "B" if policy == "pointwise-target" else "A"
    fields = ("frame", "hash", "faces", "raw", "examined", "receivers", "need", "feasible", "exchanges", "free")
    for case in (() if audit_only else CASES):
        config = PRIOR / (case + "-" + variant + ".json")
        checked_run(config, target / case, app)
        current_rows = rows(target / case / "run/frames.csv")
        prior_rows = rows(PRIOR / (case + "-" + variant + "/run/frames.csv"))
        if reuse_work:
            compare(current_rows, prior_rows, fields)
            compare(current_rows, prior_rows, ("poseHash", "projectionHash"))
        else:
            compare(current_rows, prior_rows)
        print(arm, case, "离散结果一致", flush=True)
    # 旧政策单独验证，必须显式沿用immutable，不能回到源码默认旧点自由拟合
    case = CASES[0]
    if not audit_only and not reuse_work:
        checked_run(PRIOR / (case + "-A.json"), target / "legacy", app)
        compare(rows(target / "legacy/run/frames.csv"), rows(PRIOR / (case + "-A/run/frames.csv")))
    if not audit and not audit_only:
        return
    for item in read(FEASIBILITY / "freeze.json")["cases"]:
        case = item["case"]
        spec = read(FEASIBILITY / (case + "-verify-spec.json")) if (
            FEASIBILITY / (case + "-verify-spec.json")).exists() else read(FEASIBILITY / (case + "-capture-spec.json"))
        spec["costAudit"] = True
        file = target / (case + "-spec.json")
        write(file, spec)
        result = native_run(target, case + "-audit", probe, case, "transactional", 8, "audit", arguments=[
            "--recovery-trace", win(Path(item["resolved"]["path"])), win(Path(item["witnesses"]["path"])),
            win(target / (case + "-audit")), "--proposal-feasibility", win(file)])
        if result["status"] != "ok":
            raise RuntimeError("有限同提案审计未完成")
        compare(rows(target / (case + "-audit/frames.csv")), rows(target / case / "run/frames.csv"))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--arm", required=True)
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--probe", type=Path)
    parser.add_argument("--audit", action="store_true")
    parser.add_argument("--audit-only", action="store_true")
    parser.add_argument("--policy", choices=("legacy", "pointwise-target"), default="pointwise-target")
    parser.add_argument("--allow-work-reduction", action="store_true")
    args = parser.parse_args()
    collect(args.output.resolve(), args.arm, args.app.resolve(), args.probe.resolve() if args.probe else None,
            args.audit, args.audit_only, args.policy, args.allow_work_reduction)
