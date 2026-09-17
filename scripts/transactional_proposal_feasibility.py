"""QPC-04G：固定提案的一维有理可行性审计，不生成或提交生产事务。

必要外包域判空可以签发反证，充分内包域判空则只能保留未知。
可行见证仍须经过完整核心域与实际 float 输出的独立检查。
"""

from collections import Counter
from copy import deepcopy
from dataclasses import dataclass
from fractions import Fraction as F
import math
import time

from transactional_exchange_quality import (
    Unknown, rational, geometry, cross, height_at, source_height, clip,
    closed_population, evaluate_side,
)


@dataclass
class Interval:
    """保存有向端点及活跃约束；严格端点不能经舍入偷偷纳入可行域。"""
    low: F
    high: F
    low_open: bool = False
    high_open: bool = False
    lower: object = "height_range"
    upper: object = "height_range"
    contradiction: object = None

    @property
    def empty(self):
        return self.contradiction is not None or self.low > self.high or (
            self.low == self.high and (self.low_open or self.high_open))

    def add(self, coefficient, rhs, cause, strict=False):
        """求交 coefficient*z <= rhs；零系数冲突也是整个高度域的反证。"""
        item = dict(cause=cause, coefficient=str(coefficient), rhs=str(rhs), strict=strict)
        if coefficient == 0:
            if rhs < 0 or (strict and rhs == 0):
                self.contradiction = item
            return
        bound = rhs/coefficient
        if coefficient > 0 and (bound < self.high or (bound == self.high and strict)):
            self.high, self.high_open, self.upper = bound, strict, item
        elif coefficient < 0 and (bound > self.low or (bound == self.low and strict)):
            self.low, self.low_open, self.lower = bound, strict, item

    def contains(self, value):
        return not self.empty and (value > self.low or (value == self.low and not self.low_open)) and (
            value < self.high or (value == self.high and not self.high_open))

    def evidence(self):
        return dict(low=str(self.low), high=str(self.high), lowApprox=float(self.low),
                    highApprox=float(self.high), lowOpen=self.low_open, highOpen=self.high_open,
                    empty=self.empty, lower=self.lower, upper=self.upper, contradiction=self.contradiction)


