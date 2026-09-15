"""有限局部恢复调查的身份、原生采集、独立几何核查与图形入口。"""

import argparse
import json
from fractions import Fraction
from pathlib import Path
import shutil
import subprocess
import time

from run_transactional_platform import ROOT, identity, native_run, win, write
from run_transactional_quality_audit import CORE_FIELDS
from transactional_platform_report import compare, rows
from transactional_recovery_geometry import validate, self_test

SOURCES = ("tests/TransactionalQualityProvenanceProbe.cpp", "tests/CMakeLists.txt",
           "src/experiment/greedy_transactional_lod/TransactionalRecoveryAudit.h",
           "src/experiment/greedy_transactional_lod/TransactionalRecoveryAudit.cpp",
           "src/experiment/greedy_transactional_lod/TransactionalFitCounterfactual.h",
           "src/experiment/greedy_transactional_lod/TransactionalFitCounterfactual.cpp")


def freeze(output, probe):
    """冻结PQ-03结束版本；历史基线有字段修正，两个程序身份分开记录。"""
    prior = ROOT / "benchmark-output/cpu-refinement/pq-03/run-01"
    if identity(probe)["sha256"] != "34cb72ae25d78b2015b3888fa83f2bfb5b5b79bd98c40c4933d20a6dfe804d2f":
        raise RuntimeError("PQ-04 freeze requires the recorded PQ-03 final binary")
    baseline = json.loads((prior / "commands/after-default.json").read_text())
    output.mkdir(parents=True, exist_ok=False)
    saved = output / "before/quality-provenance-probe.exe"
    saved.parent.mkdir();shutil.copy2(probe, saved)
    for source in SOURCES[:2]:
        destination = output / "before" / source
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / source, destination)
    write(output / "freeze.json", dict(
        protocol="pq04-recovery-v1", beforeProbe=identity(saved),
        commit=subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        sourcesBefore={f: identity(ROOT / f) for f in SOURCES[:2]},
        baselineDirectory=str(prior / "after-default"), baselineCommand=baseline,
        witness=[.97985345125198364, .94871795177459717],
        limits=dict(geometries=64, heightsPerGeometry=2, modelQueriesPerGeometry=1, sampleVisits=2000000, seconds=60),
        scope="frame8 root -111 and direct edge neighbours; unchanged 24 batches"))


def collect(output, probe):
    frozen = json.loads((output / "freeze.json").read_text())
    after = dict(probe=identity(probe), sources={f: identity(ROOT / f) for f in SOURCES})
    path = output / "after-freeze.json"
    if path.exists() and json.loads(path.read_text()) != after:
        raise RuntimeError("Frozen sources changed; preserve this run and use a separate validation")
    write(path, after)
    for name, flags in (("after-default", []), ("recovery-audit", ["--recovery-audit"])):
        result = native_run(output, name, probe, "peking", "transactional", 8, "audit",
                            arguments=[win(output / name), "immutable", "--witness", *(format(v, ".17g") for v in frozen["witness"]), *flags])
        if result["status"] != "ok":
            raise RuntimeError("Native recovery run failed: " + name)


def figure(output, domain, candidates, audits):
    from experiment_infrastructure.asset_preview import configure_font, plt
    root_points = {p[0]: p for p in domain["geometry"]["points"]}
    a, b = root_points[-583186], root_points[3]
    midpoint = ((a[1]+b[1])*.5, (a[2]+b[2])*.5)
    ab = next(c for c in candidates if c.get("legacyReason") and any(p[0] == c["target"]["newVertex"] and tuple(p[1:3]) == midpoint for p in c["target"]["points"]))
    successful = [c for c in candidates if any(v.get("witnessProgress") for v in c.get("variants", []))]
    structural = [c for c in candidates if not c.get("legacyReason") and c.get("shape") and all(c["shape"])]
    chosen = (successful or structural or [ab])[0]
    configure_font()
    fig, axes = plt.subplots(1, 3, figsize=(15, 5.3), layout="constrained")
    for ax, (title, geometry, name) in zip(axes, [("冻结根与直接邻面", domain["geometry"], None),
                                                 ("原 AB 中点细分：相邻侧坏角", ab["target"], ab["name"]),
                                                 ("仅翻边 AB → C–4：旧点与面数不变", chosen["target"], chosen["name"]) ]):
        # 中间图的实际替换支持小于整个冻结域，未变邻面只作为浅灰背景。
        for face in domain["geometry"]["faces"]:
            xy = [root_points[v][1:3] for v in face]
            ax.fill(*zip(*xy), color="#f1f2f3")
        points = {p[0]: p[1:3] for p in geometry["points"]}
        for i, face in enumerate(geometry["faces"]):
            xy = [points[v] for v in face]
            bad = name and not all(a["allowed"] for a in audits[name]["exactAngles"][i]["angles"])
            ax.fill(*zip(*xy), color="#e6b7a6" if bad else "#dce8ec", alpha=.7)
            ax.plot(*zip(*(xy+xy[:1])), color="#4b6576", linewidth=1)
        for v, xy in points.items():
            label = {-583186: "A", 3: "B", -828436: "C"}.get(v, "P" if v == geometry["newVertex"] else str(v))
            ax.annotate(label, xy, xytext=(3, 4), textcoords="offset points", fontsize=9)
        ax.scatter(*domain["witness"], color="#ad3232", marker="x", s=40, label="q2")
        ax.set(xlabel="U", ylabel="V", title=title, aspect="equal")
        ax.set_xlim(.9275, 1.01);ax.set_ylim(.855, 1.02)
        ax.grid(alpha=.15);ax.legend(fontsize=8)
    fig.suptitle("PQ-04｜固定局部域的形状恢复调查", fontsize=15)
    fig.supxlabel("红色面违反原最小角；几何示意不代表持续质量或完整预算交换已经成立", fontsize=10)
    fig.savefig(output / "local-recovery.png", dpi=160)
    plt.close(fig)
    write(output / "figure-selection.json", dict(ab=ab["name"], representative=chosen["name"], rule="first witness-progress result, else first shape-valid, else AB failure"))


