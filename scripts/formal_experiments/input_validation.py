"""完整核对 CPU 配对尝试，损坏数据不能通过挑选部分块进入统计。"""

import csv
from datetime import datetime
import hashlib
import json
import math
from pathlib import Path
import re

from cpu_pilot_artifacts import PASSES, load_discovered_inputs, verify_output_identities
from cpu_pilot_support import file_identity, read_rows

PAIR_COLUMNS = ["schemaVersion","dataPurpose","measurementMode","measurementProtocolVersion","runId","scenarioId","terrainId","analysisSplit","sampleIndex","passId","selectionRank","selectionStratum","selectionSeed","selectorVersion","passInputVersion","cameraPoseHash","viewInputHash","replayInputHash","selectionFeatureHash","primaryWorkValue","featureVector","manifestWarmupCount","manifestRepeatCount","manifestParallelWorkerCount","warmupCount","measuredRepeatCount","parallelWorkerCount","absoluteBlockIndex","repeatIndex","isWarmup","blockOrder","orderIndex","requestedAction","actualAction","requestedWorkerCount","actualWorkerCount","fallbackReason","executionPath","fallbackDetail","resultHash","validationPerformed","diagnosticsDisabledAtExecution","correct","equivalent","status","failure","wallMs","stateCloneMs","inputCheckMs","validationMs","workerPreparationMs","featureCollectionMs","pre_mergeQueueEntryCount","pre_splitQueueEntryCount","pre_activeTriangleCount","pre_triangleBudget","pre_remainingTriangleBudget","pre_topologyEditCount","pre_maxActiveDepth","pre_maxDepth","planning_interiorCandidateCount","planning_boundaryCandidateCount","planning_scheduledCandidateCount","planning_nonEmptyChunkCount","planning_dirtyTriangleCount","planning_dirtyRangeCount","planning_meshReason","post_earlyCommitCount","post_activeTriangleCount","post_dirtyTriangleCount","post_dirtyRangeCount","scoreMs","heapifyMs","candidateSnapshotMs","chunkBuildMs","queueInvalidationMs","commitMs","resultMergeMs","indexQueueRefreshMs","serialConvergenceMs"]
TARGET_COLUMNS = ("schemaVersion,dataPurpose,scenarioId,passId,sampleIndex,selectionStratum,"
                  "selectionRank,selectionSeed,primaryWorkValue,featureVector,replayInputHash,"
                  "selectionFeatureHash,analysisSplit,cameraPoseHash,viewInputHash").split(",")
ACTIONS = {
    "mergeScore": ("serialFullRefresh", "parallelFullRefresh"),
    "splitScore": ("serialFullRefresh", "parallelFullRefresh"),
    "mergeTopology": ("serialImmediate", "parallelAssisted"),
    "splitTopology": ("serialImmediate", "parallelAssisted"),
    "meshEmit": ("serialDirty", "parallelDirty", "serialFull"),
}
MESH_ORDERS = ("ABC", "ACB", "BAC", "BCA", "CAB", "CBA")
PAIR_FILES = ("selected-targets.csv", "pair-samples.csv", "warmup-samples.csv", "target-summary.csv",
              "pair-summary.csv", "timing-calibration.csv")
SUMMARY_COLUMNS = ("schemaVersion,dataPurpose,measurementProtocolVersion,runId,status,backend,buildConfiguration,"
                   "scenarioCount,targetCount,completedTargetCount,unavailableGroupCount,warmupRowCount,measuredRowCount,"
                   "calibrationComplete,timingEnvironmentValid,totalMs,failure").split(",")
TARGET_SUMMARY_COLUMNS = ("schemaVersion,dataPurpose,runId,scenarioId,passId,sampleIndex,selectionRank,selectionStratum,"
                          "replayInputHash,selectionFeatureHash,status,warmupCount,measuredRepeatCount,parallelWorkerCount,"
                          "expectedWarmupRowCount,expectedMeasuredRowCount,warmupRowCount,measuredRowCount,"
                          "sourceValidationPerformed,sourceValidationPassed,workerPreparationMs,rebuildMs,failure").split(",")
CALIBRATION_COLUMNS = ("schemaVersion,dataPurpose,measurementProtocolVersion,runId,position,kind,scenarioId,passId,"
                       "sampleIndex,index,wallMs,p50Ms,p95Ms,p99Ms,replayInputHash,resultHash,correct,validationPerformed,"
                       "diagnosticsDisabledAtExecution,clockResolutionNs").split(",")
