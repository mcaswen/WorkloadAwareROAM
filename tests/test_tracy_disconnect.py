"""用真实采集器断连验证会话拒绝完整结果，默认没有工具时跳过。"""

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from profiling.process import stop_process


class TracyDisconnectTests(unittest.TestCase):
    def test_missing_collector_has_connection_deadline(self):
        fixture = os.environ.get("ROAM_TRACY_TEST_EXE")
        if not fixture:
            self.skipTest("需要 Tracy 夹具")
        with tempfile.TemporaryDirectory(prefix="roam-no-collector-") as folder:
            windows = Path(folder) / "windows.csv"
            env = dict(os.environ, ROAM_PROFILE_BACKEND="tracy", ROAM_PROFILE_WINDOWS=str(windows), TRACY_NO_EXIT="0")
            env.pop("ROAM_PROFILE_WAIT", None)
            result = subprocess.run([fixture, "1000"], env=env, capture_output=True, text=True, timeout=18)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("connection unavailable", result.stderr)
            self.assertFalse(windows.exists())

    def test_connection_loss_never_completes_window(self):
        capture = os.environ.get("ROAM_TRACY_TEST_CAPTURE")
        fixture = os.environ.get("ROAM_TRACY_TEST_EXE")
        if not capture or not fixture:
            self.skipTest("需要真实 Tracy 工具与夹具")
        with tempfile.TemporaryDirectory(prefix="roam-disconnect-") as folder:
            root = Path(folder)
            collector = subprocess.Popen([capture, "-a", "127.0.0.1", "-o", str(root / "partial.tracy")],
                                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
            target = None
            try:
                env = dict(os.environ, ROAM_PROFILE_BACKEND="tracy", ROAM_PROFILE_WINDOWS=str(root / "windows.csv"),
                           ROAM_PROFILE_HOLD_MS="1000", TRACY_NO_EXIT="0")
                target = subprocess.Popen([fixture, "1000"], env=env, stdout=subprocess.PIPE,
                                          stderr=subprocess.PIPE, text=True, start_new_session=True)
                deadline = time.monotonic() + 16
                while not (root / "windows.csv").exists() and target.poll() is None and time.monotonic() < deadline:
                    time.sleep(.01)
                self.assertTrue((root / "windows.csv").exists(), "未建立连接会话")
                stop_process(collector)
                _, stderr = target.communicate(timeout=6)
                self.assertNotEqual(target.returncode, 0)
                self.assertIn("connection lost", stderr)
                self.assertNotIn("# complete", (root / "windows.csv").read_text())
            finally:
                if target is not None:
                    stop_process(target)
                stop_process(collector)


if __name__ == "__main__":
    unittest.main()
