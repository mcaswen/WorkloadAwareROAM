"""用已知解析负载核验工具产物，保留失败数据；不执行自然算法实验。"""

import argparse
from collections import Counter
import csv
import hashlib
import io
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import tempfile
import time

from profiling.environment import command, inspect_environment


def identity(path):
    path = Path(path).resolve(strict=True)
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return {"path": str(path), "bytes": path.stat().st_size, "sha256": digest.hexdigest()}


def perf_stack_coverage(text):
    """缺栈样本也进入分母，避免只按能显示的主线程误报全覆盖。"""
    threads = {}
    for block in text.strip().split("\n\n"):
        lines = block.splitlines()
        match = re.search(r"\b(\d+)/(\d+)\b", lines[0]) if lines else None
        if not match:
            continue
        tid = match[2]
        entry = threads.setdefault(tid, {"samples": 0, "leaf": 0, "parent": 0})
        entry["samples"] += 1
        entry["leaf"] += "ProfileHotLeaf" in block
        entry["parent"] += "ProfileHotParent" in block
    return threads


def tracy_fixture_coverage(text):
    reader = csv.DictReader(io.StringIO(text))
    required = {"name", "thread", "ns_since_start", "exec_time_ns"}
    if not required.issubset(reader.fieldnames or []):
        raise ValueError("Tracy 导出缺少展开区间字段")
    rows = list(reader)
    if any(None in row or any(row.get(key) is None for key in required) for row in rows):
        raise ValueError("Tracy 导出包含不完整行")
    counts = Counter(row["name"] for row in rows)
    expected = {"fixture.frame": 1, "fixture.hot_leaf": 3, "fixture.hot_parent": 3,
                "fixture.task": 2, "fixture.wait": 1, "fixture.exception": 1, "fixture.complete": 1}
    durations_valid = all(int(row["exec_time_ns"]) >= 0 for row in rows)
    business_threads = {row["thread"] for row in rows if row["name"] == "fixture.hot_leaf"}
    complete = [int(row["ns_since_start"]) for row in rows if row["name"] == "fixture.complete"]
    frame_end = [int(row["ns_since_start"]) + int(row["exec_time_ns"])
                 for row in rows if row["name"] == "fixture.frame"]
    passed = (all(counts[name] == count for name, count in expected.items()) and
              len(business_threads) == 3 and durations_valid and
              len(complete) == 1 and len(frame_end) == 1 and complete[0] >= frame_end[0])
    return {"passed": passed, "zone_counts": dict(counts), "threads": sorted(business_threads),
            "durations_valid": durations_valid}


def stop_process(process):
    """只结束本次启动的进程组，确保异常退出不遗留采集器。"""
    if process.poll() is not None:
        return
    os.killpg(process.pid, signal.SIGTERM)
    try:
        process.wait(timeout=3)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        process.wait(timeout=3)


def capture_tracy(capture, executable, arguments, work, timeout=35):
    trace = work / "capture.tracy"
    processes = []
    started = time.monotonic()
    # 两个进程的输出独立保存，采集器不与目标争用终端或管道缓冲
    with (work / "capture.log").open("w") as collector_log, (work / "target.log").open("w") as target_log:
        try:
            collector = subprocess.Popen([str(capture), "-a", "127.0.0.1", "-o", str(trace)],
                                         stdout=collector_log, stderr=subprocess.STDOUT, start_new_session=True)
            processes.append(collector)
            env = dict(os.environ, ROAM_PROFILE_WAIT="1", TRACY_NO_EXIT="1")
            target = subprocess.Popen([str(executable), *arguments], env=env, stdout=target_log,
                                      stderr=subprocess.STDOUT, start_new_session=True)
            processes.append(target)
            target.wait(timeout=timeout)
            collector.wait(timeout=10)
            return {"target_returncode": target.returncode, "capture_returncode": collector.returncode,
                    "elapsed_seconds": time.monotonic() - started, "complete_exit": True}
        except (OSError, subprocess.TimeoutExpired) as error:
            return {"complete_exit": False, "error": str(error),
                    "elapsed_seconds": time.monotonic() - started}
        finally:
            for process in reversed(processes):
                stop_process(process)


