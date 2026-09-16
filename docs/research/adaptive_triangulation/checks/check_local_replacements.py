"""有限局部替换夹具；有理二维几何复用历史纯函数，不运行历史矩阵。"""

import argparse
import hashlib
import itertools
import json
import resource
import sys
import time
from fractions import Fraction as F
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
GEOMETRY = ROOT / "docs/research/cpu_refinement/checks/verify_bounded_patch_refit.py"
sys.path.insert(0, str(GEOMETRY.parent))
from verify_bounded_patch_refit import boundary, edge_map, orient, validate


def complex_of(faces):
    """包含空单形；link比较必须检查边等单形，而不只是共有顶点。"""
    result = {frozenset()}
    for face in faces:
        for size in (1, 2, 3):
            result.update(frozenset(part) for part in itertools.combinations(face, size))
    return result


def link(faces, simplex):
    simplices = complex_of(faces)
    simplex = frozenset(simplex)
    return {part for part in simplices if not part & simplex and part | simplex in simplices}


def check_links(faces):
    vertices = set().union(*(set(face) for face in faces))
    boundary_vertices = set().union(*(set(edge) for edge in boundary(faces)))
    for vertex in vertices:
        local = link(faces, {vertex})
        nodes = set().union(*(set(part) for part in local))
        graph = {node: set() for node in nodes}
        for part in local:
            if len(part) == 2:
                a, b = tuple(part)
                graph[a].add(b)
                graph[b].add(a)
        reached = set()
        pending = [next(iter(nodes))]
        while pending:
            node = pending.pop()
            if node not in reached:
                reached.add(node)
                pending.extend(graph[node] - reached)
        assert reached == nodes, "disconnected_vertex_link"
        degrees = [len(graph[node]) for node in nodes]
        if vertex in boundary_vertices:
            assert degrees.count(1) == 2 and all(d in (1, 2) for d in degrees)
        else:
            assert all(d == 2 for d in degrees)


def active_points(points, faces):
    used = set().union(*(set(face) for face in faces))
    return {vertex: points[vertex] for vertex in used}


def replacement(name, points, old, new, expected_delta, fixed_boundary=True):
    old_points = active_points(points, old)
    new_points = active_points(points, new)
    area = sum(orient(*(points[v] for v in face)) for face in old)
    for vertices, faces in ((old_points, old), (new_points, new)):
        validate(vertices, faces, boundary(faces), area)
        check_links(faces)
    if fixed_boundary:
        assert boundary(old) == boundary(new)
    assert len(new) - len(old) == expected_delta
    counts = []
    for vertices, faces in ((old_points, old), (new_points, new)):
        b = len(set().union(*(set(edge) for edge in boundary(faces))))
        interior = len(vertices) - b
        assert len(faces) == 2 * interior + b - 2
        counts.append({"vertices": len(vertices), "faces": len(faces), "boundary_vertices": b})
    return {
        "name": name, "delta": expected_delta, "fixed_full_boundary": fixed_boundary,
        "points": {str(k): [str(c) for c in value] for k, value in points.items()},
        "old_faces": old, "new_faces": new, "counts": counts,
    }


def expect_rejection(name, action):
    try:
        action()
    except AssertionError:
        return {"name": name, "rejected": True}
    raise AssertionError("反例未被拒绝: " + name)


