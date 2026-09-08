"""沿已冻结 CPU pilot 输入发现工作量并选择目标，不执行策略配对。"""

import argparse
import datetime
import json
from pathlib import Path
import subprocess
import sys

from cpu_pilot_support import (ROOT, basic_environment, file_identity, git, read_rows,
                               selected_assets, source_identity, write_metadata)


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


def discover(arguments):
    output = arguments.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=False)
    metadata_path = output / "run-metadata.json"
    metadata = {"schemaVersion": 1, "dataPurpose": "exploratory", "status": "preparing",
                "startedUtc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
                "discoveryArguments": sys.argv, "outputDirectory": str(output),
                "scope": "cpu_workload_discovery_only", "selectorVersion": 1, "passInputVersion": 1,
                "selectionSeed": 20260830, "targetsPerPass": arguments.targets_per_pass}
    write_metadata(metadata_path, metadata)
    try:
        prepared = arguments.prepared_input_dir.resolve(strict=True)
        scenarios, cameras, inputs = load_prepared_inputs(prepared)
        executable = arguments.executable.resolve(strict=True)
        build_dir = (ROOT / "build" / arguments.build_preset).resolve(strict=True)
        if not executable.is_relative_to(build_dir):
            raise ValueError("可执行文件不在指定构建预设目录中")
        binaries = [executable, *sorted(executable.parent.glob("*.dll")), build_dir / "CMakeCache.txt"]
        binaries.extend(sorted(build_dir.glob("CMakeFiles/*/CMakeCXXCompiler.cmake")))
        metadata.update({"gitCommit": git("rev-parse", "HEAD"),
                         "gitStatus": git("status", "--porcelain=v1", "--untracked-files=all"),
                         "buildPreset": arguments.build_preset, "environment": basic_environment(),
                         "source": source_identity(), "buildFiles": [file_identity(path) for path in binaries],
                         "preparedInputDirectory": str(prepared), "frozenInputsAndAssets": inputs,
                         "selectedScenarioIds": [row["scenarioId"] for row in scenarios]})
        records = output / "records"
        command = [str(executable), "--benchmark", "--profile", "cpu-workload-discovery",
                   "--scenario-manifest", str(prepared / "inputs/scenarios.csv"),
                   "--camera-manifest", str(prepared / "inputs/camera-samples.csv"),
                   "--output-dir", str(records), "--targets-per-pass", str(arguments.targets_per_pass)]
        metadata.update({"command": command, "status": "discovering"})
        write_metadata(metadata_path, metadata)
        with (output / "discover.log").open("wb") as log:
            completed = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=False)
        # 子进程独占自己的新目录后将本次产物归入同一尝试
        if records.is_dir():
            for path in records.iterdir():
                path.rename(output / path.name)
            records.rmdir()
        metadata["exitCode"] = completed.returncode
        if completed.returncode:
            raise RuntimeError(f"发现进程失败，退出码 {completed.returncode}，见 discover.log")
        summary = verify_discovery_outputs(output, scenarios, cameras, arguments.targets_per_pass)
        # 运行后验证输入和执行来源没有变化且记录本次环境快照
        for identity in [*inputs, *metadata["buildFiles"]]:
            if file_identity(identity["path"]) != identity:
                raise ValueError(f"发现期间文件变化：{identity['path']}")
        metadata["sourceAfter"] = source_identity()
        metadata["environmentAfter"] = basic_environment()
        if metadata["sourceAfter"] != metadata["source"] or git("rev-parse", "HEAD") != metadata["gitCommit"]:
            raise ValueError("发现期间源码或 Git HEAD 变化")
        metadata["validatedDiscoverySummary"] = summary
        metadata["targetStatus"] = summary["targetStatus"]
        metadata["outputs"] = [file_identity(path) for path in sorted(output.iterdir())
                               if path.is_file() and path != metadata_path]
        metadata["status"] = "discovery_complete"
        metadata["finishedUtc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        write_metadata(metadata_path, metadata)
        return 0
    except Exception as error:
        # 失败时保留目标证据但撤下标准入口名称以防被误用于配对
        target_path = output / "target-states.csv"
        if target_path.exists():
            try:
                target_path.rename(output / "rejected-target-states.csv")
            except OSError as withdrawal_error:
                error = RuntimeError(f"{error}；目标撤下失败：{withdrawal_error}")
        metadata.update({"status": "failed", "error": str(error),
                         "finishedUtc": datetime.datetime.now(datetime.timezone.utc).isoformat()})
        write_metadata(metadata_path, metadata)
        print(f"CPU 工作负载发现失败：{error}", file=sys.stderr)
        return 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prepared-input-dir", type=Path, required=True)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--build-preset", required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--targets-per-pass", type=int, choices=(4, 8), default=4)
    try:
        return discover(parser.parse_args())
    except (OSError, ValueError) as error:
        print(f"无法创建 CPU 发现尝试：{error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
