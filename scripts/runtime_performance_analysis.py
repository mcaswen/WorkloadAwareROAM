"""普通运行版本比较；独立进程是统计单位，帧只用于进程内汇总。"""

import argparse
from collections import defaultdict
import csv
import hashlib
import json
import math
from pathlib import Path
import statistics

from cpu_pilot_support import file_identity

# 固定普通 CSV v4 的列契约，未知列必须先确认职责，不能靠名称猜测后忽略
BENCHMARK_FIELDS = """
profile algorithm frameIndex timeSeconds
cameraName cameraX cameraY cameraZ
heightMapWidth heightMapHeight vertexCount indexCount
buildWallMilliseconds experimentSchemaVersion terrainSize heightScale
maxDepth screenSpaceSplitThresholdPixels screenSpaceMergeThresholdPixels triangleBudget
dodParallelSplitEnabled localConstraintsEnabled topologyValidationEnabled passEvidenceEnabled
topologyPairEvidenceEnabled mergeScoreAction splitScoreAction mergeTopologyAction
splitTopologyAction meshEmitAction cpuUploadAction mergeScoreWorkerLimit
splitScoreWorkerLimit mergeTopologyWorkerLimit splitTopologyWorkerLimit meshEmitWorkerLimit
mergeScoreMinParallelEntryCount splitScoreMinParallelEntryCount meshEmitMinParallelTriangleCount splitTopologyMinParallelCandidateCount
mergeTopologyMinParallelCandidateCount parallelTopologyTargetBuild parallelTopologyPhase buildSequence
replayInputHash topologyHash activeLeafHash meshHash
normalizedMeshHash evidenceTriangleBudget budgetViolationCount queueInvariantViolationCount
resourceValidationFailureCount resultValidationEvaluated resultValidationPassed resultValidationFailureMask
referenceComparisonEvaluated referenceComparisonPassed referenceComparisonDifferenceMask passEvidenceMilliseconds
activeTriangleCount activeNodeCount originalTriangleCount subdividedTriangleCount
rebuiltTriangleCount activeSplitCount splitCount forcedSplitCount
mergeCount crackRiskCount constraintPassCount candidatePeakCount
persistentSplitQueueSize persistentMergeQueueSize queueCrossoverCount queueMembershipUpdateCount
cpuMeshFullRebuildCount cpuMeshUpdatedTriangleCount cpuMeshReusedTriangleCount cpuMeshDirtyRangeCount
rejectedSplitCount budgetRejectedSplitCount rejectedMergeCount tjunctionCount
invalidNeighborCount invalidTopologyCount cpuGpuUploadBytes cpuGpuReadbackBytes
cpuWorkerCount topologyCommitMinCandidateCount splitTopologyCommitMinCandidateCount mergeTopologyCommitMinCandidateCount
splitTopologyCandidateCount splitTopologyNonEmptyChunkCount splitTopologyCommitWorkerCount parallelSplitCommitCount
mergeTopologyCandidateCount mergeTopologyNonEmptyChunkCount mergeTopologyCommitWorkerCount parallelMergeCommitCount
interiorSplitCandidateCount boundarySplitCandidateCount interiorMergeCandidateCount boundaryMergeCandidateCount
cpuUpdateMilliseconds cpuUtilizationPercent cpuPrepareMilliseconds cpuMergeCandidateMarkMilliseconds
cpuMergeTopologyMilliseconds cpuSplitTopologyChunkBuildMilliseconds cpuSplitTopologyQueueInvalidationMilliseconds cpuSplitTopologyParallelCommitMilliseconds
cpuSplitTopologyResultMergeMilliseconds cpuSplitTopologyIndexQueueRefreshMilliseconds cpuSplitTopologySerialConvergenceMilliseconds cpuMergeTopologyChunkBuildMilliseconds
cpuMergeTopologyQueueInvalidationMilliseconds cpuMergeTopologyParallelCommitMilliseconds cpuMergeTopologyResultMergeMilliseconds cpuMergeTopologyIndexQueueRefreshMilliseconds
cpuMergeTopologySerialConvergenceMilliseconds cpuBudgetLeafCollectMilliseconds cpuErrorEvalMilliseconds cpuSplitCandidateMarkMilliseconds
cpuSplitTopologyMilliseconds cpuFinalLeafCollectMilliseconds cpuMeshEmitMilliseconds cpuFinalizeMilliseconds
cpuUploadMilliseconds algorithmRenderMilliseconds splitMilliseconds mergeMilliseconds
emitMilliseconds validateMilliseconds maxActiveDepth mergeTopologyPairEvaluated
mergeTopologyPairEquivalent mergeTopologyPairFrozenCandidateHash mergeTopologyPairFrozenCandidateCount mergeTopologyPairCandidateSnapshotMs
mergeTopologyPairEvidenceMs mergeTopologyPairSerialAction mergeTopologyPairSerialTopologyHash mergeTopologyPairSerialActiveLeafHash
mergeTopologyPairSerialQueueMembershipHash mergeTopologyPairSerialMeshEditHash mergeTopologyPairSerialActiveTriangleCount mergeTopologyPairSerialInteriorCandidateCount
mergeTopologyPairSerialBoundaryCandidateCount mergeTopologyPairSerialEffectiveWorkerCount mergeTopologyPairSerialEarlyCommitCount mergeTopologyPairSerialBudgetViolationCount
mergeTopologyPairSerialQueueInvariantViolationCount mergeTopologyPairSerialTjunctionCount mergeTopologyPairSerialInvalidNeighborCount mergeTopologyPairSerialInvalidTopologyCount
mergeTopologyPairSerialStateCloneMs mergeTopologyPairSerialChunkBuildMs mergeTopologyPairSerialQueueInvalidationMs mergeTopologyPairSerialCommitMs
mergeTopologyPairSerialResultMergeMs mergeTopologyPairSerialIndexQueueRefreshMs mergeTopologyPairSerialSerialConvergenceMs mergeTopologyPairSerialWallMs
mergeTopologyPairParallelAction mergeTopologyPairParallelTopologyHash mergeTopologyPairParallelActiveLeafHash mergeTopologyPairParallelQueueMembershipHash
mergeTopologyPairParallelMeshEditHash mergeTopologyPairParallelActiveTriangleCount mergeTopologyPairParallelInteriorCandidateCount mergeTopologyPairParallelBoundaryCandidateCount
mergeTopologyPairParallelEffectiveWorkerCount mergeTopologyPairParallelEarlyCommitCount mergeTopologyPairParallelBudgetViolationCount mergeTopologyPairParallelQueueInvariantViolationCount
mergeTopologyPairParallelTjunctionCount mergeTopologyPairParallelInvalidNeighborCount mergeTopologyPairParallelInvalidTopologyCount mergeTopologyPairParallelStateCloneMs
mergeTopologyPairParallelChunkBuildMs mergeTopologyPairParallelQueueInvalidationMs mergeTopologyPairParallelCommitMs mergeTopologyPairParallelResultMergeMs
mergeTopologyPairParallelIndexQueueRefreshMs mergeTopologyPairParallelSerialConvergenceMs mergeTopologyPairParallelWallMs splitTopologyPairEvaluated
splitTopologyPairEquivalent splitTopologyPairFrozenCandidateHash splitTopologyPairFrozenCandidateCount splitTopologyPairCandidateSnapshotMs
splitTopologyPairEvidenceMs splitTopologyPairSerialAction splitTopologyPairSerialTopologyHash splitTopologyPairSerialActiveLeafHash
splitTopologyPairSerialQueueMembershipHash splitTopologyPairSerialMeshEditHash splitTopologyPairSerialActiveTriangleCount splitTopologyPairSerialInteriorCandidateCount
splitTopologyPairSerialBoundaryCandidateCount splitTopologyPairSerialEffectiveWorkerCount splitTopologyPairSerialEarlyCommitCount splitTopologyPairSerialBudgetViolationCount
splitTopologyPairSerialQueueInvariantViolationCount splitTopologyPairSerialTjunctionCount splitTopologyPairSerialInvalidNeighborCount splitTopologyPairSerialInvalidTopologyCount
splitTopologyPairSerialStateCloneMs splitTopologyPairSerialChunkBuildMs splitTopologyPairSerialQueueInvalidationMs splitTopologyPairSerialCommitMs
splitTopologyPairSerialResultMergeMs splitTopologyPairSerialIndexQueueRefreshMs splitTopologyPairSerialSerialConvergenceMs splitTopologyPairSerialWallMs
splitTopologyPairParallelAction splitTopologyPairParallelTopologyHash splitTopologyPairParallelActiveLeafHash splitTopologyPairParallelQueueMembershipHash
splitTopologyPairParallelMeshEditHash splitTopologyPairParallelActiveTriangleCount splitTopologyPairParallelInteriorCandidateCount splitTopologyPairParallelBoundaryCandidateCount
splitTopologyPairParallelEffectiveWorkerCount splitTopologyPairParallelEarlyCommitCount splitTopologyPairParallelBudgetViolationCount splitTopologyPairParallelQueueInvariantViolationCount
splitTopologyPairParallelTjunctionCount splitTopologyPairParallelInvalidNeighborCount splitTopologyPairParallelInvalidTopologyCount splitTopologyPairParallelStateCloneMs
splitTopologyPairParallelChunkBuildMs splitTopologyPairParallelQueueInvalidationMs splitTopologyPairParallelCommitMs splitTopologyPairParallelResultMergeMs
splitTopologyPairParallelIndexQueueRefreshMs splitTopologyPairParallelSerialConvergenceMs splitTopologyPairParallelWallMs pass_mergeScoreRequestedAction
pass_mergeScoreEffectiveAction pass_mergeScoreFallbackReason pass_mergeScoreMembershipUpdate pass_mergeScorePriorityRefresh
pass_mergeScoreDataUpdate pass_mergeScoreRequestedWorkerCount pass_mergeScoreEffectiveWorkerCount pass_mergeScoreCandidateCount
pass_mergeScoreDirtyItemCount pass_mergeScoreScoreMs pass_mergeScoreHeapifyMs pass_mergeScoreCandidateSnapshotMs
pass_mergeScoreMembershipUpdateCount pass_mergeScoreMembershipUpdateMs pass_mergeScoreWallMs pass_splitScoreRequestedAction
pass_splitScoreEffectiveAction pass_splitScoreFallbackReason pass_splitScoreMembershipUpdate pass_splitScorePriorityRefresh
pass_splitScoreDataUpdate pass_splitScoreRequestedWorkerCount pass_splitScoreEffectiveWorkerCount pass_splitScoreCandidateCount
pass_splitScoreDirtyItemCount pass_splitScoreScoreMs pass_splitScoreHeapifyMs pass_splitScoreCandidateSnapshotMs
pass_splitScoreMembershipUpdateCount pass_splitScoreMembershipUpdateMs pass_splitScoreWallMs pass_mergeTopologyRequestedAction
pass_mergeTopologyEffectiveAction pass_mergeTopologyFallbackReason pass_mergeTopologyMembershipUpdate pass_mergeTopologyPriorityRefresh
pass_mergeTopologyDataUpdate pass_mergeTopologyRequestedWorkerCount pass_mergeTopologyEffectiveWorkerCount pass_mergeTopologyCandidateCount
pass_mergeTopologyDirtyItemCount pass_mergeTopologyScoreMs pass_mergeTopologyHeapifyMs pass_mergeTopologyCandidateSnapshotMs
pass_mergeTopologyMembershipUpdateCount pass_mergeTopologyMembershipUpdateMs pass_mergeTopologyWallMs pass_splitTopologyRequestedAction
pass_splitTopologyEffectiveAction pass_splitTopologyFallbackReason pass_splitTopologyMembershipUpdate pass_splitTopologyPriorityRefresh
pass_splitTopologyDataUpdate pass_splitTopologyRequestedWorkerCount pass_splitTopologyEffectiveWorkerCount pass_splitTopologyCandidateCount
pass_splitTopologyDirtyItemCount pass_splitTopologyScoreMs pass_splitTopologyHeapifyMs pass_splitTopologyCandidateSnapshotMs
pass_splitTopologyMembershipUpdateCount pass_splitTopologyMembershipUpdateMs pass_splitTopologyWallMs pass_meshEmitRequestedAction
pass_meshEmitEffectiveAction pass_meshEmitFallbackReason pass_meshEmitMembershipUpdate pass_meshEmitPriorityRefresh
pass_meshEmitDataUpdate pass_meshEmitRequestedWorkerCount pass_meshEmitEffectiveWorkerCount pass_meshEmitCandidateCount
pass_meshEmitDirtyItemCount pass_meshEmitScoreMs pass_meshEmitHeapifyMs pass_meshEmitCandidateSnapshotMs
pass_meshEmitMembershipUpdateCount pass_meshEmitMembershipUpdateMs pass_meshEmitWallMs pass_cpuUploadRequestedAction
pass_cpuUploadEffectiveAction pass_cpuUploadFallbackReason pass_cpuUploadMembershipUpdate pass_cpuUploadPriorityRefresh
pass_cpuUploadDataUpdate pass_cpuUploadRequestedWorkerCount pass_cpuUploadEffectiveWorkerCount pass_cpuUploadCandidateCount
pass_cpuUploadDirtyItemCount pass_cpuUploadScoreMs pass_cpuUploadHeapifyMs pass_cpuUploadCandidateSnapshotMs
pass_cpuUploadMembershipUpdateCount pass_cpuUploadMembershipUpdateMs pass_cpuUploadWallMs passed
""".split()

