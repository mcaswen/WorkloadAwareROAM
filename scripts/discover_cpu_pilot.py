"""沿已冻结 CPU pilot 输入发现工作量并选择目标，不执行策略配对。"""

import argparse
import datetime
import json
from pathlib import Path
import subprocess
import sys

from cpu_pilot_support import (ROOT, basic_environment, file_identity, git, read_rows,
                               selected_assets, source_identity, write_metadata)


from cpu_pilot_artifacts import PASSES, load_prepared_inputs, verify_discovery_outputs


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
