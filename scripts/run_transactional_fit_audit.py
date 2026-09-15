"""在冻结原轨迹上诊断单高度拟合，反事实不参与实际状态更新。"""

import argparse
import csv
import json
from pathlib import Path
import shutil
import subprocess
import time

from run_transactional_platform import ROOT, identity, native_run, win, write
from run_transactional_quality_audit import CORE_FIELDS
from transactional_platform_report import compare, rows

PRIOR = ROOT / "benchmark-output/cpu-refinement/pq-02/run-01"
SOURCES = ("src/algorithms/greedy_transactional_lod/TransactionalCertification.h",
           "src/algorithms/greedy_transactional_lod/TransactionalCertification.cpp",
           "tests/TransactionalQualityProvenanceProbe.cpp", "tests/CMakeLists.txt")


def freeze(output, probe):
    output.mkdir(parents=True, exist_ok=False)
    previous = json.loads((PRIOR / "commands/residual.json").read_text())
    if identity(probe)["sha256"] != previous["binary"]["sha256"]:
        raise RuntimeError("Current binary differs from PQ-02; establish a fresh baseline first")
    saved = output / "before/quality-provenance-probe.exe"
    saved.parent.mkdir()
    shutil.copy2(probe, saved)
    for source in SOURCES:
        target = output / "before" / source
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / source, target)
    previous_freeze = json.loads((PRIOR / "freeze.json").read_text())
    write(output / "freeze.json", {
        "protocol": "pq03-fit-v1", "beforeProbe": identity(saved),
        "commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "sourcesBefore": {f: identity(ROOT / f) for f in SOURCES},
        "witness": previous_freeze["witness"], "witnessSource": previous_freeze["witnessSource"],
        "baselineCommand": previous, "baselineDirectory": str(PRIOR / "residual"),
        "baselinePolicy": "reuse same binary and unchanged local Windows/WSL environment",
        "limits": {"queries": 32, "resolutionPx": .001, "sampleVisits": 2000000, "seconds": 60},
        "scope": "frame7 private proposal only; unchanged 24-batch B trajectory"})


def collect(output, probe):
    frozen = json.loads((output / "freeze.json").read_text())
    if identity(Path(frozen["witnessSource"]["path"])) != frozen["witnessSource"]:
        raise RuntimeError("Frozen witness source changed")
    extra_sources = ("src/experiment/greedy_transactional_lod/TransactionalFitCounterfactual.h",
                     "src/experiment/greedy_transactional_lod/TransactionalFitCounterfactual.cpp")
    after = dict(probe=identity(probe), sources={f: identity(ROOT / f) for f in SOURCES+extra_sources})
    file = output / "after-freeze.json"
    if file.exists() and json.loads(file.read_text()) != after:
        raise RuntimeError("Measured sources changed; use a separate run")
    write(file, after)
    witness = ["--witness", *(format(v, ".17g") for v in frozen["witness"])]
    for name, extra in (("after-default", []), ("fit-audit", ["--fit-audit"])):
        result = native_run(output, name, probe, "peking", "transactional", 8, "audit",
                            arguments=[win(output / name), "immutable", *witness, *extra])
        if result["status"] != "ok":
            raise RuntimeError("Native run failed: " + name)


def figure(output, audit_name, diagnostic):
    """只画已查询的阈值和已认证高度，不补采样来制造平滑误差曲线。"""
    from experiment_infrastructure.asset_preview import configure_font, plt
    configure_font()
    variants = diagnostic["variants"]
    labels = ("原选值", "源高度投影", "模型搜索选值")
    colors = ("#5d6776", "#277eac", "#bc5b30")
    fig, axes = plt.subplots(1, 2, figsize=(13, 5.2), layout="constrained")
    ax = axes[0]
    h0 = diagnostic["initialHeight"]
    interval = diagnostic["observedInterval"]
    ax.axvspan(h0+interval["lowerDelta"], h0+interval["upperDelta"], color="#e5ece9", label="原可行高度区间")
    ax.axhline(interval["targetPx"], color="#777777", linestyle="--", linewidth=1, label="原接受阈值")
    ax.axvline(h0, color="#999999", linestyle=":", linewidth=1, label="未拟合初高")
    for item, label, color in zip(variants, labels, colors):
        ax.scatter(item["height"], item["errorUpperPx"], s=55, color=color, zorder=3, label=label)
        ax.annotate(f'{item["errorUpperPx"]:.6f}', (item["height"], item["errorUpperPx"]),
                    xytext=(4, 9), textcoords="offset points", fontsize=9)
    ax.set(xlabel="新点高度（世界单位）", ylabel="认证补丁最大误差（px）", title="相同连接、样本和原区间：三个实际认证点")
    ax.set_ylim(1.59, 1.76)
    ax.legend(fontsize=8, loc="upper right")
    queries = list(csv.DictReader((output / audit_name / "fit-queries.csv").open()))
    ax = axes[1]
    for status, color, marker, label in (("feasible", "#277eac", "o", "模型可行"),
                                          ("model_empty", "#bc5b30", "x", "模型判空"),
                                          ("numeric_unknown", "#777777", "s", "数值未知")):
        subset = [(i+1, float(q["targetPx"])) for i, q in enumerate(queries) if q["status"] == status]
        if subset:
            ax.scatter(*zip(*subset), color=color, marker=marker, label=label)
    ax.axhspan(diagnostic["lowerTarget"], diagnostic["bestModel"]["targetPx"], color="#dbe8ed")
    ax.set(xlabel="查询序号", ylabel="查询阈值（px）", title="有限二分留痕：模型括区不代表几何最优")
    ax.legend(fontsize=9, loc="lower right")
    for ax in axes:
        ax.grid(alpha=.2)
    fig.suptitle("PQ-03｜frame7 固定拓扑的一维高度反事实", fontsize=15)
    fig.supxlabel("只展示局部诊断：更低最大值仍可能使其他点退化；未分叉持续轨迹", fontsize=10)
    fig.savefig(output / "fit-counterfactual.png", dpi=160)
    plt.close(fig)