def report(output, make_figure=False):
    start = time.monotonic();self_test()
    freeze = json.loads((output / "freeze.json").read_text());baseline = Path(freeze["baselineDirectory"])
    for name in ("after-default", "recovery-audit"):
        current = output / name
        compare(rows(current / "frames.csv"), rows(baseline / "frames.csv"), CORE_FIELDS, name)
        for file in ("witnesses.csv", "transactions.jsonl", "recovery.jsonl"):
            if (current / file).read_bytes() != (baseline / file).read_bytes():
                raise RuntimeError("Original trajectory changed: " + name + "/" + file)
        for frame in (0, 2, 15, 16, 23):
            file = f"mesh-{frame}.bin"
            if identity(current / file)["sha256"] != identity(baseline / file)["sha256"]:
                raise RuntimeError("Actual mesh changed: " + name + "/" + file)
    current = output / "recovery-audit"
    domain = json.loads((current / "recovery-domain.json").read_text())
    summary = json.loads((current / "recovery-summary.json").read_text())
    candidates = [json.loads(line) for line in (current / "recovery-candidates.jsonl").read_text().splitlines()]
    audits = {}
    for candidate in candidates:
        if "target" not in candidate:
            continue
        result = validate(candidate["old"], candidate["target"])
        exact_shape = [all(a["allowed"] for a in f["angles"]) and Fraction(f["orientation"]) > 0 for f in result["exactAngles"]]
        if exact_shape != candidate["shape"] or not result["structuralValid"]:
            raise RuntimeError("Independent geometry audit disagrees: " + candidate["name"] + " " + str(result))
        if result["faceDelta"] != candidate["faceDelta"]:
            raise RuntimeError("Face accounting changed: " + candidate["name"])
        audits[candidate["name"]] = result
    local = [c for c in candidates if "target" in c and not c.get("legacyReason")]
    accepted = [c["name"] for c in local if any(v["accepted"] for v in c.get("variants", []))]
    progress = [c["name"] for c in local if any(v["witnessProgress"] for v in c.get("variants", []))]
    result = dict(summary=summary, domain=domain, candidates=candidates, geometryAudits=audits,
                  localGeometries=len(local), localShapeValid=sum(all(c["shape"]) for c in local),
                  acceptedGeometries=accepted, witnessProgressGeometries=progress,
                  trajectory="24 frames, three original diagnostics and five actual meshes unchanged",
                  baselineProcess=freeze["baselineCommand"],
                  processes={name: json.loads((output / "commands" / (name+".json")).read_text()) for name in ("after-default", "recovery-audit")},
                  reportSeconds=time.monotonic()-start)
    write(output / "recovery-analysis.json", result)
    if make_figure:
        figure(output, domain, candidates, audits)
    print(json.dumps({k: result[k] for k in ("summary", "localGeometries", "localShapeValid", "acceptedGeometries", "witnessProgressGeometries")}, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=("freeze", "collect", "report"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--probe", type=Path)
    parser.add_argument("--figure", action="store_true")
    args = parser.parse_args()
    if args.mode in ("freeze", "collect"):
        if args.probe is None:
            parser.error("freeze/collect requires --probe")
        (freeze if args.mode == "freeze" else collect)(args.output.resolve(), args.probe.resolve())
    else:
        report(args.output.resolve(), args.figure)
