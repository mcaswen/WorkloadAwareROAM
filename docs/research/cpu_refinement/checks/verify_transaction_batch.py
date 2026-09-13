"""验证同代事务的组合与局部续接，不构造持久或并行运行时。"""

import hashlib
import itertools
import json
import math
import time
from dataclasses import dataclass
from fractions import Fraction as F
from pathlib import Path

from verify_bounded_patch_refit import boundary, canonical, edge_map, height_on, star, validate
from verify_cavity_retriangulation import inside_triangle, solve_cavity


@dataclass(frozen=True)
class Transaction:
    """局部替换持有独立结果；访问集合描述核心几何，派生项在屏障后恢复。"""

    key: str
    removed: frozenset
    inserted: frozenset
    deleted_vertices: frozenset
    vertices: dict
    reads: frozenset
    writes: frozenset


def grid(width=9):
    points = {f"{x},{y}": (F(x), F(y), F(0)) for x in range(width + 1) for y in range(3)}
    faces = set()
    for x in range(width):
        for y in range(2):
            a, b, c, d = (f"{x},{y}", f"{x+1},{y}", f"{x+1},{y+1}", f"{x},{y+1}")
            faces.update((canonical((a, b, c), points), canonical((a, c, d), points)))
    return points, frozenset(faces)


def resources(removed, inserted, deleted, vertices):
    support_vertices = set().union(*(set(f) for f in removed))
    reads = {("height", v) for v in support_vertices} | {("face", f) for f in removed}
    # 整边关联是普通资源；共享边写在此保守拒绝，而非假设两次写可交换
    edges = set(edge_map(removed)) | set(edge_map(inserted))
    reads |= {("edge", e) for e in edges}
    writes = {("edge", e) for e in edges} | {("face", f) for f in removed | inserted}
    writes |= {("height", v) for v in set(deleted) | set(vertices)}
    return frozenset(reads), frozenset(writes)


def exchange(points, faces, offset, key):
    center = f"{offset+1},1"
    ring = [f"{offset},0", f"{offset+1},0", f"{offset+2},1", f"{offset+2},2", f"{offset+1},2", f"{offset},1"]
    donor = star(faces, center)
    cost, replacement, _ = solve_cavity(ring, points, [], 30)
    assert cost == 0 and len(donor) - len(replacement) == 2
    a, b, c, d = f"{offset+3},0", f"{offset+4},0", f"{offset+4},1", f"{offset+3},1"
    receiver = frozenset({canonical((a, b, c), points), canonical((a, c, d), points)})
    added = {key: (F(2*offset+7, 2), F(1, 2), F(1))}
    target_points = dict(points, **added)
    children = frozenset(canonical(f, target_points) for f in ((a,b,key), (b,c,key), (c,d,key), (d,a,key)))
    removed, inserted = donor | receiver, replacement | children
    deleted = frozenset({center})
    r, w = resources(removed, inserted, deleted, added)
    return Transaction(key, removed, inserted, deleted, added, r, w)


def conflict(a, b):
    return bool(a.writes & (b.reads | b.writes) or b.writes & a.reads)


def apply(points, faces, transactions):
    current_points, current_faces = dict(points), set(faces)
    seen = set()
    for t in transactions:
        assert t.key not in seen, "重复贡献身份"
        seen.add(t.key)
        assert t.removed <= current_faces
        current_faces.difference_update(t.removed)
        current_faces.update(t.inserted)
        for v in t.deleted_vertices:
            del current_points[v]
        current_points.update(t.vertices)
    return current_points, frozenset(current_faces)


def reference_height(q):
    x, y = q
    return sum(max(F(0), 1-abs(2*x-center)) * max(F(0), 1-abs(2*y-1)) for center in (7, 17))


def sample_record(points, faces, q):
    covered = tuple(sorted(f for f in faces if inside_triangle(q, f, points)))
    assert covered
    heights = {height_on(points, f, q) for f in covered}
    assert len(heights) == 1
    return covered[0], covered, abs(next(iter(heights))-reference_height(q))


def projection(points, faces, queries):
    samples = {q: sample_record(points, faces, q) for q in queries}
    incident = {v: star(faces, v) for v in points}
    scores = {}
    for face in faces:
        errors = [record[2] for record in samples.values() if face in record[1]]
        edge = max(math.dist(points[a][:2], points[b][:2]) for a, b in zip(face, face[1:]+face[:1]))
        scores[face] = max(float(max(errors, default=F(0))), .2*edge)
    return samples, incident, scores


