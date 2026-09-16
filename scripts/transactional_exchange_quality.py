"""只读局部质量契约：独立有理几何、逐点条件与可审计的有限和界。

输入 double 按实际二进制值精确解释；Q 坐标与 U16 双线性源另行恢复。
不调用生产 Fit/Measure，也不从运行时缓存误差推导接受结论。
"""

from collections import Counter
from fractions import Fraction as F
import math
import time


OPERATING_POINTS = [(F(e), F(1, h)) for e in ("0.25", "0.5", "1") for h in (1024, 256)]
SUM_BITS = 128


class Unknown(ValueError):
    """不完整或不可定义的证据；不是数学不可行，也不是接受。"""


def rational(value):
    if value is None or not math.isfinite(value):
        raise Unknown("nonfinite_input")
    return F(value)


def cap_test(old, new, target_squared):
    return new <= max(old, target_squared)


def excess(value, target_squared):
    return max(F(0), value - target_squared)


def sum_interval(values):
    """逐项精确取 128 位二进制有向界，避免异分母连加造成位长爆炸。"""
    scale = 1 << SUM_BITS
    lower = upper = 0
    for value in values:
        quotient, remainder = divmod(value.numerator * scale, value.denominator)
        lower += quotient
        upper += quotient + bool(remainder)
    return F(lower, scale), F(upper, scale)


def geometry(raw):
    points = {p[0]: tuple(map(rational, p[1:])) for p in raw["points"]}
    return [(face, tuple(points[v] for v in face)) for face in raw["faces"]]


def cross(a, b, c):
    return (b[0]-a[0])*(c[1]-a[1]) - (b[1]-a[1])*(c[0]-a[0])


def height_at(triangles, q, work):
    values = []
    for _, (a, b, c) in triangles:
        work["coverageTests"] += 1
        area = cross(a, b, c)
        if area <= 0:
            raise Unknown("invalid_orientation")
        wa, wb, wc = cross(q, b, c), cross(a, q, c), cross(a, b, q)
        if min(wa, wb, wc) >= 0:
            values.append((wa*a[2] + wb*b[2] + wc*c[2]) / area)
    if not values:
        raise Unknown("missing_coverage")
    if any(h != values[0] for h in values):
        raise Unknown("inconsistent_shared_geometry")
    return values[0]


