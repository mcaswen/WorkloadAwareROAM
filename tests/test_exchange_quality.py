"""手算反例与几何边界；不以生产 Accepts 或待测总汇作 oracle。"""

import sys
from pathlib import Path
import unittest
from fractions import Fraction as F
from collections import Counter

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from transactional_exchange_quality import (cap_test, sum_interval, screen_errors, combine,
                                           summarize, source_height, height_at, closed_population,
                                           geometry, evaluate_side, inspect_batch, Unknown)


class ExchangeQualityTests(unittest.TestCase):
    def test_pointwise_threshold(self):
        self.assertTrue(cap_test(F(1, 16), F(1, 4), F(1, 4)))
        self.assertFalse(cap_test(F(1, 16), F(1, 4)+F(1, 1000000), F(1, 4)))
        self.assertTrue(cap_test(F(4), F(3), F(1)))
        self.assertFalse(cap_test(F(4), F(5), F(1)))

    def test_fixed_height_envelope(self):
        value, initial, target = F(9), F(9), F(1)
        for proposed in (F(4), F(6), F(1, 4), F(1), F(2), F(1, 2)):
            if cap_test(value, proposed, target):
                value = proposed
            self.assertLessEqual(value, max(initial, target))
        self.assertEqual(value, F(1, 2))

    def test_exact_sum_enclosure(self):
        values = [F(1, 3), F(-1, 7), F(1, 11)]
        lower, upper = sum_interval(values)
        self.assertLessEqual(lower, sum(values))
        self.assertGreaterEqual(upper, sum(values))
        self.assertEqual(sum_interval([F(0)]), (0, 0))

    @staticmethod
    def point(sid, a0, a1, u0=0, u1=0, visible=True):
        return dict(id=sid, visible=visible, a0=F(a0), a1=F(a1), u0=F(u0), u1=F(u1),
                    oldHeight=F(0), newHeight=F(0))

    def test_potential_does_not_replace_pointwise(self):
        result = summarize({0: self.point(0, 9, 4), 1: self.point(1, 0, 4)}, F(1))
        op = result["operatingPoints"][-1]
        self.assertEqual(op["deltaPsiLower"], 2)
        self.assertFalse(op["screenPass"])
        self.assertFalse(op["accepted"])

    def test_invisible_donor_still_checked(self):
        records = {0: self.point(0, 4, 1), 1: self.point(1, 0, 0, 0, 1, False)}
        op = summarize(records, F(1))["operatingPoints"][0]
        self.assertTrue(op["screenPass"])
        self.assertFalse(op["heightPass"])
        self.assertEqual(op["heightWitness"], [1])

    def test_free_and_shared_interface(self):
        shared = self.point(1, 0, 0)
        receiver = {0: self.point(0, 4, 1), 1: shared}
        donor = {1: shared, 2: self.point(2, 0, 0)}
        work = Counter()
        self.assertEqual(len(combine([receiver], work)), 2)
        result = combine([receiver, donor], work)
        self.assertEqual(len(result), 3)
        self.assertEqual(work["sharedSamples"], 1)
        donor[1] = {**shared, "newHeight": F(1)}
        with self.assertRaises(Unknown):
            combine([receiver, donor], work)

    def test_near_plane_unknown(self):
        config = dict(terrainSize=F(1), width=100, height=100, zeroToOne=True,
                      matrix=list(map(F, [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, .5, 0, 1, 0, 1])))
        with self.assertRaisesRegex(Unknown, "near_plane"):
            screen_errors(config, (F(1, 2), F(1, 2)), F(0), F(0), F(-2))

    def test_source_vertex_not_neighbourhood_guarantee(self):
        # 双线性 xy 的三个顶点均在源上，单三角线性面内部仍有残差
        source = dict(width=2, height=2, values=[0, 0, 0, 65535])
        ref = source_height(source, 1, 1, 3, F(1))
        triangle = [([0, 1, 2], ((F(0), F(0), F(0)), (F(1), F(0), F(0)), (F(1), F(1), F(1))))]
        height = height_at(triangle, (F(1, 3), F(1, 3)), Counter())
        self.assertEqual(ref, F(1, 9))
        self.assertEqual(height-ref, F(2, 9))

    def test_missing_coverage_unknown(self):
        with self.assertRaisesRegex(Unknown, "missing_coverage"):
            height_at([], (F(0), F(0)), Counter())

    def test_closed_population_and_missing_export(self):
        raw = dict(points=[[0, 0, 0, 0], [1, 1, 0, 0], [2, 0, 1, 0]], faces=[[0, 1, 2]])
        source = dict(width=2, height=2, values=[0, 0, 0, 0])
        population = closed_population(geometry(raw), source, 6, Counter())
        # 三顶点、三中点以及落在斜边上的两个六分格采样点
        self.assertEqual(set(population.values()), {(0, 0), (6, 0), (0, 6), (3, 0), (0, 3), (3, 3), (4, 2), (2, 4)})
        side = dict(old=raw, new=raw, samples=[])
        with self.assertRaisesRegex(Unknown, "closed_support_mismatch"):
            evaluate_side(side, source, {}, 6, Counter(), float("inf"))

    def test_tiny_progress_is_not_silently_accepted(self):
        point = self.point(0, F(1)+F(1, 2**200), F(1))
        result = summarize({0: point}, F(1))["operatingPoints"][-1]
        self.assertEqual(result["progress"], "unknown")
        self.assertFalse(result["accepted"])
        self.assertEqual(F(result["deltaPsiLowerExact"]), 0)
        self.assertGreater(F(result["deltaPsiUpperExact"]), 0)

    def test_censored_batch_has_no_results(self):
        result, points = inspect_batch(dict(frame=3, approved=10, status="censored-samples"), {})
        self.assertEqual(result["exchanges"], [])
        self.assertEqual(result["work"]["seconds"], 0)
        self.assertEqual(points, [])


if __name__ == "__main__":
    unittest.main()
