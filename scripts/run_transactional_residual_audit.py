"""追溯冻结旧点策略的一个既有残余见证，复用原生启动和身份对照。"""

import argparse
from fractions import Fraction
import json
import math
from pathlib import Path
import shutil
import struct
import subprocess
import time

from run_transactional_platform import ROOT, identity, native_run, win, write
from run_transactional_quality_audit import CORE_FIELDS
from transactional_platform_report import compare, rows

PRIOR = ROOT / "benchmark-output/cpu-refinement/pq-01/run-01"
SOURCES = ("src/experiment/greedy_transactional_lod/TransactionalQualityProvenance.h",
           "src/experiment/greedy_transactional_lod/TransactionalQualityProvenance.cpp",
           "tests/TransactionalQualityProvenanceProbe.cpp")


def baseline(output, probe):
    output.mkdir(parents=True, exist_ok=False)
    quality_file = PRIOR / "opengl-peking-transactional8-export/quality-15/quality.json"
    witness = json.loads(quality_file.read_text())["screenWitness"][1:]
    saved = output / "before/quality-provenance-probe.exe"
    saved.parent.mkdir()
    shutil.copy2(probe, saved)
    for name in SOURCES:
        destination = output / "before" / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / name, destination)
    write(output / "freeze.json", {
        "protocol": "pq02-residual-v1", "witness": witness,
        "witnessSource": identity(quality_file), "beforeProbe": identity(saved),
        "commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "sourcesBefore": {name: identity(ROOT / name) for name in SOURCES},
        "priorRun": str(PRIOR), "heightPolicy": "immutable", "budget": 50000,
        "workers": 8, "prefix": 160,
        "sourceAsset": identity(ROOT / "assets/heightmaps/Hm_Terrain_Peking_513.png"),
        "scope": "24 batches; diagnostic only; no algorithm changes"})
    result = native_run(output, "before-default", saved, "peking", "transactional", 8, "audit",
                        arguments=[win(output / "before-default"), "immutable"])
    if result["status"] != "ok":
        raise RuntimeError("Before diagnostic failed")
    compare(rows(output / "before-default/frames.csv"), rows(PRIOR / "b-provenance/frames.csv"),
            CORE_FIELDS, "before B/PQ-01 identity")


def collect(output, probe):
    freeze = json.loads((output / "freeze.json").read_text())
    if identity(Path(freeze["witnessSource"]["path"])) != freeze["witnessSource"]:
        raise RuntimeError("Frozen witness source changed")
    after_file = output / "after-freeze.json"
    after = {"probe": identity(probe), "sources": {name: identity(ROOT / name) for name in SOURCES}}
    if after_file.exists() and json.loads(after_file.read_text()) != after:
        raise RuntimeError("After binary/source changed; use a separate run")
    write(after_file, after)
    extra = ["--witness", *(format(value, ".17g") for value in freeze["witness"])]
    for name, arguments in (("after-default", []), ("residual", extra)):
        result = native_run(output, name, probe, "peking", "transactional", 8, "audit",
                            arguments=[win(output / name), "immutable", *arguments])
        if result["status"] != "ok":
            raise RuntimeError("Diagnostic failed: " + name)
        compare(rows(output / name / "frames.csv"), rows(PRIOR / "b-provenance/frames.csv"),
                CORE_FIELDS, name + "/PQ-01 identity")


def point_quality(frozen):
    """读取既有独立评价的同序号误差，不用两个不同位置的全局最大值相减。"""
    source = Path(frozen["witnessSource"]["path"])
    if identity(source) != frozen["witnessSource"]:
        raise RuntimeError("Frozen quality source changed")
    quality = json.loads(source.read_text())
    baseline_file = PRIOR / "opengl-peking-dod8-export/quality-15/quality.json"
    baseline = json.loads(baseline_file.read_text())
    ordinal = quality["screenWitness"][0]
    if quality["sampleHash"] != baseline["sampleHash"] or quality["q"] != baseline["q"]:
        raise RuntimeError("Point quality sample domains differ")
    errors, sources = [], []
    for file in (source, baseline_file):
        data = file.with_name("errors.f64")
        if data.stat().st_size != quality["q"] * 8 or not 0 <= ordinal < quality["q"]:
            raise RuntimeError("Invalid point error array")
        with data.open("rb") as stream:
            stream.seek(ordinal * 8)
            value = struct.unpack("<d", stream.read(8))[0]
        if not math.isfinite(value):
            raise RuntimeError("Point is not comparable in the frozen view")
        errors.append(value)
        sources.append(dict(quality=identity(file), errors=identity(data)))
    return dict(frame=15, sampleOrdinal=ordinal, sampleHash=quality["sampleHash"],
                immutablePx=errors[0], dodPx=errors[1], excessPx=errors[0]-errors[1], sources=sources)


