"""在新目录执行一次 CPU pilot 配对，核验来源与实际进程后才声明完成。"""

import argparse
import datetime
import json
from pathlib import Path
import subprocess
import sys
import time
import uuid

from cpu_pilot_artifacts import load_discovered_inputs
from cpu_pilot_support import ROOT, basic_environment, file_identity, git, source_identity, write_metadata
from formal_experiments.input_validation import PAIR_FILES, select_frozen_targets, validate_pairing_outputs


def utc_now():
    return datetime.datetime.now(datetime.timezone.utc).isoformat()


def archive_records(records, output):
    """先核对全部目标路径，再归档 C++ 独占目录；已有文件不能被覆盖。"""
    if not records.is_dir():
        return
    sources = list(records.iterdir())
    if any(not source.is_file() or (output / source.name).exists() for source in sources):
        raise ValueError("配对产物归档发生文件名冲突")
    for source in sources:
        source.rename(output / source.name)
    records.rmdir()


def withdraw_outputs(output):
    for name in PAIR_FILES:
        source = output / name
        target = output / ("rejected-" + name)
        if source.exists():
            if target.exists():
                raise ValueError(f"失败证据名称冲突：{target}")
            source.rename(target)


def run_pairing(arguments):
    output = arguments.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=False)
    metadata_path = output / "run-metadata.json"
    records = output / "records"
    metadata = {"schemaVersion": 1, "dataPurpose": "exploratory", "measurementProtocolVersion": 1,
                "status": "preparing", "scope": "cpu_pass_pair_pilot", "attemptId": uuid.uuid4().hex,
                "startedUtc": utc_now(), "outputDirectory": str(output), "pairingArguments": sys.argv}
    write_metadata(metadata_path, metadata)
    process = None
    try:
        prepared = arguments.prepared_input_dir.resolve(strict=True)
        discovered = arguments.discovery_dir.resolve(strict=True)
        scenarios, cameras, targets, inputs = load_discovered_inputs(discovered, prepared)
        selection = {"scenarioIds": arguments.scenario_id, "passId": arguments.pass_id,
                     "sampleIndex": arguments.sample_index, "warmupCount": arguments.pass_warmups,
                     "measuredRepeatCount": arguments.pass_repeats, "parallelWorkerCount": arguments.pass_workers}
        selected_scenes, selected = select_frozen_targets(scenarios, targets, selection)
        executable = arguments.executable.resolve(strict=True)
        build_dir = (ROOT / "build" / arguments.build_preset).resolve(strict=True)
        if not executable.is_relative_to(build_dir):
            raise ValueError("可执行文件不在指定构建预设目录中")
        binaries = [executable, *sorted(executable.parent.rglob("*.dll")), build_dir / "CMakeCache.txt"]
        binaries.extend(sorted(build_dir.glob("CMakeFiles/*/CMakeCXXCompiler.cmake")))
        metadata.update({"gitCommit": git("rev-parse", "HEAD"),
                         "gitStatus": git("status", "--porcelain=v1", "--untracked-files=all"),
                         "buildPreset": arguments.build_preset, "environment": basic_environment(),
                         "source": source_identity(), "buildFiles": [file_identity(path) for path in binaries],
                         "preparedInputDirectory": str(prepared), "discoveryDirectory": str(discovered),
                         "frozenInputsAndAssets": inputs, "selection": selection,
                         "selectedScenarioIds": [row["scenarioId"] for row in selected_scenes],
                         "selectedTargetCount": len(selected)})
        command = [str(executable), "--benchmark", "--profile", "cpu-pass-pair-pilot",
                   "--scenario-manifest", str(prepared / "inputs/scenarios.csv"),
                   "--camera-manifest", str(prepared / "inputs/camera-samples.csv"),
                   "--target-manifest", str(discovered / "target-states.csv"), "--output-dir", str(records)]
        for scenario_id in arguments.scenario_id:
            command.extend(("--scenario-id", scenario_id))
        command.extend(("--pass-id", arguments.pass_id))
        for flag, value in (("--sample-index", arguments.sample_index), ("--pass-warmups", arguments.pass_warmups),
                            ("--pass-repeats", arguments.pass_repeats), ("--pass-workers", arguments.pass_workers)):
            if value is not None:
                command.extend((flag, str(value)))
        metadata.update(command=command, status="pairing", processStartedUtc=utc_now())
        write_metadata(metadata_path, metadata)
        start = time.perf_counter()
        with (output / "pair.log").open("wb") as log:
            process = subprocess.Popen(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
            metadata["processPid"] = process.pid
            write_metadata(metadata_path, metadata)
            metadata["exitCode"] = process.wait()
        metadata.update(processFinishedUtc=utc_now(), processWallSeconds=time.perf_counter() - start)
        archive_records(records, output)
        if metadata["exitCode"]:
            raise RuntimeError(f"配对进程失败，退出码 {metadata['exitCode']}，见 pair.log")
        validated = validate_pairing_outputs(output, scenarios, targets, selection)
        metadata["runId"] = validated["runId"]
        metadata["validatedPairSummary"] = validated["summary"]
        metadata["calibration"] = validated["calibration"]
        metadata["buildFilesAfter"] = [file_identity(item["path"]) for item in metadata["buildFiles"]]
        metadata["frozenInputsAndAssetsAfter"] = [file_identity(item["path"]) for item in inputs]
        metadata["sourceAfter"] = source_identity()
        metadata["environmentAfter"] = basic_environment()
        if metadata["buildFiles"] != metadata["buildFilesAfter"] or inputs != metadata["frozenInputsAndAssetsAfter"] or \
                metadata["source"] != metadata["sourceAfter"] or git("rev-parse", "HEAD") != metadata["gitCommit"]:
            raise ValueError("配对期间源码、构建、输入或资产发生变化")
        metadata["outputs"] = [file_identity(path) for path in sorted(output.iterdir())
                               if path.is_file() and path != metadata_path]
        metadata.update(status="pairing_complete", finishedUtc=utc_now())
        write_metadata(metadata_path, metadata)
        print(f"CPU 配对尝试完成：{metadata['runId']}，{len(selected)} 个目标；仍需独立分析与环境漂移检查")
        return 0
    except (Exception, KeyboardInterrupt) as error:
        if process is not None and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        try:
            archive_records(records, output)
            withdraw_outputs(output)
        except (OSError, ValueError) as archive_error:
            error = RuntimeError(f"{error}；失败证据归档异常：{archive_error}")
        interrupted = isinstance(error, KeyboardInterrupt)
        metadata.update(status="interrupted" if interrupted else "failed", error=str(error) or "用户中断",
                        finishedUtc=utc_now())
        if process is not None:
            metadata["exitCode"] = process.returncode
        metadata["outputs"] = [file_identity(path) for path in sorted(output.iterdir())
                               if path.is_file() and path != metadata_path]
        write_metadata(metadata_path, metadata)
        print(f"CPU 配对尝试未完成：{metadata['error']}", file=sys.stderr)
        return 130 if interrupted else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prepared-input-dir", type=Path, required=True)
    parser.add_argument("--discovery-dir", type=Path, required=True)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--build-preset", required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--scenario-id", action="append", default=[])
    parser.add_argument("--pass-id", choices=("all", "mergeScore", "mergeTopology", "splitScore", "splitTopology", "meshEmit"), default="all")
    parser.add_argument("--sample-index", type=int)
    parser.add_argument("--pass-warmups", type=int)
    parser.add_argument("--pass-repeats", type=int)
    parser.add_argument("--pass-workers", type=int)
    try:
        return run_pairing(parser.parse_args())
    except (OSError, ValueError) as error:
        print(f"无法创建 CPU 配对尝试：{error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())

