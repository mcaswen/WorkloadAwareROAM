"""用本地控制端验证 C++ 会话，不依赖 perf 权限和采样噪声。"""

import os
from pathlib import Path
import select
import subprocess
import sys
import tempfile
import threading
import time
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from profiling.report import read_windows


class ProfileSessionTests(unittest.TestCase):
    def exchange(self, response, split_null=False):
        executable = os.environ.get("ROAM_SESSION_TEST_EXE")
        if not executable:
            self.skipTest("需要已构建的会话夹具")
        with tempfile.TemporaryDirectory(prefix="roam-session-") as name:
            directory = Path(name)
            control, ack = directory / "control", directory / "ack"
            os.mkfifo(control)
            os.mkfifo(ack)
            cf, af = os.open(control, os.O_RDWR | os.O_NONBLOCK), os.open(ack, os.O_RDWR | os.O_NONBLOCK)
            stop = threading.Event()
            requests = []
            def server():
                pending = b""
                while not stop.is_set():
                    if not select.select([cf], [], [], .05)[0]:
                        continue
                    pending += os.read(cf, 128)
                    while b"\n" in pending:
                        line, pending = pending.split(b"\n", 1)
                        requests.append(line)
                        os.write(af, response)
                        if split_null:
                            time.sleep(.001)
                            os.write(af, b"\x00")
            thread = threading.Thread(target=server)
            thread.start()
            try:
                env = dict(os.environ, ROAM_PERF_CONTROL=str(control), ROAM_PERF_ACK=str(ack),
                           ROAM_PROFILE_WINDOWS=str(directory / "windows.csv"))
                result = subprocess.run([executable, "1000"], capture_output=True, text=True, env=env, timeout=7)
                windows = (directory / "windows.csv").read_text()
                return result, requests[:], windows
            finally:
                stop.set()
                thread.join(timeout=2)
                os.close(cf)
                os.close(af)

    def test_plain_and_fragmented_null_ack(self):
        for split in (False, True):
            result, requests, windows = self.exchange(b"ack\n", split)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(requests, [b"enable", b"disable"])
            self.assertEqual(len(read_windows(windows, 1)), 1)

    def test_invalid_ack_retains_incomplete_window(self):
        result, _, windows = self.exchange(b"bad\n\x00")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("protocol mismatch", result.stderr)
        with self.assertRaises(ValueError):
            read_windows(windows)

    def test_no_ack_is_bounded_failure(self):
        result, _, windows = self.exchange(b"")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("timed out", result.stderr)
        self.assertNotIn("# complete", windows)


if __name__ == "__main__":
    unittest.main()
