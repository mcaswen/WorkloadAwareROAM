"""用真实进程验证发现完整性、独立重建、重复性与失败证据。"""

import argparse
import csv
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


CONFIG = None


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_rows(path, rows):
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def reference_selection(rows, requested):
    """独立按协议重算选择以覆盖百分位和距离，而不是调用 C++ 选择器。"""
    def fnv(data):
        value = 14695981039346656037
        for byte in data:
            value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
        return value

    def binary32(value):
        return struct.unpack("<f", struct.pack("<f", float(value)))[0]

    ranked = sorted((row for row in rows if row["selectionEligible"] == "true"),
                    key=lambda row: (binary32(row["primaryWorkValue"]), int(row["sampleIndex"])))
    if not ranked:
        return []
    strata = ("low", "middle", "high")
    entries = []
    for rank, row in enumerate(ranked):
        layer = min(2, 3 * rank // len(ranked))
        text = f"20260830|{row['scenarioId']}|{row['passId']}|{row['sampleIndex']}|{strata[layer]}"
        entries.append({"row": row, "layer": layer, "tie": (fnv(text.encode("utf-8")), int(row["sampleIndex"])),
                        "raw": [binary32(value) for value in row["featureVector"].split(";")], "normalized": []})
    for dimension in range(len(entries[0]["raw"])):
        values = sorted(entry["raw"][dimension] for entry in entries)
        def percentile(p):
            position = p * (len(values) - 1)
            left = int(position)
            return values[left] + (values[min(left + 1, len(values) - 1)] - values[left]) * (position - left)
        low, high = percentile(.05), percentile(.95)
        for entry in entries:
            entry["normalized"].append(0.0 if high == low else max(0.0, min(1.0, (entry["raw"][dimension] - low) / (high - low))))
    selected = []
    for layer in range(3):
        selected.extend((entry, strata[layer]) for entry in sorted(
            (item for item in entries if item["layer"] == layer), key=lambda item: item["tie"])[:requested // 4])
    while len(selected) < min(requested, len(entries)):
        remaining = [entry for entry in entries if not any(entry is item for item, _ in selected)]
        def distance_key(entry):
            nearest = min(sum((a - b) ** 2 for a, b in zip(entry["normalized"], item["normalized"])) for item, _ in selected)
            return (-nearest, *entry["tie"])
        selected.append((min(remaining, key=distance_key), "coverage"))
    return [(entry["row"]["sampleIndex"], layer) for entry, layer in selected]


class DiscoveryTests(unittest.TestCase):
    @classmethod
    def run_command(cls, command):
        result = subprocess.run(list(map(str, command)), cwd=CONFIG.source, capture_output=True,
                                text=True, encoding="utf-8", errors="replace", timeout=600)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        return result

    @classmethod
    def setUpClass(cls):
        global discovery
        sys.path.insert(0, str(CONFIG.source / "scripts"))
        import discover_cpu_pilot as discovery
        CONFIG.output.mkdir(parents=True, exist_ok=True)
        cls.work = Path(tempfile.mkdtemp(prefix="attempt-", dir=CONFIG.output))
        cls.prepared = cls.work / "prepared"
        cls.subset = cls.work / "prepared-subset"
        cls.prepare_command = [sys.executable, CONFIG.source / "scripts/prepare_cpu_pilot.py",
                               "--executable", CONFIG.executable, "--build-preset", CONFIG.preset]
        cls.run_command([*cls.prepare_command, "--output-dir", cls.prepared])
        cls.run_command([*cls.prepare_command, "--output-dir", cls.subset, "--scenario-id", "test129-a-b512"])
        cls.command = [sys.executable, CONFIG.source / "scripts/discover_cpu_pilot.py",
                       "--executable", CONFIG.executable, "--build-preset", CONFIG.preset]
        cls.complete = cls.work / "complete"
        cls.run_command([*cls.command, "--prepared-input-dir", cls.prepared, "--output-dir", cls.complete])
        cls.scenarios, cls.cameras, _ = discovery.load_prepared_inputs(cls.prepared)

    def test_complete_and_independent_root_rebuild(self):
        summary = discovery.verify_discovery_outputs(self.complete, self.scenarios, self.cameras, 4)
        self.assertEqual(int(summary["recordCount"]), 1920)
        metadata = json.loads((self.complete / "run-metadata.json").read_text(encoding="utf-8"))
        self.assertEqual(metadata["status"], "discovery_complete")
        self.assertEqual(metadata["source"], metadata["sourceAfter"])
        self.assertIn("environmentAfter", metadata)
        for identity in metadata["outputs"]:
            self.assertEqual(digest(Path(identity["path"])), identity["sha256"])
        # 新进程从根重新推进全部相机并核对实际阶段输入与原始选择特征
        self.run_command([CONFIG.replay_verifier, self.prepared / "inputs/scenarios.csv",
                          self.prepared / "inputs/camera-samples.csv", self.complete / "target-states.csv"])

    def test_subset_repeat_and_eight_points(self):
        scenarios, cameras, _ = discovery.load_prepared_inputs(self.subset)
        results = []
        for name in ("subset-first", "subset-second"):
            output = self.work / name
            self.run_command([*self.command, "--prepared-input-dir", self.subset, "--output-dir", output])
            summary = discovery.verify_discovery_outputs(output, scenarios, cameras, 4)
            self.assertEqual(int(summary["recordCount"]), 320)
            results.append((output / "target-states.csv").read_bytes())
        self.assertEqual(*results)
        # 同一低预算场景的合并拓扑没有候选也必须输出覆盖不足
        coverage = discovery.read_rows(self.work / "subset-first/target-coverage.csv")
        group = next(row for row in coverage if row["passId"] == "mergeTopology")
        self.assertEqual((group["eligibleCount"], group["selectedCount"]), ("0", "0"))
        output = self.work / "subset-eight"
        self.run_command([*self.command, "--prepared-input-dir", self.subset, "--output-dir", output,
                          "--targets-per-pass", "8"])
        discovery.verify_discovery_outputs(output, scenarios, cameras, 8)

    def test_reference_selection_and_time_independence(self):
        records = discovery.read_rows(self.complete / "discovery.csv")
        targets = discovery.read_rows(self.complete / "target-states.csv")
        for scene in self.scenarios:
            for stage in discovery.PASSES:
                group = [row for row in records if row["scenarioId"] == scene["scenarioId"] and row["passId"] == stage]
                actual = sorted((row for row in targets if row["scenarioId"] == scene["scenarioId"] and row["passId"] == stage),
                                key=lambda row: int(row["selectionRank"]))
                expected = [(row["sampleIndex"], row["selectionStratum"]) for row in actual]
                self.assertEqual(reference_selection(group, 4), expected)
                for row in group:
                    for field in ("featureCollectionMs", "inputHashMs", "validationMs"):
                        row[field] = "987654321.25"
                self.assertEqual(reference_selection(group, 4), expected)

    def test_refuses_existing_directory_and_changed_frozen_input(self):
        before = {path.name: digest(path) for path in self.complete.iterdir() if path.is_file()}
        result = subprocess.run(list(map(str, [*self.command, "--prepared-input-dir", self.prepared,
                                               "--output-dir", self.complete])), cwd=CONFIG.source, capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(before, {path.name: digest(path) for path in self.complete.iterdir() if path.is_file()})
        corrupt = self.work / "corrupt-prepared"
        shutil.copytree(self.subset, corrupt)
        with (corrupt / "inputs/camera-samples.csv").open("ab") as stream:
            stream.write(b"\n")
        failed = self.work / "failed-input"
        result = subprocess.run(list(map(str, [*self.command, "--prepared-input-dir", corrupt,
                                               "--output-dir", failed])), cwd=CONFIG.source, capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(json.loads((failed / "run-metadata.json").read_text(encoding="utf-8"))["status"], "failed")

    def test_rejects_incomplete_records_and_target_mismatch(self):
        output = self.work / "record-checks"
        shutil.copytree(self.complete, output)
        records = discovery.read_rows(output / "discovery.csv")
        write_rows(output / "discovery.csv", records[:-1])
        with self.assertRaises(ValueError):
            discovery.verify_discovery_outputs(output, self.scenarios, self.cameras, 4)
        write_rows(output / "discovery.csv", [*records[:-1], records[0]])
        with self.assertRaises(ValueError):
            discovery.verify_discovery_outputs(output, self.scenarios, self.cameras, 4)
        write_rows(output / "discovery.csv", records)
        targets = discovery.read_rows(output / "target-states.csv")
        targets[0]["replayInputHash"] = "1"
        write_rows(output / "target-states.csv", targets)
        with self.assertRaises(ValueError):
            discovery.verify_discovery_outputs(output, self.scenarios, self.cameras, 4)

    def test_source_change_marks_real_execution_failed(self):
        identity = discovery.source_identity()
        changed = dict(identity, sha256="changed-during-execution")
        output = self.work / "source-change"
        arguments = argparse.Namespace(output_dir=output, prepared_input_dir=self.subset,
                                       executable=CONFIG.executable, build_preset=CONFIG.preset, targets_per_pass=4)
        # 控制证据读取结果以覆盖变化分支而不修改共享仓库源码
        with patch.object(discovery, "source_identity", side_effect=[identity, changed]):
            self.assertEqual(discovery.discover(arguments), 1)
        metadata = json.loads((output / "run-metadata.json").read_text(encoding="utf-8"))
        self.assertEqual(metadata["status"], "failed")
        self.assertEqual(metadata["exitCode"], 0)
        self.assertTrue((output / "discovery.csv").exists())
        self.assertFalse((output / "target-states.csv").exists())
        self.assertTrue((output / "rejected-target-states.csv").exists())

    def test_zero_target_output_contract(self):
        # 合成记录只验证全组无目标的输出契约；不冒充固定协议的真实发现结果
        output = self.work / "zero-target-contract"
        shutil.copytree(self.complete, output)
        records = discovery.read_rows(output / "discovery.csv")
        for row in records:
            row.update(status="no_work", primaryWorkValue="0", selectionEligible="false", selectionExclusionReason="no_work")
        write_rows(output / "discovery.csv", records)
        coverage = discovery.read_rows(output / "target-coverage.csv")
        for row in coverage:
            row.update(eligibleCount="0", selectedCount="0", lowCount="0", middleCount="0", highCount="0",
                       coverageCount="0", missingCount="4", insufficiencyReason="no_eligible_candidates")
        write_rows(output / "target-coverage.csv", coverage)
        summary = discovery.read_rows(output / "discovery-summary.csv")
        summary[0].update(targetCount="0", insufficientGroupCount="30", targetStatus="targets_unavailable",
                          validRecordCount="0", noWorkRecordCount="1920", failedRecordCount="0")
        write_rows(output / "discovery-summary.csv", summary)
        (output / "target-states.csv").unlink()
        discovery.verify_discovery_outputs(output, self.scenarios, self.cameras, 4)
        (output / "target-states.csv").write_text("schemaVersion\n", encoding="utf-8")
        with self.assertRaises(ValueError):
            discovery.verify_discovery_outputs(output, self.scenarios, self.cameras, 4)


def main():
    global CONFIG
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--replay-verifier", type=Path, required=True)
    parser.add_argument("--preset", required=True)
    parser.add_argument("--output", type=Path, required=True)
    CONFIG = parser.parse_args()
    return 0 if unittest.TextTestRunner(verbosity=2).run(
        unittest.defaultTestLoader.loadTestsFromTestCase(DiscoveryTests)).wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