TIME_FIELDS = """
buildWallMilliseconds passEvidenceMilliseconds cpuUpdateMilliseconds cpuPrepareMilliseconds
cpuMergeCandidateMarkMilliseconds cpuMergeTopologyMilliseconds cpuSplitTopologyChunkBuildMilliseconds cpuSplitTopologyQueueInvalidationMilliseconds
cpuSplitTopologyParallelCommitMilliseconds cpuSplitTopologyResultMergeMilliseconds cpuSplitTopologyIndexQueueRefreshMilliseconds cpuSplitTopologySerialConvergenceMilliseconds
cpuMergeTopologyChunkBuildMilliseconds cpuMergeTopologyQueueInvalidationMilliseconds cpuMergeTopologyParallelCommitMilliseconds cpuMergeTopologyResultMergeMilliseconds
cpuMergeTopologyIndexQueueRefreshMilliseconds cpuMergeTopologySerialConvergenceMilliseconds cpuBudgetLeafCollectMilliseconds cpuErrorEvalMilliseconds
cpuSplitCandidateMarkMilliseconds cpuSplitTopologyMilliseconds cpuFinalLeafCollectMilliseconds cpuMeshEmitMilliseconds
cpuFinalizeMilliseconds cpuUploadMilliseconds algorithmRenderMilliseconds splitMilliseconds
mergeMilliseconds emitMilliseconds validateMilliseconds mergeTopologyPairCandidateSnapshotMs
mergeTopologyPairEvidenceMs mergeTopologyPairSerialStateCloneMs mergeTopologyPairSerialChunkBuildMs mergeTopologyPairSerialQueueInvalidationMs
mergeTopologyPairSerialCommitMs mergeTopologyPairSerialResultMergeMs mergeTopologyPairSerialIndexQueueRefreshMs mergeTopologyPairSerialSerialConvergenceMs
mergeTopologyPairSerialWallMs mergeTopologyPairParallelStateCloneMs mergeTopologyPairParallelChunkBuildMs mergeTopologyPairParallelQueueInvalidationMs
mergeTopologyPairParallelCommitMs mergeTopologyPairParallelResultMergeMs mergeTopologyPairParallelIndexQueueRefreshMs mergeTopologyPairParallelSerialConvergenceMs
mergeTopologyPairParallelWallMs splitTopologyPairCandidateSnapshotMs splitTopologyPairEvidenceMs splitTopologyPairSerialStateCloneMs
splitTopologyPairSerialChunkBuildMs splitTopologyPairSerialQueueInvalidationMs splitTopologyPairSerialCommitMs splitTopologyPairSerialResultMergeMs
splitTopologyPairSerialIndexQueueRefreshMs splitTopologyPairSerialSerialConvergenceMs splitTopologyPairSerialWallMs splitTopologyPairParallelStateCloneMs
splitTopologyPairParallelChunkBuildMs splitTopologyPairParallelQueueInvalidationMs splitTopologyPairParallelCommitMs splitTopologyPairParallelResultMergeMs
splitTopologyPairParallelIndexQueueRefreshMs splitTopologyPairParallelSerialConvergenceMs splitTopologyPairParallelWallMs pass_mergeScoreScoreMs
pass_mergeScoreHeapifyMs pass_mergeScoreCandidateSnapshotMs pass_mergeScoreMembershipUpdateMs pass_mergeScoreWallMs
pass_splitScoreScoreMs pass_splitScoreHeapifyMs pass_splitScoreCandidateSnapshotMs pass_splitScoreMembershipUpdateMs
pass_splitScoreWallMs pass_mergeTopologyScoreMs pass_mergeTopologyHeapifyMs pass_mergeTopologyCandidateSnapshotMs
pass_mergeTopologyMembershipUpdateMs pass_mergeTopologyWallMs pass_splitTopologyScoreMs pass_splitTopologyHeapifyMs
pass_splitTopologyCandidateSnapshotMs pass_splitTopologyMembershipUpdateMs pass_splitTopologyWallMs pass_meshEmitScoreMs
pass_meshEmitHeapifyMs pass_meshEmitCandidateSnapshotMs pass_meshEmitMembershipUpdateMs pass_meshEmitWallMs
pass_cpuUploadScoreMs pass_cpuUploadHeapifyMs pass_cpuUploadCandidateSnapshotMs pass_cpuUploadMembershipUpdateMs
pass_cpuUploadWallMs
""".split()