BOOL_FIELDS = {"isWarmup", "validationPerformed", "diagnosticsDisabledAtExecution", "correct", "equivalent"}
FLOAT_FIELDS = {field for field in PAIR_COLUMNS if field.endswith("Ms")} | {"primaryWorkValue"}
INT_FIELDS = {
    "schemaVersion", "measurementProtocolVersion", "sampleIndex", "selectionRank", "selectionSeed",
    "selectorVersion", "passInputVersion", "cameraPoseHash", "viewInputHash", "replayInputHash", "selectionFeatureHash",
    "manifestWarmupCount", "manifestRepeatCount", "manifestParallelWorkerCount", "warmupCount",
    "measuredRepeatCount", "parallelWorkerCount", "absoluteBlockIndex", "repeatIndex", "orderIndex",
    "requestedWorkerCount", "actualWorkerCount", "resultHash",
} | {field for field in PAIR_COLUMNS if field.startswith(("pre_", "planning_", "post_")) and field != "planning_meshReason"}
COMMON_FIELDS = PAIR_COLUMNS[4:PAIR_COLUMNS.index("absoluteBlockIndex")] + [
    name for name in PAIR_COLUMNS if name.startswith(("pre_", "planning_"))] + ["featureCollectionMs"]


def unsigned(value, name, maximum=(1 << 64) - 1):
    if not isinstance(value, str) or re.fullmatch(r"[0-9]+", value) is None:
        raise ValueError(f"{name} 必须是非负整数")
    result = int(value)
    if result > maximum:
        raise ValueError(f"{name} 超出可序列化范围")
    return result


def finite(value, name):
    result = float(value)
    if not math.isfinite(result) or result < 0:
        raise ValueError(f"{name} 必须是有限非负数")
    return result


def boolean(value, name):
    if value not in ("true", "false"):
        raise ValueError(f"{name} 必须是显式布尔值")
    return value == "true"


def rows_with_header(path, columns):
    with Path(path).open(encoding="utf-8-sig", newline="") as stream:
        reader = csv.DictReader(stream, strict=True)
        if reader.fieldnames != list(columns):
            raise ValueError(f"CSV 表头与版本契约不一致：{path}")
        rows = list(reader)
    if any(None in row or None in row.values() for row in rows):
        raise ValueError(f"CSV 行宽不一致：{path}")
    return rows


def target_key(row):
    return row["scenarioId"], row["passId"], int(row["sampleIndex"])


def select_frozen_targets(scenarios, targets, selection):
    requested = selection.get("scenarioIds", [])
    if len(requested) != len(set(requested)):
        raise ValueError("场景筛选重复")
    known = {scene["scenarioId"] for scene in scenarios}
    if not set(requested).issubset(known):
        raise ValueError("未知场景筛选")
    selected_ids = set(requested) if requested else known
    stage = selection.get("passId", "all")
    sample = selection.get("sampleIndex")
    if stage not in (*PASSES, "all"):
        raise ValueError("未知 CPU 阶段")
    if sample is not None and (type(sample) is not int or not 1 <= sample < 64 or len(requested) != 1 or stage == "all"):
        raise ValueError("单采样必须同时选择唯一场景和具体阶段，且采样号为 1..63")
    for field in ("warmupCount", "measuredRepeatCount", "parallelWorkerCount"):
        value = selection.get(field)
        if value is not None and (type(value) is not int or not (0 if field == "warmupCount" else 1) <= value < (1 << 32)):
            raise ValueError(f"非法测量数量：{field}")
    warmups = selection.get("warmupCount")
    repeats = selection.get("measuredRepeatCount")
    if (5 if warmups is None else warmups) + (30 if repeats is None else repeats) >= (1 << 32):
        raise ValueError("绝对块号溢出")
    selected_scenes = [scene for scene in scenarios if scene["scenarioId"] in selected_ids]
    selected = [target for target in targets if target["scenarioId"] in selected_ids and
                (stage == "all" or target["passId"] == stage) and
                (sample is None or int(target["sampleIndex"]) == sample)]
    return selected_scenes, selected


def configuration(scene, selection):
    names = (("warmupCount", "passWarmupCount", 5), ("measuredRepeatCount", "passMeasuredRepeatCount", 30),
             ("parallelWorkerCount", "parallelWorkerCount", 8))
    result = {}
    for actual, manifest, expected in names:
        value = unsigned(scene[manifest], manifest, (1 << 32) - 1)
        if value != expected:
            raise ValueError("冻结场景的测量协议默认值改变")
        result[actual] = value if selection.get(actual) is None else selection[actual]
    result.update(manifestWarmupCount=5, manifestRepeatCount=30, manifestParallelWorkerCount=8)
    return result


