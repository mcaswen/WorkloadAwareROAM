"""解析反例验证证据方向；不把自然数据的结果硬编码成测试答案。"""

from collections import Counter
from fractions import Fraction as F
import time
import unittest

from transactional_proposal_feasibility import (
    Interval, sqrt_enclosure, affine_height, screen_constraints, constrain,
    check_records, evaluate_height, exact_config, shape_failure, excess_is_unchanged,
)
from transactional_exchange_quality import geometry, closed_population, Unknown


class FeasibilityTests(unittest.TestCase):
    def test_exact_interval_and_strict_boundary(self):
        interval = Interval(F(-2), F(2))
        interval.add(F(-1), F(-1), "lower")
        interval.add(F(1), F(1), "upper")
        self.assertTrue(interval.contains(F(1)))
        interval.add(F(-1), F(-1), "positive_domain", strict=True)
        self.assertTrue(interval.empty)

    def test_inner_empty_is_not_impossibility(self):
        low, high = sqrt_enclosure(F(2), bits=2)
        self.assertLessEqual(low*low, 2)
        self.assertGreaterEqual(high*high, 2)
        # 1.4 <= z <= sqrt(2)：低精度充分半径1.25会漏解，外包1.5没有判空。
        inner, outer = Interval(F("1.4"), low), Interval(F("1.4"), high)
        self.assertTrue(inner.empty)
        self.assertFalse(outer.empty)
        self.assertLess(F("1.4")**2, 2)
        self.assertEqual(sqrt_enclosure(F(9, 4)), (F(3, 2), F(3, 2)))

    def test_zero_coefficient_global_margin_conflict(self):
        def record(sid, old, new):
            return dict(id=sid, visible=True, a0=F(old)**2, a1=F(new)**2, u0=F(0), u1=F(0))
        config = dict(qualityTargetPixels=F(1, 2), heightScale=F(1), qualityHeightRatio=F(1, 256))
        result = check_records({0: record(0, 2, 2), 1: record(1, 1, F(1, 2))}, config)
        self.assertTrue(result["accepted"])
        self.assertGreater(F(result["newMaxSquared"]), F("1.99")**2)
        self.assertFalse(check_records({0: record(0, 2, 2)}, config)["accepted"])

    def test_projective_linear_constraints_match_direct_error(self):
        sample = dict(id=4, a=F(1, 3), b=F(2, 3), ref=F(1, 2),
                      dc=(F(0), F(0), F(1), F(2)), c=(F(0), F(1), F(0), F(1, 5)))
        interval = Interval(F(-1), F(2))
        radius = F(1, 10)
        screen_constraints(interval, sample, radius, "analytic")
        for i in range(-100, 201):
            z = F(i, 100)
            h = sample["a"]+sample["b"]*z
            direct = abs(h-sample["ref"]) <= radius*(2+h/5)
            self.assertEqual(interval.contains(z), direct)

    def test_zero_coefficient_does_not_imply_old_surface(self):
        # 固定连接变化可能已降低常数项；不依赖新点高度不等于不产生收益。
        sample = dict(visible=True, a0=F(4), a=F(1), old=F(2), b=F(0))
        self.assertFalse(excess_is_unchanged([sample], F(1,4)))
        sample["a"] = sample["old"]
        self.assertTrue(excess_is_unchanged([sample], F(1,4)))

    def test_projection_domain_is_not_screen_zero(self):
        config = dict(heightScale=F(10), qualityHeightRatio=F(1), qualityTargetPixels=F(1), zeroToOne=True)
        sample = dict(id=0, a=F(0), b=F(1), ref=F(0), old=F(1), visible=True,
                      k=F(0), a0=F(0), dc=(0,0,F(1),F(0)), c=(0,0,0,F(1)))
        inner, outer, legacy = (Interval(F(-10), F(20)) for _ in range(3))
        constrain(sample, inner, outer, legacy, config, F(1), Counter())
        self.assertFalse(outer.contains(F(0)))
        self.assertTrue(outer.contains(F(1)))

    def test_affine_shared_edge_and_shape(self):
        mesh = dict(points=[[0,0,0,1], [1,1,0,2], [2,1,1,3], [3,0,1,4], [4,.5,.5,0]],
                    faces=[[0,1,4],[1,2,4],[2,3,4],[3,0,4]])
        triangles = geometry(mesh)
        self.assertEqual(affine_height(triangles, 4, (F(1,2),F(1,2)), Counter()), (0,1))
        self.assertIsNone(shape_failure(triangles))
        mesh["points"][4][1] = .001
        self.assertIsNotNone(shape_failure(geometry(mesh)))

    def test_complete_support_and_float_rounding(self):
        source = dict(width=2, height=2, values=[0]*4)
        old = dict(points=[[0,0,0,0], [1,1,0,0], [2,1,1,0], [3,0,1,0]], faces=[[0,1,2],[0,2,3]])
        new = dict(points=old["points"]+[[4,.5,.5,0]], faces=[[0,1,4],[1,2,4],[2,3,4],[3,0,4]])
        config = exact_config(dict(terrainSize=1, heightScale=1, qualityTargetPixels=.5, qualityHeightRatio=.1,
                                  width=2, height=2, zeroToOne=True,
                                  matrix=[1,0,0,0, 0,0,1,0, 0,0,0,2, 0,0,0,1]))
        population = closed_population(geometry(old), source, 6, Counter())
        raw = dict(input=dict(old=old, new=new, newVertex=4, denominator=6),
                   samples=[[sid,x,y,False,None,None,None] for sid,(x,y) in population.items()])
        core, output = evaluate_height(raw, source, config, .1, Counter(), time.monotonic()+2)
        self.assertEqual(core["heightViolations"], [])
        self.assertTrue(output["heightViolations"])
        raw["samples"].pop()
        with self.assertRaisesRegex(Unknown, "closed_support_mismatch"):
            evaluate_height(raw, source, config, .1, Counter(), time.monotonic()+2)


if __name__ == "__main__":
    unittest.main()
