"""准备一次 CPU pilot 输入并记录计时外证据，不执行发现或策略配对。"""

import argparse
import csv
import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]


def file_identity(path):
    path = Path(path).resolve(strict=True)
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return {"path": str(path), "bytes": path.stat().st_size, "sha256": digest.hexdigest()}


def git(*arguments):
    return subprocess.check_output(["git", "-C", str(ROOT), *arguments]).decode("utf-8").strip()


def source_identity():
    # 包括尚未提交的新源码，不能只用 HEAD 冒充正在运行的工作区身份
    names = git("ls-files", "--cached", "--others", "--exclude-standard", "-z").split("\0")
    roots = {"src", "tests", "scripts", "cmake", "third_party", "tools"}
    files = {}
    for name in sorted(set(names)):
        if not name:
            continue
        relative = Path(name)
        if relative.parts[0] not in roots and name not in {"CMakeLists.txt", "CMakePresets.json", "vcpkg.json"}:
            continue
        path = ROOT / relative
        files[name] = file_identity(path) if path.is_file() else {"missing": True}
    encoded = json.dumps(files, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return {"sha256": hashlib.sha256(encoded).hexdigest(), "files": files}


def read_rows(path):
    with Path(path).open(encoding="utf-8-sig", newline="") as stream:
        reader = csv.DictReader(stream, strict=True)
        if not reader.fieldnames or len(set(reader.fieldnames)) != len(reader.fieldnames):
            raise ValueError(f"缺失或重复表头：{path}")
        rows = list(reader)
        if any(None in row or any(value is None for value in row.values()) for row in rows):
            raise ValueError(f"CSV 列数不完整：{path}")
        return rows


def selected_assets(scenario_path, selected):
    rows = read_rows(scenario_path)
    if selected:
        rows = [row for row in rows if row["scenarioId"] in selected]
        if {row["scenarioId"] for row in rows} != set(selected):
            raise ValueError("请求的场景不存在")
    if not rows:
        raise ValueError("场景清单为空")
    assets = {}
    for row in rows:
        path = (ROOT / row["heightMapPath"]).resolve(strict=True)
        identity = file_identity(path)
        if identity["sha256"] != row["heightMapSha256"]:
            raise ValueError(f"高度图 SHA-256 不匹配：{path}")
        assets[str(path)] = identity
    return rows, assets


def basic_environment():
    result = {
        "platform": platform.platform(), "machine": platform.machine(),
        "processor": platform.processor(), "logicalProcessorCount": os.cpu_count(),
        "python": sys.version, "cwd": str(ROOT), "parentPid": os.getpid(),
    }
    if hasattr(os, "sched_getaffinity"):
        result["affinity"] = sorted(os.sched_getaffinity(0))
    if os.name == "nt":
        command = (
            "[Console]::OutputEncoding = [Text.UTF8Encoding]::new(); "
            "$cpu = Get-CimInstance Win32_Processor; $osInfo = Get-CimInstance Win32_OperatingSystem; "
            "@{cpu=@($cpu | Select-Object Name,NumberOfCores,NumberOfLogicalProcessors); "
            "memoryKiB=$osInfo.TotalVisibleMemorySize; processPriority=(Get-Process -Id " + str(os.getpid()) +
            ").PriorityClass.ToString(); affinity=(Get-Process -Id " + str(os.getpid()) +
            ").ProcessorAffinity.ToInt64(); powerPlan=(powercfg /getactivescheme | Out-String).Trim()} | ConvertTo-Json -Depth 4"
        )
        try:
            raw = subprocess.check_output(["powershell", "-NoProfile", "-NonInteractive", "-Command", command], timeout=30)
            result["windows"] = json.loads(raw.decode("utf-8-sig"))
        except (OSError, subprocess.SubprocessError, ValueError) as error:
            result["environmentQueryError"] = str(error)
    return result


def write_metadata(path, metadata):
    with path.open("w", encoding="utf-8", newline="\n") as stream:
        json.dump(metadata, stream, ensure_ascii=False, indent=2)
        stream.write("\n")


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
