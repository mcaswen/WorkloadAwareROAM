"""使用真实应用检查输入冻结、摘要与失败目录语义。"""

import argparse
import csv
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


CONFIG = None


def rows(path):
    with path.open(encoding="utf-8-sig", newline="") as stream:
        return list(csv.DictReader(stream))


def write_rows(path, values):
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(values[0]))
        writer.writeheader()
        writer.writerows(values)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class PreparationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = CONFIG.source.resolve()
        CONFIG.output.mkdir(parents=True, exist_ok=True)
        cls.work = Path(tempfile.mkdtemp(prefix="attempt-", dir=CONFIG.output))
        cls.scene_manifest = cls.root / "docs/parallel-roam/cpu-pilot-scenarios-v1.csv"
        cls.ready = cls.work / "ready"
        cls.command = [sys.executable, str(cls.root / "scripts/prepare_cpu_pilot.py"),
                       "--executable", str(CONFIG.executable), "--build-preset", CONFIG.preset]
        result = subprocess.run([*cls.command, "--output-dir", str(cls.ready)], cwd=cls.root,
                                capture_output=True, text=True, encoding="utf-8", errors="replace")
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        cls.metadata = json.loads((cls.ready / "run-metadata.json").read_text(encoding="utf-8"))
        cls.cameras = rows(cls.ready / "inputs/camera-samples.csv")

    def run_script(self, output, *extra):
        return subprocess.run([*self.command, "--output-dir", str(output), *map(str, extra)], cwd=self.root,
                              capture_output=True, text=True, encoding="utf-8", errors="replace")

    def run_app(self, output, *extra):
        return subprocess.run([str(CONFIG.executable), "--benchmark", "--profile", "cpu-pilot-inputs",
                               "--scenario-manifest", str(self.scene_manifest), "--output-dir", str(output),
                               *map(str, extra)], cwd=self.root, capture_output=True)

    def test_traceability_and_complete_inputs(self):
        metadata = self.metadata
        self.assertEqual(metadata["status"], "inputs_ready")
        self.assertEqual(metadata["dataPurpose"], "exploratory")
        self.assertEqual(metadata["scope"], "cpu_pilot_inputs_only")
        self.assertEqual(metadata["targetStatus"], "not_provided")
        self.assertEqual(len(self.cameras), 384)
        self.assertEqual(len(metadata["scenarios"]), 6)
        self.assertEqual(metadata["validatedInputSummary"]["backend"], CONFIG.backend)
        self.assertEqual(metadata["validatedInputSummary"]["buildConfiguration"], CONFIG.configuration)
        self.assertTrue(metadata["validatedInputSummary"]["compiler"])
        self.assertGreater(metadata["environment"]["logicalProcessorCount"], 0)
        self.assertEqual(metadata["command"][0], str(CONFIG.executable.resolve()))
        self.assertIn("--profile", metadata["command"])
        for identity in [*metadata["buildFiles"], *metadata["assets"].values(), *metadata["outputs"]]:
            self.assertEqual(digest(Path(identity["path"])), identity["sha256"])
        source = metadata["source"]
        self.assertIn("src/experiment/formal/FormalExperimentCamera.cpp", source["files"])
        serialized = json.dumps(source["files"], ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")
        self.assertEqual(hashlib.sha256(serialized).hexdigest(), source["sha256"])
        for scenario in metadata["scenarios"]:
            camera_rows = [row for row in self.cameras if row["scenarioId"] == scenario["scenarioId"]]
            self.assertEqual({int(row["sampleIndex"]) for row in camera_rows}, set(range(64)))
            self.assertEqual(scenario["serialWorkerCount"], "1")
            self.assertEqual(scenario["parallelWorkerCount"], "8")
            for name in ("mergeScoreParallelMinimum", "splitScoreParallelMinimum", "splitTopologyParallelMinimum",
                         "mergeTopologyParallelMinimum", "meshParallelMinimum"):
                self.assertEqual(scenario[name], "0")

    def test_frozen_subset_and_five_targets(self):
        selected = [row for row in self.cameras if row["scenarioId"] == "test129-a-b512"]
        camera_path = self.work / "subset cameras.csv"
        write_rows(camera_path, selected)
        target = selected[1]
        targets = [{"schemaVersion": "1", "dataPurpose": "exploratory", "scenarioId": target["scenarioId"],
                    "passId": stage, "sampleIndex": "1", "selectionStratum": "low", "selectionRank": "0",
                    "selectionSeed": "20260830", "primaryWorkValue": "1", "featureVector": "1;0.5",
                    "replayInputHash": "123", "selectionFeatureHash": "456", "analysisSplit": "train",
                    "cameraPoseHash": target["cameraPoseHash"], "viewInputHash": target["viewInputHash"]}
                   for stage in ("mergeScore", "mergeTopology", "splitScore", "splitTopology", "meshEmit")]
        target_path = self.work / "targets.csv"
        write_rows(target_path, targets)
        output = self.work / "subset"
        result = self.run_script(output, "--scenario-id", "test129-a-b512", "--camera-manifest", camera_path,
                                 "--target-manifest", target_path)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(rows(output / "inputs/camera-samples.csv"), selected)
        metadata = json.loads((output / "run-metadata.json").read_text(encoding="utf-8"))
        self.assertEqual(metadata["targetStatus"], "references_validated")
        self.assertEqual(metadata["validatedInputSummary"]["targetCount"], "5")
        self.assertEqual(len(metadata["originalManifests"]), 3)
        self.assertEqual(metadata["scope"], "cpu_pilot_inputs_only")

    def test_missing_rows_and_failure_status(self):
        camera_path = self.work / "missing.csv"
        selected = [row for row in self.cameras if row["scenarioId"] == "test129-a-b512"]
        write_rows(camera_path, selected[:-1])
        output = self.work / "missing"
        result = self.run_script(output, "--scenario-id", "test129-a-b512", "--camera-manifest", camera_path)
        self.assertNotEqual(result.returncode, 0)
        metadata = json.loads((output / "run-metadata.json").read_text(encoding="utf-8"))
        self.assertEqual(metadata["status"], "failed")
        self.assertEqual(rows(output / "inputs/input-summary.csv")[0]["status"], "failed")
        self.assertIn(b"Incomplete camera manifest", (output / "prepare.log").read_bytes())

    def test_corrupted_asset_rejected_by_sha256(self):
        scenario_rows = rows(self.scene_manifest)
        scenario = next(row for row in scenario_rows if row["scenarioId"] == "test129-a-b512")
        asset = bytearray((self.root / scenario["heightMapPath"]).read_bytes())
        asset[-1] ^= 1
        changed_asset = self.work / "changed.pgm"
        changed_asset.write_bytes(asset)
        scenario["heightMapPath"] = str(changed_asset)
        changed_manifest = self.work / "changed-asset.csv"
        write_rows(changed_manifest, [scenario])
        output = self.work / "bad-asset"
        result = self.run_script(output, "--scenario-manifest", changed_manifest)
        self.assertNotEqual(result.returncode, 0)
        metadata = json.loads((output / "run-metadata.json").read_text(encoding="utf-8"))
        self.assertEqual(metadata["status"], "failed")
        self.assertIn("SHA-256", metadata["error"])
        self.assertFalse((output / "inputs").exists())

    def test_existing_directory_never_overwritten(self):
        before = {str(path): digest(path) for path in self.ready.rglob("*") if path.is_file()}
        result = self.run_script(self.ready)
        self.assertNotEqual(result.returncode, 0)
        after = {str(path): digest(path) for path in self.ready.rglob("*") if path.is_file()}
        self.assertEqual(before, after)
        bare_before = digest(self.ready / "inputs/input-summary.csv")
        self.assertNotEqual(self.run_app(self.ready / "inputs").returncode, 0)
        self.assertEqual(bare_before, digest(self.ready / "inputs/input-summary.csv"))
        interrupted = self.work / "interrupted"
        interrupted.mkdir()
        marker = interrupted / "run-metadata.json"
        marker.write_text('{"status":"preparing"}\n', encoding="utf-8")
        self.assertNotEqual(self.run_script(interrupted).returncode, 0)
        self.assertEqual(json.loads(marker.read_text())["status"], "preparing")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--preset", required=True)
    parser.add_argument("--backend", required=True)
    parser.add_argument("--configuration", required=True)
    parser.add_argument("--output", type=Path, required=True)
    CONFIG, remaining = parser.parse_known_args()
    unittest.main(argv=[sys.argv[0], *remaining])
