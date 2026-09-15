"""独立进程归约不能混入视觉采集、另一版本或无效质量。"""
import copy
import unittest
from experiment_infrastructure.process_statistics import aggregate, configuration_key


def observation(name, cost, mode="timing"):
    return dict(id=name, path=name, taskId="task", sourceId="source", cpuOnly=False,
                binary=dict(sha256="binary"), case=dict(id="case", terrain="terrain",
                algorithm="dod", workers=8, budget=50000), executionDevice="cpu",
                cpuTimeMeaning="complete-update", status="ok", mode=mode,
                frames=[dict(cpuMs=cost, warmup=False, faces=50000)],
                groups=dict(warm=dict(cpuMs=cost, count=1)), totals={})


class ProcessStatisticsTests(unittest.TestCase):
    def test_visual_cost_is_not_an_independent_timing_repeat(self):
        runs = [observation("one", 1), observation("two", 2), observation("three", 6),
                observation("visual", 100, "visual")]
        data = dict(runs=runs, quality=[])
        result = aggregate(data)
        config = result["configurations"][0]
        self.assertEqual(config["processCount"], 3)
        self.assertEqual(config["groups"]["warm"]["cpuMs"]["values"], [1, 2, 6])
        self.assertEqual(config["groups"]["warm"]["cpuMs"]["mean"], 3)
        self.assertEqual(config["visualRuns"], ["visual"])

    def test_binary_and_source_identity_do_not_merge(self):
        first = observation("one", 1)
        second = copy.deepcopy(first)
        second["binary"]["sha256"] = "other"
        self.assertNotEqual(configuration_key(first), configuration_key(second))
        second = copy.deepcopy(first)
        second["sourceId"] = "other"
        self.assertNotEqual(configuration_key(first), configuration_key(second))

    def test_partial_quality_keeps_failure_and_n_but_no_quality_value(self):
        run = observation("visual", 100, "visual")
        quality = dict(run="visual", path="quality", frames=[
            dict(frame=3, faces=49999, status="invalid-quality",
                 result=dict(screenMax=.1, heightMax=.2, ambiguous=1))])
        result = aggregate(dict(runs=[run], quality=[quality]))
        row = result["quality"][0]
        self.assertEqual(row["N"], 49999)
        self.assertEqual(row["ambiguous"], 1)
        self.assertIsNone(row["Emax"])
        self.assertIsNone(row["Hmax"])
        self.assertFalse(row["valid"])

    def test_failed_process_is_not_silently_counted(self):
        run = observation("failed", 5)
        run["status"] = "timeout"
        result = aggregate(dict(runs=[run], quality=[]))
        self.assertEqual(result["configurations"][0]["processCount"], 0)
        self.assertEqual(result["failures"][0]["status"], "timeout")


if __name__ == "__main__":
    unittest.main()