PROBE_FIELDS = (
    "probeProtocolVersion diagnosticsMode scenarioId frameIndex cameraPoseHash viewInputHash "
    "probeBuildInputHash probeGeometryHash probeValidationMilliseconds buildWallMilliseconds".split()
    + BENCHMARK_FIELDS[BENCHMARK_FIELDS.index("experimentSchemaVersion"):]
)
ENVIRONMENT_FIELDS = {"cpuUtilizationPercent"}
DIAGNOSTICS = {
    "diagnostics-off": {"passEvidenceEnabled": "false", "topologyValidationEnabled": "false",
                        "topologyPairEvidenceEnabled": "false"},
    "diagnostics-on": {"passEvidenceEnabled": "true", "topologyValidationEnabled": "false",
                       "topologyPairEvidenceEnabled": "false"},
}
# 别名只作映射，不能把同一个包络重复累计
ALIASES = {
    "cpuMergeCandidateMarkMilliseconds": "pass_mergeScoreWallMs",
    "cpuSplitCandidateMarkMilliseconds": "pass_splitScoreWallMs",
    "cpuMeshEmitMilliseconds": "pass_meshEmitWallMs",
    "emitMilliseconds": "pass_meshEmitWallMs",
}


def write_json(path, value):
    Path(path).write_text(json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False) + "\n", encoding="utf-8")