def check_perf(args, work):
    record = command([args.perf, "record", "-e", "cpu-clock:u", "-F", "499",
                      "--call-graph", args.callgraph, "-o", str(work / "perf.data"),
                      "--", str(args.executable), str(args.iterations)], timeout=30)
    result = {"record": record, "passed": False}
    if record["status"] != "ok":
        return result
    exported = command([args.perf, "script", "-i", str(work / "perf.data"),
                        "-F", "comm,pid,tid,time,event,ip,sym,dso"], timeout=30)
    (work / "perf-script.txt").write_text(exported["stdout"])
    report = command([args.perf, "report", "--stdio", "--no-children", "-i", str(work / "perf.data")], timeout=30)
    (work / "perf-report.txt").write_text(report["stdout"])
    threads = perf_stack_coverage(exported["stdout"])
    # 已知三个线程都在相同热点计算，任一线程只有事件没有栈都不能通过
    result.update(threads=threads, script_returncode=exported["returncode"],
                  report_returncode=report["returncode"],
                  passed=exported["status"] == "ok" and report["status"] == "ok" and
                  len(threads) == 3 and all(t["leaf"] > 0 and t["parent"] > 0 for t in threads.values()))
    return result


def main():
    parser = argparse.ArgumentParser(description="CPU profiler 解析能力验证")
    parser.add_argument("backend", choices=("perf", "tracy"))
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--native-work-root", type=Path, default=Path.home() / ".cache/roam-profiling/runs")
    parser.add_argument("--perf", default="perf")
    parser.add_argument("--callgraph", default="dwarf,16384", choices=("fp", "dwarf,16384"))
    parser.add_argument("--capture", type=Path)
    parser.add_argument("--csvexport", type=Path)
    parser.add_argument("--iterations", type=int, default=100000000)
    args = parser.parse_args()
    args.executable = args.executable.resolve(strict=True)
    if args.backend == "tracy" and (not args.capture or not args.csvexport):
        parser.error("Tracy 需要 --capture 与 --csvexport")
    args.output.mkdir(parents=True, exist_ok=False)
    args.native_work_root.mkdir(parents=True, exist_ok=True)
    # perf 在 Windows 挂载盘直接写入曾失败，原始采集在 Linux 文件系统进行
    work = Path(tempfile.mkdtemp(prefix="capability-", dir=args.native_work_root))
    result = {"backend": args.backend, "executable": identity(args.executable), "native_work": str(work)}
    try:
        if args.backend == "perf":
            result["environment"] = inspect_environment(args.perf)
            result.update(check_perf(args, work))
        else:
            result["tools"] = {name: identity(path) for name, path in
                               (("capture", args.capture), ("csvexport", args.csvexport))}
            result.update(capture_tracy(args.capture, args.executable, [str(args.iterations)], work))
            if result.get("complete_exit") and (work / "capture.tracy").exists():
                exported = command([str(args.csvexport), "-u", str(work / "capture.tracy")], timeout=30)
                (work / "zones.csv").write_text(exported["stdout"])
                result["export"] = {k: v for k, v in exported.items() if k != "stdout"}
                result.update(tracy_fixture_coverage(exported["stdout"]))
                result["passed"] &= result["target_returncode"] == 0 and result["capture_returncode"] == 0 and exported["status"] == "ok"
            else:
                result["passed"] = False
    except (OSError, ValueError) as error:
        result.update(passed=False, error=str(error))
    finally:
        # 原生目录保留；副本归档失败也不能把该次能力验证标为成功
        shutil.copytree(work, args.output / "artifacts")
        result["artifacts"] = [identity(p) for p in work.iterdir() if p.is_file()]
        (args.output / "result.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({"passed": result["passed"], "output": str(args.output)}, ensure_ascii=False))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
