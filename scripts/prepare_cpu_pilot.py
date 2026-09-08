"""准备一次 CPU pilot 输入并记录计时外证据，不执行发现或策略配对。"""

import argparse
import datetime
from pathlib import Path
import shutil
import subprocess
import sys

from cpu_pilot_support import (ROOT, basic_environment, file_identity, git, read_rows,
                               selected_assets, source_identity, write_metadata)


def prepare(arguments):
    output = arguments.output_dir.resolve()
    # exist_ok=False 连空目录也拒绝；失败后只保留本次新建目录，不清理历史尝试
    output.mkdir(parents=True, exist_ok=False)
    metadata_path = output / "run-metadata.json"
    metadata = {
        "schemaVersion": 1, "dataPurpose": "exploratory", "status": "preparing",
        "startedUtc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "preparationArguments": sys.argv, "outputDirectory": str(output),
        "targetStatus": "not_provided" if arguments.target_manifest is None else "provided_unchecked",
        "scope": "cpu_pilot_inputs_only",
    }
    write_metadata(metadata_path, metadata)
    try:
        executable = arguments.executable.resolve(strict=True)
        build_dir = (ROOT / "build" / arguments.build_preset).resolve(strict=True)
        if not executable.is_relative_to(build_dir):
            raise ValueError("可执行文件不在指定构建预设目录中")
        cache = build_dir / "CMakeCache.txt"
        metadata["gitCommit"] = git("rev-parse", "HEAD")
        metadata["gitStatus"] = git("status", "--porcelain=v1", "--untracked-files=all")
        metadata["buildPreset"] = arguments.build_preset
        metadata["environment"] = basic_environment()
        metadata["source"] = source_identity()
        binaries = [executable, *sorted(executable.parent.glob("*.dll")), cache]
        binaries.extend(sorted(build_dir.glob("CMakeFiles/*/CMakeCXXCompiler.cmake")))
        metadata["buildFiles"] = [file_identity(path) for path in binaries]
        metadata["originalManifests"] = {}
        manifests = output / "manifests"
        manifests.mkdir()
        frozen = {}
        for kind, path in (("scenario", arguments.scenario_manifest), ("camera", arguments.camera_manifest),
                           ("target", arguments.target_manifest)):
            if path is None:
                continue
            identity = file_identity(path)
            metadata["originalManifests"][kind] = identity
            target = manifests / f"{kind}.csv"
            shutil.copyfile(identity["path"], target)
            if file_identity(target)["sha256"] != identity["sha256"]:
                raise ValueError("复制期间输入清单发生变化")
            frozen[kind] = target
        rows, assets = selected_assets(frozen["scenario"], arguments.scenario_id)
        metadata["assets"] = assets
        metadata["selectedScenarioIds"] = [row["scenarioId"] for row in rows]
        command = [str(executable), "--benchmark", "--profile", "cpu-pilot-inputs",
                   "--scenario-manifest", str(frozen["scenario"]), "--output-dir", str(output / "inputs")]
        for scenario in arguments.scenario_id:
            command.extend(["--scenario-id", scenario])
        for kind in ("camera", "target"):
            if kind in frozen:
                command.extend([f"--{kind}-manifest", str(frozen[kind])])
        metadata["command"] = command
        write_metadata(metadata_path, metadata)
        with (output / "prepare.log").open("wb") as log:
            completed = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=False)
        metadata["exitCode"] = completed.returncode
        if completed.returncode:
            raise RuntimeError(f"输入校验进程失败，退出码 {completed.returncode}，见 prepare.log")
        summary_rows = read_rows(output / "inputs/input-summary.csv")
        if len(summary_rows) != 1 or summary_rows[0]["status"] != "inputs_validated":
            raise ValueError("输入摘要缺失或未完成")
        summary = summary_rows[0]
        scenarios = read_rows(output / "inputs/scenarios.csv")
        cameras = read_rows(output / "inputs/camera-samples.csv")
        expected = {(row["scenarioId"], str(index)) for row in scenarios for index in range(64)}
        actual = [(row["scenarioId"], row["sampleIndex"]) for row in cameras]
        if len(scenarios) != len(rows) or len(actual) != len(expected) or set(actual) != expected or \
                int(summary["expectedCameraCount"]) != len(expected) or int(summary["cameraCount"]) != len(actual):
            raise ValueError("输入记录存在缺行、重复或错误场景")
        if any(row["dataPurpose"] != "exploratory" for row in [summary, *scenarios, *cameras]):
            raise ValueError("CPU pilot 输出用途错误")
        target_path = output / "inputs/target-states.csv"
        target_count = len(read_rows(target_path)) if "target" in frozen else 0
        expected_target_status = "references_validated" if "target" in frozen else "not_provided"
        if int(summary["targetCount"]) != target_count or summary["targetStatus"] != expected_target_status:
            raise ValueError("目标完整性摘要不一致")
        # 子进程结束后重新核对输入，常规编辑或资产替换不能沿用本次身份
        for identity in [*assets.values(), *metadata["buildFiles"], *metadata["originalManifests"].values()]:
            if file_identity(identity["path"]) != identity:
                raise ValueError(f"准备期间文件变化：{identity['path']}")
        if source_identity() != metadata["source"] or git("rev-parse", "HEAD") != metadata["gitCommit"]:
            raise ValueError("准备期间源码或 Git HEAD 变化")
        metadata["validatedInputSummary"] = summary
        metadata["scenarios"] = scenarios
        metadata["targetStatus"] = expected_target_status
        metadata["outputs"] = [file_identity(path) for directory in (manifests, output / "inputs")
                               for path in sorted(directory.iterdir()) if path.is_file()]
        metadata["status"] = "inputs_ready"
        metadata["finishedUtc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        write_metadata(metadata_path, metadata)
        return 0
    except Exception as error:
        metadata["status"] = "failed"
        metadata["error"] = str(error)
        metadata["finishedUtc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        write_metadata(metadata_path, metadata)
        print(f"CPU pilot 输入准备失败：{error}", file=sys.stderr)
        return 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--build-preset", required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--scenario-manifest", type=Path,
                        default=ROOT / "docs/parallel-roam/cpu-pilot-scenarios-v1.csv")
    parser.add_argument("--scenario-id", action="append", default=[])
    parser.add_argument("--camera-manifest", type=Path)
    parser.add_argument("--target-manifest", type=Path)
    try:
        return prepare(parser.parse_args())
    except (OSError, ValueError) as error:
        print(f"无法创建 CPU pilot 尝试：{error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
