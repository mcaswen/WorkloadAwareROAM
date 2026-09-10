"""短进程验证执行顺序、失败留存、亲和性恢复，以及真实探针的开关与几何契约。"""

import argparse
import csv
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from compare_runtime_performance import normalize_config, run_process, schedule
from runtime_performance_analysis import summarize_frames
from runtime_performance_environment import WindowsEnvironment

CONFIG = None


@unittest.skipUnless(os.name == "nt", "Windows 环境协议")
class RunnerTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.work = Path(self.temporary.name)
        self.environment = WindowsEnvironment()

    def test_failure_timeout_and_affinity_restore(self):
        original = self.environment.affinity()[0]
        selected = original & -original
        for name, script, timeout in (("failure", "raise SystemExit(7)", 10),
                                      ("timeout", "import time; time.sleep(30)", .05)):
            with self.subTest(name=name):
                with self.assertRaises((RuntimeError, subprocess.TimeoutExpired)):
                    run_process([sys.executable, "-c", script], self.work / name,
                                self.environment, selected, timeout)
                result = json.loads((self.work / name / "process.json").read_text(encoding="utf-8"))
                self.assertEqual(result["status"], "failed")
                self.assertIsNotNone(result["exitCode"])
                self.assertEqual(self.environment.affinity()[0], original)
        with self.assertRaises(OSError):
            run_process([str(self.work / "missing.exe")], self.work / "missing", self.environment, selected)
        self.assertEqual(self.environment.affinity()[0], original)
        self.assertEqual(json.loads((self.work / "missing/process.json").read_text(encoding="utf-8"))["status"], "failed")

    def test_success_records_actual_child_and_does_not_overwrite(self):
        mask = self.environment.affinity()[0]
        result = run_process([sys.executable, "-c", "print('retained')"], self.work / "success", self.environment, mask)
        self.assertEqual(result["observedAffinity"], mask)
        self.assertGreater(result["peakWorkingSetBytes"], 0)
        self.assertGreater(result["pid"], 0)
        with self.assertRaises(FileExistsError):
            run_process([sys.executable, "-c", "pass"], self.work / "success", self.environment, mask)
        self.assertIn(b"retained", (self.work / "success/process.log").read_bytes())

    def test_complete_schedule_and_control_labels(self):
        config = {"protocolVersion": 1, "versions": {"old": {}, "new": {}}, "groups": [
            {"id": kind, "comparison": kind, "layout": "abba", "affinity": "all", "mode": "benchmark",
             "backend": "OpenGL", "profile": "standard", "policy": "serial-incremental"} for kind in ("AB", "AA", "BB")]}
        groups, _ = normalize_config(config, self.environment)
        records = list(schedule(groups))
        self.assertEqual(len(records), 72)
        self.assertEqual(sum(row[-1] for row in records), 12)
        self.assertEqual([row[0]["id"] for row in records[12:24:4]], ["BB", "AA", "AB"])
        self.assertEqual("".join(row[3] for row in records[12:16]), "BAAB")
        self.assertEqual(groups[1]["versions"], {"A": "old", "B": "old"})
        self.assertEqual(groups[2]["versions"], {"A": "new", "B": "new"})
        for mask in (True, "l3:-1", "l3:999"):
            config["groups"][0]["affinity"] = mask
            with self.assertRaises(ValueError): normalize_config(config, self.environment)

    def test_probe_modes_replay_identical_geometry(self):
        if CONFIG is None:
            self.skipTest("命令行未提供真实应用与探针")
        inputs = self.work / "inputs"
        subprocess.run([str(CONFIG.application), "--benchmark", "--profile", "cpu-pilot-inputs",
                        "--scenario-manifest", str(ROOT / "docs/parallel-roam/cpu-pilot-scenarios-v1.csv"),
                        "--output-dir", str(inputs)], cwd=ROOT, check=True, capture_output=True)
        geometry = []
        for mode in ("diagnostics-on", "diagnostics-off"):
            output = self.work / f"{mode}.csv"
            subprocess.run([str(CONFIG.probe), str(ROOT), str(inputs / "scenarios.csv"),
                            str(inputs / "camera-samples.csv"), "test129-a-b4096", "maximum-parallel-incremental",
                            mode, str(output)], cwd=ROOT, check=True, capture_output=True)
            self.assertEqual(summarize_frames(output, mode)["rowCount"], 64)
            with output.open(encoding="utf-8", newline="") as stream:
                rows = list(csv.DictReader(stream))
            geometry.append([(r["cameraPoseHash"], r["viewInputHash"], r["probeBuildInputHash"],
                              r["probeGeometryHash"], r["activeTriangleCount"]) for r in rows])
            if mode == "diagnostics-off":
                self.assertTrue(all(float(r["passEvidenceMilliseconds"]) == 0 for r in rows))
        self.assertEqual(geometry[0], geometry[1])


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--application", type=Path)
    parser.add_argument("--probe", type=Path)
    args, remaining = parser.parse_known_args()
    if args.application and args.probe:
        CONFIG = args
    unittest.main(argv=[sys.argv[0], *remaining])