def percentile(values, fraction=.95):
    ordered = sorted(values)
    if not ordered:
        raise ValueError("不能汇总空样本")
    position = (len(ordered) - 1) * fraction
    lower = math.floor(position)
    return ordered[lower] + (ordered[math.ceil(position)] - ordered[lower]) * (position - lower)


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def summarize_frames(path, mode="benchmark", identity=None):
    """逐列核对原始文件，环境值保留独立统计，不进入算法身份。"""
    actual = file_identity(Path(path))
    if identity is not None and (actual["sha256"], actual["bytes"]) != (identity["sha256"], identity["bytes"]):
        raise ValueError(f"CSV 摘要不符：{path}")
    expected = BENCHMARK_FIELDS if mode == "benchmark" else PROBE_FIELDS
    if mode != "benchmark" and mode not in DIAGNOSTICS:
        raise ValueError("未知诊断模式")
    time_fields = TIME_FIELDS + ([] if mode == "benchmark" else ["probeValidationMilliseconds"])
    values = {key: [] for key in time_fields + sorted(ENVIRONMENT_FIELDS)}
    semantic = hashlib.sha256()
    with Path(path).open(encoding="utf-8-sig", newline="") as stream:
        reader = csv.DictReader(stream, strict=True)
        if reader.fieldnames != expected:
            raise ValueError("CSV 列必须完整匹配冻结契约，不能添加、遗漏、重复或重排列")
        count = 0
        for row in reader:
            if None in row or any(value is None or value == "" for value in row.values()):
                raise ValueError("CSV 含缺失值或多余单元格")
            if any(value.lower() in {"nan", "inf", "-inf", "+inf", "infinity"} for value in row.values()):
                raise ValueError("CSV 含非有限值")
            if row["experimentSchemaVersion"] != "4" or row["frameIndex"] != str(count):
                raise ValueError("版本或连续帧序号不符")
            flags = DIAGNOSTICS["diagnostics-on" if mode == "benchmark" else mode]
            if any(row[key] != value for key, value in flags.items()):
                raise ValueError("实际诊断开关与冻结模式不符")
            if mode != "benchmark" and (row["probeProtocolVersion"] != "1" or row["diagnosticsMode"] != mode):
                raise ValueError("Probe 协议或模式不符")
            if row["passed"] not in {"1", "true"} or row["resultValidationPassed"] not in {"1", "true"}:
                raise ValueError("算法结果未通过校验")
            for key in ("budgetViolationCount", "queueInvariantViolationCount", "resourceValidationFailureCount",
                        "resultValidationFailureMask", "tjunctionCount", "invalidNeighborCount", "invalidTopologyCount"):
                if int(row[key]) != 0:
                    raise ValueError(f"算法结果失败：{key}")
            for key in values:
                value = float(row[key])
                if not math.isfinite(value) or value < 0:
                    raise ValueError(f"无效计量：{key}")
                values[key].append(value)
            semantic.update(json.dumps([row[key] for key in expected if key not in values],
                                       separators=(",", ":")).encode())
            semantic.update(b"\n")
            count += 1
    if not count or (mode != "benchmark" and count != 64):
        raise ValueError("帧数量不完整")
    return {"csv": actual, "rowCount": count, "semanticSha256": semantic.hexdigest(),
            "metrics": {f"{key}.{stat}": statistics.median(samples) if stat == "median" else percentile(samples)
                        for key, samples in values.items() if key not in ENVIRONMENT_FIELDS
                        for stat in ("median", "p95")},
            "environment": {key: {"median": statistics.median(values[key]), "p95": percentile(values[key])}
                            for key in ENVIRONMENT_FIELDS}}


