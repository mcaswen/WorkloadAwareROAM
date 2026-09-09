"""验证固定描述规则、坏配对拒绝和中文报告，合成值只用于统计单元测试。"""

import copy
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))

from formal_experiments.crossover_analysis import analyze_attempts
from formal_experiments.paired_statistics import run_drift, summarize_pair
from formal_experiments.report_writer import write_report


def attempt(times_a, times_b, *, run_id="fixture", sample=1, fallback=False):
    rows = []
    for block, (a, b) in enumerate(zip(times_a, times_b)):
        for action, value, workers in (("serialFullRefresh", a, 1), ("parallelFullRefresh", b, 8)):
            rows.append({"runId": run_id, "isWarmup": False, "absoluteBlockIndex": block,
                         "requestedAction": action, "wallMs": value, "selectionRank": 0,
                         "selectionStratum": "low", "primaryWorkValue": 10.0, "replayInputHash": 123, "resultHash": 124,
                         "warmupCount": 0, "measuredRepeatCount": len(times_a), "parallelWorkerCount": 8,
                         "requestedWorkerCount": workers, "actualWorkerCount": 1 if fallback else workers,
                         "fallbackReason": "belowParallelThreshold" if fallback and workers == 8 else "none",
                         "fallbackDetail": "insufficient_safe_chunks" if fallback and workers == 8 else ""})
    return {"runId": run_id, "groups": {("scene", "mergeScore", sample): rows}, "targetSummaries": [],
            "directory": "synthetic-statistics-fixture", "summary": {"backend": "OpenGL", "warmupRowCount": "0", "measuredRowCount": str(len(rows))},
            "calibration": {"noiseMs": .001, "environmentValid": True, "batches": {}}, "failed": False}


