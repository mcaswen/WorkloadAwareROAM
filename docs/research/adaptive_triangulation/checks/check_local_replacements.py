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


def attribute_at(points, faces, attributes, query):
    """固定参数点的分片线性属性；共享边上的插值必须一致。"""
    values = []
    for face in faces:
        a, b, c = (points[v] for v in face)
        area = orient(a, b, c)
        weights = (orient(query, b, c) / area, orient(a, query, c) / area,
                   orient(a, b, query) / area)
        if all(weight >= 0 for weight in weights):
            values.append(sum(weight * attributes[v] for weight, v in zip(weights, face)))
    assert values and all(value == values[0] for value in values)
    return values[0]


def run_batch():
    """三块分离补丁的实际键值更新，不用声明净额替代真实面数。"""
    square = {i: tuple(map(F, p)) for i, p in enumerate([(0, 0), (2, 0), (2, 2), (0, 2), (1, 1)])}
    pentagon = {i: tuple(map(F, p)) for i, p in enumerate([(0, 0), (3, 0), (4, 2), (2, 4), (0, 3), (2, 2)])}
    coarse = [(0, 1, 2), (0, 2, 3)]
    split = [(0, 1, 4), (1, 2, 4), (0, 4, 3), (4, 2, 3)]
    flipped = [(0, 1, 3), (1, 2, 3)]
    fan = [(i, (i + 1) % 5, 5) for i in range(5)]
    removed = [(0, 1, 2), (0, 2, 3), (0, 3, 4)]
    fixtures = [
        ("split", square, coarse, split, {0: F(1), 1: F(1), 2: F(1), 3: F(1), 4: F(0)}, 2),
        ("flip", square, coarse, flipped, {0: F(1), 1: F(0), 2: F(1), 3: F(0)}, 0),
        ("remove", pentagon, fan, removed, {i: F(i == 5) for i in range(6)}, -2),
    ]
    state = {}
    transactions = []
    records = []
    observations = []
    for index, (name, local, old, new, attributes, delta) in enumerate(fixtures):
        points = {v: (xy[0] + 10 * index, xy[1]) for v, xy in local.items()}
        records.append(replacement(name, points, old, new, delta))
        before = {}
        after = {}
        for label, faces, destination in (("old", old, before), ("new", new, after)):
            for face_index, face in enumerate(faces):
                destination[(index, "face", label, face_index)] = face
            for vertex in active_points(points, faces):
                destination[(index, "vertex", vertex)] = (*points[vertex], attributes[vertex])
        state.update(before)
        writes = {key for key in before.keys() | after.keys() if before.get(key) != after.get(key)}
        # 包含创建缺席检查和全部旧几何/属性；固定分区使远处面无关。
        reads = set(before) | set(after)
        transactions.append({"name": name, "reads": reads, "writes": writes,
                             "before": before, "after": after})
        queries = [(F(1), F(1)), (F(1), F(1, 2)), (F(1, 2), F(1))]
        if name == "remove":
            queries = [(F(2), F(2)), (F(2), F(1)), (F(1), F(1))]
        for q, weight, target in zip(queries, [F(1), F(2), F(0)], [F(0), F(1, 16), F(1, 4)]):
            observations.append((index, (q[0] + 10 * index, q[1]), weight, target))

    def apply_transaction(current, transaction):
        assert all(current.get(key) == transaction["before"].get(key) for key in transaction["reads"])
        result = dict(current)
        for key in transaction["writes"]:
            if key in transaction["after"]:
                result[key] = transaction["after"][key]
            else:
                result.pop(key, None)
        return result

    def losses(current):
        result = []
        for index, query, _, _ in observations:
            faces = [value for key, value in current.items() if key[:2] == (index, "face")]
            vertices = {key[2]: value for key, value in current.items() if key[:2] == (index, "vertex")}
            value = attribute_at({v: record[:2] for v, record in vertices.items()}, faces,
                                 {v: record[2] for v, record in vertices.items()}, query)
            result.append(value * value)  # 固定独立参考属性为0；不是地形高度。
        return result

    def potential(values):
        return sum(weight * max(value - target, F(0))
                   for value, (_, _, weight, target) in zip(values, observations))

    def count(current):
        return sum(key[1] == "face" for key in current)

    def adjacency(current):
        result = {}
        for index in range(3):
            faces = [value for key, value in sorted(current.items()) if key[:2] == (index, "face")]
            check_links(faces)
            result[index] = edge_map(faces)
        return result

    for a, b in itertools.combinations(transactions, 2):
        assert not a["writes"] & (b["reads"] | b["writes"])
        assert not b["writes"] & (a["reads"] | a["writes"])
    old_losses = losses(state)
    old_potential = potential(old_losses)
    single = []
    for transaction in transactions:
        updated = apply_transaction(state, transaction)
        values = losses(updated)
        assert all(value <= max(old, observation[3]) for value, old, observation in zip(values, old_losses, observations))
        single.append({"name": transaction["name"], "delta": count(updated) - count(state),
                       "losses": list(map(str, values)), "gain": str(old_potential - potential(values)),
                       "reads": sorted(map(str, transaction["reads"])),
                       "writes": sorted(map(str, transaction["writes"]))})
    permutations = []
    final_reference = None
    for order in itertools.permutations(range(3)):
        current = state
        counts = [count(current)]
        for index in order:
            current = apply_transaction(current, transactions[index])
            counts.append(count(current))
        if final_reference is None:
            final_reference = current
        assert current == final_reference
        assert adjacency(current) == adjacency(final_reference)
        assert count(current) == count(state) == 9
        gain = old_potential - potential(losses(current))
        assert gain == sum(F(item["gain"]) for item in single)
        permutations.append({"order": order, "counts": counts, "peak": max(counts),
                             "final_potential": str(potential(losses(current))), "gain": str(gain)})

    # 质量反例以独立精确算术核对，避免把依赖遗漏误称为一般定理失败。
    coupled = lambda x, y: (x + y - F(3, 4)) ** 2
    assert coupled(1, 0) < coupled(0, 0) and coupled(0, 1) < coupled(0, 0) < coupled(1, 1)
    assert -F(1) * 1 > -F(1) * 4  # 负权重破坏势不增。
    assert F(0) * 100 == 0  # 零权重不证明观察点达标。
    # 同一超标值由两个幂等清零器修复：最终降1，而单项收益之和为2。
    assert (1 - 0) < (1 - 0) + (1 - 0)
    # 环境改变：旧目标1下损失1达标；新目标0下势增至1，几何完全未改。
    assert max(F(1) - 1, 0) < max(F(1) - 0, 0)
    negative = [
        {"name": "coupled_loss_missing_reads", "old": "9/16", "each": "1/16", "together": "25/16"},
        {"name": "negative_weight", "old_potential": "-4", "next_potential": "-1"},
        {"name": "zero_weight_masks_violation", "loss": "100", "potential": "0"},
        {"name": "shared_improvement_double_count", "batch_gain": "1", "sum_single_gains": "2"},
        {"name": "changed_environment", "old_potential": "0", "new_potential": "1"},
    ]
    return {"positive": records, "negative": negative, "single_transactions": single,
            "observations": [{"patch": i, "q": list(map(str, q)), "weight": str(w), "target": str(t),
                              "reference_attribute": "0"} for i, q, w, t in observations],
            "old_losses": list(map(str, old_losses)), "old_potential": str(old_potential),
            "final_losses": list(map(str, losses(final_reference))), "permutations": permutations,
            "scope": "三个独立圆盘组件；有理属性插值损失；不是一般几何或生产并发证明"}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--stage", choices=("att-01", "att-03"), default="att-01")
    args = parser.parse_args()
    start = time.perf_counter()
    result = run() if args.stage == "att-01" else run_batch()
    result["stage"] = args.stage
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
