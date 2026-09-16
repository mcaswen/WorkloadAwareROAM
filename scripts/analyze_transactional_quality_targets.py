"""QPC-04C：核对冻结质量人口、评分分解与接受证据，不运行替代策略。"""

import argparse
import csv
import json
import math
from pathlib import Path
import resource
import time

import numpy as np

from run_transactional_platform import ROOT, identity
from run_transactional_recovery_trace import checked_frame, sampling_coordinates


DATA = ROOT / "docs/research/cpu_refinement/data"
HISTORY = ROOT / "benchmark-output/experiment-infrastructure/fer-02"
BOUNDARY = ROOT / "benchmark-output/cpu-refinement/qpc-04b/run-01"
PRIOR = ROOT / "benchmark-output/cpu-refinement/qpc-01/run-01"
POINTS = (0.1, 0.25, 0.5, 1, 2, 4)
SNAPSHOTS = {"peking547-b50000-transactional-t8": (15,),
             "dem-sierra-b50000-transactional-t8": (48, 95)}


def read(path):
    return json.loads(path.read_text(encoding="utf-8"))


def rows(path):
    with path.open(encoding="utf-8-sig", newline="") as stream:
        return list(csv.DictReader(stream))


def quality_panel(frozen):
    """原始文件身份与可见mask先校验，再统计绝对操作点和逐点超额。"""
    result = []
    references = {}
    for record in frozen["rows"]:
        algorithm = "dod" if record["variant"] == "dod" else "transactional"
        case = record["case"] + "-" + algorithm + "-t8"
        root = BOUNDARY if record["variant"] == "on" else HISTORY
        directory, index, entry = checked_frame(root, case, record["frame"])
        index_path = directory.parent / "quality-index.json"
        expected = frozen["indexHashes"][str(index_path.relative_to(ROOT))]
        if identity(index_path)["sha256"] != expected:
            raise RuntimeError("质量索引不同于规划前冻结身份")
        quality = read(directory / "quality.json")
        if quality["status"] != "sampled_only" or any(quality[k] for k in (
                "missing", "ambiguous", "nearCrossing", "invalidGeometry", "invalidProjection")):
            raise RuntimeError("质量证据不完整，不能缩小人口继续比较")
        values = np.fromfile(directory / "errors.f64", dtype="<f8")
        if len(values) != entry["sampleCount"] or not np.all(np.isfinite(values)):
            raise RuntimeError("误差数组长度或数值无效")
        visible = values >= 0
        signature = (index["sourceSha256"], entry["sampleHash"], entry["poseHash"], entry["projectionHash"])
        key = (record["case"], record["frame"])
        if record["variant"] == "off":
            references[key] = (signature, values)
        baseline_signature, baseline = references[key]
        if signature != baseline_signature or not np.array_equal(visible, baseline >= 0):
            raise RuntimeError("比较双方的源、样本、视图或可见人口不同")
        active = values[visible]
        counts = {str(p): int(np.count_nonzero(active > p)) for p in POINTS}
        if counts != record["abovePx"] or float(active.max()) != record["EmaxPx"]:
            raise RuntimeError("重算结果与规划前核对不同")
        result.append({**record, "aboveFraction": {p: n / len(active) for p, n in counts.items()},
                       "DmaxVsOffPx": float(np.max(active - baseline[visible])),
                       "excessVsOffAbove025": int(np.count_nonzero(active - baseline[visible] > .25))})
    return result


def sample_coordinates(width):
    """只比较参数坐标与集合身份，不把坐标接近升级为误差或可见性等价。"""
    uv = sampling_coordinates(width, width)
    n = width - 1
    integers = np.rint(uv * (6 * n)).astype(np.int64)
    code = integers[:, 1] * (6 * n + 1) + integers[:, 0]
    residues = integers % 6
    runtime = {(0, 0): width * width, (3, 0): n * width, (0, 3): n * width,
               (3, 3): n * n, (4, 2): n * n, (2, 4): n * n}
    expected = {(0, 0): width * width, (3, 0): n * width, (0, 3): n * width,
                (3, 3): n * n, (2, 2): n * n, (4, 4): n * n}
    actual = {(x, y): int(np.count_nonzero((residues[:, 0] == x) & (residues[:, 1] == y)))
              for x, y in expected}
    if actual != expected or len(np.unique(code)) != len(uv) or sum(actual.values()) != len(uv):
        raise RuntimeError("独立采样集合不符合真实栅格三角形的发射规则")
    exact_grid = integers / float(6 * n)
    # 两种对角线共享顶点与中点，但每格两个重心不同；身份差异不能被舍入容差吞掉
    common = sum(count for key, count in actual.items() if key in runtime)
    return {"width": width, "q": len(uv), "bijectionToRuntimeGrid": False,
            "commonIdealPositions": common, "evaluationOnlyPositions": len(uv) - common,
            "runtimeOnlyPositions": sum(runtime.values()) - common,
            "evaluationGroups": {str(k): v for k, v in actual.items()},
            "runtimeGroups": {str(k): v for k, v in runtime.items()},
            "maxUvDifference": float(np.max(np.abs(uv - exact_grid))),
            "nonidenticalCoordinates": int(np.count_nonzero(np.any(uv != exact_grid, axis=1))),
            "ordering": "位置集合和发射顺序均不同；共同理想位置在非dyadic栅格上仍有float UV差异"}