def source_height(source, x, y, denominator, scale):
    # 独立从源栅格和精确参数域坐标构造，不复用导出的缓存高度
    sx = F(x, denominator) * (source["width"]-1)
    sy = F(y, denominator) * (source["height"]-1)
    ix, iy = min(sx.numerator//sx.denominator, source["width"]-2), min(sy.numerator//sy.denominator, source["height"]-2)
    tx, ty = sx-ix, sy-iy
    base = iy*source["width"]+ix
    values = source["values"]
    a = values[base]*(1-tx) + values[base+1]*tx
    b = values[base+source["width"]]*(1-tx) + values[base+source["width"]+1]*tx
    return (a*(1-ty)+b*ty)*scale/65535


def clip(config, q, height):
    coordinates = ((q[0]-F(1, 2))*config["terrainSize"], height,
                   (q[1]-F(1, 2))*config["terrainSize"], F(1))
    return tuple(sum((config["matrix"][4*i+j]*coordinates[j] for j in range(4)), F(0)) for i in range(4))


def screen_errors(config, q, reference, old, new):
    rc = clip(config, q, reference)
    near = F(0) if config["zeroToOne"] else -rc[3]
    visible = rc[3] > 0 and -rc[3] <= rc[0] <= rc[3] and -rc[3] <= rc[1] <= rc[3] and near <= rc[2] <= rc[3]
    if not visible:
        return False, F(0), F(0)
    result = []
    for height in (old, new):
        mc = clip(config, q, height)
        if mc[3] <= 0 or mc[2] < (0 if config["zeroToOne"] else -mc[3]):
            raise Unknown("near_plane")
        dx = (mc[0]/mc[3]-rc[0]/rc[3])*config["width"]/2
        dy = (mc[1]/mc[3]-rc[1]/rc[3])*config["height"]/2
        result.append(dx*dx+dy*dy)
    return visible, *result


def closed_population(triangles, source, denominator, work):
    """在局部包围盒枚举六组 Q，独立核查 C++ 的完整闭面关联。"""
    n = source["width"]-1
    if source["height"] != n+1 or denominator != 6*n:
        raise Unknown("unsupported_sample_schema")
    points = [p for _, triangle in triangles for p in triangle]
    groups = ((0, 0, n+1, n+1), (3, 0, n, n+1), (0, 3, n+1, n),
              (3, 3, n, n), (4, 2, n, n), (2, 4, n, n))
    result = {}
    start = 0
    for gx, gy, columns, rows in groups:
        xmin = max(0, math.ceil((min(p[0] for p in points)*denominator-gx)/6))
        xmax = min(columns-1, math.floor((max(p[0] for p in points)*denominator-gx)/6))
        ymin = max(0, math.ceil((min(p[1] for p in points)*denominator-gy)/6))
        ymax = min(rows-1, math.floor((max(p[1] for p in points)*denominator-gy)/6))
        for iy in range(ymin, ymax+1):
            for ix in range(xmin, xmax+1):
                x, y = 6*ix+gx, 6*iy+gy
                q = F(x, denominator), F(y, denominator)
                for _, (a, b, c) in triangles:
                    work["supportEnumerationTests"] += 1
                    if min(cross(a, b, q), cross(b, c, q), cross(c, a, q)) >= 0:
                        result[start+iy*columns+ix] = (x, y)
                        break
        start += columns*rows
    return result


def evaluate_side(side, source, config, denominator, work, deadline):
    old_points = {p[0]: tuple(map(rational, p[1:])) for p in side["old"]["points"]}
    for point in side["new"]["points"]:
        if point[0] in old_points and tuple(map(rational, point[1:])) != old_points[point[0]]:
            raise Unknown("surviving_geometry_changed")
    old, new = geometry(side["old"]), geometry(side["new"])
    population = closed_population(old, source, denominator, work)
    supplied = {row[0]: (row[1], row[2]) for row in side["samples"]}
    if len(supplied) != len(side["samples"]) or supplied != population:
        raise Unknown("closed_support_mismatch")
    result = {}
    for row in side["samples"]:
        if time.monotonic() > deadline:
            raise Unknown("censored_time")
        sid, x, y, cached_visible, cached_height, cached_reference, cached_error = row
        q = F(x, denominator), F(y, denominator)
        h0, h1 = height_at(old, q, work), height_at(new, q, work)
        ref = source_height(source, x, y, denominator, config["heightScale"])
        visible, a0, a1 = screen_errors(config, q, ref, h0, h1)
        if visible != bool(cached_visible):
            raise Unknown("visibility_boundary_mismatch")
        u0, u1 = (h0-ref)**2, (h1-ref)**2
        values = (h0, h1, ref, a0, a1, u0, u1)
        work["rationalSampleEvaluations"] += 1
        work["squaredErrorEvaluations"] += 4
        work["maxRationalBits"] = max(work["maxRationalBits"], *(max(v.numerator.bit_length(), v.denominator.bit_length()) for v in values))
        for name, actual, cached in (("height", h0, cached_height), ("reference", ref, cached_reference), ("screenSquared", a0, cached_error)):
            if cached is not None:
                work[name+"CacheMaxDifference"] = max(work[name+"CacheMaxDifference"], abs(float(actual)-cached))
        result[sid] = dict(id=sid, uv=q, visible=visible, oldHeight=h0, newHeight=h1,
                           reference=ref, a0=a0, a1=a1, u0=u0, u1=u1)
    return result


def combine(sides, work):
    """共享闭边界只能去重，不能把真实覆盖重叠当独立进展相加。"""
    result = {}
    for side in sides:
        for sid, value in side.items():
            if sid in result:
                old = result[sid]
                work["sharedSamples"] += 1
                if old != value or value["oldHeight"] != value["newHeight"]:
                    raise Unknown("changed_shared_support")
            else:
                result[sid] = value
    return result


def summarize(records, height_scale):
    points = list(records.values())
    visible = [p for p in points if p["visible"]]
    min_screen = max((p["a1"] for p in visible if p["a1"] > p["a0"]), default=F(0))
    min_height = max((p["u1"] for p in points if p["u1"] > p["u0"]), default=F(0))
    cases = []
    for e, ratio in OPERATING_POINTS:
        screen_bad = [p["id"] for p in visible if not cap_test(p["a0"], p["a1"], e*e)]
        height_bad = [p["id"] for p in points if not cap_test(p["u0"], p["u1"], (ratio*height_scale)**2)]
        differences = [excess(p["a0"], e*e)-excess(p["a1"], e*e) for p in visible]
        lower, upper = sum_interval(differences)
        old_lower, old_upper = sum_interval([excess(p["a0"], e*e) for p in visible])
        progress = "positive" if lower > 0 else "nonpositive" if upper <= 0 else "unknown"
        cases.append(dict(e=float(e), hRatio=str(ratio), screenPass=not screen_bad, heightPass=not height_bad,
                          screenRejected=len(screen_bad), heightRejected=len(height_bad),
                          screenWitness=screen_bad[:1], heightWitness=height_bad[:1], progress=progress,
                          deltaPsiLower=float(lower), deltaPsiUpper=float(upper),
                          deltaPsiLowerExact=str(lower), deltaPsiUpperExact=str(upper),
                          oldPsiLower=float(old_lower), oldPsiUpper=float(old_upper),
                          accepted=not screen_bad and not height_bad and progress == "positive"))
    return dict(closed=len(points), visible=len(visible), minScreenPx=math.sqrt(float(min_screen)),
                minScreenSquaredExact=str(min_screen), minHeightSquaredExact=str(min_height),
                minHeight=math.sqrt(float(min_height)), minHeightRatio=math.sqrt(float(min_height))/float(height_scale),
                operatingPoints=cases)


def inspect_batch(raw, source, deadline=float("inf")):
    started = time.monotonic()
    work = Counter(seconds=0)
    result = dict(frame=raw["frame"], approved=raw["approved"], status=raw["status"], exchanges=[], work=work)
    if raw["status"] != "complete":
        return result, []
    config = {**raw, "terrainSize": rational(raw["terrainSize"]), "heightScale": rational(raw["heightScale"]),
              "matrix": tuple(map(rational, raw["matrix"]))}
    known = []
    point_rows = []
    for exchange in raw["exchanges"]:
        entry = dict(index=exchange["index"], kind=exchange["kind"], donorCenter=exchange["donor"]["center"] if exchange["donor"] else None,
                     receiverKind=exchange["receiver"]["kind"], receiverRoot=exchange["receiver"]["root"])
        try:
            sides = []
            for name in ("receiver", "donor"):
                side = exchange[name]
                if side is not None:
                    records = evaluate_side(side, source, config, raw["denominator"], work, deadline)
                    sides.append(records)
                    entry[name] = summarize(records, config["heightScale"])
            records = combine(sides, work)
            entry.update(status="complete", **summarize(records, config["heightScale"]))
            known.append(records)
            # 舍入列只用于制图；接受判定已由上面的有理不等式决定
            for p in records.values():
                point_rows.append(dict(exchange=exchange["index"], sample=p["id"], u=float(p["uv"][0]), v=float(p["uv"][1]),
                                       visible=p["visible"], oldHeight=float(p["oldHeight"]), newHeight=float(p["newHeight"]),
                                       reference=float(p["reference"]), oldScreen=math.sqrt(float(p["a0"])), newScreen=math.sqrt(float(p["a1"])),
                                       oldResidual=math.sqrt(float(p["u0"])), newResidual=math.sqrt(float(p["u1"]))))
        except Unknown as error:
            entry.update(status="unknown", reason=str(error))
        result["exchanges"].append(entry)
    try:
        combined = combine(known, work)
        result["batchSharedInterface"] = "checked" if len(known) == len(raw["exchanges"]) else "partial-unknown"
        result["uniqueClosed"] = len(combined)
    except Unknown as error:
        result["batchSharedInterface"] = str(error)
        # 组合前提失败不能保留孤立的 accepted 标志用于批级准入
        for entry in result["exchanges"]:
            for point in entry.get("operatingPoints", []):
                point["accepted"] = False
        result["status"] = "unknown-composition"
    work["geometryReusedAcrossParameterPoints"] = work["rationalSampleEvaluations"]*5
    work["seconds"] = time.monotonic()-started
    return result, point_rows
