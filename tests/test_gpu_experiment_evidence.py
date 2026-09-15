"""覆盖共享观测层的代次、缺测、适用维度与配对边界。"""
import unittest
from experiment_infrastructure.gpu_observations import samples, summarize
from experiment_infrastructure.suite import case_variants
from experiment_infrastructure.quality import valid_quality, pointwise_excess


class GpuEvidenceTests(unittest.TestCase):
    def rows(self):
        return [dict(frame=str(i), resourceGeneration="1", topologyGeneration=str(i + 1),
                     timingGeneration=str(generation), computeMs=str(cost))
                for i, generation, cost in [(0, 0, 0), (1, 1, 2), (2, 1, 2), (3, 3, 4)]]

    def test_delayed_observation_is_attributed_and_deduplicated(self):
        result = samples(self.rows(), "timingGeneration", ("computeMs",))
        self.assertEqual([s["frame"] for s in result["samples"]], [0, 2])
        self.assertEqual(result["duplicateObservations"], 1)
        self.assertEqual(result["unobservedSourceFrames"], 2)

    def test_conflicting_duplicate_is_not_overwritten(self):
        rows = self.rows()
        rows[2]["computeMs"] = "99"
        with self.assertRaises(ValueError):
            samples(rows, "timingGeneration", ("computeMs",))

    def test_group_uses_source_opportunity_not_arrival(self):
        result = summarize(self.rows(), {"warm": [{"frame": 2}, {"frame": 3}]})
        compute = result["groups"]["warm"]["compute"]
        self.assertEqual(compute["computeMs"]["mean"], 4)
        self.assertEqual(compute["sampleCount"], 1)
        self.assertEqual(compute["unobserved"], 1)

    def test_cpu_dimensions_do_not_multiply_gpu_cases(self):
        base = dict(id="base", budget=50000, heightPolicy="immutable", flipRecovery=True)
        cases = case_variants(base, [50000, 200000], [1, 8],
                              ["classic", "dod", "transactional", "cbt"],
                              ["fixed64", "scaled"], [16, 8, 4], [524288])
        self.assertEqual(len(cases), 2 + 4 + 8 + 3)
        gpu = [case for case in cases if case["algorithm"] == "cbt"]
        self.assertEqual(len(gpu), 3)
        self.assertTrue(all(case["backend"] == "d3d12" and not case["flipRecovery"] for case in gpu))
        self.assertTrue(all(case["workers"] == 1 for case in cases if case["algorithm"] == "classic"))

    def test_invalid_partial_quality_is_not_valid(self):
        item = dict(status="ok", result=dict(screenMax=1, ambiguous=1))
        self.assertFalse(valid_quality(item))
        item["result"]["ambiguous"] = 0
        self.assertTrue(valid_quality(item))

    def test_distinct_budget_does_not_change_pointwise_domain(self):
        identity = dict(terrain="t", sourceHash="s", sampleHash="q", sampleCount=2,
                        poseHash="p", projectionHash="z")
        result = pointwise_excess({**identity, "budget": 50000},
                                 {**identity, "budget": 200000}, [1, 5], [3, 4])
        self.assertEqual(result["Dmax"], 1)


if __name__ == "__main__":
    unittest.main()