def validate_calibration(path, run_id, first_scene):
    rows = rows_with_header(path, CALIBRATION_COLUMNS)
    expected = {(position, kind, index) for position in ("before", "after")
                for kind in ("empty", "no_work") for index in range(1000)}
    seen = set()
    batches = {}
    for row in rows:
        identity = (row["position"], row["kind"], unsigned(row["index"], "标定序号"))
        if identity not in expected or identity in seen:
            raise ValueError("标定存在缺失、重复或额外身份")
        seen.add(identity)
        if row["schemaVersion"] != "1" or row["dataPurpose"] != "exploratory" or row["measurementProtocolVersion"] != "1" or \
                row["runId"] != run_id or row["scenarioId"] != first_scene or row["sampleIndex"] != "0" or row["correct"] != "true":
            raise ValueError("标定运行、根输入或协议不一致")
        for field in ("wallMs", "p50Ms", "p95Ms", "p99Ms", "clockResolutionNs"):
            row[field] = finite(row[field], field)
        if row["clockResolutionNs"] <= 0:
            raise ValueError("缺少时钟名义分辨率")
        if row["kind"] == "no_work":
            if row["passId"] != "mergeScore" or row["validationPerformed"] != "true" or \
                    row["diagnosticsDisabledAtExecution"] != "true" or \
                    unsigned(row["replayInputHash"], "标定输入") == 0 or unsigned(row["resultHash"], "标定结果") == 0:
                raise ValueError("无工作标定缺少真实执行证据")
        elif row["passId"] != "not_applicable" or row["replayInputHash"] != "0" or row["resultHash"] != "0":
            raise ValueError("空时钟标定冒充阶段执行")
        batches.setdefault(identity[:2], []).append(row)
    if seen != expected:
        raise ValueError("前后标定批次不完整")
    summary = {}
    for identity, batch in batches.items():
        ordered = sorted(row["wallMs"] for row in batch)
        quantiles = {name: ordered[math.ceil(1000 * q) - 1] for name, q in (("p50Ms", .50), ("p95Ms", .95), ("p99Ms", .99))}
        if any(any(row[field] != value for field, value in quantiles.items()) for row in batch):
            raise ValueError("标定分位数没有对应原始值")
        if identity[1] == "no_work" and len({(row["replayInputHash"], row["resultHash"]) for row in batch}) != 1:
            raise ValueError("无工作标定的根输入或结果发生漂移")
        summary["/".join(identity)] = {**quantiles, "count": 1000, "clockResolutionNs": batch[0]["clockResolutionNs"]}
    if {(row["replayInputHash"], row["resultHash"]) for row in batches[("before", "no_work")]} != \
            {(row["replayInputHash"], row["resultHash"]) for row in batches[("after", "no_work")]}:
        raise ValueError("前后无工作标定不是相同根输入")
    return {"batches": summary, "noiseMs": max(item["p99Ms"] for item in summary.values()),
            "environmentValid": all(summary[position + "/empty"]["p99Ms"] <= .01 for position in ("before", "after"))}


