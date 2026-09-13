"""复核有界补丁推导的有限几何例子，保留反例及可重现结果。

这里只使用精确有理数和小网格，不是生产网格实现或一般定理证明器。
"""

import argparse
import hashlib
import itertools
import json
from collections import defaultdict
from fractions import Fraction as F
from pathlib import Path


def orient(a, b, c):
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])


def canonical(face, points):
    a, b, c = face
    assert orient(points[a], points[b], points[c]) != 0
    if orient(points[a], points[b], points[c]) < 0:
        face = (a, c, b)
    return min(face[i:] + face[:i] for i in range(3))


def edge_map(faces):
    edges = defaultdict(list)
    for face in faces:
        for a, b in zip(face, face[1:] + face[:1]):
            edges[tuple(sorted((a, b)))].append((a, b, face))
    return edges


def boundary(faces):
    return frozenset(edge for edge, uses in edge_map(faces).items() if len(uses) == 1)


def star(faces, vertex):
    return frozenset(face for face in faces if vertex in face)


def neighbors(faces, vertex):
    return set().union(*(set(face) for face in star(faces, vertex))) - {vertex}


def interior_degree_three(faces):
    outside = set().union(*(set(edge) for edge in boundary(faces)))
    vertices = set().union(*(set(face) for face in faces))
    return sorted(v for v in vertices - outside if len(neighbors(faces, v)) == 3)


def on_segment(a, b, c):
    return orient(a, b, c) == 0 and all(min(a[i], b[i]) <= c[i] <= max(a[i], b[i]) for i in (0, 1))


def validate(points, faces, expected_boundary, expected_area2):
    """独立检查嵌入、边关联和圆盘计数，不借助插入或回收的内部记录。"""
    assert len(faces) == len(set(faces))
    vertices = set().union(*(set(face) for face in faces))
    assert vertices == set(points)
    assert all(orient(*(points[v] for v in face)) > 0 for face in faces)
    assert sum(orient(*(points[v] for v in face)) for face in faces) == expected_area2
    edges = edge_map(faces)
    assert len(vertices) - len(edges) + len(faces) == 1
    assert boundary(faces) == expected_boundary
    for uses in edges.values():
        assert len(uses) in (1, 2)
        if len(uses) == 2:
            assert uses[0][:2] == uses[1][:2][::-1]
    for edge in edges:
        for vertex in vertices - set(edge):
            assert not on_segment(points[edge[0]], points[edge[1]], points[vertex])
    for first, second in itertools.combinations(edges, 2):
        if set(first) & set(second):
            continue
        a, b = (points[v] for v in first)
        c, d = (points[v] for v in second)
        assert not (orient(a, b, c) * orient(a, b, d) < 0 and orient(c, d, a) * orient(c, d, b) < 0)


def centroid(points, face):
    return tuple(sum(points[v][i] for v in face) / 3 for i in range(3))


def insert(points, faces, face, vertex, height=None):
    result_points = dict(points)
    q = centroid(points, face)
    result_points[vertex] = q if height is None else (q[0], q[1], F(height))
    a, b, c = face
    children = {canonical(f, result_points) for f in ((a, b, vertex), (b, c, vertex), (c, a, vertex))}
    return result_points, (faces - {face}) | children


def remove_current(points, faces, vertex):
    assert vertex in interior_degree_three(faces)
    coarse = canonical(tuple(sorted(neighbors(faces, vertex))), points)
    result_points = {v: point for v, point in points.items() if v != vertex}
    return result_points, (faces - star(faces, vertex)) | {coarse}


def height_on(points, face, point):
    a, b, c = (points[v] for v in face)
    area = orient(a, b, c)
    weights = (orient(point, b, c) / area, orient(a, point, c) / area, orient(a, b, point) / area)
    assert all(weight >= 0 for weight in weights)
    return sum(weight * points[v][2] for weight, v in zip(weights, face))


def triangle_fixture():
    points = {"a": (F(0), F(0), F(0)), "b": (F(3), F(0), F(0)), "c": (F(0), F(3), F(0))}
    return points, {("a", "b", "c")}