def report(output, audit_name="fit-audit", make_figure=False):
    started = time.monotonic()
    frozen = json.loads((output / "freeze.json").read_text())
    baseline = Path(frozen["baselineDirectory"])
    for name in ("after-default", audit_name):
        current = output / name
        compare(rows(current / "frames.csv"), rows(baseline / "frames.csv"), CORE_FIELDS, name)
        for file in ("witnesses.csv", "transactions.jsonl", "recovery.jsonl"):
            if (current / file).read_bytes() != (baseline / file).read_bytes():
                raise RuntimeError("Diagnostic trajectory changed: " + name + "/" + file)
        for frame in (0, 2, 15, 16, 23):
            file = f"mesh-{frame}.bin"
            if identity(current / file)["sha256"] != identity(baseline / file)["sha256"]:
                raise RuntimeError("Actual mesh changed: " + name + "/" + file)
    diagnostic = json.loads((output / audit_name / "fit-counterfactual.json").read_text())
    if diagnostic["status"] == "complete":
        with (output / audit_name / "fit-model.csv").open() as stream:
            model = list(csv.DictReader(stream))
        with (output / audit_name / "fit-local-samples.csv").open() as stream:
            local = list(csv.DictReader(stream))
        # 零权重样本不能通过本自由高度改变；这里仅解释已冻结模型的误差平台
        fixed = [s for s in model if float(s["beta"]) == 0 and float(s["clipW"]) > 0]
        floor = max(fixed, key=lambda s: float(s["factor"])*abs(float(s["difference"]))/float(s["clipW"]), default=None)
        tradeoffs = {}
        for name in ("source_projection", "model_minimum"):
            subset = [s for s in local if s["variant"] == name]
            tradeoffs[name] = {key: max(subset, key=lambda s: float(s[key] or "-inf"))
                              for key in ("heightExcessVsOriginal", "screenExcessVsOriginal")}
        write(output / "fit-local-analysis.json", dict(zeroWeightSamples=len(fixed),
              totalSamples=len(model), zeroWeightModelFloor=floor,
              modelFloorPx=(float(floor["factor"])*abs(float(floor["difference"]))/float(floor["clipW"])) if floor else None,
              fixedSampleGeometry=next((s for s in local if floor and s["sample"] == floor["sample"] and s["variant"] == "original"), None),
              tradeoffWitnesses=tradeoffs,
              scope="Derived from recorded floating model and local Q only; not a continuous-surface optimum"))
    timing = {name: json.loads((output / "commands" / (name + ".json")).read_text())
              for name in ("after-default", audit_name)}
    write(output / "fit-audit-summary.json", dict(protocol=frozen["protocol"],
          trajectory="24 frames, three diagnostic files and five actual meshes unchanged",
          baselineProcess=frozen["baselineCommand"], processes=timing,
          diagnosticDirectory=audit_name, diagnostic=diagnostic, reportSeconds=time.monotonic()-started))
    if make_figure and diagnostic["status"] == "complete":
        figure(output, audit_name, diagnostic)
    print(json.dumps(diagnostic, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=("freeze", "collect", "report"))
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--probe", type=Path)
    parser.add_argument("--audit-name", default="fit-audit", choices=("fit-audit", "fit-audit-schema"))
    parser.add_argument("--figure", action="store_true")
    args = parser.parse_args()
    if args.mode != "report" and args.probe is None:
        parser.error("freeze/collect require --probe")
    if args.mode == "freeze":
        freeze(args.output.resolve(), args.probe.resolve())
    elif args.mode == "collect":
        collect(args.output.resolve(), args.probe.resolve())
    else:
        report(args.output.resolve(), args.audit_name, args.figure)
