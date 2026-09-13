"""复核局部删点的最小最大误差递推，并保留几何与质量反例。

复用上一轮精确几何检查；耳切穷举作为独立组合参考，不运行历史检查集。
"""

import argparse
import hashlib
import itertools
import json
from fractions import Fraction as F
from pathlib import Path

from verify_bounded_patch_refit import boundary, canonical, height_on, interior_degree_three, neighbors, orient, star, validate


def inside_polygon(point, ring, points):
    winding = 0
    for a_id, b_id in zip(ring, ring[1:] + ring[:1]):
        a, b = points[a_id], points[b_id]
        cross = orient(a, b, point)
        if cross == 0 and all(min(a[i], b[i]) <= point[i] <= max(a[i], b[i]) for i in (0, 1)):
            return False
        if a[1] <= point[1] < b[1] and cross > 0:
            winding += 1
        elif b[1] <= point[1] < a[1] and cross < 0:
            winding -= 1
    return winding != 0


def inside_triangle(point, face, points):
    a, b, c = (points[v] for v in face)
    return orient(a, b, point) >= 0 and orient(b, c, point) >= 0 and orient(c, a, point) >= 0


def visible(a_id, b_id, ring, points):
    edges = list(zip(ring, ring[1:] + ring[:1]))
    if any({a_id, b_id} == {u, v} for u, v in edges):
        return True
    a, b = points[a_id], points[b_id]
    for v in ring:
        if v not in (a_id, b_id):
            p = points[v]
            if orient(a, b, p) == 0 and all(min(a[i], b[i]) <= p[i] <= max(a[i], b[i]) for i in (0, 1)):
                return False
    for u, v in edges:
        if {a_id, b_id} & {u, v}:
            continue
        c, d = points[u], points[v]
        if orient(a, b, c) * orient(a, b, d) < 0 and orient(c, d, a) * orient(c, d, b) < 0:
            return False
    return inside_polygon(((a[0] + b[0]) / 2, (a[1] + b[1]) / 2), ring, points)


def admissible(face, ring, points):
    if orient(*(points[v] for v in face)) <= 0:
        return False
    if not all(visible(a, b, ring, points) for a, b in zip(face, face[1:] + face[:1])):
        return False
    center = tuple(sum(points[v][i] for v in face) / 3 for i in (0, 1))
    return inside_polygon(center, ring, points)


def shape_allowed(face, points, floor):
    """30° 和 45° 使用平方点积判据，避免浮点反三角函数。"""
    assert floor in (0, 30, 45)
    for i in range(3):
        a, b, c = (points[face[j % 3]] for j in (i, i + 1, i + 2))
        u, v = (b[0] - a[0], b[1] - a[1]), (c[0] - a[0], c[1] - a[1])
        dot = sum(x * y for x, y in zip(u, v))
        product = sum(x * x for x in u) * sum(y * y for y in v)
        if floor == 30 and dot > 0 and 4 * dot * dot > 3 * product:
            return False
        if floor == 45 and dot > 0 and 2 * dot * dot > product:
            return False
    return True


def ear_triangulations(ring, points):
    if len(ring) == 3:
        if orient(*(points[v] for v in ring)) > 0:
            return {frozenset({canonical(tuple(ring), points)})}
        return set()
    result = set()
    for i, vertex in enumerate(ring):
        face = (ring[i - 1], vertex, ring[(i + 1) % len(ring)])
        if orient(*(points[v] for v in face)) <= 0:
            continue
        if any(inside_triangle(points[v], face, points) for v in ring if v not in face):
            continue
        if not visible(face[0], face[2], ring, points):
            continue
        for rest in ear_triangulations(ring[:i] + ring[i + 1:], points):
            result.add(rest | {canonical(face, points)})
    return result


def triangle_cost(face, points, samples):
    return max((abs(height_on(points, face, point) - reference) for point, reference in samples if inside_triangle(point, face, points)), default=F(0))


def solve_cavity(ring, points, samples, floor):
    costs, choices, values = {}, {}, {}
    for i, k, j in itertools.combinations(range(len(ring)), 3):
        face = (ring[i], ring[k], ring[j])
        if admissible(face, ring, points) and shape_allowed(face, points, floor):
            costs[i, k, j] = triangle_cost(face, points, samples)
    for i in range(len(ring) - 1):
        values[i, i + 1] = F(0)
    for gap in range(2, len(ring)):
        for i in range(len(ring) - gap):
            j = i + gap
            options = []
            for k in range(i + 1, j):
                if (i, k, j) in costs and (i, k) in values and (k, j) in values:
                    options.append((max(costs[i, k, j], values[i, k], values[k, j]), k))
            if options:
                values[i, j], choices[i, j] = min(options)
    if (0, len(ring) - 1) not in values:
        return None, frozenset(), len(costs)

    def reconstruct(i, j):
        if j == i + 1:
            return frozenset()
        k = choices[i, j]
        return reconstruct(i, k) | reconstruct(k, j) | {canonical((ring[i], ring[k], ring[j]), points)}

    return values[0, len(ring) - 1], reconstruct(0, len(ring) - 1), len(costs)


