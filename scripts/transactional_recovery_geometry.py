"""冻结double参数域的精确局部几何审计，不承担生产更新。"""

from collections import Counter, defaultdict
from fractions import Fraction
import math


def points(geometry):
    return {int(p[0]): tuple(Fraction(float(v)) for v in p[1:3]) for p in geometry["points"]}


def cross(a, b, c):
    return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])


def inside_segment(p, a, b):
    return cross(a, b, p) == 0 and p != a and p != b and all(min(a[i], b[i]) <= p[i] <= max(a[i], b[i]) for i in range(2))


def crossing(a, b, c, d):
    return cross(a, b, c)*cross(a, b, d) < 0 and cross(c, d, a)*cross(c, d, b) < 0


def angles(geometry):
    positions = points(geometry)
    result = []
    for face in geometry["faces"]:
        values = []
        for i, center in enumerate(face):
            p, q, r = (positions[face[j % 3]] for j in (i, i+1, i+2))
            u, v = tuple(q[j]-p[j] for j in range(2)), tuple(r[j]-p[j] for j in range(2))
            dot = sum(x*y for x, y in zip(u, v))
            norms = sum(x*x for x in u)*sum(x*x for x in v)
            allowed = norms > 0 and (dot <= 0 or 10*dot*dot <= 9*norms)
            cosine = float(dot)/math.sqrt(float(norms)) if norms else float("nan")
            values.append(dict(vertex=center, degrees=math.degrees(math.acos(max(-1., min(1., cosine)))) if norms else None,
                               allowed=allowed, dot=str(dot), norms=str(norms), cosineSquared=str(dot*dot/norms) if norms else None))
        result.append(dict(face=face, orientation=str(cross(*(positions[v] for v in face))), angles=values))
    return result


def edges(geometry):
    uses = defaultdict(list)
    for index, face in enumerate(geometry["faces"]):
        for i, a in enumerate(face):
            b = face[(i+1) % 3]
            uses[tuple(sorted((a, b)))].append((a, b, index))
    return uses


def validate(old, target):
    """覆盖由边界、方向、配对、无相交和面积联合核查，形状另列。"""
    old_points, new_points = points(old), points(target)
    before, after = edges(old), edges(target)
    errors = []
    old_used = {v for f in old["faces"] for v in f}
    new_used = {v for f in target["faces"] for v in f}
    expected_new = set() if target["newVertex"] is None else {target["newVertex"]}
    if new_used != old_used | expected_new:
        errors.append("vertex_population")
    if any(new_points.get(v) != old_points[v] for v in old_used):
        errors.append("old_position_changed")
    old_heights = {p[0]: p[3] for p in old["points"]}
    if any(p[0] in old_heights and p[3] != old_heights[p[0]] for p in target["points"]):
        errors.append("old_height_changed")
    boundary = lambda e: Counter((u[0][0], u[0][1]) for u in e.values() if len(u) == 1)
    if boundary(before) != boundary(after):
        errors.append("boundary_changed")
    for edge, uses in after.items():
        if len(uses) not in (1, 2) or (len(uses) == 2 and uses[0][:2] != uses[1][:2][::-1]):
            errors.append("edge_pairing:"+str(edge))
    if len({tuple(sorted(f)) for f in target["faces"]}) != len(target["faces"]):
        errors.append("duplicate_face")
    exact_angles = angles(target)
    if any(Fraction(f["orientation"]) <= 0 for f in exact_angles):
        errors.append("nonpositive_orientation")
    area = lambda g, p: sum(cross(*(p[v] for v in f)) for f in g["faces"])
    if area(old, old_points) != area(target, new_points):
        errors.append("area_changed")
    all_edges = list(after)
    for i, (a, b) in enumerate(all_edges):
        for c, d in all_edges[i+1:]:
            if not {a, b} & {c, d} and crossing(new_points[a], new_points[b], new_points[c], new_points[d]):
                errors.append("edge_crossing")
        if any(v not in (a, b) and inside_segment(new_points[v], new_points[a], new_points[b]) for v in new_used):
            errors.append("hanging_vertex")
    shape = all(Fraction(f["orientation"]) > 0 and all(a["allowed"] for a in f["angles"]) for f in exact_angles)
    return dict(structuralValid=not errors, shapeValid=shape, errors=sorted(set(errors)), exactAngles=exact_angles,
                twiceArea=str(area(target, new_points)), faceDelta=len(target["faces"])-len(old["faces"]),
                newVertices=len(new_used-old_used))


def self_test():
    old = dict(points=[[0, 0, 0, 0], [1, 1, 0, 0], [2, 1, 1, 0], [3, 0, 1, 0]], faces=[[0, 1, 2], [0, 2, 3]], newVertex=None)
    flipped = dict(old, faces=[[1, 3, 0], [3, 1, 2]])
    assert validate(old, flipped)["structuralValid"] and validate(old, flipped)["shapeValid"]
    assert not validate(old, dict(flipped, faces=flipped["faces"][:1]))["structuralValid"]
    assert not validate(old, dict(flipped, faces=flipped["faces"]*2))["structuralValid"]
    hanging = dict(old, points=old["points"]+[[4, .5, .5, 0]], faces=[[0, 1, 4], [1, 2, 4], [0, 2, 3]], newVertex=4)
    assert "hanging_vertex" in validate(old, hanging)["errors"]
    assert crossing((0, 0), (1, 1), (0, 1), (1, 0))
    # 最小角的等号必须保留，不能用十进制角度舍入决定合法性
    # 用整坐标表达同一角，避免把1/3转double的扰动误写成等号
    boundary = dict(points=[[0, 0, 0, 0], [1, 3, 0, 0], [2, 3, 1, 0]], faces=[[0, 1, 2]], newVertex=None)
    a = angles(boundary)[0]["angles"][0]
    assert a["allowed"] and a["cosineSquared"] == "9/10"