def paired_statistics(records, layout):
    """相邻两个进程产生一个 B-A 差；四进程块提供两个差，不增加样本计数。"""
    blocks = defaultdict(list)
    for row in records:
        if not math.isfinite(row["value"]) or row["value"] < 0:
            raise ValueError("进程汇总值必须是非负有限数")
        blocks[row["block"]].append(row)
    differences, block_differences = [], []
    for block in sorted(blocks):
        rows = sorted(blocks[block], key=lambda row: row["slot"])
        expected = ("ABBA" if block % 2 == 0 else "BAAB") if layout == "abba" else ("AB" if block % 2 == 0 else "BA")
        if "".join(row["label"] for row in rows) != expected or [r["slot"] for r in rows] != list(range(len(expected))):
            raise ValueError("配对顺序、位置或数量不符")
        pairs = []
        for index in range(0, len(rows), 2):
            pair = {r["label"]: r for r in rows[index:index + 2]}
            pairs.append(pair["B"]["value"] - pair["A"]["value"])
        differences.extend(pairs)
        block_differences.append(statistics.mean(pairs))
    left = [row["value"] for row in records if row["label"] == "A"]
    right = [row["value"] for row in records if row["label"] == "B"]
    delta = statistics.median(right) - statistics.median(left)
    old_range = max(left) - min(left)
    return {"aValues": left, "bValues": right, "aMedian": statistics.median(left),
            "bMedian": statistics.median(right), "medianDifference": delta,
            "relativeDifference": delta / statistics.median(left) if statistics.median(left) else None,
            "aRange": old_range, "investigate": delta > old_range,
            "pairedDifferences": differences, "pairedMedianDifference": statistics.median(differences),
            "blockDifferences": block_differences,
            "positivePairs": sum(value > 0 for value in differences), "pairCount": len(differences)}


