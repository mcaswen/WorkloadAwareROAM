"""采集入口的拒绝条件必须在启动昂贵工具和覆盖数据之前生效。"""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class CpuProfileCliTests(unittest.TestCase):
    def test_family_repetition_rejected_before_input_or_tool_access(self):
        with tempfile.TemporaryDirectory() as name:
            output = Path(name) / "result"
            result = subprocess.run([sys.executable, str(ROOT / "scripts/run_cpu_profile.py"), "perf",
                                     "--mode", "dod", "--replays", "2", "--executable", "missing",
                                     "--snapshot", "missing", "--output", str(output)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)
            self.assertIn("必须为 1", result.stderr)
            self.assertFalse(output.exists())

    def test_existing_output_never_replaced(self):
        with tempfile.TemporaryDirectory() as name:
            output = Path(name)
            marker = output / "preserve"
            marker.write_text("unchanged")
            result = subprocess.run([sys.executable, str(ROOT / "scripts/run_cpu_profile.py"), "perf",
                                     "--executable", sys.executable, "--snapshot", str(marker),
                                     "--output", str(output)], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(marker.read_text(), "unchanged")
            self.assertEqual(list(output.iterdir()), [marker])


if __name__ == "__main__":
    unittest.main()
