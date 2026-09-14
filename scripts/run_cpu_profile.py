"""在冻结自然轨迹上采集函数热点，所有采集数据保留到独立目录。"""

import argparse
import csv
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

from cpu_pilot_support import file_identity
from profiling import perf_backend, tracy_backend
from profiling.environment import inspect_environment
from profiling.report import read_windows, summarize_perf, markdown_perf
from profiling.tracy_report import summarize_tracy, markdown_tracy

ROOT = Path(__file__).resolve().parents[1]


def source_manifest():
    # 只散列项目源码与构建输入，避免遍历缓存、下载目录和大体积原始实验
    tracked = subprocess.check_output(["git", "-C", str(ROOT), "ls-files", "--cached", "--others",
                                       "--exclude-standard", "-z", "src", "tests", "cmake", "scripts",
                                       "CMakeLists.txt", "CMakePresets.json"], text=True)
    return {name: file_identity(ROOT / name) for name in sorted(set(tracked.split("\0")))
            if name and (ROOT / name).is_file()}


def archive_inputs(snapshot, work, mode):
    """只归档探针实际读取的冻结依赖，不扫描整个资产或历史实验目录。"""
    scenario = json.loads(snapshot.read_text())["scenario"]
    scaling = scenario in {f"peking547-sve-orbit64-b{b}" for b in (20000, 50000, 100000, 200000)}
    if not scaling and scenario not in ("test129-a-b4096", "peking547-a-b20000"):
        raise ValueError("仅支持已冻结的自然来源")
    manifest = ROOT / "docs/parallel-roam/cpu-pilot-scenarios-v1.csv"
    cameras = ROOT / "benchmark-output/roam-materialization/mpr-01/input-freeze/inputs/camera-samples.csv"
    paths = {"snapshot/" + snapshot.name: snapshot,
             "snapshot/" + scenario + "-source.json": snapshot.parent / (scenario + "-source.json"),
             "repository/" + str(manifest.relative_to(ROOT)): manifest,
             "repository/" + str(cameras.relative_to(ROOT)): cameras}
    if scaling:
        paths = {"snapshot/" + snapshot.name: snapshot,
                 "snapshot/" + scenario + "-source.json": snapshot.parent / (scenario + "-source.json"),
                 "snapshot/cameras.json": snapshot.parent / "cameras.json"}
    if scaling and mode == "dod":
        paths["repository/assets/heightmaps/Hm_Terrain_Peking_513.png"] = ROOT / "assets/heightmaps/Hm_Terrain_Peking_513.png"
    elif mode in ("classic", "dod"):
        with manifest.open() as stream:
            row = next(r for r in csv.DictReader(stream) if r["scenarioId"] == scenario)
        asset = ROOT / row["heightMapPath"]
        paths["repository/" + str(asset.relative_to(ROOT))] = asset
    result = {}
    for name, source in paths.items():
        target = work / "inputs" / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        result[name] = file_identity(source)
    return result