def analyze_attempt(directory):
    """失败尝试仅供审计，完整成功且来源一致的进程才进入版本统计。"""
    directory = Path(directory)
    metadata = json.loads((directory / "metadata.json").read_text(encoding="utf-8"))
    if metadata.get("protocolVersion") != 1 or metadata.get("status") != "complete":
        raise ValueError("尝试未完整成功或协议未知")
    if metadata["identitiesBefore"] != metadata["identitiesAfter"]:
        raise ValueError("执行期间源码、程序或输入身份改变")
    if metadata["environmentBefore"] != metadata["environmentAfter"] or "environmentQueryError" in metadata["environmentBefore"]:
        raise ValueError("基础环境改变或缺少实际环境观测")
    if metadata["diagnosticsProtocol"] != DIAGNOSTICS or metadata["warmupBlocks"] != 1 or metadata["measuredBlocks"] != 5:
        raise ValueError("诊断或采样协议不符")
    runs = json.loads((directory / "runs.json").read_text(encoding="utf-8"))
    groups = {group["id"]: group for group in metadata["groups"]}
    if len(groups) != len(metadata["groups"]):
        raise ValueError("重复的配置编号")
    by_group = defaultdict(list)
    paths, process_keys = set(), set()
    for row in runs:
        if row["status"] != "complete" or row["exitCode"] != 0 or row["group"] not in groups:
            raise ValueError("进程失败或配置来源缺失")
        group = groups[row["group"]]
        if row["observedAffinity"] != group["affinity"] or row["requestedAffinity"] != group["affinity"]:
            raise ValueError("实际处理器条件不符")
        if row["diagnostics"] != DIAGNOSTICS["diagnostics-on" if group["mode"] == "benchmark" else group["mode"]]:
            raise ValueError("进程诊断契约不符")
        if row["version"] != group["versions"][row["label"]]:
            raise ValueError("A/B 标签对应的版本错误")
        if row["executableSha256"] != metadata["identitiesBefore"]["versions"][row["version"]]["executable"]["sha256"]:
            raise ValueError("被测程序身份不符")
        csv_path = (directory / row["relativeDirectory"] / "frames.csv").resolve()
        if not csv_path.is_relative_to(directory.resolve()) or csv_path in paths:
            raise ValueError("CSV 重复引用或不属于当前尝试")
        key = (row["pid"], row["startedUtc"])
        if key in process_keys:
            raise ValueError("同一进程不能重复增加样本数")
        paths.add(csv_path)
        process_keys.add(key)
        summary = summarize_frames(csv_path, group["mode"], row["csv"])
        expected_frames = 24 if group["mode"] == "benchmark" and group["profile"] == "budget-saturation" else 64
        if summary["rowCount"] != row["rowCount"] or summary["rowCount"] != expected_frames:
            raise ValueError("帧数量与完成记录不符")
        summary["metrics"].update({f"process.{key}": row[key]
                                   for key in ("seconds", "cpuSeconds", "peakWorkingSetBytes")})
        by_group[row["group"]].append({**row, "summary": summary})
    result = {"attemptId": metadata["attemptId"], "cohort": metadata["cohort"], "groups": {}}
    for name, group in groups.items():
        rows = by_group[name]
        slots = 4 if group["layout"] == "abba" else 2
        expected_count = (metadata["warmupBlocks"] + metadata["measuredBlocks"]) * slots
        if len(rows) != expected_count or len({r["summary"]["semanticSha256"] for r in rows}) != 1:
            raise ValueError(f"进程数量不完整或算法语义不一致：{name}")
        if len({r["priorityClass"] for r in rows}) != 1:
            raise ValueError("同一条件下进程优先级改变")
        for block in range(metadata["warmupBlocks"] + metadata["measuredBlocks"]):
            members = [r for r in rows if r["block"] == block]
            if len(members) != slots or any(r["warmup"] != (block < metadata["warmupBlocks"]) for r in members):
                raise ValueError("预热、计量块或帧归属不符")
            paired_statistics([{**r, "value": 0.0} for r in members], group["layout"])
        measured = [row for row in rows if not row["warmup"]]
        stats = {metric: paired_statistics([{**r, "value": r["summary"]["metrics"][metric]} for r in measured], group["layout"])
                 for metric in measured[0]["summary"]["metrics"]}
        condition = {key: value for key, value in group.items() if key not in {"id", "comparison", "versions"}}
        condition["environment"] = {key: value for key, value in metadata["environmentBefore"].items() if key != "parentPid"}
        condition["observedPriorityClass"] = rows[0]["priorityClass"]
        result["groups"][name] = {
            "comparison": group["comparison"], "condition": condition,
            "semanticSha256": rows[0]["summary"]["semanticSha256"],
            "binaryIdentities": {label: metadata["identitiesBefore"]["versions"][version]
                                 for label, version in group["versions"].items()},
            "measuredProcesses": {label: sum(r["label"] == label for r in measured) for label in "AB"},
            "warmupProcesses": {label: sum(r["label"] == label and r["warmup"] for r in rows) for label in "AB"},
            "measuredBlocks": metadata["measuredBlocks"], "metrics": stats,
            "environment": [{"pid": r["pid"], "machineBusyPercent": r["machineBusyPercent"],
                             "priorityClass": r["priorityClass"], "observedAffinity": r["observedAffinity"],
                             "frameEnvironment": r["summary"]["environment"]} for r in rows],
        }
    return result


