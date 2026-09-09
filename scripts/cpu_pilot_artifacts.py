"""校验 CPU pilot 冻结输入与发现产物，供发现、配对和离线审计共用。"""

import json
from pathlib import Path

from cpu_pilot_support import file_identity, read_rows, selected_assets


PASSES = ("mergeScore", "mergeTopology", "splitScore", "splitTopology", "meshEmit")


def load_prepared_inputs(directory):
    """只校验冻结输入与资产，不要求旧准备程序等同于本次发现程序。"""
    directory = directory.resolve(strict=True)
    metadata_path = directory / "run-metadata.json"
    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    if metadata.get("status") != "inputs_ready" or metadata.get("dataPurpose") != "exploratory":
        raise ValueError("来源输入准备未完成")
    old_root = Path(metadata["outputDirectory"])
    outputs = {Path(item["path"]).relative_to(old_root).as_posix(): item for item in metadata["outputs"]}
    verified = [file_identity(metadata_path)]
    for relative in ("inputs/scenarios.csv", "inputs/camera-samples.csv", "inputs/input-summary.csv"):
        expected = outputs.get(relative)
        actual = file_identity(directory / relative)
        if expected is None or any(actual[key] != expected[key] for key in ("sha256", "bytes")):
            raise ValueError(f"冻结输入被修改或缺少摘要：{relative}")
        verified.append(actual)
    scenarios, assets = selected_assets(directory / "inputs/scenarios.csv", [])
    summary = read_rows(directory / "inputs/input-summary.csv")
    cameras = read_rows(directory / "inputs/camera-samples.csv")
    expected = {(row["scenarioId"], str(index)) for row in scenarios for index in range(64)}
    actual = [(row["scenarioId"], row["sampleIndex"]) for row in cameras]
    if len(summary) != 1 or summary[0]["status"] != "inputs_validated" or len(actual) != len(expected) or \
            set(actual) != expected or set(metadata["selectedScenarioIds"]) != {row["scenarioId"] for row in scenarios}:
        raise ValueError("冻结输入集合不完整")
    verified.extend(assets.values())
    return scenarios, cameras, verified


def verify_discovery_outputs(output, scenarios, cameras, targets_per_pass):
    """从落盘行核对唯一身份、完成状态和目标引用，文件存在本身不算完成。"""
    summary_rows = read_rows(output / "discovery-summary.csv")
    if len(summary_rows) != 1 or summary_rows[0]["status"] != "discovery_complete":
        raise ValueError("发现摘要缺失或未完成")
    summary = summary_rows[0]
    rows = read_rows(output / "discovery.csv")
    expected = {(scene["scenarioId"], str(index), stage) for scene in scenarios
                for index in range(64) for stage in PASSES}
    identities = [(row["scenarioId"], row["sampleIndex"], row["passId"]) for row in rows]
    if len(identities) != len(expected) or set(identities) != expected:
        raise ValueError("发现记录存在缺行、重复或额外身份")
    camera_by_id = {(row["scenarioId"], row["sampleIndex"]): row for row in cameras}
    for row in rows:
        camera = camera_by_id[(row["scenarioId"], row["sampleIndex"])]
        if row["schemaVersion"] != "2" or row["dataPurpose"] != "exploratory" or \
                row["status"] not in ("valid", "no_work") or row["validationPerformed"] != "true" or \
                row["validationPassed"] != "true" or row["passInputVersion"] != "1" or \
                int(row["replayInputHash"]) == 0 or int(row["selectionFeatureHash"]) == 0 or \
                any(row[field] != camera[field] for field in ("cameraPoseHash", "viewInputHash")):
            raise ValueError("发现行缺少有效阶段或相机证据")
    by_id = dict(zip(identities, rows))
    target_path = output / "target-states.csv"
    targets = read_rows(target_path) if target_path.exists() else []
    target_ids = [(row["scenarioId"], row["sampleIndex"], row["passId"]) for row in targets]
    if len(set(target_ids)) != len(target_ids):
        raise ValueError("目标重复")
    groups = {(scene["scenarioId"], stage): [] for scene in scenarios for stage in PASSES}
    for target, identity in zip(targets, target_ids):
        record = by_id.get(identity)
        if record is None or record["selectionEligible"] != "true" or target["sampleIndex"] == "0" or \
                target["schemaVersion"] != "1" or target["dataPurpose"] != "exploratory" or \
                target["selectionSeed"] != "20260830" or target["analysisSplit"] != "train" or \
                any(target[field] != record[field] for field in
                    ("replayInputHash", "selectionFeatureHash", "cameraPoseHash", "viewInputHash", "featureVector")) or \
                float(target["primaryWorkValue"]) != float(record["primaryWorkValue"]):
            raise ValueError("目标未绑定同一有效发现输入")
        groups[(identity[0], identity[2])].append(target)
    coverage = read_rows(output / "target-coverage.csv")
    coverage_ids = [(row["scenarioId"], row["passId"]) for row in coverage]
    if len(coverage_ids) != len(groups) or set(coverage_ids) != set(groups):
        raise ValueError("覆盖报告缺少组或存在重复")
    insufficient = 0
    for row in coverage:
        key = (row["scenarioId"], row["passId"])
        selected = groups[key]
        eligible_count = sum(record["selectionEligible"] == "true" for record in rows
                             if (record["scenarioId"], record["passId"]) == key)
        if row["selectorVersion"] != "1" or row["selectionSeed"] != "20260830" or \
                int(row["requestedCount"]) != targets_per_pass or int(row["selectedCount"]) != len(selected) or \
                int(row["eligibleCount"]) != eligible_count or len(selected) != min(eligible_count, targets_per_pass) or \
                int(row["missingCount"]) != targets_per_pass - len(selected) or \
                sorted(int(target["selectionRank"]) for target in selected) != list(range(len(selected))):
            raise ValueError("覆盖报告或连续目标序号不一致")
        for stratum in ("low", "middle", "high", "coverage"):
            if int(row[stratum + "Count"]) != sum(target["selectionStratum"] == stratum for target in selected):
                raise ValueError("覆盖分层数量不一致")
        insufficient += len(selected) < targets_per_pass
    expected_summary = {"scenarioCount": len(scenarios), "completedScenarioCount": len(scenarios),
                        "expectedRecordCount": len(expected), "recordCount": len(rows), "targetCount": len(targets),
                        "targetsPerPass": targets_per_pass, "insufficientGroupCount": insufficient,
                        "selectorVersion": 1, "passInputVersion": 1, "selectionSeed": 20260830,
                        "validRecordCount": sum(row["status"] == "valid" for row in rows),
                        "noWorkRecordCount": sum(row["status"] == "no_work" for row in rows),
                        "failedRecordCount": 0}
    if any(int(summary[field]) != value for field, value in expected_summary.items()) or \
            summary["targetStatus"] != ("references_validated" if targets else "targets_unavailable") or \
            (not targets and target_path.exists()):
        raise ValueError("发现完整性摘要不一致")
    return summary



