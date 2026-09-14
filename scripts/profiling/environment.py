"""只读记录采样工具与运行环境；事件可用性以实际打开结果为准。"""

import argparse
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import time


def command(arguments, timeout=15, env=None):
    """保留失败与超时，避免把缺失工具或不支持事件写成零成本。"""
    started = time.monotonic()
    result = {"argv": [str(a) for a in arguments]}
    try:
        process = subprocess.run(result["argv"], capture_output=True, text=True,
                                 timeout=timeout, env=env)
        result.update(returncode=process.returncode, stdout=process.stdout,
                      stderr=process.stderr, status="ok" if process.returncode == 0 else "failed")
    except subprocess.TimeoutExpired as error:
        def decode(value):
            return value.decode(errors="replace") if isinstance(value, bytes) else (value or "")
        result.update(returncode=None, stdout=decode(error.stdout), stderr=decode(error.stderr), status="timeout")
    except OSError as error:
        result.update(returncode=None, stdout="", stderr=str(error), status="unavailable")
    result["elapsed_seconds"] = time.monotonic() - started
    return result


def read_optional(path):
    try:
        return Path(path).read_text().strip()
    except OSError:
        return None


def inspect_environment(perf="perf", exercise=False):
    """静态能力与实际试验分开；不尝试修改权限或安装软件。"""
    sources = Path("/sys/bus/event_source/devices")
    result = {
        "platform": platform.platform(), "python": sys.version,
        "kernel": platform.release(), "cpu_count": os.cpu_count(),
        "uptime": read_optional("/proc/uptime"),
        "perf_event_paranoid": read_optional("/proc/sys/kernel/perf_event_paranoid"),
        "kptr_restrict": read_optional("/proc/sys/kernel/kptr_restrict"),
        "event_sources": sorted(p.name for p in sources.iterdir()) if sources.exists() else [],
        "tracing_readable": os.access("/sys/kernel/tracing", os.R_OK),
        "perf_path": shutil.which(perf), "perf_version": command([perf, "--version"]),
    }
    if exercise and result["perf_version"]["status"] == "ok":
        result["perf_build_options"] = command([perf, "version", "--build-options"])
        # 每种事件独立试开，硬件失败不会遮蔽软件事件的实际结果
        workload = [sys.executable, "-c", "sum(range(1000000))"]
        result["event_trials"] = {
            event: command([perf, "stat", "-x", ";", "-e", event, "--", *workload])
            for event in ("cpu-clock:u", "task-clock:u", "cycles:u", "instructions:u")
        }
    return result


def main():
    parser = argparse.ArgumentParser(description="记录 profiler 环境与可选事件试开结果")
    parser.add_argument("--perf", default="perf")
    parser.add_argument("--exercise", action="store_true")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    # 排他创建保护已有能力证据，禁止覆盖失败或旧版本记录
    with args.output.open("x", encoding="utf-8") as stream:
        json.dump(inspect_environment(args.perf, args.exercise), stream, ensure_ascii=False, indent=2)


if __name__ == "__main__":
    main()