def root_shape(witness, transaction):
    """只复算已记录补丁的参数域角度，不重新生成邻域或搜索替代恢复操作。"""
    points = {p[0]: tuple(Fraction(x) for x in p[1:3]) for p in transaction["points"]}
    q = tuple(Fraction(x) for x in witness)

    def cross(a, b, c):
        return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])

    def corners(triangle):
        result = []
        for i in range(3):
            a, b, c = triangle[i], triangle[(i+1) % 3], triangle[(i+2) % 3]
            u, v = [b[j]-a[j] for j in range(2)], [c[j]-a[j] for j in range(2)]
            dot = sum(x*y for x, y in zip(u, v))
            square = sum(x*x for x in u)*sum(x*x for x in v)
            # 与生产最小角判据相同；角度小数只展示，成败使用精确有理数比较。
            result.append(dict(cosSquared=str(dot*dot/square),
                               degrees=math.degrees(math.acos(float(dot)/math.sqrt(float(square)))),
                               belowMinimum=dot > 0 and 10*dot*dot > 9*square))
        return result

    matches = []
    for face in transaction["geometry"]["newFaces"]:
        triangle = [points[v] for v in face]
        if all(cross(triangle[i], triangle[(i+1) % 3], q) > 0 for i in range(3)):
            edges = []
            for i in range(3):
                j, k = (i+1) % 3, (i+2) % 3
                midpoint = tuple((triangle[i][v]+triangle[j][v])/2 for v in range(2))
                children = [corners([triangle[j], triangle[k], midpoint]),
                            corners([triangle[k], triangle[i], midpoint])]
                edges.append(dict(edge=[face[i], face[j]], rootChildren=children,
                                  rootChildrenPass=not any(c["belowMinimum"] for child in children for c in child)))
            matches.append(dict(face=face, points=[[str(x) for x in p] for p in triangle],
                                corners=corners(triangle), midpointSplits=edges))
    if len(matches) != 1:
        raise RuntimeError("Local shape derivation needs a unique strictly containing face")
    return dict(sourceFrame=transaction["frame"], scope="recorded root only; no neighbor or general repair search",
                **matches[0])


def figure(output, result):
    """将几何残差、可见误差和批前排名分开绘制，不把离屏投影当作可见质量。"""
    from experiment_infrastructure.asset_preview import configure_font, plt
    configure_font()
    after = [r for r in result["states"] if r["phase"] == "after"]
    before = [r for r in result["states"] if r["phase"] == "before"]
    frames = [int(r["frame"]) for r in after]
    plot, axes = plt.subplots(3, 1, figsize=(10, 9), sharex=True, layout="constrained")
    for axis in axes:
        axis.axvspan(-0.5, 6.5, color="#dddddd", alpha=.45)
        axis.axvspan(15.5, 23.5, color="#dddddd", alpha=.45)
        axis.grid(alpha=.22)
        axis.set_xlim(-.5, 23.5)
    axes[0].step(frames, [float(r["heightResidual"]) for r in after], where="mid", color="#b54738", marker=".")
    axes[0].set(title="见证曲面高度减源高度：第7帧后不再改变", ylabel="世界高度残差")
    visible = [float(r["error"]) if r["visible"] == "1" else math.nan for r in after]
    axes[1].plot(frames, visible, "o-", label="当前视图可见误差", color="#b54738")
    axes[1].step(frames, [float(r["futureError"]) for r in after], where="mid", linestyle="--",
                 color="#376996", label="统一投影到第15帧视图，仅用于几何归因")
    axes[1].set(title="灰区为当前不可见；返回后的最大值下降不代表此点恢复", ylabel="屏幕偏移 / px")
    axes[1].legend(fontsize=9, loc="lower left")
    axes[2].plot(frames, [int(r["rank"]) for r in before], "o-", color="#376996", label="覆盖根的批前排名")
    axes[2].axhline(160, color="#b54738", linestyle="--", label="冻结前缀上限160")
    axes[2].set(yscale="log", ylabel="排名 / 对数轴", xlabel="帧序号（从0起算）",
                title="第8帧：排名38但形状失败；第15帧：排名216，未入前缀")
    axes[2].set_xticks(frames)
    axes[2].legend(fontsize=9, loc="upper left")
    plot.suptitle("PQ-02 固定残余点：两次局部修改与后续恢复阻塞", fontsize=14)
    for extension in ("png", "svg"):
        plot.savefig(output / f"residual-timeline.{extension}", dpi=160)
    plt.close(plot)