def sqrt_enclosure(value, bits=96):
    """整数平方根给出必要/充分方向；完整平方数保留精确有理端点。"""
    if value < 0:
        raise Unknown("negative_square")
    a, b = math.isqrt(value.numerator), math.isqrt(value.denominator)
    if a*a == value.numerator and b*b == value.denominator:
        return F(a, b), F(a, b)
    scale = 1 << bits
    lower = math.isqrt(value.numerator*scale*scale//value.denominator)
    return F(lower, scale), F(lower+1, scale)


def affine_height(triangles, new_vertex, q, work):
    """系数来自新连接及固定旧点，不假定未拟合曲面等于旧曲面。"""
    values = []
    for ids, points in triangles:
        work["affineFaceTests"] += 1
        a, b, c = points
        area = cross(a, b, c)
        if area <= 0:
            raise Unknown("invalid_orientation")
        weights = (cross(q, b, c), cross(a, q, c), cross(a, b, q))
        if min(weights) >= 0:
            constant = sum((w*p[2] for w, p, v in zip(weights, points, ids) if v != new_vertex), F(0))/area
            variable = sum((w for w, v in zip(weights, ids) if v == new_vertex), F(0))/area
            values.append((constant, variable))
    if not values or any(v != values[0] for v in values):
        raise Unknown("affine_coverage_or_shared_edge")
    return values[0]


def shape_failure(triangles):
    """独立复算生产参数域平方角度判据；高度不能改变此失败。"""
    for face, points in triangles:
        if cross(*points) <= 0:
            return dict(face=face, reason="orientation")
        for i in range(3):
            p, q, r = points[i], points[(i+1)%3], points[(i+2)%3]
            x, y, z, w = q[0]-p[0], q[1]-p[1], r[0]-p[0], r[1]-p[1]
            dot = x*z+y*w
            lhs, rhs = 10*dot*dot, 9*(x*x+y*y)*(z*z+w*w)
            if dot > 0 and lhs > rhs:
                return dict(face=face, corner=i, reason="minimum_angle", lhs=str(lhs), rhs=str(rhs))
    return None


def exact_config(raw):
    config = dict(raw)
    for key in ("terrainSize", "heightScale", "qualityTargetPixels", "qualityHeightRatio"):
        config[key] = rational(config[key])
    config["matrix"] = tuple(map(rational, config["matrix"]))
    return config


def model_sample(row, old, new, data, source, config, work):
    sid, x, y, cached_visible, *_ = row
    q = F(x, data["denominator"]), F(y, data["denominator"])
    h0 = height_at(old, q, work)
    a, b = affine_height(new, data["newVertex"], q, work)
    ref = source_height(source, x, y, data["denominator"], config["heightScale"])
    rc, dc = clip(config, q, ref), clip(config, q, F(0))
    c = tuple(config["matrix"][4*i+1] for i in range(4))
    near = 0 if config["zeroToOne"] else -rc[3]
    visible = rc[3] > 0 and -rc[3] <= rc[0] <= rc[3] and -rc[3] <= rc[1] <= rc[3] and near <= rc[2] <= rc[3]
    if visible != bool(cached_visible):
        raise Unknown("visibility_boundary_mismatch")
    k = old_error = F(0)
    if visible:
        w0 = dc[3]+c[3]*h0
        near0 = dc[2]+c[2]*h0 + (w0 if not config["zeroToOne"] else 0)
        if w0 <= 0 or near0 < 0:
            raise Unknown("old_projection_domain")
        k = ((c[0]*rc[3]-c[3]*rc[0])/rc[3]*config["width"]/2)**2 + (
             (c[1]*rc[3]-c[3]*rc[1])/rc[3]*config["height"]/2)**2
        old_error = k*(h0-ref)**2/w0**2
    values = (a, b, ref, h0, k, old_error)
    work["maxCoefficientBits"] = max(work["maxCoefficientBits"], *(
        max(v.numerator.bit_length(), v.denominator.bit_length()) for v in values))
    work["modelSamples"] += 1
    return dict(id=sid, uv=q, a=a, b=b, ref=ref, old=h0, visible=visible,
                k=k, a0=old_error, dc=dc, c=c)


def screen_constraints(interval, sample, radius, label):
    a, b, r = sample["a"], sample["b"], sample["ref"]
    dw, cw = sample["dc"][3], sample["c"][3]
    interval.add((1-radius*cw)*b, radius*(dw+cw*a)-(a-r), (sample["id"], label, "+"))
    interval.add((-1-radius*cw)*b, radius*(dw+cw*a)+(a-r), (sample["id"], label, "-"))


def constrain(sample, inner, outer, legacy, config, target, work):
    a, b, r = sample["a"], sample["b"], sample["ref"]
    radius = max(abs(sample["old"]-r), config["heightScale"]*config["qualityHeightRatio"])
    for interval in (inner, outer):
        interval.add(b, r+radius-a, (sample["id"], "height", "+"))
        interval.add(-b, a-r+radius, (sample["id"], "height", "-"))
    work["heightConstraints"] += 2
    if not sample["visible"]:
        return
    dw, cw = sample["dc"][3], sample["c"][3]
    nc = sample["c"][2]+(cw if not config["zeroToOne"] else 0)
    nd = sample["dc"][2]+(dw if not config["zeroToOne"] else 0)
    for interval in (inner, outer, legacy):
        interval.add(-cw*b, dw+cw*a, (sample["id"], "positive_w"), strict=True)
        interval.add(-nc*b, nd+nc*a, (sample["id"], "near_plane"))
    if sample["k"] == 0:
        return
    # 外包使用更大半径，仅在正分母域内才保持必要性。
    cap = max(sample["a0"], config["qualityTargetPixels"]**2)
    lo, hi = sqrt_enclosure(cap/sample["k"])
    screen_constraints(inner, sample, lo, "screen_inner")
    screen_constraints(outer, sample, hi, "screen_outer")
    if target >= 0:
        _, old_hi = sqrt_enclosure(target**2/sample["k"])
        screen_constraints(legacy, sample, old_hi, "legacy_screen_outer")
    work["screenConstraints"] += 2


def check_records(records, config):
    """完整闭支持逐点损伤与有向进展分别判断，不用抽样最大值替代。"""
    from analyze_transactional_target_quality import condition
    return dict(**condition(records, config, receiver=True), visible=sum(p["visible"] for p in records.values()),
                oldMaxSquared=str(max((p["a0"] for p in records.values() if p["visible"]), default=F(0))),
                newMaxSquared=str(max((p["a1"] for p in records.values() if p["visible"]), default=F(0))))


def evaluate_height(raw, source, config, height, work, deadline):
    """先存为binary64，再独立重建核心及float输出；公开域重新枚举覆盖。"""
    from analyze_transactional_target_quality import public_side
    data = raw["input"]
    side = dict(old=data["old"], new=deepcopy(data["new"]), samples=raw["samples"])
    for p in side["new"]["points"]:
        if p[0] == data["newVertex"]:
            p[3] = float(height)
    output = []
    for domain in ("core", "float"):
        started = time.monotonic()
        current = side if domain == "core" else public_side(side, source, config, data["denominator"], work)
        records = evaluate_side(current, source, config, data["denominator"], work, deadline)
        summary = check_records(records, config)
        # 留一个实际违规点的精确数据，不能只报告拒绝计数。
        ids = summary["screenViolations"][:1]+summary["heightViolations"][:1]
        summary["witnesses"] = [{key: str(value) for key, value in records[sid].items()} for sid in dict.fromkeys(ids)]
        output.append(dict(domain=domain, seconds=time.monotonic()-started, **summary))
    return output


def trial_values(raw, source, config, inner, outer):
    """顺序预冻结：旧Fit、源高投影、中点，再按层次取内部点；至多32项。"""
    interval = inner if not inner.empty else outer
    data = raw["input"]
    point = next(p for p in data["new"]["points"] if p[0] == data["newVertex"])
    den = data["denominator"]
    source_value = source_height(source, rational(point[1])*den, rational(point[2])*den, den, config["heightScale"])
    candidates = [raw["fitHeight"], min(interval.high, max(interval.low, source_value)), (interval.low+interval.high)/2]
    for depth in range(1, 6):
        candidates.extend(interval.low+(interval.high-interval.low)*F(i, 2**depth)
                          for i in range(1, 2**depth, 2))
    seen = set()
    for value in candidates:
        if value is None:
            continue
        value = float(value)
        if value in seen or not math.isfinite(value) or not outer.contains(F(value)):
            continue
        seen.add(value)
        yield value
        if len(seen) >= 32:
            break


def excess_is_unchanged(models, target_squared):
    """零系数只表示与z无关，还必须等于旧高度才能证明没有任何收益。"""
    above = [p for p in models if p["visible"] and p["a0"] > target_squared]
    return all(p["b"] == 0 and p["a"] == p["old"] for p in above)


def analyze(raw, source, deadline):
    started = time.monotonic()
    work = Counter()
    data = raw["input"]
    result = dict(frame=data["frame"], root=data["root"], ordinal=data["ordinal"], kind=data["kind"],
                  oldReason=raw["oldReason"], pointwiseReason=raw["pointwiseReason"], inPrefix=raw["inPrefix"],
                  samples=len(raw["samples"]), oldWork=raw["oldWork"], oldConservativeInterval=raw["interval"],
                  oldFitHeight=raw["fitHeight"], status="unknown_numeric", trials=[], work=work)
    try:
        if raw["status"] != "complete":
            result["status"] = "censored"
            return result
        new = geometry(data["new"])
        failure = shape_failure(new)
        if failure:
            result.update(status="shape_rejected", certificate=failure)
            return result
        if raw["constructionReason"] or not new:
            result.update(status="fixed_proposal_rejected", reason=raw["constructionReason"])
            return result
        config = exact_config(data["config"])
        if data["kind"] == "B":
            domains = evaluate_height(raw, source, config, raw["fitHeight"], work, deadline)
            result.update(status="feasible_witness" if all(d["accepted"] for d in domains) else
                          "fixed_proposal_rejected", fixedHeight=True, domains=domains)
            return result
        old = geometry(data["old"])
        population = closed_population(old, source, data["denominator"], work)
        supplied = {r[0]: (r[1], r[2]) for r in raw["samples"]}
        if supplied != population or len(supplied) != len(raw["samples"]):
            raise Unknown("closed_support_mismatch")
        scale = config["heightScale"]
        inner, outer, legacy = (Interval(-scale, 2*scale) for _ in range(3))
        target = F(raw["targetMicropixels"], 1000000)
        if target < 0:
            legacy.add(F(0), F(-1), "negative_legacy_target")
        models = []
        construction = time.monotonic()
        for row in raw["samples"]:
            if time.monotonic() > deadline:
                raise Unknown("censored_time")
            sample = model_sample(row, old, new, data, source, config, work)
            models.append(sample)
            constrain(sample, inner, outer, legacy, config, target, work)
            # 两条必要约束已经矛盾时无需扫描余下样本；这不是可行证书。
            if outer.empty:
                break
        result.update(inner=inner.evidence(), outer=outer.evidence(), legacyOuter=legacy.evidence(),
                      modelSeconds=time.monotonic()-construction, modelComplete=len(models)==len(raw["samples"]))
        if outer.empty:
            result["status"] = "core_infeasible_proven"
            return result
        # 必要域若只剩一个值，且完整曲面在此值等于旧曲面，整个域都不可能有正进展。
        # 这比有限试值失败更强；非binary64单点还单独给出存储不可行证据。
        if outer.low == outer.high:
            result["singleton"] = dict(height=str(outer.low), binary64Representable=F(float(outer.low))==outer.low)
            if all(p["a"]+p["b"]*outer.low == p["old"] for p in models):
                result.update(status="safe_but_no_progress_proven",
                              progressProof="necessary_singleton_reproduces_old_surface_on_complete_Q")
                return result
            if F(float(outer.low)) != outer.low:
                result.update(status="storage_infeasible_proven",
                              reason="necessary_singleton_is_not_binary64")
                return result
        e = config["qualityTargetPixels"]**2
        if excess_is_unchanged(models, e):
            result.update(status="safe_but_no_progress_proven" if not inner.empty else "unknown_progress",
                          progressProof="all_above_target_samples_preserve_old_height_independent_of_z")
            return result
        core_found = False
        for height in trial_values(raw, source, config, inner, outer):
            if time.monotonic() > deadline:
                raise Unknown("censored_time")
            domains = evaluate_height(raw, source, config, height, work, deadline)
            entry = dict(height=height, domains=domains,
                         legacyMaximumAccepts=target >= 0 and F(domains[0]["newMaxSquared"]) <= target**2)
            result["trials"].append(entry)
            core_found |= domains[0]["accepted"]
            if all(d["accepted"] for d in domains):
                result.update(status="feasible_witness", witness=entry)
                return result
        result["status"] = "core_feasible_output_unresolved" if core_found else "unknown_progress"
    except Unknown as error:
        result.update(status="censored" if str(error)=="censored_time" else "unknown_numeric", reason=str(error))
    finally:
        result["seconds"] = time.monotonic()-started
    return result