def run():
    points = {0: (F(0), F(0)), 1: (F(2), F(0)), 2: (F(2), F(2)),
              3: (F(0), F(2)), 4: (F(1), F(1))}
    coarse = [(0, 1, 2), (0, 2, 3)]
    split = [(0, 1, 4), (1, 2, 4), (0, 4, 3), (4, 2, 3)]
    flipped = [(0, 1, 3), (1, 2, 3)]
    positive = [replacement("internal_edge_split", points, coarse, split, 2),
                replacement("edge_collapse_4_to_0", points, split, coarse, -2),
                replacement("edge_flip", points, coarse, flipped, 0)]
    assert link(split, {0}) & link(split, {4}) == link(split, {0, 4})
    # 真正执行端点识别，避免仅以相同面数冒充折叠。
    collapsed = [tuple(0 if v == 4 else v for v in face) for face in split]
    collapsed = [face for face in collapsed if len(set(face)) == 3]
    assert {frozenset(f) for f in collapsed} == {frozenset(f) for f in coarse}

    tri_points = {0: (F(0), F(0)), 1: (F(2), F(0)), 2: (F(0), F(2)), 3: (F(1), F(0))}
    triangle = [(0, 1, 2)]
    boundary_split = [(0, 3, 2), (3, 1, 2)]
    positive += [replacement("free_boundary_split", tri_points, triangle, boundary_split, 1, False),
                 replacement("free_boundary_collapse", tri_points, boundary_split, triangle, -1, False)]
    assert boundary(triangle) & boundary(boundary_split) == {(0, 2), (1, 2)}
    assert link(boundary_split, {0}) & link(boundary_split, {3}) == link(boundary_split, {0, 3})

    pentagon = {i: tuple(map(F, point)) for i, point in enumerate([(0, 0), (3, 0), (4, 2), (2, 4), (0, 3), (2, 2)])}
    fan = [(i, (i + 1) % 5, 5) for i in range(5)]
    retriangulated = [(0, 1, 2), (0, 2, 3), (0, 3, 4)]
    positive.append(replacement("degree_five_removal", pentagon, fan, retriangulated, -2))
    positive.append(replacement("complete_roam_diamond", points, coarse, split, 2))

    # g(u,v)=(0,u,v) 单射且保留面积；同一(x,y)对应不同z，不能靠高度场求值。
    vertical = {key: (F(0), *value) for key, value in points.items()}
    assert vertical[0][:2] == vertical[3][:2] and vertical[0][2] != vertical[3][2]
    vertical_record = replacement("vertical_surface_split", points, coarse, split, 2)
    vertical_record["world_points"] = {str(key): list(map(str, value)) for key, value in vertical.items()}
    vertical_record["embedding_certificate"] = "injective linear map; exterior restricted to x>=2"
    positive.append(vertical_record)

    negative = []
    hanging = [(0, 1, 4), (1, 2, 4), (0, 2, 3)]
    negative.append(expect_rejection("one_sided_internal_split", lambda: validate(points, hanging, boundary(coarse), F(8))))
    concave = {i: tuple(map(F, p)) for i, p in enumerate([(0, 0), (2, 0), (F(1, 2), F(1, 2)), (0, 2)])}
    negative.append(expect_rejection("concave_flip", lambda: validate(concave, flipped, boundary(coarse), F(2))))
    tetrahedron = [(0, 1, 2), (0, 3, 1), (0, 2, 3), (1, 3, 2)]
    common = link(tetrahedron, {0}) & link(tetrahedron, {1})
    edge_link = link(tetrahedron, {0, 1})
    assert common != edge_link and common - edge_link == {frozenset({2, 3})}
    negative.append({"name": "tetrahedron_collapse_link_failure", "extra_simplex": [2, 3]})

    # 新补丁可作为图像面局部嵌入，但其内部点与不相关保留面相交。
    patch_face = [(F(0), F(0), F(0)), (F(2), F(0), F(0)), (F(1), F(1), F(2))]
    external_face = [(F(3, 4), F(1, 4), F(1)), (F(5, 4), F(1, 4), F(1)), (F(1), F(3, 4), F(1))]
    weights = [F(1, 4), F(1, 4), F(1, 2)]
    interpolate = lambda face: tuple(sum(w * p[k] for w, p in zip(weights, face)) for k in range(3))
    witness = interpolate(patch_face)
    assert witness == interpolate(external_face) == (F(1), F(1, 2), F(1))
    negative.append({"name": "remote_surface_intersection", "witness": list(map(str, witness)),
                     "patch_face": [[str(c) for c in p] for p in patch_face],
                     "external_face": [[str(c) for c in p] for p in external_face]})
    return {"positive": positive, "negative": negative,
            "scope": "有限组合与精确局部几何；不是一般几何或Lean证明"}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    start = time.perf_counter()
    result = run()
    result["seconds"] = time.perf_counter() - start
    result["peak_rss_bytes"] = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss * 1024
    result["sources"] = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                         for p in (Path(__file__).resolve(), GEOMETRY)}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"positive": len(result["positive"]), "negative": len(result["negative"]),
                      "seconds": result["seconds"], "peak_rss_bytes": result["peak_rss_bytes"]}))


if __name__ == "__main__":
    main()
