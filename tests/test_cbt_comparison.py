"""检查逐点差和延迟GPU样本归属，不重复运行图形算法。"""
import unittest
from experiment_infrastructure.cbt_report import pointwise_excess, gpu_samples, nearest_count_reference


class CbtComparisonTests(unittest.TestCase):
    def identity(self):
        return dict(terrain="terrain", sourceHash="source", sampleHash="Q", sampleCount=2,
            poseHash="view", projectionHash="ZO")

    def test_excess_is_not_difference_of_maxima(self):
        identity = self.identity()
        result = pointwise_excess(identity, identity, [0, 10], [9, 8])
        self.assertEqual(result["Dmax"], 2)
        self.assertEqual(result["witnessOrdinal"], 1)

    def test_mismatched_reference_or_visibility_rejected(self):
        left = self.identity()
        right = {**left, "sampleHash": "different"}
        with self.assertRaises(ValueError):
            pointwise_excess(left, right, [1, 2], [1, 2])
        with self.assertRaises(ValueError):
            pointwise_excess(left, left, [-1, 2], [1, 2])

    def test_gpu_sample_dedup_and_warmup_use_source_generation(self):
        rows = [dict(frame=str(i), resourceGeneration="1", topologyGeneration=str(i + 1),
            timingGeneration=str(sample), computeMs=str(cost))
            for i, sample, cost in [(0, 0, 0), (1, 1, 90), (2, 1, 90), (3, 3, 2), (4, 3, 2)]]
        self.assertEqual(gpu_samples(rows, "timingGeneration", "computeMs", 2), [2])

    def test_invalid_nearest_count_is_not_replaced_by_valid_alternative(self):
        candidates = [dict(N=60, parameter=8, valid=False), dict(N=35, parameter=16, valid=True)]
        reference = nearest_count_reference(dict(N=61), candidates)
        self.assertEqual(reference["N"], 60)
        self.assertFalse(reference["valid"])


if __name__ == "__main__":
    unittest.main()