def check_history_and_degree():
    points, faces = triangle_fixture()
    expected_boundary = boundary(faces)
    old_points, old_faces = insert(points, faces, next(iter(faces)), "v")
    target = next(face for face in old_faces if set(face) == {"a", "b", "v"})
    refined_points, refined_faces = insert(old_points, old_faces, target, "q", F(1, 2))
    refined_points["v"] = (*refined_points["v"][:2], F(1))
    assert len(neighbors(old_faces, "v")) == 3
    assert len(neighbors(refined_faces, "v")) == 4
    assert len(neighbors(refined_faces, "q")) == 3
    coarse_points, coarse_faces = remove_current(refined_points, refined_faces, "q")
    validate(coarse_points, coarse_faces, expected_boundary, F(9))
    assert coarse_faces == old_faces and coarse_points != old_points
    outside = refined_faces - star(refined_faces, "q")
    wrong_points = dict(coarse_points)
    wrong_points["v"] = old_points["v"]
    changed = []
    for face in sorted(outside):
        query = centroid(refined_points, face)
        before = height_on(refined_points, face, query)
        assert height_on(coarse_points, face, query) == before
        wrong = height_on(wrong_points, face, query)
        assert wrong != before
        changed.append({"face": face, "current_height": str(before), "historical_reset_height": str(wrong)})
    # 只保留原顶点时，插点的零增量确实复现旧面；这不等于回收包含旧曲面。
    zero_points, zero_faces = insert(old_points, old_faces, target, "q")
    for face in zero_faces - old_faces:
        query = centroid(zero_points, face)
        assert height_on(zero_points, face, query) == height_on(old_points, target, query)
    return {"status": "通过；错误历史恢复被反例排除", "center_degree": [3, 4, 3], "face_count": [3, 5, 3], "outside_faces_changed_by_wrong_reset": changed}


def check_donor_geometry():
    states = removals = boundary_exclusions = 0
    for seed in range(4):
        points, faces = triangle_fixture()
        expected_boundary = boundary(faces)
        for step in range(9):
            validate(points, faces, expected_boundary, F(9))
            donors = interior_degree_three(faces)
            for first, second in itertools.combinations(donors, 2):
                assert not star(faces, first) & star(faces, second)
            for donor in donors:
                next_points, next_faces = remove_current(points, faces, donor)
                validate(next_points, next_faces, expected_boundary, F(9))
                assert len(next_faces) == len(faces) - 2
                assert all(next_points[v] == points[v] for v in next_points)
                removals += 1
            if step == 1:
                # 外边界三价点可以邻接内部三价点，不能省略“内部”前提。
                assert len(neighbors(faces, "a")) == 3 and "a" not in donors
                assert any(v in neighbors(faces, "a") for v in donors)
                boundary_exclusions += 1
            states += 1
            if step < 8:
                target = sorted(faces)[(seed * 5 + step * 3) % len(faces)]
                points, faces = insert(points, faces, target, f"p{step}", F((seed + step) % 5 - 2, 3))
    return {"status": "通过", "states": states, "independently_validated_removals": removals, "boundary_counterexamples": boundary_exclusions, "construction": "4 条确定性面重心插入序列，每条 0～8 次插入；未假定最小角约束"}


def square_fixture():
    points = {"a": (F(0), F(0), F(0)), "b": (F(2), F(0), F(0)), "c": (F(2), F(2), F(0)), "d": (F(0), F(2), F(0))}
    return points, {("a", "b", "c"), ("a", "c", "d")}


def check_disjoint_application():
    points, faces = square_fixture()
    expected_boundary = boundary(faces)
    points, faces = insert(points, faces, ("a", "b", "c"), "u")
    points, faces = insert(points, faces, ("a", "c", "d"), "v")
    assert not star(faces, "u") & star(faces, "v")
    specifications = []
    for center, new_vertex, height in (("u", "x", F(1)), ("v", "y", F(-1))):
        target = next(face for face in faces if {"a", "c", center} == set(face))
        specifications.append((center, new_vertex, height, target))

    def apply_order(order):
        state_points, state_faces = dict(points), set(faces)
        for center, new_vertex, height, target in order:
            state_points, state_faces = insert(state_points, state_faces, target, new_vertex, height / 2)
            state_points[center] = (*state_points[center][:2], height)
        validate(state_points, state_faces, expected_boundary, F(8))
        return state_points, state_faces

    forward = apply_order(specifications)
    backward = apply_order(list(reversed(specifications)))
    assert forward == backward
    assert len(neighbors(forward[1], "a")) - len(neighbors(faces, "a")) == 2
    assert len(neighbors(forward[1], "c")) - len(neighbors(faces, "c")) == 2
    return {"status": "通过", "orders_compared": 2, "shared_boundary_degree_delta": {"a": 2, "c": 2}, "scope": "串行排列逻辑等价；共享度数需归约，不是并发内存安全测试"}