def fixture(vertices, center=(F(0), F(0))):
    points = {str(i): (F(x), F(y), F(0)) for i, (x, y) in enumerate(vertices)}
    ring = list(points)
    with_center = dict(points, u=(F(center[0]), F(center[1]), F(0)))
    faces = {canonical(("u", a, b), with_center) for a, b in zip(ring, ring[1:] + ring[:1])}
    assert all(orient(with_center["u"], points[a], points[b]) > 0 for a, b in zip(ring, ring[1:] + ring[:1]))
    return ring, with_center, faces


def check_dp_against_ears():
    fixtures = {
        "square": fixture([(-1, -1), (1, -1), (1, 1), (-1, 1)]),
        "grid_hexagon": fixture([(-1, -1), (0, -1), (1, 0), (1, 1), (0, 1), (-1, 0)]),
        "wide_hexagon": fixture([(2, 0), (1, 2), (-1, 2), (-2, 0), (-1, -2), (1, -2)]),
        "concave_pentagon": fixture([(0, 0), (3, 0), (3, 3), (F(3, 2), 1), (0, 3)], (F(3, 2), F(1, 2))),
    }
    comparisons = 0
    summaries = []
    for name, (ring, original, old_faces) in fixtures.items():
        boundary_points = {v: original[v] for v in ring}
        all_triangulations = ear_triangulations(ring, boundary_points)
        area = sum(orient(*(original[v] for v in face)) for face in old_faces)
        for triangulation in all_triangulations:
            validate(boundary_points, triangulation, boundary(old_faces), area)
            assert len(triangulation) == len(old_faces) - 2
        shape_summary = {}
        for floor in (0, 30, 45):
            legal = [t for t in all_triangulations if all(shape_allowed(f, boundary_points, floor) for f in t)]
            shape_summary[str(floor)] = len(legal)
            for profile in range(4):
                points = {v: (*original[v][:2], F((int(v) * 3 + profile) % 5 - 2, 3)) for v in ring}
                queries = {original["u"][:2]}
                queries.update(tuple(sum(original[v][axis] for v in face) / 3 for axis in (0, 1)) for face in old_faces)
                samples = [(q, F((index + profile) % 4 - 1, 2)) for index, q in enumerate(sorted(queries))]
                expected = min((max(triangle_cost(f, points, samples) for f in t) for t in legal), default=None)
                actual, selected, _ = solve_cavity(ring, points, samples, floor)
                assert actual == expected
                if expected is not None:
                    assert selected in legal
                    assert max(triangle_cost(f, points, samples) for f in selected) == expected
                comparisons += 1
        summaries.append({"fixture": name, "degree": len(ring), "ear_triangulations": len(all_triangulations), "legal_by_angle_floor": shape_summary})
    return {"status": "通过", "comparisons": comparisons, "fixtures": summaries, "scope": "固定边界高度、有限样本最大高度误差；不验证真实透视与连续质量"}


def check_irreducible_cavities():
    ring, points, faces = fixture([(2, 0), (1, 2), (-1, 2), (-2, 0), (-1, -2), (1, -2)])
    assert all(shape_allowed(face, points, 45) for face in faces)
    ears = [canonical((ring[i - 1], ring[i], ring[(i + 1) % 6]), points) for i in range(6)]
    assert all(not shape_allowed(face, points, 30) for face in ears)
    value, _, _ = solve_cavity(ring, points, [], 30)
    assert value is None

    ring, points, faces = fixture([(-1, -1), (0, -1), (1, 0), (1, 1), (0, 1), (-1, 0)])
    points["u"] = (F(0), F(0), F(1))
    samples = [((F(0), F(0)), F(1))]
    value, selected, _ = solve_cavity(ring, points, samples, 30)
    assert value == 1 and selected
    assert all(points[v][2] == 0 for v in ring)
    assert height_on(points, next(iter(faces)), (F(0), F(0))) == 1
    return {"status": "通过", "shape_obstruction": {"old_minimum_angle_at_least": 45, "requested_floor": 30, "rejected_ears": 6, "result": "全部无新增顶点重剖分不满足形状下限"}, "quality_obstruction": {"boundary_height_range": [0, 0], "reference_at_removed_vertex": 1, "optimal_sampled_error": str(value), "rejected_quality_threshold": "1/2"}}