def report(output, draw_figure=False):
    started = time.monotonic()
    frozen = json.loads((output / "freeze.json").read_text())
    # 既有两见证输出要求原样一致，新增观测不得改变生产结果或旧诊断含义。
    for filename in ("witnesses.csv", "transactions.jsonl", "recovery.jsonl"):
        if (output / "before-default" / filename).read_bytes() != (output / "after-default" / filename).read_bytes():
            raise RuntimeError("Default diagnostic changed: " + filename)
    for name in ("before-default", "after-default", "residual"):
        compare(rows(output / name / "frames.csv"), rows(PRIOR / "b-provenance/frames.csv"),
                CORE_FIELDS, name + " trajectory")
        for frame in (0, 2, 15, 16, 23):
            filename = f"mesh-{frame}.bin"
            if identity(output / name / filename)["sha256"] != identity(PRIOR / "b-provenance" / filename)["sha256"]:
                raise RuntimeError("Exported mesh changed: " + name + "/" + filename)
    states = [r for r in rows(output / "residual/witnesses.csv") if r["witness"] == "2"]
    if len(states) != 48:
        raise RuntimeError("Residual witness needs all before/after observations")
    for row in states:
        if [float(row["u"]), float(row["v"])] != frozen["witness"]:
            raise RuntimeError("Residual coordinates changed")
    before = {int(r["frame"]): r for r in states if r["phase"] == "before"}
    after = {int(r["frame"]): r for r in states if r["phase"] == "after"}
    transactions = [json.loads(line) for line in (output / "residual/transactions.jsonl").read_text().splitlines()]
    covered, changes = [], []
    for transaction in transactions:
        for witness in transaction["witnesses"]:
            if witness["id"] != 2:
                continue
            record = dict(transaction=transaction, witness=witness,
                          before=before[transaction["frame"]], after=after[transaction["frame"]])
            covered.append(record)
            if abs(witness["heightAfter"] - witness["heightBefore"]) > 1e-8:
                if "geometry" not in transaction:
                    raise RuntimeError("Missing local geometry evidence")
                changes.append(record)
    recovery = [record for line in (output / "residual/recovery.jsonl").read_text().splitlines()
                if (record := json.loads(line))["witness"] == 2]
    timing = {name: json.loads((output / "commands" / (name + ".json")).read_text())
              for name in ("before-default", "after-default", "residual")}
    result = {"protocol": frozen["protocol"], "witness": frozen["witness"],
              "identity": "24-frame logic and five actual mesh artifacts unchanged; default diagnostic byte-identical",
              "states": states, "covered": covered, "changes": changes, "recovery": recovery,
              "pointQuality": point_quality(frozen),
              "localShape": root_shape(frozen["witness"], changes[-1]["transaction"]) if changes else None,
              "diagnosticProcesses": timing, "reportSeconds": time.monotonic() - started}
    write(output / "residual-audit.json", result)
    if draw_figure:
        figure(output, result)
    print("追加见证涉及补丁", len(covered), "实际高度变化", len(changes))
    for record in changes:
        t, w = record["transaction"], record["witness"]
        print("frame", t["frame"], "kind", t["kind"], "exchange", t["exchange"],
              "height", w["heightBefore"], "->", w["heightAfter"], "visible", w["visible"])
    for row in states:
        if row["phase"] == "after":
            print("frame", row["frame"], "h", row["height"], "error", row["error"],
                  "rank", row["rank"])


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=("baseline", "collect", "report"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--probe", type=Path)
    parser.add_argument("--figure", action="store_true", help="归约后绘制见证时序图，不重新运行算法")
    args = parser.parse_args()
    if args.mode != "report" and args.probe is None:
        parser.error("baseline/collect require --probe")
    output = args.output.resolve()
    if args.mode == "baseline":
        baseline(output, args.probe.resolve())
    elif args.mode == "collect":
        collect(output, args.probe.resolve())
    else:
        report(output, args.figure)