def review_micro(cohorts, metric):
    """只实现冻结的两批次微秒契约；缺少配套对照时保留未关闭。"""
    details = []
    millisecond_metrics = {f"{field}.{stat}" for field in TIME_FIELDS + ["probeValidationMilliseconds"]
                           for stat in ("median", "p95")}
    if metric not in millisecond_metrics:
        return {"closed": False, "reason": "微秒契约只适用于以毫秒记录的计时列"}
    if len(cohorts) != 2 or len({c["attemptId"] for c in cohorts}) != 2:
        return {"closed": False, "reason": "需要两个独立批次"}
    signatures = set()
    for cohort in cohorts:
        groups = cohort["groups"]
        if set(groups) != {"AB", "AA", "BB"}:
            return {"closed": False, "reason": "缺少同条件 A/A 或 B/B 对照"}
        ab, aa, bb = (groups[key] for key in ("AB", "AA", "BB"))
        if any(g["measuredBlocks"] != 5 or g["measuredProcesses"] != {"A": 10, "B": 10}
               or len(g["metrics"][metric]["pairedDifferences"]) != 10 for g in groups.values()):
            return {"closed": False, "reason": "受控进程样本不完整"}
        if len({digest(g["condition"]) for g in groups.values()}) != 1 or len({g["semanticSha256"] for g in groups.values()}) != 1:
            return {"closed": False, "reason": "输入、结果或环境条件不一致"}
        if any(g["comparison"] != key for key, g in groups.items()):
            return {"closed": False, "reason": "同程序对照标签错误"}
        if not (aa["binaryIdentities"]["A"] == aa["binaryIdentities"]["B"] == ab["binaryIdentities"]["A"]
                and bb["binaryIdentities"]["A"] == bb["binaryIdentities"]["B"] == ab["binaryIdentities"]["B"]):
            return {"closed": False, "reason": "对照程序或构建身份不一致"}
        signatures.add(digest([ab["condition"], ab["binaryIdentities"], ab["semanticSha256"]]))
        version = ab["metrics"][metric]
        controls = aa["metrics"][metric]["pairedDifferences"] + bb["metrics"][metric]["pairedDifferences"]
        bound = percentile([abs(value) for value in controls])
        delta = version["pairedMedianDifference"]
        stable_outside = (all(value > bound for value in version["pairedDifferences"]) or
                          all(value < -bound for value in version["pairedDifferences"]))
        accepted = (abs(delta) <= .01 and abs(version["medianDifference"]) <= .01
                    and bound <= .01 and abs(delta) <= bound and not stable_outside)
        details.append({"attemptId": cohort["attemptId"], "controlAbsoluteP95": bound,
                        "accepted": accepted, "version": version,
                        "aa": aa["metrics"][metric], "bb": bb["metrics"][metric]})
    closed = len(signatures) == 1 and all(row["accepted"] for row in details)
    return {"closed": closed, "reason": "在已记录条件及分辨能力下，无版本相关证据" if closed else "未满足不可分辨契约",
            "cohorts": details}