def repair(points, faces, target_points, target_faces, transactions, old):
    """枚举完整闭支持后局部替换；外部 owner 的边界点同样进入待修复集。"""
    samples, incident, scores = (dict(part) for part in old)
    removed = frozenset().union(*(t.removed for t in transactions))
    inserted = frozenset().union(*(t.inserted for t in transactions))
    affected_q = {q for q in samples if any(inside_triangle(q, f, points) for f in removed)}
    touched_faces = set(removed | inserted)
    for q in affected_q:
        touched_faces.update(samples[q][1])
        # 外接口面也在目标查询中；此有限 oracle 使用全域查询并独立计费
        samples[q] = sample_record(target_points, target_faces, q)
        touched_faces.update(samples[q][1])
    touched_v = set().union(*(set(f) for f in removed | inserted))
    for v in touched_v:
        if v in target_points:
            incident[v] = star(target_faces, v)
        else:
            incident.pop(v)
    for face in touched_faces:
        if face not in target_faces:
            scores.pop(face, None)
            continue
        errors = [record[2] for record in samples.values() if face in record[1]]
        edge = max(math.dist(target_points[a][:2], target_points[b][:2]) for a,b in zip(face,face[1:]+face[:1]))
        scores[face] = max(float(max(errors, default=F(0))), .2*edge)
    return (samples, incident, scores), len(affected_q)


def check():
    points, faces = grid()
    expected_boundary, area = boundary(faces), F(36)
    transactions = [exchange(points, faces, 0, "qA"), exchange(points, faces, 5, "qB")]
    # 面不交但共写接口边的相邻版本应被当前保守模型拒绝
    adjacent = exchange(points, faces, 4, "adjacent")
    assert not transactions[0].removed & adjacent.removed
    assert conflict(transactions[0], adjacent)
    assert not conflict(*transactions)
    outputs = [apply(points, faces, order) for order in itertools.permutations(transactions)]
    assert outputs[0] == outputs[1]
    final_points, final_faces = outputs[0]
    validate(final_points, final_faces, expected_boundary, area)
    assert len(final_faces) == len(faces) == 36
    queries = [(F(x,2), F(y,2)) for x in range(19) for y in range(5)]
    old = projection(points, faces, queries)
    expected = projection(final_points, final_faces, queries)
    repaired, affected = repair(points, faces, final_points, final_faces, transactions, old)
    assert repaired == expected
    assert max(x[2] for x in expected[0].values()) <= max(x[2] for x in old[0].values())
    assert set(expected[1]) == set(final_points)

    # 寻找 owner 在保留面上的公共边点，直接复核仅扫描删除面 owner 会漏项
    removed = frozenset().union(*(t.removed for t in transactions))
    external = [q for q, record in old[0].items() if record[0] not in removed and set(record[1]) & removed]
    assert external
    stale = [q for q in external if old[0][q] != expected[0][q]]
    assert stale

    # 不共享面的证书仍可能读取对方写入的高度
    left = Transaction("height", frozenset(), frozenset(), frozenset(), {}, frozenset(), frozenset({("height","v")}))
    right = Transaction("reader", frozenset(), frozenset(), frozenset(), {}, frozenset({("height","v")}), frozenset())
    assert not left.removed & right.removed and conflict(left, right)
    duplicate_rejected = False
    try:
        apply(points, faces, [transactions[0], transactions[0]])
    except AssertionError:
        duplicate_rejected = True
    assert duplicate_rejected
    assert len(faces) + 2*1 > len(faces)  # 没有回收的额外接收不能伪装成净零交换

    # 两个远离面只共享保留点；关联集合的唯一增量合并不依赖顺序
    shared = []
    for a, b in itertools.combinations(sorted(faces), 2):
        if len(set(a) & set(b)) == 1:
            shared = [a, b]
            break
    shared_transactions = []
    for index, face in enumerate(shared):
        key = f"shared{index}"
        added = {key:tuple(sum(points[v][axis] for v in face)/3 for axis in range(3))}
        target = dict(points, **added)
        inserted = frozenset(canonical((a,b,key),target) for a,b in zip(face,face[1:]+face[:1]))
        removed = frozenset({face})
        r,w = resources(removed,inserted,frozenset(),added)
        shared_transactions.append(Transaction(key,removed,inserted,frozenset(),added,r,w))
    assert not conflict(*shared_transactions)
    first = apply(points,faces,shared_transactions)
    assert first == apply(points,faces,list(reversed(shared_transactions)))
    validate(*first,expected_boundary,area)
    assert len(first[1]) == len(faces)+4
    local,_ = repair(points,faces,*first,shared_transactions,old)
    assert local == projection(*first,queries)
    return {"status":"通过", "transactions":2, "permutations":4, "faces":36,
            "samples":len(queries), "affectedSamples":affected,
            "externalOwnerChangedSamples":len(stale), "heightReadConflictRejected":True,
            "duplicateContributionRejected":True, "localContinuationEqualsRebuild":True,
            "sharedVertexSetReduction":True, "sharedEdgeConflictRejected":True,
            "limitations":"有理小网格与正交诊断评分；不验证自然透视求解或生产并发"}


if __name__ == "__main__":
    started = time.perf_counter()
    result = check()
    result["seconds"] = time.perf_counter() - started
    directory = Path(__file__).resolve().parent
    result["sha256"] = {p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (Path(__file__), directory/"verify_bounded_patch_refit.py", directory/"verify_cavity_retriangulation.py")}
    print(json.dumps(result, ensure_ascii=False, indent=2))
