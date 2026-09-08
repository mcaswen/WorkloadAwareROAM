"""共享 CPU pilot 文件、源码与环境证据读取能力。"""

import csv
import hashlib
import json
import os
from pathlib import Path
import platform
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