def snapshot(path, witnesses):
    """固定候选人口，只更换离线排序键；不推演新事务或未来状态。"""
    data = rows(path)
    if not data:
        raise RuntimeError("评分快照为空")
    for row in data:
        for key in ("root", "slot", "visibleSamples", "contributions", "inPrefix", "prefixLimit", "rawCount", "faces"):
            row[key] = int(row[key])
        for key in ("errorSquared", "densitySquared", "prioritySquared", "thresholdPx"):
            row[key] = float(row[key])
        if max(row["errorSquared"], row["densitySquared"]) != row["prioritySquared"]:
            raise RuntimeError("闭面误差和独立密度项不能复现持久评分")
    limit = data[0]["prefixLimit"]
    threshold = data[0]["thresholdPx"]
    if len(data) != data[0]["faces"] or len({r["root"] for r in data}) != len(data):
        raise RuntimeError("活动面快照不完整或身份重复")
    eligible = [r for r in data if math.isfinite(r["prioritySquared"]) and r["prioritySquared"] > threshold ** 2]
    ordered = sorted(eligible, key=lambda r: (-r["prioritySquared"], r["root"], r["slot"]))
    if len(ordered) != data[0]["rawCount"]:
        raise RuntimeError("重算资格与生产索引人口不同")
    actual_prefix = {r["root"] for r in data if r["inPrefix"]}
    if actual_prefix != {r["root"] for r in ordered[:limit]}:
        raise RuntimeError("重算全序与生产精确前缀不同")
    error_order = sorted(eligible, key=lambda r: (-r["errorSquared"], r["root"], r["slot"]))
    priority_rank = {r["root"]: i + 1 for i, r in enumerate(ordered)}
    error_rank = {r["root"]: i + 1 for i, r in enumerate(error_order)}

    def population(items):
        return {"roots": len(items),
                "errorAboveTrigger": sum(r["errorSquared"] > threshold ** 2 for r in items),
                "densityOnlyAboveTrigger": sum(r["errorSquared"] <= threshold ** 2 < r["densitySquared"] for r in items),
                "densityDominant": sum(r["densitySquared"] > r["errorSquared"] for r in items),
                "noVisibleSamples": sum(r["visibleSamples"] == 0 for r in items),
                "maxSampleErrorPx": math.sqrt(max((r["errorSquared"] for r in items), default=0))}

    selected = []
    by_id = {r["root"]: r for r in data}
    for witness in witnesses:
        for old in witness["targetRoots"]:
            row = by_id[old["root"]]
            if priority_rank.get(row["root"], 0) != old["rank"]:
                raise RuntimeError("全根快照与原见证排名不一致")
            selected.append({"witness": witness["localWitness"], "root": row["root"],
                             "sampleMaxPx": math.sqrt(row["errorSquared"]),
                             "densityPx": math.sqrt(row["densitySquared"]),
                             "priorityPx": math.sqrt(row["prioritySquared"]),
                             "rank": priority_rank.get(row["root"], 0),
                             "errorRankSamePopulation": error_rank.get(row["root"], 0)})
    return {"source": identity(path), "thresholdPx": threshold, "prefixLimit": limit,
            "all": population(data), "eligible": population(eligible),
            "prefix": population(ordered[:limit]), "outsidePrefix": population(ordered[limit:]),
            "errorOrderedPrefixSamePopulation": population(error_order[:limit]),
            "prefixOverlap": len(actual_prefix & {r["root"] for r in error_order[:limit]}),
            "nonfinite": sum(not math.isfinite(r["prioritySquared"]) for r in data),
            "nonfiniteWithErrorAboveTrigger": sum(not math.isfinite(r["prioritySquared"]) and
                                                   r["errorSquared"] > threshold ** 2 for r in data),
            "witnesses": selected, "scoreAndIndexExact": True}


