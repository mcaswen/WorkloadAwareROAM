"""用真实应用进程验证 CPU 配对、产物追溯、失败审计和坏数据拒绝。"""

import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

CONFIG = None


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_rows(path, rows, fields):
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def refresh_output_digest(directory, filename):
    path = directory / "run-metadata.json"
    metadata = json.loads(path.read_text(encoding="utf-8"))
    saved = next(item for item in metadata["outputs"] if Path(item["path"]).name == filename)
    saved.update(bytes=(directory / filename).stat().st_size, sha256=digest(directory / filename))
    path.write_text(json.dumps(metadata, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


class PairingTests(unittest.TestCase):
    @classmethod
    def command(cls, command, expected=0):
        result = subprocess.run(list(map(str, command)), cwd=CONFIG.source, capture_output=True,
                                text=True, encoding="utf-8", errors="replace", timeout=600,
                                env={**os.environ, "PYTHONUTF8": "1"})
        if result.returncode != expected:
            raise RuntimeError(f"退出码 {result.returncode}，预期 {expected}\n{result.stdout}\n{result.stderr}")
        return result

    @classmethod
    def setUpClass(cls):
        global artifacts, validation, runner
        sys.path.insert(0, str(CONFIG.source / "scripts"))
        import cpu_pilot_artifacts as artifacts
        from formal_experiments import input_validation as validation
        import run_cpu_pilot as runner
        CONFIG.output.mkdir(parents=True, exist_ok=True)
        cls.work = Path(tempfile.mkdtemp(prefix="attempt-", dir=CONFIG.output))
        cls.prepared = cls.work / "prepared"
        cls.discovered = cls.work / "discovered"
        common = ["--executable", CONFIG.executable, "--build-preset", CONFIG.preset]
        cls.command([sys.executable, CONFIG.source / "scripts/prepare_cpu_pilot.py", *common,
                     "--output-dir", cls.prepared, "--scenario-id", "test129-a-b4096"])
        cls.command([sys.executable, CONFIG.source / "scripts/discover_cpu_pilot.py", *common,
                     "--prepared-input-dir", cls.prepared, "--output-dir", cls.discovered])
        cls.base = [sys.executable, CONFIG.source / "scripts/run_cpu_pilot.py", *common,
                    "--prepared-input-dir", cls.prepared, "--discovery-dir", cls.discovered,
                    "--pass-warmups", "1", "--pass-repeats", "6", "--pass-workers", "2"]
        cls.first = cls.work / "first"
        cls.second = cls.work / "second"
        cls.command([*cls.base, "--output-dir", cls.first])
        cls.command([*cls.base, "--output-dir", cls.second])
        cls.target = next(row for row in artifacts.read_rows(cls.discovered / "target-states.csv")
                          if row["passId"] == "meshEmit" and row["selectionRank"] == "2")
        cls.filtered = cls.work / "filtered"
        cls.command([*cls.base, "--output-dir", cls.filtered, "--scenario-id", "test129-a-b4096",
                     "--pass-id", "meshEmit", "--sample-index", cls.target["sampleIndex"]])

    def copy_attempt(self, name):
        destination = self.work / name
        shutil.copytree(self.first, destination)
        return destination

    def test_independent_processes_and_subset_keep_identity(self):
        first = validation.load_pair_attempt(self.first)
        second = validation.load_pair_attempt(self.second)
        self.assertNotEqual(first["runId"], second["runId"])
        self.assertGreater(first["metadata"]["processPid"], 0)
        self.assertEqual(first["metadata"]["source"], first["metadata"]["sourceAfter"])
        for key, rows in first["groups"].items():
            self.assertEqual({(row["replayInputHash"], row["resultHash"]) for row in rows},
                             {(row["replayInputHash"], row["resultHash"]) for row in second["groups"][key]})
        filtered = validation.load_pair_attempt(self.filtered)
        self.assertEqual(len(filtered["groups"]), 1)
        self.assertEqual(filtered["targets"], [self.target])
        self.assertEqual(next(iter(filtered["groups"].values()))[0]["selectionRank"], 2)
        self.assertEqual(filtered["summary"]["warmupRowCount"], "3")
        self.assertEqual(filtered["summary"]["measuredRowCount"], "18")
        from formal_experiments.crossover_analysis import analyze_attempts
        with self.assertRaises(ValueError):
            analyze_attempts([first, validation.load_pair_attempt(self.copy_attempt("duplicate-run"))])

    def test_existing_directory_is_untouched(self):
        before = {path.relative_to(self.first).as_posix(): digest(path) for path in self.first.rglob("*") if path.is_file()}
        self.command([*self.base, "--output-dir", self.first], expected=1)
        after = {path.relative_to(self.first).as_posix(): digest(path) for path in self.first.rglob("*") if path.is_file()}
        self.assertEqual(before, after)
        empty = self.work / "existing-empty"
        empty.mkdir()
        self.command([*self.base, "--output-dir", empty], expected=1)
        self.assertEqual(list(empty.iterdir()), [])

    def test_raw_digest_and_semantic_corruption_rejected(self):
        for mutation in ("missing", "duplicate", "nan", "result", "extra_action", "warmup"):
            copied = self.copy_attempt("bad-" + mutation)
            filename = "pair-samples.csv"
            rows = artifacts.read_rows(copied / filename)
            fields = list(rows[0])
            if mutation == "missing":
                rows.pop()
            elif mutation == "duplicate":
                rows[-1] = rows[0].copy()
            elif mutation == "nan":
                rows[0]["wallMs"] = "nan"
            elif mutation == "result":
                rows[0]["resultHash"] = str(int(rows[0]["resultHash"]) + 1)
            elif mutation == "extra_action":
                rows[0]["requestedAction"] = "serialFull"
            else:
                rows[0]["isWarmup"] = "true"
            write_rows(copied / filename, rows, fields)
            with self.assertRaises(ValueError):
                validation.load_pair_attempt(copied)
            # 更新摘要后仍须被结构或语义核验拒绝，不能只靠摘要发现损坏
            refresh_output_digest(copied, filename)
            with self.assertRaises(ValueError):
                validation.load_pair_attempt(copied)
        reordered = self.copy_attempt("reordered")
        rows = artifacts.read_rows(reordered / "pair-samples.csv")
        write_rows(reordered / "pair-samples.csv", list(reversed(rows)), list(rows[0]))
        refresh_output_digest(reordered, "pair-samples.csv")
        from formal_experiments.crossover_analysis import analyze_attempts
        self.assertEqual(analyze_attempts([validation.load_pair_attempt(self.first)])["pairs"],
                         analyze_attempts([validation.load_pair_attempt(reordered)])["pairs"])

    def test_missing_or_self_inconsistent_provenance_rejected(self):
        for mutation in ("source", "build", "source-hash", "time", "wall", "environment"):
            copied = self.copy_attempt("bad-provenance-" + mutation)
            path = copied / "run-metadata.json"
            metadata = json.loads(path.read_text(encoding="utf-8"))
            if mutation == "source":
                metadata["source"] = metadata["sourceAfter"] = {}
            elif mutation == "build":
                metadata["buildFiles"] = metadata["buildFilesAfter"] = []
            elif mutation == "source-hash":
                metadata["source"]["sha256"] = metadata["sourceAfter"]["sha256"] = "0" * 64
            elif mutation == "time":
                metadata.pop("processStartedUtc")
            elif mutation == "wall":
                metadata["processWallSeconds"] = float("nan")
            else:
                metadata["environment"] = metadata["environmentAfter"] = {}
            path.write_text(json.dumps(metadata, ensure_ascii=False), encoding="utf-8")
            with self.assertRaises(ValueError):
                validation.load_pair_attempt(copied)

    def test_source_digest_and_actual_rebuild_mismatch(self):
        damaged = self.work / "damaged-discovery"
        shutil.copytree(self.discovered, damaged)
        target_path = damaged / "target-states.csv"
        rows = artifacts.read_rows(target_path)
        rows[0]["replayInputHash"] = str(int(rows[0]["replayInputHash"]) + 1)
        changed = rows[0]
        write_rows(target_path, rows, list(rows[0]))
        with self.assertRaises(ValueError):
            artifacts.load_discovered_inputs(damaged, self.prepared)
        # 构造结构自洽但与真实状态不符的输入，让实际 C++ 重建而不是脚本摘要来拒绝
        discovery_rows = artifacts.read_rows(damaged / "discovery.csv")
        match = next(row for row in discovery_rows if validation.target_key(row) == validation.target_key(changed))
        match["replayInputHash"] = changed["replayInputHash"]
        write_rows(damaged / "discovery.csv", discovery_rows, list(discovery_rows[0]))
        refresh_output_digest(damaged, "target-states.csv")
        refresh_output_digest(damaged, "discovery.csv")
        command = self.base.copy()
        command[command.index("--discovery-dir") + 1] = damaged
        destination = self.work / "rebuild-mismatch"
        self.command([*command, "--output-dir", destination], expected=1)
        metadata = json.loads((destination / "run-metadata.json").read_text(encoding="utf-8"))
        self.assertEqual(metadata["status"], "failed")
        self.assertGreater(metadata["processPid"], 0)
        self.assertFalse((destination / "pair-samples.csv").exists())
        self.assertIn("Frozen target identity mismatch", (destination / "pair.log").read_text(encoding="utf-8"))

    def test_input_changed_after_process_cannot_publish(self):
        prepared = self.work / "changed-input"
        shutil.copytree(self.prepared, prepared)
        output = self.work / "changed-during-attempt"
        arguments = argparse.Namespace(output_dir=output, prepared_input_dir=prepared, discovery_dir=self.discovered,
            executable=CONFIG.executable, build_preset=CONFIG.preset, scenario_id=["test129-a-b4096"],
            pass_id="meshEmit", sample_index=int(self.target["sampleIndex"]), pass_warmups=0, pass_repeats=1, pass_workers=2)
        original = runner.validate_pairing_outputs
        def change_after_validation(*args):
            result = original(*args)
            with (prepared / "inputs/camera-samples.csv").open("ab") as stream:
                stream.write(b"\n")
            return result
        with patch.object(runner, "validate_pairing_outputs", side_effect=change_after_validation):
            self.assertEqual(runner.run_pairing(arguments), 1)
        metadata = json.loads((output / "run-metadata.json").read_text(encoding="utf-8"))
        self.assertEqual(metadata["status"], "failed")
        self.assertTrue((output / "rejected-pair-samples.csv").exists())
        self.assertFalse((output / "pair-samples.csv").exists())

    def test_failed_attempt_only_generates_audit(self):
        failed = self.copy_attempt("failed-audit")
        path = failed / "run-metadata.json"
        metadata = json.loads(path.read_text(encoding="utf-8"))
        metadata.update(status="interrupted", error="实际进程被中断，未完成")
        path.write_text(json.dumps(metadata, ensure_ascii=False), encoding="utf-8")
        output = self.work / "failed-analysis"
        self.command([sys.executable, CONFIG.source / "scripts/analyze_pass_crossover.py",
                      "--run-dir", failed, "--output-dir", output])
        self.assertEqual(artifacts.read_rows(output / "paired-targets.csv"), [])
        self.assertIn("实际进程被中断", (output / "report.md").read_text(encoding="utf-8"))

    def test_terminated_child_preserves_incomplete_attempt(self):
        output = self.work / "terminated-child"
        command = self.base.copy()
        command[command.index("--pass-repeats") + 1] = "10000"
        child_pid = None
        with (self.work / "termination-parent.log").open("wb") as log:
            parent = subprocess.Popen(list(map(str, [*command, "--output-dir", output])), cwd=CONFIG.source,
                                      stdout=log, stderr=subprocess.STDOUT, env={**os.environ, "PYTHONUTF8": "1"})
            try:
                deadline = time.monotonic() + 45
                while time.monotonic() < deadline and parent.poll() is None:
                    try:
                        metadata = json.loads((output / "run-metadata.json").read_text(encoding="utf-8"))
                        child_pid = metadata.get("processPid")
                        if child_pid and (output / "records/pair-samples.pending.csv").exists():
                            break
                    except (OSError, ValueError):
                        pass
                    time.sleep(.05)
                self.assertIsNotNone(child_pid)
                self.assertIsNone(parent.poll())
                if os.name == "nt":
                    subprocess.run(["taskkill", "/PID", str(child_pid), "/F"], capture_output=True, check=True)
                else:
                    import signal
                    os.kill(child_pid, signal.SIGTERM)
                self.assertEqual(parent.wait(timeout=30), 1)
            finally:
                if parent.poll() is None:
                    if child_pid and os.name == "nt":
                        subprocess.run(["taskkill", "/PID", str(child_pid), "/F"], capture_output=True, check=False)
                    parent.terminate()
                    parent.wait(timeout=10)
        metadata = json.loads((output / "run-metadata.json").read_text(encoding="utf-8"))
        self.assertEqual(metadata["status"], "failed")
        self.assertFalse((output / "pair-samples.csv").exists())
        self.assertTrue((output / "pair-samples.pending.csv").exists())


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--preset", required=True)
    parser.add_argument("--output", type=Path, required=True)
    CONFIG, remaining = parser.parse_known_args()
    CONFIG.source = CONFIG.source.resolve()
    CONFIG.executable = CONFIG.executable.resolve()
    CONFIG.output = CONFIG.output.resolve()
    unittest.main(argv=[sys.argv[0], *remaining])
