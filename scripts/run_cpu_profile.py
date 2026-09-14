"""在冻结自然轨迹上采集函数热点，所有采集数据保留到独立目录。"""

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

from cpu_pilot_support import file_identity
from profiling import perf_backend
from profiling.environment import inspect_environment
from profiling.report import read_windows, summarize_perf, markdown_perf

ROOT = Path(__file__).resolve().parents[1]


def source_manifest():
    # 只散列项目源码与构建输入，避免遍历缓存、下载目录和大体积原始实验
    tracked = subprocess.check_output(["git", "-C", str(ROOT), "ls-files", "--cached", "--others",
                                       "--exclude-standard", "-z", "src", "tests", "cmake", "scripts",
                                       "CMakeLists.txt", "CMakePresets.json"], text=True)
    return {name: file_identity(ROOT / name) for name in sorted(set(tracked.split("\0")))
            if name and (ROOT / name).is_file()}


def main():
    parser = argparse.ArgumentParser(description="CPU 自然轨迹函数采样")
    parser.add_argument("backend", choices=("perf",))
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--snapshot", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--mode", default="trajectory-c-timing", choices=("trajectory-a-timing", "trajectory-b-timing", "trajectory-c-timing"))
    parser.add_argument("--replays", type=int, choices=range(1, 33), default=1)
    parser.add_argument("--perf", default="perf")
    parser.add_argument("--callgraph", choices=("fp", "dwarf,16384"), default="fp")
    parser.add_argument("--native-work-root", type=Path, default=Path.home() / ".cache/roam-profiling/runs")
    args = parser.parse_args()
    args.executable = args.executable.resolve(strict=True)
    args.snapshot = args.snapshot.resolve(strict=True)
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=False)
    args.native_work_root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="natural-perf-", dir=args.native_work_root))
    result = {"executable": file_identity(args.executable), "snapshot": file_identity(args.snapshot),
              "source_commit": subprocess.check_output(["git", "-C", str(ROOT), "rev-parse", "HEAD"], text=True).strip(),
              "environment": inspect_environment(args.perf), "native_work": str(work),
              "mode": args.mode, "replays": args.replays, "complete": False}
    (work / "source-files.json").write_text(json.dumps(source_manifest(), ensure_ascii=False, indent=2), encoding="utf-8")
    # 保存实际带符号程序，后续重编译不能改变旧采样对应的符号身份
    shutil.copy2(args.executable, work / "recorded-executable")
    cache = args.executable.parent.parent / "CMakeCache.txt"
    commands = args.executable.parent.parent / "compile_commands.json"
    for path in (cache, commands):
        if path.exists():
            shutil.copy2(path, work / path.name)
    try:
        arguments = [str(args.snapshot), args.mode, str(work / "program"), "--profile", str(args.replays)]
        result["record"] = perf_backend.record(args.perf, args.executable, arguments, work, ROOT)
        if not result["record"]["complete"]:
            raise RuntimeError("perf 或被测进程未完整结束")
        result["exports"] = perf_backend.export(args.perf, work)
        if any(value["status"] != "ok" for value in result["exports"].values()):
            raise RuntimeError("官方 perf 导出失败")
        windows = read_windows((work / "program/profile-windows.csv").read_text(), expected=8 * args.replays)
        summary = summarize_perf((work / "perf-script.txt").read_text(), windows)
        report_text = (work / "perf-report.txt").read_text()
        import re
        lost = re.search(r"Total Lost Samples:\s*(\d+)", report_text)
        summary["lost_samples"] = int(lost[1]) if lost else None
        summary["stack_mode"] = "machine functions; inline expansion disabled for unambiguous symbol aggregation"
        (work / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")
        (work / "report.md").write_text(markdown_perf(summary), encoding="utf-8")
        result.update(complete=True, roi_samples=summary["roi_samples"])
    except (OSError, ValueError, RuntimeError) as error:
        result["error"] = str(error)
    finally:
        # 控制端点没有数据内容；保留 argv 和日志，归档只复制普通产物
        shutil.copytree(work, args.output / "artifacts", ignore=shutil.ignore_patterns("*.fifo"))
        result["files"] = {str(p.relative_to(work)): file_identity(p) for p in work.iterdir() if p.is_file()}
        (args.output / "manifest.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({k: result[k] for k in ("complete", "roi_samples", "error") if k in result}, ensure_ascii=False))
    return 0 if result["complete"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