def validate_pairing_outputs(output, scenarios, targets, selection):
    """供运行脚本与离线分析共用；先审计整次尝试，再允许统计读取。"""
    output = Path(output)
    selected_scenes, expected_targets = select_frozen_targets(scenarios, targets, selection)
    if not expected_targets:
        raise ValueError("请求没有任何冻结目标")
    selected = rows_with_header(output / "selected-targets.csv", TARGET_COLUMNS)
    expected_by_id = {target_key(row): row for row in expected_targets}
    if len(selected) != len(expected_by_id) or {target_key(row) for row in selected} != set(expected_by_id) or \
            any(row != expected_by_id[target_key(row)] for row in selected):
        raise ValueError("执行目标列表改变了冻结目标身份或数量")
    summary_rows = rows_with_header(output / "pair-summary.csv", SUMMARY_COLUMNS)
    if len(summary_rows) != 1:
        raise ValueError("配对摘要必须恰好一行")
    summary = summary_rows[0]
    run_id = summary["runId"]
    if not run_id or summary["status"] != "pairing_internal_complete" or summary["dataPurpose"] != "exploratory" or \
            summary["schemaVersion"] != "1" or summary["measurementProtocolVersion"] != "1" or summary["failure"] or \
            summary["backend"] not in ("OpenGL", "D3D12") or summary["buildConfiguration"] != "RelWithDebInfo":
        raise ValueError("配对内部状态、后端或协议未完成")
    finite(summary["totalMs"], "整次尝试时间")
    scene_map = {scene["scenarioId"]: scene for scene in selected_scenes}
    groups = {key: [] for key in expected_by_id}
    file_counts = {}
    positions = set()
    for is_warmup, filename in ((False, "pair-samples.csv"), (True, "warmup-samples.csv")):
        rows = rows_with_header(output / filename, PAIR_COLUMNS)
        file_counts[filename] = len(rows)
        for row in rows:
            for field in INT_FIELDS:
                row[field] = unsigned(row[field], field)
            for field in FLOAT_FIELDS:
                row[field] = finite(row[field], field)
            for field in BOOL_FIELDS:
                row[field] = boolean(row[field], field)
            key = target_key(row)
            target = expected_by_id.get(key)
            if target is None:
                raise ValueError("原始行包含额外目标")
            scene = scene_map[key[0]]
            config = configuration(scene, selection)
            if row["schemaVersion"] != 2 or row["dataPurpose"] != "exploratory" or row["measurementMode"] != "pilot_measurement" or \
                    row["measurementProtocolVersion"] != 1 or row["runId"] != run_id or row["selectorVersion"] != 1 or \
                    row["passInputVersion"] != 1 or row["terrainId"] != scene["terrainId"] or row["analysisSplit"] != "train" or \
                    any(row[field] != value for field, value in config.items()):
                raise ValueError("策略行协议、运行或配置不一致")
            for field in ("selectionRank", "selectionSeed", "cameraPoseHash", "viewInputHash", "replayInputHash", "selectionFeatureHash"):
                if row[field] != unsigned(target[field], field):
                    raise ValueError(f"策略行改变冻结身份：{field}")
            if row["selectionStratum"] != target["selectionStratum"] or row["featureVector"] != target["featureVector"] or \
                    row["primaryWorkValue"] != float(target["primaryWorkValue"]):
                raise ValueError("策略行改变冻结选择特征")
            actions = ACTIONS[key[1]]
            block = row["absoluteBlockIndex"]
            warmups, repeats = config["warmupCount"], config["measuredRepeatCount"]
            order = MESH_ORDERS[block % 6] if len(actions) == 3 else ("AB" if block % 2 == 0 else "BA")
            if block >= warmups + repeats or row["isWarmup"] != is_warmup or is_warmup != (block < warmups) or \
                    row["repeatIndex"] != (block if is_warmup else block - warmups) or row["blockOrder"] != order or \
                    row["orderIndex"] >= len(actions) or row["requestedAction"] != actions[ord(order[row["orderIndex"]]) - ord("A")]:
                raise ValueError("策略行顺序或预热计数不一致")
            identity = (*key, block, row["orderIndex"])
            if identity in positions:
                raise ValueError("重复的块内策略身份")
            positions.add(identity)
            requested = row["requestedAction"]
            parallel = requested == actions[1]
            workers = config["parallelWorkerCount"] if parallel else 1
            actual = row["actualWorkerCount"]
            fallback = row["fallbackReason"]
            if row["requestedWorkerCount"] != workers or actual > workers or row["actualAction"] not in actions or \
                    fallback not in ("none", "noWork", "parallelDisabled", "belowParallelThreshold", "resourceCapacity") or \
                    (actual > 1 and (row["executionPath"] != "thread_pool" or row["actualAction"] != requested or fallback != "none")) or \
                    (actual == 1 and row["executionPath"] != "caller_thread") or \
                    (actual == 0 and (row["executionPath"] != "no_work" or fallback != "noWork")) or \
                    (fallback == "none" and (row["fallbackDetail"] or row["actualAction"] != requested or (parallel and actual < 2))) or \
                    (fallback != "none" and not row["fallbackDetail"]):
                raise ValueError("线程请求、实际路径或回退证据冲突")
            if row["status"] not in ("valid", "no_work") or row["failure"] or row["resultHash"] == 0 or \
                    not all(row[field] for field in ("validationPerformed", "diagnosticsDisabledAtExecution", "correct", "equivalent")) or \
                    (row["status"] == "no_work") != (fallback == "noWork"):
                raise ValueError("策略行没有完成有效结果检查")
            groups[key].append(row)
    for key, rows in groups.items():
        config = configuration(scene_map[key[0]], selection)
        expected_count = (config["warmupCount"] + config["measuredRepeatCount"]) * len(ACTIONS[key[1]])
        if len(rows) != expected_count:
            raise ValueError("目标缺少完整的预热或计时块")
        reference = rows[0]
        if any(row["resultHash"] != reference["resultHash"] or
               any(row[field] != reference[field] for field in COMMON_FIELDS) for row in rows):
            raise ValueError("同一目标的共同输入或规范化结果冲突")
    target_summary = rows_with_header(output / "target-summary.csv", TARGET_SUMMARY_COLUMNS)
    stages = PASSES if selection.get("passId", "all") == "all" else (selection["passId"],)
    unavailable = {(scene["scenarioId"], stage, 0) for scene in selected_scenes for stage in stages
                   if not any(key[:2] == (scene["scenarioId"], stage) for key in groups)}
    summary_keys = [target_key(row) for row in target_summary]
    if len(summary_keys) != len(set(summary_keys)) or set(summary_keys) != set(groups) | unavailable:
        raise ValueError("目标摘要集合不完整或重复")
    for row in target_summary:
        key = target_key(row)
        config = configuration(scene_map[key[0]], selection)
        if row["schemaVersion"] != "1" or row["dataPurpose"] != "exploratory" or row["runId"] != run_id or \
                any(unsigned(row[field], field) != config[field] for field in ("warmupCount", "measuredRepeatCount", "parallelWorkerCount")):
            raise ValueError("目标摘要运行或配置冲突")
        if key in unavailable:
            if row["status"] != "targets_unavailable" or not row["failure"] or any(unsigned(row[field], field) for field in
                    ("warmupRowCount", "measuredRowCount", "expectedWarmupRowCount", "expectedMeasuredRowCount")):
                raise ValueError("空组摘要冒充有效目标")
            continue
        target = expected_by_id[key]
        if row["status"] != "valid" or row["sourceValidationPerformed"] != "true" or row["sourceValidationPassed"] != "true" or row["failure"] or \
                any(row[field] != target[field] for field in ("selectionRank", "selectionStratum", "replayInputHash", "selectionFeatureHash")):
            raise ValueError("目标摘要没有通过来源帧或身份核验")
        actions = len(ACTIONS[key[1]])
        for field, expected in (("warmupRowCount", config["warmupCount"] * actions),
                                ("expectedWarmupRowCount", config["warmupCount"] * actions),
                                ("measuredRowCount", config["measuredRepeatCount"] * actions),
                                ("expectedMeasuredRowCount", config["measuredRepeatCount"] * actions)):
            if unsigned(row[field], field) != expected:
                raise ValueError("目标摘要策略行数冲突")
        finite(row["workerPreparationMs"], "线程准备时间")
        finite(row["rebuildMs"], "来源重建时间")
    expected_summary = {"scenarioCount": len(selected_scenes), "targetCount": len(groups), "completedTargetCount": len(groups),
                        "unavailableGroupCount": len(unavailable), "warmupRowCount": file_counts["warmup-samples.csv"],
                        "measuredRowCount": file_counts["pair-samples.csv"]}
    if any(unsigned(summary[field], field) != value for field, value in expected_summary.items()):
        raise ValueError("配对总摘要数量与原始行不一致")
    calibration = validate_calibration(output / "timing-calibration.csv", run_id, selected_scenes[0]["scenarioId"])
    if summary["calibrationComplete"] != "true" or boolean(summary["timingEnvironmentValid"], "环境标记") != calibration["environmentValid"]:
        raise ValueError("标定摘要与原始校准不一致")
    return {"runId": run_id, "groups": groups, "targets": selected, "targetSummaries": target_summary,
            "summary": summary, "calibration": calibration, "selection": selection, "directory": str(output.resolve())}