def check_shape_deadlock():
    points = {f"{x}{y}": (F(x), F(y), F(0)) for x in range(3) for y in range(3)}
    faces = set()
    for x, y in itertools.product(range(2), repeat=2):
        a, b, c, d = f"{x}{y}", f"{x + 1}{y}", f"{x + 1}{y + 1}", f"{x}{y + 1}"
        faces.update((canonical((a, b, c), points), canonical((a, c, d), points)))
    validate(points, faces, boundary(faces), F(8))
    assert interior_degree_three(faces) == []
    assert len(neighbors(faces, "11")) == 6
    for face in faces:
        angle_classes = []
        for i in range(3):
            origin, first, second = (points[face[j % 3]] for j in (i, i + 1, i + 2))
            u = (first[0] - origin[0], first[1] - origin[1])
            v = (second[0] - origin[0], second[1] - origin[1])
            dot = sum(a * b for a, b in zip(u, v))
            lengths = sum(a * a for a in u) * sum(b * b for b in v)
            assert dot == 0 or (dot > 0 and 2 * dot * dot == lengths)
            angle_classes.append(90 if dot == 0 else 45)
        assert sorted(angle_classes) == [45, 45, 90]
    return {"status": "通过；受限操作集停滞反例成立", "triangles": len(faces), "interior_degree_three_donors": 0, "angles_degrees": [45, 45, 90], "angle_floor_degrees": 30, "reason": "1→3 必须把原 45° 角分为两个至少 30° 的角，不可能；不否定其他原语"}


def check_four_rotations():
    # 本轮只检查新的 D=3、b=4 分支，不重复此前 D=2 的三次旋转矩阵。
    masks = range(15)
    matrices = minimum_best = 0
    for rows in itertools.product(masks, repeat=4):
        valid = [sum(not (rows[i] & (1 << ((i + shift) % 4))) for i in range(4)) for shift in range(4)]
        best = max(valid)
        assert best >= 1
        minimum_best = best if matrices == 0 else min(minimum_best, best)
        matrices += 1
    return {"status": "通过", "D": 3, "b": 4, "forbidden_matrices": matrices, "minimum_best_rotation_width": minimum_best, "scope": "有限组合核查，不验证一般 D 的定理、几何选择器或实际并行宽度"}


def check_quantized_descent():
    eta = F(1, 2)
    arrays = list(itertools.product((F(0), F(1, 2), F(1), F(3, 2), F(2)), repeat=3))
    cases = 0
    for before in arrays:
        for after in arrays:
            if max(after) + eta > max(before):
                continue
            old_all = before + (F(3),)
            new_all = after + (F(3),)
            old_sorted = sorted((value // eta for value in old_all), reverse=True)
            new_sorted = sorted((value // eta for value in new_all), reverse=True)
            assert new_sorted < old_sorted
            assert max(old_all) == max(new_all)
            cases += 1
    return {"status": "通过", "accepted_error_transitions": cases, "eta": str(eta), "unchanged_global_maximum": "3", "scope": "固定样本误差向量核查；不证明这些向量都有实际网格实现，也不证明质量达标"}


def main():
    parser = argparse.ArgumentParser(description="有界补丁推导的精确有限核查")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    results = {
        "schema": 1,
        "script_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "arithmetic": "几何与高度使用 fractions.Fraction；组合检查使用整数",
        "BP01_history_and_degree": check_history_and_degree(),
        "BP02_current_donor_geometry": check_donor_geometry(),
        "BP03_disjoint_application": check_disjoint_application(),
        "BP04_shape_deadlock": check_shape_deadlock(),
        "BP05_four_rotations": check_four_rotations(),
        "BP06_quantized_descent": check_quantized_descent(),
        "limitations": "非 Lean 证明；不计算连续屏幕误差，不执行真实线程，不测 CPU 性能。",
    }
    content = json.dumps(results, ensure_ascii=False, indent=2) + "\n"
    if args.output is not None:
        args.output.write_text(content, encoding="utf-8")
    print(content, end="")


if __name__ == "__main__":
    main()