class StatisticsTests(unittest.TestCase):
    def test_fixed_direction_noise_and_denominator(self):
        self.assertEqual(summarize_pair([1, 1, 1], [2, 2, 2], 0)["winner"], "A")
        self.assertEqual(summarize_pair([2, 2, 2], [1, 1, 1], 0)["winner"], "B")
        self.assertEqual(summarize_pair([1, 2, 1], [2, 1, 2], 1.0)["winner"], "tie")
        threshold = summarize_pair([0, 0], [.01, .01], 0)
        self.assertEqual(threshold["winner"], "tie")
        self.assertIsNone(threshold["relativeGainB"])
        self.assertEqual(summarize_pair([0, 0], [0, 0], 0)["winner"], "tie")
        self.assertEqual(summarize_pair([2, 2], [1, 1], 0)["relativeGainB"], .5)

    def test_difference_is_paired_before_aggregation(self):
        result = summarize_pair([0, 10, 100], [0, 9, 200], 0)
        self.assertEqual(result["medianDifferenceMs"], 0)
        self.assertNotEqual(result["medianDifferenceMs"], result["medianAMs"] - result["medianBMs"])

    def test_invalid_statistics_inputs(self):
        for a, b, noise in (([], [], 0), ([1], [1, 2], 0), ([float("nan")], [1], 0),
                            ([1], [float("inf")], 0), ([-1], [1], 0), ([1], [1], -1)):
            with self.assertRaises(ValueError):
                summarize_pair(a, b, noise)
        with self.assertRaises(ValueError):
            run_drift([1, 1])
        self.assertTrue(run_drift([1, 1.01, 1.02])["stable"])
        self.assertFalse(run_drift([1, 1.1, 1.2])["stable"])
        self.assertIsNone(run_drift([0, 0, 0])["relativeRange"])

    def test_bidirectional_is_within_one_run(self):
        first = attempt([1] * 6, [2] * 6)
        other = attempt([3] * 6, [1] * 6, sample=2)
        other_rows = next(iter(other["groups"].values()))
        for row in other_rows:
            row["selectionStratum"] = "high"
            row["primaryWorkValue"] = 100.0
        first["groups"].update(other["groups"])
        result = analyze_attempts([first])
        self.assertEqual(result["signals"][0]["signal"], "观察到双向描述性信号")
        self.assertEqual({row["stratum"] for row in result["strata"]}, {"low", "high"})
        reversed_rows = copy.deepcopy(first)
        for rows in reversed_rows["groups"].values():
            rows.reverse()
        self.assertEqual(result["pairs"], analyze_attempts([reversed_rows])["pairs"])

    def test_fallback_and_environment_withhold_winners(self):
        result = analyze_attempts([attempt([2] * 6, [1] * 6, fallback=True)])
        self.assertEqual(result["pairs"][0]["winner"], "excluded")
        self.assertEqual(result["pairs"][0]["fallbackBlockCount"], 6)
        invalid = attempt([2] * 6, [1] * 6)
        invalid["calibration"]["environmentValid"] = False
        self.assertEqual(analyze_attempts([invalid])["pairs"][0]["winner"], "excluded")

    def test_mesh_keeps_valid_serial_pair(self):
        mesh = attempt([1] * 6, [2] * 6)
        rows = next(iter(mesh["groups"].values()))
        for row in rows:
            parallel = row["requestedWorkerCount"] == 8
            row["requestedAction"] = "parallelDirty" if parallel else "serialDirty"
            if parallel:
                row.update(fallbackReason="belowParallelThreshold", fallbackDetail="below_parallel_threshold", actualWorkerCount=1)
        for block in range(6):
            full = copy.deepcopy(rows[block * 2])
            full.update(requestedAction="serialFull", wallMs=3)
            rows.append(full)
        mesh["groups"] = {("scene", "meshEmit", 1): rows}
        results = analyze_attempts([mesh])["pairs"]
        self.assertEqual(len(results), 3)
        self.assertEqual([row["winner"] for row in results], ["excluded", "A", "excluded"])

    def test_duplicate_attempt_and_result_conflicts(self):
        first = attempt([1] * 6, [2] * 6)
        with self.assertRaises(ValueError):
            analyze_attempts([first, copy.deepcopy(first)])
        second = attempt([1] * 6, [2] * 6, run_id="other")
        next(iter(second["groups"].values()))[0]["resultHash"] = 99
        with self.assertRaises(ValueError):
            analyze_attempts([first, second])

    def test_failed_attempt_is_audit_only_and_report_is_chinese(self):
        failed = {"failed": True, "directory": "failed", "failure": "缺少完整块", "runId": "bad",
                  "groups": attempt([100], [1])["groups"]}
        result = analyze_attempts([failed])
        self.assertEqual(result["pairs"], [])
        self.assertEqual(len(result["failedAttempts"]), 1)
        with tempfile.TemporaryDirectory() as directory:
            write_report(Path(directory), result)
            report = (Path(directory) / "report.md").read_text(encoding="utf-8")
            self.assertIn("CPU 阶段配对探索报告", report)
            self.assertIn("缺少完整块", report)
            self.assertIn("其局部完整目标没有进入胜负统计", report)
            self.assertNotIn("formal crossover passed", report)

    def test_short_run_repetition_does_not_revalidate_bad_first_round(self):
        runs = []
        for index, value in enumerate((1.0, 1.3, 1.6, 1.0, 1.0, 1.0)):
            current = attempt([value] * 10, [value * 2] * 10, run_id=f"short-{index}")
            rows = next(iter(current["groups"].values()))
            for row in rows:
                row["warmupCount"] = 2
                row["absoluteBlockIndex"] += 2
            current["groups"] = {("test129-a-b4096", "mergeScore", 1): rows}
            current["metadata"] = {"processPid": 100 + index, "processStartedUtc": f"2026-09-09T10:00:0{index}Z"}
            runs.append(current)
        first = analyze_attempts(runs[:3])
        self.assertTrue(all(row["status"] == "repeat_required" for row in first["shortRunStability"]))
        self.assertTrue(all(row["winner"] == "excluded" for row in first["pairs"]))
        second = analyze_attempts(runs)
        self.assertTrue(all(row["status"] == "stable" for row in second["shortRunStability"]))
        self.assertTrue(all(row["winner"] == "excluded" for row in second["pairs"] if row["runId"] in {"short-0", "short-1", "short-2"}))
        self.assertTrue(all(row["winner"] == "A" for row in second["pairs"] if row["runId"] in {"short-3", "short-4", "short-5"}))

    def test_execution_versions_and_counts_are_separate_groups(self):
        first = attempt([1] * 6, [2] * 6)
        second = attempt([1] * 6, [2] * 6, run_id="other-source")
        second["metadata"] = {"source": {"sha256": "other"}}
        third = attempt([1] * 10, [2] * 10, run_id="other-count")
        result = analyze_attempts([first, second, third])
        self.assertEqual(len(result["consistency"]), 3)
        self.assertTrue(all(row["runCount"] == 1 for row in result["consistency"]))

    def test_costs_include_warmup_without_changing_measured_pairs(self):
        current = attempt([1] * 6, [2] * 6)
        rows = next(iter(current["groups"].values()))
        warmup = copy.deepcopy(rows[0])
        warmup.update(isWarmup=True, wallMs=100)
        rows.append(warmup)
        for row in rows:
            row.update(stateCloneMs=2, inputCheckMs=3, validationMs=4, workerPreparationMs=5, featureCollectionMs=6)
        current["targetSummaries"] = [{"status": "valid", "scenarioId": "scene", "passId": "mergeScore", "sampleIndex": "1",
                                       "rebuildMs": "7", "workerPreparationMs": "8"}]
        result = analyze_attempts([current])
        cost = result["targetCosts"][0]
        self.assertEqual(cost["stateCloneMsSum"], 26)
        self.assertEqual(cost["validationMsMedian"], 4)
        self.assertEqual(cost["rebuildToTargetMs"], 7)
        self.assertEqual(cost["warmupRowCount"], 1)
        self.assertEqual(cost["inputFeatureProbeMs"], 6)
        self.assertNotIn("featureCollectionMsSum", cost)
        self.assertEqual(result["pairs"][0]["medianAMs"], 1)

    def test_environment_groups_and_within_attempt_changes(self):
        first = attempt([1] * 6, [2] * 6)
        first["metadata"] = {"environment": {"powerPlan": "A", "parentPid": 1},
                             "environmentAfter": {"powerPlan": "A", "parentPid": 1}}
        second = copy.deepcopy(first)
        second["runId"] = "second-environment"
        second["metadata"] = {"environment": {"powerPlan": "B"}, "environmentAfter": {"powerPlan": "B"}}
        result = analyze_attempts([first, second])
        self.assertEqual(len(result["consistency"]), 2)
        self.assertTrue(all(row["runCount"] == 1 for row in result["consistency"]))
        second["metadata"]["environmentAfter"]["powerPlan"] = "C"
        changed = analyze_attempts([second])
        self.assertTrue(changed["runs"][0]["environmentChanged"])
        self.assertEqual(changed["pairs"][0]["winner"], "excluded")
        self.assertIn("execution_environment_changed", changed["pairs"][0]["exclusionReasons"])


if __name__ == "__main__":
    unittest.main()