def validate_execution_metadata(metadata):
    """历史文件无需仍在原位置，但保存的来源必须完整、自洽，不能以两边同时缺失冒充一致。"""
    def identity(item):
        if not isinstance(item, dict) or not isinstance(item.get("path"), str) or not item["path"] or \
                type(item.get("bytes")) is not int or item["bytes"] < 0 or \
                re.fullmatch(r"[0-9a-f]{64}", str(item.get("sha256", ""))) is None:
            raise ValueError("缺少有效的文件来源身份")

    source = metadata.get("source")
    if not isinstance(source, dict) or not isinstance(source.get("files"), dict) or not source["files"]:
        raise ValueError("完成尝试缺少源码文件身份")
    for name, item in source["files"].items():
        if not isinstance(name, str) or not name:
            raise ValueError("源码文件名无效")
        if item != {"missing": True}:
            identity(item)
    encoded = json.dumps(source["files"], ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")
    if source.get("sha256") != hashlib.sha256(encoded).hexdigest():
        raise ValueError("源码总摘要与保存的文件身份不一致")
    for field in ("buildFiles", "frozenInputsAndAssets"):
        items = metadata.get(field)
        if not isinstance(items, list) or not items:
            raise ValueError(f"完成尝试缺少 {field}")
        for item in items:
            identity(item)
    paths = [item["path"] for item in metadata["buildFiles"]]
    command = metadata.get("command")
    if len(paths) != len(set(paths)) or not any(Path(path).name == "CMakeCache.txt" for path in paths) or \
            not isinstance(command, list) or not command or not all(isinstance(value, str) for value in command) or command[0] not in paths:
        raise ValueError("实际命令与构建身份不完整或冲突")
    times = []
    for field in ("startedUtc", "processStartedUtc", "processFinishedUtc", "finishedUtc"):
        value = metadata.get(field)
        if not isinstance(value, str):
            raise ValueError(f"缺少进程时间：{field}")
        stamp = datetime.fromisoformat(value)
        if stamp.tzinfo is None:
            raise ValueError("进程时间缺少时区")
        times.append(stamp)
    if times != sorted(times):
        raise ValueError("尝试与进程起止顺序不一致")
    if type(metadata.get("processWallSeconds")) not in (int, float):
        raise ValueError("缺少实际进程墙钟")
    finite(metadata["processWallSeconds"], "进程墙钟")
    for field in ("environment", "environmentAfter"):
        if not isinstance(metadata.get(field), dict) or not metadata[field]:
            raise ValueError("完成尝试缺少前后环境记录")


def load_pair_attempt(directory):
    directory = Path(directory).resolve(strict=True)
    metadata_path = directory / "run-metadata.json"
    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    if metadata.get("schemaVersion") != 1 or metadata.get("dataPurpose") != "exploratory":
        raise ValueError("不支持的配对元数据")
    if metadata.get("status") != "pairing_complete":
        return {"directory": str(directory), "failed": True, "failure": metadata.get("error", "尝试尚未完成"),
                "runId": metadata.get("runId"), "metadata": metadata}
    validate_execution_metadata(metadata)
    if metadata.get("measurementProtocolVersion") != 1 or metadata.get("exitCode") != 0 or \
            type(metadata.get("processPid")) is not int or metadata["processPid"] <= 0 or \
            metadata.get("source") != metadata.get("sourceAfter") or \
            metadata.get("buildFiles") != metadata.get("buildFilesAfter") or \
            metadata.get("frozenInputsAndAssets") != metadata.get("frozenInputsAndAssetsAfter"):
        raise ValueError("完成元数据缺少一致的实际执行身份")
    verified = verify_output_identities(directory, metadata, (*PAIR_FILES, "pair.log"))
    scenes, cameras, targets, inputs = load_discovered_inputs(
        Path(metadata["discoveryDirectory"]), Path(metadata["preparedInputDirectory"]))
    # 历史源码不要求等于当前工作区，但冻结输入必须仍对应这次执行保存的摘要
    saved = sorted((Path(item["path"]).name, item["bytes"], item["sha256"]) for item in metadata["frozenInputsAndAssets"])
    current = sorted((Path(item["path"]).name, item["bytes"], item["sha256"]) for item in inputs)
    if saved != current:
        raise ValueError("分析所用冻结来源与执行来源不同")
    result = validate_pairing_outputs(directory, scenes, targets, metadata["selection"])
    if result["runId"] != metadata.get("runId") or result["summary"] != metadata.get("validatedPairSummary"):
        raise ValueError("实际运行身份或完成摘要被替换")
    selected_scenes, selected_targets = select_frozen_targets(scenes, targets, metadata["selection"])
    if metadata.get("selectedScenarioIds") != [scene["scenarioId"] for scene in selected_scenes] or \
            metadata.get("selectedTargetCount") != len(selected_targets):
        raise ValueError("执行元数据的选择集合不一致")
    result.update(metadata=metadata, files=[file_identity(metadata_path), *verified], failed=False)
    return result