def verify_output_identities(directory, metadata, required):
    """按原尝试内的相对路径验证摘要，目录搬移不能改变文件内容。"""
    directory = Path(directory).resolve(strict=True)
    original = Path(metadata["outputDirectory"])
    expected = {}
    for item in metadata["outputs"]:
        relative = Path(item["path"]).relative_to(original)
        if ".." in relative.parts or relative.as_posix() in expected:
            raise ValueError("产物路径重复或越过尝试目录")
        expected[relative.as_posix()] = item
    verified = []
    for name in required:
        actual = file_identity(directory / name)
        saved = expected.get(name)
        if saved is None or any(actual[key] != saved[key] for key in ("sha256", "bytes")):
            raise ValueError(f"产物被修改或缺少摘要：{name}")
        verified.append(actual)
    return verified


def load_discovered_inputs(directory, prepared_directory):
    """要求发现已完成且绑定同一套准备输入，旧程序身份不必等于本次程序。"""
    directory = Path(directory).resolve(strict=True)
    scenarios, cameras, prepared_files = load_prepared_inputs(Path(prepared_directory))
    metadata_path = directory / "run-metadata.json"
    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    if metadata.get("schemaVersion") != 1 or metadata.get("status") != "discovery_complete" or \
            metadata.get("dataPurpose") != "exploratory":
        raise ValueError("来源工作量发现未完成")
    if metadata.get("selectorVersion") != 1 or metadata.get("passInputVersion") != 1 or \
            metadata.get("selectionSeed") != 20260830 or metadata.get("targetsPerPass") not in (4, 8):
        raise ValueError("来源发现协议不受支持")
    # 绑定文件内容而非旧目录绝对地址，允许将完整冻结目录搬到其他位置
    saved_inputs = sorted((Path(item["path"]).name, item["bytes"], item["sha256"])
                          for item in metadata["frozenInputsAndAssets"])
    current_inputs = sorted((Path(item["path"]).name, item["bytes"], item["sha256"])
                            for item in prepared_files)
    if saved_inputs != current_inputs:
        raise ValueError("发现没有绑定当前冻结准备输入")
    files = verify_output_identities(directory, metadata,
                                     ("discovery.csv", "discovery-summary.csv", "target-coverage.csv", "discover.log"))
    summary = verify_discovery_outputs(directory, scenarios, cameras, metadata["targetsPerPass"])
    if summary != metadata.get("validatedDiscoverySummary"):
        raise ValueError("发现完成元数据与落盘摘要不一致")
    targets = []
    if summary["targetStatus"] == "references_validated":
        files.extend(verify_output_identities(directory, metadata, ("target-states.csv",)))
        targets = read_rows(directory / "target-states.csv")
    expected_scenes = {row["scenarioId"] for row in scenarios}
    if set(metadata["selectedScenarioIds"]) != expected_scenes:
        raise ValueError("发现完成场景集合不一致")
    return scenarios, cameras, targets, [*prepared_files, file_identity(metadata_path), *files]