def make_grid():
    points = {f"{x},{y}": (F(x), F(y), F(0)) for x in range(5) for y in range(3)}
    faces = set()
    for x in range(4):
        for y in range(2):
            a, b, c, d = f"{x},{y}", f"{x + 1},{y}", f"{x + 1},{y + 1}", f"{x},{y + 1}"
            faces.update((canonical((a, b, c), points), canonical((a, c, d), points)))
    return points, faces


def bilinear_bump(point):
    x, y = point
    return max(F(0), 1 - abs(2 * x - 7)) * max(F(0), 1 - abs(2 * y - 1))


def mesh_height(points, faces, query):
    for face in sorted(faces):
        if inside_triangle(query, face, points):
            return height_on(points, face, query)
    raise AssertionError("采样点没有对应面")


def check_budget_exchange():
    points, faces = make_grid()
    outside = boundary(faces)
    validate(points, faces, outside, F(16))
    assert interior_degree_three(faces) == []
    ring = ["0,0", "1,0", "2,1", "2,2", "1,2", "0,1"]
    donor = star(faces, "1,1")
    assert len(donor) == 6 and len(neighbors(faces, "1,1")) == 6
    samples = [(points["1,1"][:2], F(0))]
    samples.extend((tuple(sum(points[v][i] for v in face) / 3 for i in (0, 1)), F(0)) for face in donor)
    cost, replacement, _ = solve_cavity(ring, points, samples, 30)
    assert cost == 0 and len(replacement) == 4
    coarse_points = {v: point for v, point in points.items() if v != "1,1"}
    coarse_faces = (faces - donor) | replacement
    validate(coarse_points, coarse_faces, outside, F(16))
    a, b, c, d = "3,0", "4,0", "4,1", "3,1"
    receiver = {canonical((a, b, c), points), canonical((a, c, d), points)}
    assert not donor & receiver
    final_points = dict(coarse_points, q=(F(7, 2), F(1, 2), F(1)))
    children = {canonical(face, final_points) for face in ((a, b, "q"), (b, c, "q"), (c, d, "q"), (d, a, "q"))}
    final_faces = (coarse_faces - receiver) | children
    validate(final_points, final_faces, outside, F(16))
    assert all(shape_allowed(face, final_points, 30) for face in final_faces)
    assert [len(faces), len(coarse_faces), len(final_faces)] == [16, 14, 16]
    queries = [(F(x, 8), F(y, 8)) for x in range(33) for y in range(17)]
    old_errors = [abs(mesh_height(points, faces, q) - bilinear_bump(q)) for q in queries]
    new_errors = [abs(mesh_height(final_points, final_faces, q) - bilinear_bump(q)) for q in queries]
    assert max(old_errors) == 1 and max(new_errors) == F(1, 4)
    assert max(new_errors) + F(1, 2) <= max(old_errors)
    maxima = [[str(x), str(y)] for (x, y), error in zip(queries, new_errors) if error == max(new_errors)]
    return {"status": "通过", "initial_interior_degree_three_count": 0, "donor_face_change": [6, 4], "receiver_face_change": [2, 4], "active_triangle_trajectory": [16, 14, 16], "budget": 16, "angle_floor_degrees": 30, "samples": len(queries), "sampled_height_error": [str(max(old_errors)), str(max(new_errors))], "new_maximum_points": maxima, "eta": "1/2", "scope": "固定 16 面解析夹具；连续 1/4 上界另由文档代数证明，不是自然轨迹或性能结果"}


def main():
    parser = argparse.ArgumentParser(description="局部空腔重剖分的精确有限核查")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    source = Path(__file__)
    results = {
        "schema": 1,
        "source_sha256": {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in (source, source.with_name("verify_bounded_patch_refit.py"))},
        "CR01_dp_vs_ear_enumeration": check_dp_against_ears(),
        "CR02_irreducible_cavities": check_irreducible_cavities(),
        "CR03_budget_exchange": check_budget_exchange(),
        "limitations": "纸面推导的有限复核；没有一般形式化证明、生产实现或 CPU 性能测量。",
    }
    text = json.dumps(results, ensure_ascii=False, indent=2) + "\n"
    if args.output:
        args.output.write_text(text, encoding="utf-8")
    print(text, end="")


if __name__ == "__main__":
    main()
