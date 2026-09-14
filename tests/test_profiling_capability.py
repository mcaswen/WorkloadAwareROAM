"""针对工具失败与证据解析的定向检查，不代替真实工具采集。"""

import io
import csv
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from profiling.capability import perf_stack_coverage, tracy_fixture_coverage
from profiling.environment import command
from profiling.report import read_windows, summarize_perf


class ProfilingCapabilityTests(unittest.TestCase):
    def test_windows_and_event_weights(self):
        windows = read_windows("replay,round,start_ns,end_ns,enable_ns,disable_ns\n0,0,1000000000,2000000000,5,6\n# complete\n", 1)
        text = ("task 1/2 0.900000000: 10 cpu-clock:u:\n\t01 outside (/exe)\n\n"
                "task 1/2 1.100000000: 20 cpu-clock:u:\n\t01 leaf (/exe)\n\t02 parent (/exe)\n\t03 parent (/exe)\n\n"
                "task 1/3 1.900000000: 30 cpu-clock:u:\n\n")
        result = summarize_perf(text, windows)
        self.assertEqual((result["roi_samples"], result["outside_samples"], result["missing_stack_samples"]), (2, 1, 1))
        self.assertEqual(result["roi_event_weight"], 50)
        self.assertEqual(next(row for row in result["inclusive"] if row["function"] == "parent")["percent"], 40)
        with self.assertRaises(ValueError):
            read_windows("replay,round,start_ns,end_ns,enable_ns,disable_ns\n")
        with self.assertRaises(ValueError):
            read_windows("replay,round,start_ns,end_ns,enable_ns,disable_ns\n0,0,9,8,0,0\n# complete\n")

    def test_missing_tool_and_timeout_are_not_success(self):
        self.assertEqual(command(["/missing/roam-profiler-tool"])["status"], "unavailable")
        result = command([sys.executable, "-c", "import time; time.sleep(2)"], timeout=0.02)
        self.assertEqual(result["status"], "timeout")

    def test_thread_without_stack_remains_in_denominator(self):
        text = ("fixture 10/10 0.1: cpu-clock:u:\n\t01 ProfileHotLeaf\n\t02 ProfileHotParent\n\n"
                "fixture 10/11 0.2: cpu-clock:u:\n\n")
        result = perf_stack_coverage(text)
        self.assertEqual(result["10"]["parent"], 1)
        self.assertEqual(result["11"], {"samples": 1, "leaf": 0, "parent": 0})

    def test_tracy_rejects_missing_columns_and_truncated_tail(self):
        with self.assertRaises(ValueError):
            tracy_fixture_coverage("name,thread\nfixture.frame,1\n")
        stream = io.StringIO()
        writer = csv.writer(stream)
        writer.writerow(["name", "thread", "ns_since_start", "exec_time_ns"])
        writer.writerow(["fixture.frame", "1", 0, 100])
        for tid in (1, 2, 3):
            writer.writerow(["fixture.hot_leaf", tid, 10, 20])
            writer.writerow(["fixture.hot_parent", tid, 9, 22])
        for tid in (2, 3):
            writer.writerow(["fixture.task", tid, 5, 50])
        writer.writerow(["fixture.wait", 1, 60, 20])
        writer.writerow(["fixture.exception", 1, 80, 10])
        self.assertFalse(tracy_fixture_coverage(stream.getvalue())["passed"])
        writer.writerow(["fixture.complete", 1, 101, 1])
        self.assertTrue(tracy_fixture_coverage(stream.getvalue())["passed"])


if __name__ == "__main__":
    unittest.main()