def write_report(result, output):
    output = Path(output)
    output.mkdir(parents=True, exist_ok=False)
    write_json(output / "analysis.json", result)
    lines = ["# 普通运行性能比较", "", "独立进程为主要统计单位；帧中位数与 P95 先在每个进程内汇总。"
             "A/A、B/B 为位置对照，不能解释为版本提升。完整进程差、块差及所有计时列见 analysis.json。", "",
             "本表触发项按两个进程组的中位数之差超过同期 A 极差判断；配对中位差另列。"
             "历史触发项必须另与原基线及原极差核对，不能用同期极差自动关闭。", ""]
    selected = ("buildWallMilliseconds.median", "buildWallMilliseconds.p95", "cpuUpdateMilliseconds.median",
                "passEvidenceMilliseconds.median", "pass_splitScoreHeapifyMs.median", "process.seconds")
    for name, group in result["groups"].items():
        lines += [f"## {name}（{group['comparison']}）", "",
                  f"计量进程 A/B：{group['measuredProcesses']}；预热进程：{group['warmupProcesses']}；"
                  f"计量块：{group['measuredBlocks']}；处理器掩码：{group['condition']['affinity']:#x}", "",
                  "| 指标 | A 中位数 | B 中位数 | 中位数之差 B−A | 配对中位差 B−A | A 极差 | 同期触发调查 |",
                  "| --- | ---: | ---: | ---: | ---: | ---: | --- |"]
        for metric in selected:
            row = group["metrics"][metric]
            lines.append(f"| {metric} | {row['aMedian']:.7f} | {row['bMedian']:.7f} | "
                         f"{row['medianDifference']:+.7f} | {row['pairedMedianDifference']:+.7f} | "
                         f"{row['aRange']:.7f} | {row['investigate']} |")
        lines += [""]
    (output / "report.md").write_text("\n".join(lines), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("attempt", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    write_report(analyze_attempt(args.attempt), args.output)


if __name__ == "__main__":
    main()
