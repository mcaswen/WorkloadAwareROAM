"""验证版本统计的独立单位、CSV 语义归属及不可分辨关闭边界。"""

import copy
import csv
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from runtime_performance_analysis import (
    BENCHMARK_FIELDS, DIAGNOSTICS, PROBE_FIELDS, analyze_attempt, paired_statistics, review_micro,
    summarize_frames, write_json)

HEAP_METRIC = "pass_splitScoreHeapifyMs.median"


def frame(fields, index=0, mode="benchmark"):
    row = dict.fromkeys(fields, "0")
    row.update({"frameIndex": str(index), "experimentSchemaVersion": "4", "passed": "1",
                "resultValidationPassed": "1"})
    row.update(DIAGNOSTICS["diagnostics-on" if mode == "benchmark" else mode])
    if mode != "benchmark":
        row.update({"probeProtocolVersion": "1", "diagnosticsMode": mode})
    return row


def paired(values, block=1):
    order = "BAAB" if block % 2 else "ABBA"
    return paired_statistics([{"block": block, "slot": slot, "label": label, "value": value}
                              for slot, (label, value) in enumerate(zip(order, values))], "abba")


def cohort(identity, difference=.002, noise=.004):
    versions = {"A": {"sha": "old"}, "B": {"sha": "new"}}
    groups = {}
    for label in ("AB", "AA", "BB"):
        records = []
        for block in range(1, 6):
            for slot, side in enumerate("BAAB" if block % 2 else "ABBA"):
                value = 1 + ((difference if label == "AB" else (noise if slot < 2 else -noise)) if side == "B" else 0)
                records.append({"block": block, "slot": slot, "label": side, "value": value})
        metric = paired_statistics(records, "abba")
        groups[label] = {"comparison": label, "condition": {"affinity": 3}, "semanticSha256": "same",
                         "binaryIdentities": versions if label == "AB" else dict.fromkeys("AB", versions["A" if label == "AA" else "B"]),
                         "metrics": {HEAP_METRIC: metric}, "measuredBlocks": 5,
                         "measuredProcesses": {"A": 10, "B": 10}}
    return {"attemptId": identity, "groups": groups}


class AnalysisTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.csv = Path(self.temporary.name) / "frames.csv"

    def write(self, rows, fields=BENCHMARK_FIELDS):
        with self.csv.open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=fields)
            writer.writeheader()
            writer.writerows(rows)

    def test_environment_is_not_algorithm_identity(self):
        row = frame(BENCHMARK_FIELDS)
        self.write([row])
        original = summarize_frames(self.csv)
        row["cpuUtilizationPercent"] = "234.5"
        self.write([row])
        changed = summarize_frames(self.csv)
        self.assertEqual(original["semanticSha256"], changed["semanticSha256"])
        self.assertNotEqual(original["environment"], changed["environment"])
        row["activeTriangleCount"] = "20000"
        self.write([row])
        self.assertNotEqual(original["semanticSha256"], summarize_frames(self.csv)["semanticSha256"])

    def test_unknown_columns_missing_values_and_bad_numbers_fail(self):
        for key, value in (("buildWallMilliseconds", "nan"), ("buildWallMilliseconds", "-1"),
                           ("passed", "0"), ("queueInvariantViolationCount", "1"), ("frameIndex", "2"),
                           ("passEvidenceEnabled", "false"), ("activeTriangleCount", "")):
            with self.subTest(key=key, value=value):
                row = frame(BENCHMARK_FIELDS)
                row[key] = value
                self.write([row])
                with self.assertRaises(ValueError): summarize_frames(self.csv)
        self.write([{**frame(BENCHMARK_FIELDS), "newCounter": "1"}], BENCHMARK_FIELDS + ["newCounter"])
        with self.assertRaises(ValueError): summarize_frames(self.csv)
        self.write([frame(BENCHMARK_FIELDS)], BENCHMARK_FIELDS + ["passed"])
        with self.assertRaises(ValueError): summarize_frames(self.csv)

    def test_probe_requires_all_three_flags_and_complete_trajectory(self):
        rows = [frame(PROBE_FIELDS, index, "diagnostics-off") for index in range(64)]
        self.write(rows, PROBE_FIELDS)
        self.assertEqual(summarize_frames(self.csv, "diagnostics-off")["rowCount"], 64)
        for flag in DIAGNOSTICS["diagnostics-off"]:
            altered = copy.deepcopy(rows)
            altered[12][flag] = "true"
            self.write(altered, PROBE_FIELDS)
            with self.assertRaises(ValueError): summarize_frames(self.csv, "diagnostics-off")
        self.write(rows[:-1], PROBE_FIELDS)
        with self.assertRaises(ValueError): summarize_frames(self.csv, "diagnostics-off")

    def test_hash_rejects_modified_csv(self):
        self.write([frame(BENCHMARK_FIELDS)])
        identity = summarize_frames(self.csv)["csv"]
        self.write([{**frame(BENCHMARK_FIELDS), "cpuUtilizationPercent": "10"}])
        with self.assertRaises(ValueError): summarize_frames(self.csv, identity=identity)

    def test_attempt_rejects_duplicate_missing_and_mismatched_processes(self):
        directory = Path(self.temporary.name)
        group = {"id": "case", "comparison": "AB", "layout": "pair", "mode": "benchmark",
                 "affinity": 3, "profile": "standard", "versions": {"A": "old", "B": "new"}}
        identities = {"versions": {version: {"executable": {"sha256": version}} for version in ("old", "new")}}
        metadata = {"protocolVersion": 1, "status": "complete", "identitiesBefore": identities,
                    "identitiesAfter": identities, "environmentBefore": {}, "environmentAfter": {},
                    "diagnosticsProtocol": DIAGNOSTICS, "warmupBlocks": 1, "measuredBlocks": 5,
                    "groups": [group], "attemptId": "fixture", "cohort": "fixture"}
        runs = []
        for block in range(6):
            for slot, label in enumerate("AB" if block % 2 == 0 else "BA"):
                child = directory / f"{block}-{slot}"
                child.mkdir()
                self.csv = child / "frames.csv"
                self.write([frame(BENCHMARK_FIELDS, index) for index in range(64)])
                summary = summarize_frames(self.csv)
                runs.append({"group": "case", "status": "complete", "exitCode": 0,
                             "observedAffinity": 3, "requestedAffinity": 3, "diagnostics": DIAGNOSTICS["diagnostics-on"],
                             "version": group["versions"][label], "label": label,
                             "executableSha256": group["versions"][label], "relativeDirectory": child.name,
                             "pid": 100 + len(runs), "startedUtc": "fixture", "csv": summary["csv"],
                             "rowCount": 64, "seconds": 1, "cpuSeconds": .5, "peakWorkingSetBytes": 100,
                             "block": block, "slot": slot, "warmup": block == 0,
                             "priorityClass": 32, "machineBusyPercent": 20})
        write_json(directory / "metadata.json", metadata)
        write_json(directory / "runs.json", runs)
        result = analyze_attempt(directory)
        self.assertEqual(result["groups"]["case"]["measuredProcesses"], {"A": 5, "B": 5})
        for altered in (runs + [runs[0]], runs[:-1], [{**runs[0], "observedAffinity": 1}, *runs[1:]],
                        [{**runs[0], "version": "new"}, *runs[1:]], [{**runs[0], "status": "failed"}, *runs[1:]]):
            write_json(directory / "runs.json", altered)
            with self.assertRaises(ValueError): analyze_attempt(directory)
        write_json(directory / "runs.json", runs)
        metadata["identitiesAfter"] = {}
        write_json(directory / "metadata.json", metadata)
        with self.assertRaises(ValueError): analyze_attempt(directory)

    def test_pair_differences_preserve_order_and_process_count(self):
        # 三个相邻进程对的中位差为 2，中位数之差为 1，二者不能替代
        rows = []
        for block, (left, right) in enumerate(((0, 50), (100, 101), (200, 202))):
            order = "AB" if block % 2 == 0 else "BA"
            values = {"A": left, "B": right}
            rows.extend({"block": block, "slot": slot, "label": label, "value": values[label]}
                        for slot, label in enumerate(order))
        result = paired_statistics(rows, "pair")
        self.assertEqual(result["pairedDifferences"], [50, 1, 2])
        self.assertEqual(result["pairedMedianDifference"], 2)
        self.assertEqual(result["medianDifference"], 1)
        self.assertEqual(len(result["aValues"]), 3)
        rows[-1]["slot"] = 0
        with self.assertRaises(ValueError): paired_statistics(rows, "pair")

    def test_micro_contract_needs_independent_matched_controls(self):
        first, second = cohort("one"), cohort("two")
        self.assertTrue(review_micro([first, second], HEAP_METRIC)["closed"])
        self.assertFalse(review_micro([first, first], HEAP_METRIC)["closed"])
        second["groups"].pop("BB")
        self.assertFalse(review_micro([first, second], HEAP_METRIC)["closed"])
        self.assertFalse(review_micro([first, cohort("two", noise=.1)], HEAP_METRIC)["closed"])
        self.assertFalse(review_micro([cohort("one", difference=.006), cohort("two", difference=.006)], HEAP_METRIC)["closed"])
        second = cohort("two")
        second["groups"]["AA"]["condition"] = {"affinity": 7}
        self.assertFalse(review_micro([first, second], HEAP_METRIC)["closed"])

    def test_micro_contract_rejects_seconds_and_memory_units(self):
        first, second = cohort("one"), cohort("two")
        for metric in ("process.seconds", "process.cpuSeconds", "process.peakWorkingSetBytes"):
            self.assertFalse(review_micro([first, second], metric)["closed"])


if __name__ == "__main__":
    unittest.main()
