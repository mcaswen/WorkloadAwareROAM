"""校验跨线程关联与不完整轨迹，示例只用于解析而非自然性能证据。"""

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from profiling.tracy_report import summarize_tracy


HEADER = "name,src_file,src_line,ns_since_start,exec_time_ns,thread,value\n"
ROWS = ["profile.frame,a.cpp,1,100,100,1,0:0",
        "gtp.dispatch,a.cpp,2,110,80,1,receiver_stage",
        "pool.enqueue,a.cpp,3,115,5,1,",
        "pool.wait,a.cpp,4,120,65,1,",
        "gtp.task,b.cpp,1,125,50,2,receiver_stage/0",
        "gtp.fit,b.cpp,2,130,30,2,",
        "profile.complete,a.cpp,5,210,1,1,"]


class TracyReportTests(unittest.TestCase):
    def test_same_thread_self_and_worker_association(self):
        result = summarize_tracy(HEADER + "\n".join(ROWS) + "\n", 1)
        self.assertEqual(result["functions"]["gtp.dispatch"]["self_ns"], 10)
        self.assertEqual(result["functions"]["gtp.task"]["self_ns"], 20)
        phase = result["frames"][0]["phases"][0]
        self.assertEqual(phase["threads"], ["2"])
        self.assertEqual(phase["after_last_task_ns"], 15)

    def test_reject_incomplete_or_crossing_intervals(self):
        variants = [ROWS[:-1], ROWS + ["broken"],
                    [row.replace("125,50", "125,100") for row in ROWS],
                    ROWS + ["gtp.fit,b.cpp,2,180,15,1,"]]
        for rows in variants:
            with self.assertRaises(ValueError):
                summarize_tracy(HEADER + "\n".join(rows) + "\n", 1)


if __name__ == "__main__":
    unittest.main()