def acceptance_events():
    result = []
    for case, group, frame in (("dem-canyon-b50000-transactional-t8", 48, 8),
                               ("peking547-b200000-transactional-t8", 15, 13)):
        path = BOUNDARY / (case + "-trace-on") / f"frame-{group}/transactions.jsonl"
        entries = [json.loads(line) for line in path.read_text().splitlines()]
        receiver = next(r for r in entries if r["frame"] == frame and r["kind"] == "B" and
                        any(w["id"] == 0 for w in r["witnesses"]))
        partner = next((r for r in entries if r["frame"] == frame and
                        r["exchange"] == receiver["exchange"] and r["kind"] == "D"), None)
        geometry = receiver["witnessGeometry"][0]["geometry"]
        result.append({"case": case, "frame": frame, "source": identity(path),
                       "root": receiver["receiverRoot"], "samples": receiver["sampleCount"],
                       "closedSamples": geometry["closedSampleCount"],
                       "oldVisibleMaxPx": geometry["oldVisibleMaxPx"],
                       "targetPx": receiver["targetPx"],
                       "newVisibleUpperPx": math.sqrt(receiver["errorUpperSquared"]),
                       "donorUpperPx": math.sqrt(partner["errorUpperSquared"]) if partner else None,
                       "witnesses": receiver["witnesses"], "geometry": receiver["witnessGeometry"]})
    return result


def compatibility(output):
    result = {}
    for case in SNAPSHOTS:
        old = PRIOR / case
        new = output / case
        paths = [p for p in old.rglob("*") if p.is_file() and p.suffix in (".csv", ".jsonl")]
        for path in paths:
            other = new / path.relative_to(old)
            if not other.exists() or path.read_bytes() != other.read_bytes():
                raise RuntimeError("只读诊断改变旧证据：" + str(path.relative_to(old)))
        result[case] = {"legacyFilesByteExact": len(paths), "frames": len(rows(new / "frames.csv")),
                        "observationCosts": rows(new / "priority-audit.csv")}
    before = rows(output / "normal-before/frames.csv")
    after = rows(output / "normal-after/frames.csv")
    if len(before) != len(after):
        raise RuntimeError("普通前后运行机会数不同")
    semantics = [k for k in before[0] if not k.endswith("Ms")]
    for left, right in zip(before, after):
        if any(left[k] != right[k] for k in semantics):
            raise RuntimeError("普通模式语义或工作量变化")
    costs = {}
    for name, records in (("before", before), ("after", after)):
        values = np.array([float(r["cpuMs"]) for r in records[3:]])
        costs[name] = {"meanMs": float(values.mean()), "medianMs": float(np.median(values)),
                       "p95Ms": float(np.quantile(values, .95)), "maxMs": float(values.max())}
    result["normal"] = {"framesExact": len(before), "fieldsExact": semantics, "costs": costs}
    return result


def analyze(output):
    destination = output / "summary.json"
    if destination.exists():
        raise RuntimeError("拒绝覆盖已有核查报告")
    started = time.perf_counter()
    frozen = read(DATA / "qpc_04c_quality_target/preplan_check.json")
    prior = read(DATA / "qpc_01_quality_recovery/summary.json")["cases"]
    quality = quality_panel(frozen)
    coordinates = [sample_coordinates(width) for width in (513, 547)]
    snapshots = {}
    for case, frames in SNAPSHOTS.items():
        snapshots[case] = {}
        for frame in frames:
            witnesses = [w for w in prior[case]["witnesses"] if w["frame"] == frame]
            snapshots[case][str(frame)] = snapshot(output / case / f"priority-{frame}.csv", witnesses)
    witnesses = [{"case": case, **w} for case, record in prior.items() for w in record["witnesses"]]
    result = {"scope": "QPC-04C误差目标诊断；未改变生产策略", "quality": quality,
              "sampleCoordinates": coordinates, "prioritySnapshots": snapshots,
              "acceptanceEvents": acceptance_events(), "witnesses": witnesses,
              "compatibility": compatibility(output),
              "maxWitnessInternalOutputDifferencePx": max(abs(w["internalVsActualPx"]) for w in witnesses),
              "analysisSeconds": time.perf_counter() - started,
              "peakRssKiB": resource.getrusage(resource.RUSAGE_SELF).ru_maxrss}
    destination.write_text(json.dumps(result, ensure_ascii=False, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    print(destination)
    print(json.dumps({"analysisSeconds": result["analysisSeconds"], "peakRssKiB": result["peakRssKiB"]}))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    analyze(parser.parse_args().output.resolve())
