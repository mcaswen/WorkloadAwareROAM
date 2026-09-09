"""审计一个或多个 CPU 配对尝试，并输出最小中文探索报告。"""

import argparse
import json
from pathlib import Path
import sys
import time

from cpu_pilot_support import file_identity
from formal_experiments.crossover_analysis import analyze_attempts
from formal_experiments.input_validation import load_pair_attempt
from formal_experiments.report_writer import write_report


def peak_memory_bytes():
    if sys.platform == "win32":
        import ctypes
        from ctypes import wintypes
        class MemoryCounters(ctypes.Structure):
            _fields_ = [("cb", wintypes.DWORD), ("PageFaultCount", wintypes.DWORD),
                        ("PeakWorkingSetSize", ctypes.c_size_t), ("WorkingSetSize", ctypes.c_size_t),
                        ("QuotaPeakPagedPoolUsage", ctypes.c_size_t), ("QuotaPagedPoolUsage", ctypes.c_size_t),
                        ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t), ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
                        ("PagefileUsage", ctypes.c_size_t), ("PeakPagefileUsage", ctypes.c_size_t)]
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.GetCurrentProcess.restype = wintypes.HANDLE
        psapi = ctypes.WinDLL("psapi", use_last_error=True)
        psapi.GetProcessMemoryInfo.argtypes = [wintypes.HANDLE, ctypes.POINTER(MemoryCounters), wintypes.DWORD]
        counters = MemoryCounters()
        counters.cb = ctypes.sizeof(counters)
        if not psapi.GetProcessMemoryInfo(kernel.GetCurrentProcess(), ctypes.byref(counters), counters.cb):
            raise OSError(ctypes.get_last_error(), "无法读取分析进程峰值内存")
        return counters.PeakWorkingSetSize
    import resource
    value = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    return int(value if sys.platform == "darwin" else value * 1024)


def analyze(run_directories, output):
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    start = time.perf_counter()
    metadata = {"schemaVersion": 1, "dataPurpose": "exploratory", "status": "analyzing",
                "runDirectories": [str(Path(path).resolve()) for path in run_directories]}
    path = output / "analysis-metadata.json"
    try:
        attempts = [load_pair_attempt(directory) for directory in run_directories]
        result = analyze_attempts(attempts)
        write_report(output, result)
        metadata.update(status="analysis_complete", wallSeconds=time.perf_counter() - start,
                        peakMemoryBytes=peak_memory_bytes(),
                        inputs=[item for attempt in attempts for item in attempt.get("files", [])],
                        independentRunIds=[run["runId"] for run in result["runs"]],
                        failedAttemptCount=len(result["failedAttempts"]))
        sources = [Path(__file__), Path(__file__).parent / "cpu_pilot_artifacts.py",
                   Path(__file__).parent / "cpu_pilot_support.py",
                   *sorted((Path(__file__).parent / "formal_experiments").glob("*.py"))]
        metadata["analysisSources"] = [file_identity(source) for source in sources]
        metadata["outputs"] = [file_identity(source) for source in sorted(output.iterdir()) if source.is_file()]
        path.write_text(json.dumps(metadata, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print(f"CPU 配对分析完成：{output / 'report.md'}")
        return 0
    except Exception as error:
        metadata.update(status="failed", error=str(error), wallSeconds=time.perf_counter() - start)
        path.write_text(json.dumps(metadata, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print(f"CPU 配对分析失败：{error}", file=sys.stderr)
        return 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run-dir", type=Path, action="append", required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    arguments = parser.parse_args()
    try:
        return analyze(arguments.run_dir, arguments.output_dir)
    except (OSError, ValueError) as error:
        print(f"无法创建分析目录：{error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())

