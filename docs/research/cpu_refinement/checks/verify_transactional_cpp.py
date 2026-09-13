"""独立核对 C++ 发布网格与局部证书；不向 C++ 提供目标或拟合结果。"""

import argparse
from fractions import Fraction as F
import hashlib
import json
import math
from pathlib import Path
import time

from transaction_gate_contract import Snapshot, shape
from transaction_gate_geometry import proposals, barycentric, exact_error2


def angle_violation(face, points):
    """返回最坏锐角平方判据，正数是实际舍入几何的精确反例。"""
    worst = F(0)
    for i in range(3):
        a, b, c = (points[face[j % 3]] for j in (i, i + 1, i + 2))
        u, v = (b[0] - a[0], b[1] - a[1]), (c[0] - a[0], c[1] - a[1])
        dot = u[0] * v[0] + u[1] * v[1]
        if dot > 0:
            worst = max(worst, 10 * dot * dot - 9 * sum(x*x for x in u) * sum(x*x for x in v))
    return worst


def verify(snapshot_path, run_path, output):
    started = time.monotonic()
    old_data = json.loads(snapshot_path.read_text())
    source = json.loads(snapshot_path.with_name(old_data["scenario"] + "-source.json").read_text())
    report = json.loads((run_path / "summary.json").read_text())
    final_data = json.loads((run_path / "mesh.json").read_text())
    old = Snapshot(old_data, source)
    old.validate()
    old.refresh(started + 120)
    final = Snapshot(final_data, source)
    final.validate()
    final.refresh(time.monotonic() + 120)
    assert report["D_raw"] == len(old.raw)
    assert [row[0] for row in report["intents"]] == [old.ids[i] for i in old.raw[:64]]
    assert report["faces"] == len(final.faces) <= old_data["budget"]
    assert math.isclose(report["before"]["sampledScreenMaxPx"], max(old.errors2)**.5, rel_tol=1e-12, abs_tol=1e-12)
    assert max(final.errors2) <= max(old.errors2) + 1e-8
    points = {v: tuple(map(F, p)) for v, p in final.points.items()}
    checks = 0
    for exchange in report["exchanges"]:
        target = F(exchange["targetMicropixels"], 1000000)
        for name in ("receiver", "reclamation") if exchange["hasDonor"] else ("receiver",):
            patch = exchange[name]
            sample_ids = sorted({sid for slot in patch["support"] for sid in old.face_samples[slot] if old.visibility[sid]})
            for sid in sample_ids:
                ix, iy = old.decode(sid)
                q = F(ix*1048576, 6*old.nx), F(iy*1048576, 6*old.ny)
                for face in patch["faces"]:
                    weights = barycentric(q, face, points)
                    if min(weights) >= 0:
                        break
                else:
                    raise AssertionError("C++ 发布补丁缺失旧可见样本")
                height = sum(w*points[v][2] for w, v in zip(weights, face))
                error = exact_error2(old, sid, height)
                assert error is not None and error <= target*target, (sid, name, error, target)
                checks += 1
    # 仅解释已出现的数值差异，不执行旧 oracle 来替换 C++ 的失败提案
    historical_path = snapshot_path.parent.parent / (snapshot_path.stem + "-final.json")
    historical = json.loads(historical_path.read_text())
    assert historical["backends"]["poolCenters"] == report["pool"]
    changes = []
    for previous, current, attempts in zip(historical["intentRows"], report["intents"], report["attempts"]):
        if previous["attempts"] == attempts:
            continue
        root = old.ids.index(current[0])
        diagnostics = []
        for ordinal, proposal in enumerate(proposals(old, root)):
            if ordinal >= len(attempts) or ordinal >= len(previous["attempts"]):
                break
            if previous["attempts"][ordinal] == attempts[ordinal]:
                continue
            rounded = {v: (F(float(p[0]/1048576))*1048576, F(float(p[1]/1048576))*1048576, F(p[2]))
                       for v, p in proposal.points.items()}
            ideal_valid = all(shape(face, proposal.points) for face in proposal.faces)
            violation = max((angle_violation(face, rounded) for face in proposal.faces), default=F(0))
            diagnostics.append({"ordinal": ordinal, "kind": proposal.kind,
                                "python": previous["attempts"][ordinal][1], "cpp": attempts[ordinal][1],
                                "idealShapeValid": ideal_valid, "roundedAngleViolation": str(violation),
                                "explainedByRoundedShape": ideal_valid and violation > 0 and attempts[ordinal][1] == "shape_infeasible"})
        changes.append({"faceId": current[0], "pythonReceiver": previous["receiver"],
                        "cppResult": current[1], "differences": diagnostics})
    result = {"status": "通过", "numericContract": report["numericContract"], "exactLocalChecks": checks,
              "before": old.summary(), "after": final.summary(), "receiverDifferences": changes,
              "seconds": time.monotonic()-started,
              "sha256": {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in
                         (snapshot_path, run_path/"mesh.json", run_path/"summary.json", Path(__file__))}}
    if output.exists():
        raise RuntimeError("拒绝覆盖已有独立核查")
    output.write_text(json.dumps(result, ensure_ascii=False, indent=2))
    print(json.dumps({k: result[k] for k in ("status", "exactLocalChecks", "seconds")}, ensure_ascii=False))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="C++ 单批发布结果的独立精确核查")
    parser.add_argument("snapshot", type=Path)
    parser.add_argument("run", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    verify(args.snapshot, args.run, args.output)