def main():
    parser = argparse.ArgumentParser(description="CPU 自然轨迹函数采样")
    parser.add_argument("backend", choices=("perf", "tracy"))
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--snapshot", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--mode", default="trajectory-c-timing", choices=("trajectory-a-timing", "trajectory-b-timing", "trajectory-c-timing", "classic", "dod"))
    parser.add_argument("--replays", type=int, choices=range(1, 33), default=1)
    parser.add_argument("--workers", type=int, choices=(4, 8))
    parser.add_argument("--limit-policy", choices=("fixed64", "scaled"))
    parser.add_argument("--perf", default="perf")
    parser.add_argument("--capture", type=Path)
    parser.add_argument("--csvexport", type=Path)
    parser.add_argument("--callgraph", choices=("fp", "dwarf,16384"), default="fp")
    parser.add_argument("--native-work-root", type=Path, default=Path.home() / ".cache/roam-profiling/runs")
    args = parser.parse_args()
    if args.mode in ("classic", "dod") and args.replays != 1:
        parser.error("家族采集只支持原八轮，--replays 必须为 1")
    if args.workers and args.mode not in ("trajectory-c-timing", "dod"):
        parser.error("--workers 仅用于 C 或 DOD")
    if args.limit_policy and args.mode not in ("trajectory-b-timing", "trajectory-c-timing"):
        parser.error("--limit-policy 仅用于 B/C")
    if args.backend == "tracy":
        if not args.capture or not args.csvexport:
            parser.error("Tracy 需要 --capture 与 --csvexport")
        args.capture = args.capture.resolve(strict=True)
        args.csvexport = args.csvexport.resolve(strict=True)
    args.executable = args.executable.resolve(strict=True)
    args.snapshot = args.snapshot.resolve(strict=True)
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=False)
    args.native_work_root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix=f"natural-{args.backend}-", dir=args.native_work_root))
    result = {"executable": file_identity(args.executable), "snapshot": file_identity(args.snapshot),
              "source_commit": subprocess.check_output(["git", "-C", str(ROOT), "rev-parse", "HEAD"], text=True).strip(),
              "environment": inspect_environment(args.perf), "native_work": str(work),
              "backend": args.backend, "mode": args.mode, "replays": args.replays,
              "workers": args.workers, "limit_policy": args.limit_policy, "complete": False}
    (work / "source-files.json").write_text(json.dumps(source_manifest(), ensure_ascii=False, indent=2), encoding="utf-8")
    # 保存实际带符号程序，后续重编译不能改变旧采样对应的符号身份
    shutil.copy2(args.executable, work / "recorded-executable")
    cache = args.executable.parent.parent / "CMakeCache.txt"
    commands = args.executable.parent.parent / "compile_commands.json"
    for path in (cache, commands):
        if path.exists():
            shutil.copy2(path, work / path.name)
    try:
        result["inputs"] = archive_inputs(args.snapshot, work, args.mode)
        arguments = [str(args.snapshot), args.mode, str(work / "program"), "--profile", str(args.replays)]
        if args.workers:
            arguments.extend(["--workers", str(args.workers)])
        if args.limit_policy:
            arguments.extend(["--limit-policy", args.limit_policy])
        if args.backend == "perf":
            result["record"] = perf_backend.record(args.perf, args.executable, arguments, work, ROOT)
            if not result["record"]["complete"]:
                raise RuntimeError("perf 或被测进程未完整结束")
            result["exports"] = perf_backend.export(args.perf, work)
        else:
            from profiling.environment import command
            result["tools"] = {name: file_identity(path) for name, path in
                               (("capture", args.capture), ("csvexport", args.csvexport))}
            result["tool_versions"] = {name: command([str(path), option]) for name, path, option in
                                       (("capture", args.capture, "--help"), ("csvexport", args.csvexport, "-V"))}
            if any("0.14.1" not in v["stdout"] + v["stderr"] for v in result["tool_versions"].values()):
                raise RuntimeError("Tracy 工具不是冻结的 0.14.1")
            result["record"] = tracy_backend.capture_tracy(args.capture, args.executable, arguments, work, cwd=ROOT)
            if not result["record"]["complete_exit"]:
                raise RuntimeError("Tracy 或被测进程未完整结束")
            result["exports"] = tracy_backend.export(args.csvexport, work)
        if file_identity(args.executable)["sha256"] != result["executable"]["sha256"]:
            raise RuntimeError("采集期间程序发生变化，不能确认符号身份")
        if any(value["status"] != "ok" for value in result["exports"].values()):
            raise RuntimeError("官方工具导出失败")
        windows = read_windows((work / "program/profile-windows.csv").read_text(), expected=8 * args.replays)
        if args.backend == "perf":
            summary = summarize_perf((work / "perf-script.txt").read_text(), windows)
            report_text = (work / "perf-report.txt").read_text()
            import re
            lost = re.search(r"Total Lost Samples:\s*(\d+)", report_text)
            summary["lost_samples"] = int(lost[1]) if lost else None
            summary["stack_mode"] = "machine functions; inline expansion disabled for unambiguous symbol aggregation"
            report = markdown_perf(summary)
            result["roi_samples"] = summary["roi_samples"]
        else:
            summary = summarize_tracy((work / "zones.csv").read_text(), len(windows))
            if [f["identity"] for f in summary["frames"]] != [f'{w["replay"]}:{w["round"]}' for w in windows]:
                raise RuntimeError("Tracy 帧与会话窗口身份不一致")
            report = markdown_tracy(summary)
            result["roi_zones"] = summary["roi_zones"]
        (work / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")
        (work / "report.md").write_text(report, encoding="utf-8")
        result["complete"] = True
    except (OSError, ValueError, RuntimeError) as error:
        result["error"] = str(error)
    finally:
        # 控制端点没有数据内容；保留 argv 和日志，归档只复制普通产物
        shutil.copytree(work, args.output / "artifacts", ignore=shutil.ignore_patterns("*.fifo"))
        result["files"] = {str(p.relative_to(work)): file_identity(p) for p in work.iterdir() if p.is_file()}
        (args.output / "manifest.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({k: result[k] for k in ("complete", "roi_samples", "roi_zones", "error") if k in result}, ensure_ascii=False))
    return 0 if result["complete"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
