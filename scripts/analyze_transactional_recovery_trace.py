"""核对 QPC-01 追溯身份，归约见证因果链和诊断图；不修改实验策略。"""

import argparse
from collections import Counter
import csv
import json
from pathlib import Path
import statistics

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

from run_transactional_platform import ROOT, identity, write
from run_transactional_recovery_trace import CASES


def rows(path):
    with path.open() as stream:
        return list(csv.DictReader(stream))


def objects(path):
    return [json.loads(line) for line in path.read_text().splitlines()]


def equal_columns(actual, expected, columns):
    if len(actual) != len(expected):
        raise RuntimeError("Frame count differs")
    for a, b in zip(actual, expected):
        for left, right in columns:
            if a[left] != b[right]:
                raise RuntimeError(f"Frame {a['frame']}: {left} differs: {a[left]} != {b[right]}")


def save_csv(path, values):
    with path.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(values[0]))
        writer.writeheader()
        writer.writerows(values)


def analyze(raw, fer, output):
    output.mkdir(parents=True, exist_ok=True)
    selection = json.loads((raw / "selection.json").read_text())
    summary = {"scope": "QPC-01 diagnostic evidence; no production repair", "cases": {},
               "selection": identity(raw / "selection.json"),
               "validation": json.loads((raw / "validation.json").read_text())}
    columns = "frame hash faces raw examined receivers need feasible exchanges free pairs conflicts donorReuse touches".split()
    flip_columns = [("flipTriggered", "triggered"), ("flipAttempts", "attempts"),
                    ("flipCertified", "certified"), ("flipConflicts", "conflicts"), ("flips", "executed")]
    witnesses, decisions, events, clusters = [], [], [], []
    for case in CASES:
        record = json.loads((raw / "commands" / f"{case}.json").read_text())
        if record["status"] != "ok":
            raise RuntimeError(f"Incomplete trace: {case}")
        current = rows(raw / case / "frames.csv")
        reference = rows(fer / f"r1-{case}/run/frames.csv")
        flips = rows(fer / f"r1-{case}/run/flip-recovery.csv")
        equal_columns(current, reference, [(key, key) for key in columns])
        equal_columns(current, flips, [("frame", "frame"), *flip_columns])
        if any(row["boundaryUnchanged"] != "1" for row in current):
            raise RuntimeError("Boundary invariant failed")
        config = json.loads((fer / f"r1-{case}/inputs/resolved.json").read_text())
        selected = selection["cases"][case]
        result = {"frames": len(current), "ferFrameIdentityExact": True,
                  "diagnosticSeconds": record["elapsedSeconds"], "binary": record["binary"],
                  "budget": config["budget"], "prefix": 64 * config["budget"] // 20000,
                  "splitPixels": config["splitPixels"], "finalFaces": int(current[-1]["faces"]),
                  "boundaryEdges": len(rows(raw / case / "boundary.csv")), "witnesses": [],
                  "warmNoTopologyFrames": sum(int(r["exchanges"]) + int(r["free"]) + int(r["flips"]) == 0 for r in current[3:]),
                  "totals": {key: sum(int(r[key]) for r in current) for key in
                             ("receivers", "need", "feasible", "exchanges", "free", "pairs", "conflicts", "donorReuse", "flips")},
                  "pages": [{k: p[k] for k in ("frame", "visibleSamples", "excessSamples", "components", "largestComponentSamples")}
                            for p in selected["pages"]]}
        for frame in CASES[case]:
            group = raw / case / f"frame-{frame}"
            observed = rows(group / "witnesses.csv")
            roots = objects(group / "roots.jsonl")
            recovery = objects(group / "recovery.jsonl")
            transactions = objects(group / "transactions.jsonl")
            points = [point for point in selected["witnesses"] if point["frame"] == frame]
            lookup = {(item["frame"], item["witness"], item["root"]): item for item in recovery}
            for row in observed:
                witnesses.append({"case": case, "sourceFrame": frame, **row})
            for root in roots:
                if root["phase"] != "before":
                    continue
                reason = lookup[(root["frame"], root["witness"], root["root"])]
                stage = reason["stage"]
                if stage == "outside_prefix" and root["rank"] == 0:
                    stage = "below_threshold_or_nonfinite"
                decisions.append({"case": case, "sourceFrame": frame, "frame": root["frame"],
                                  "witness": root["witness"], "root": root["root"], "rank": root["rank"],
                                  "priority": root["priority"], "threshold": root["threshold"],
                                  "sampleMaxPx": root["sampleMaxPx"], "stage": stage,
                                  "selected": reason.get("selected", False),
                                  "details": json.dumps(reason, separators=(",", ":"))})
            for i, point in enumerate(points):
                history = [row for row in observed if int(row["witness"]) == i]
                seed = next(row for row in history if row["phase"] == "seed")
                target = next(row for row in history if row["phase"] == "after" and int(row["frame"]) == frame)
                changes = []
                for t in transactions:
                    hit = next((w for w in t["witnesses"] if w["id"] == i), None)
                    if hit is None or abs(hit["heightAfter"] - hit["heightBefore"]) <= 1e-10:
                        continue
                    geometry = next(g["geometry"] for g in t["witnessGeometry"] if g["witness"] == i)
                    before = next(r for r in history if r["phase"] == "before" and int(r["frame"]) == t["frame"])
                    after = next(r for r in history if r["phase"] == "after" and int(r["frame"]) == t["frame"])
                    event = {"frame": t["frame"], "kind": t["kind"], "root": t["receiverRoot"],
                             "heightBefore": hit["heightBefore"], "heightAfter": hit["heightAfter"],
                             "fixedFutureBeforePx": float(before["futureError"]),
                             "fixedFutureAfterPx": float(after["futureError"]), "visible": hit["visible"],
                             "unfittedWitnessHeight": geometry["unfittedWitnessHeight"]}
                    changes.append(event)
                    events.append({"case": case, "sourceFrame": frame, "witness": i, **event,
                                   "geometry": geometry, "sampleCount": t["sampleCount"], "targetPx": t["targetPx"]})
                final_roots = [root for root in roots if root["phase"] == "before" and root["frame"] == frame and root["witness"] == i]
                stages = Counter(d["stage"] for d in decisions if d["case"] == case and d["sourceFrame"] == frame and d["witness"] == i)
                result["witnesses"].append({**point, "localWitness": i, "seedHeight": float(seed["height"]),
                    "targetHeight": float(target["height"]), "referenceHeight": float(seed["reference"]),
                    "seedFixedFuturePx": float(seed["futureError"]), "targetInternalPx": float(target["error"]),
                    "internalVsActualPx": float(target["error"]) - point["actualPx"],
                    "heightRange": max(float(r["height"]) for r in history) - min(float(r["height"]) for r in history),
                    "changes": changes, "rootObservationCounts": dict(stages), "targetRoots": final_roots,
                    "targetRecovery": [lookup[(frame, i, root["root"])] for root in final_roots]})
            for row in rows(raw / "selection" / f"{case}-frame-{frame}-clusters.csv"):
                clusters.append({"case": case, "frame": frame, **row})
        summary["cases"][case] = result
    before, after = rows(raw / "normal-before/frames.csv"), rows(raw / "normal-after/frames.csv")
    equal_columns(before, after, [(key, key) for key in columns])
    equal_columns(before, rows(fer / "r1-peking547-b50000-transactional-t8/run/frames.csv"), [(key, key) for key in columns])
    old = statistics.mean(float(r["cpuMs"]) for r in before[3:])
    new = statistics.mean(float(r["cpuMs"]) for r in after[3:])
    summary["normalCompatibility"] = {"frames": len(before), "warmBeforeMs": old, "warmAfterMs": new,
                                      "ratio": new / old, "scope": "one process pair; no renderer"}
    summary["oldProbeCompatibility"] = {name: identity(raw / "pq-before" / name)["sha256"] ==
                                       identity(raw / "pq-after" / name)["sha256"]
                                       for name in ("witnesses.csv", "transactions.jsonl", "recovery.jsonl")}
    if not all(summary["oldProbeCompatibility"].values()):
        raise RuntimeError("Historical default provenance changed")
    write(output / "summary.json", summary)
    write(output / "selection.json", selection)
    write(output / "geometry-events.json", events)
    for name, table in (("witnesses", witnesses), ("root-decisions", decisions), ("clusters", clusters)):
        save_csv(output / f"{name}.csv", table)
    charts(raw, selection, witnesses, output)
    return summary


def charts(raw, selection, witnesses, output):
    panels = [(case, frames[0]) for case, frames in CASES.items()]
    figure, axes = plt.subplots(2, 2, figsize=(12, 10), constrained_layout=True)
    for axis, (case, frame) in zip(axes.flat, panels):
        labels = np.load(raw / "selection" / f"{case}-frame-{frame}-clusters.npz")["labels"]
        axis.imshow(labels >= 0, origin="lower", extent=(0, 1, 0, 1), cmap="Blues", vmin=0, vmax=1)
        labels_at = []
        for i, point in enumerate(p for p in selection["cases"][case]["witnesses"] if p["frame"] == frame):
            axis.scatter(point["u"], point["v"], color="#d06020", marker="x", s=55, clip_on=False)
            near = next((item for item in labels_at if np.hypot(item[0] - point["u"], item[1] - point["v"]) < 0.035), None)
            if near is None:
                labels_at.append([point["u"], point["v"], str(i)])
            else:
                near[2] += "/" + str(i)
        for u, v, label in labels_at:
            text_position = (min(0.94, max(0.06, u)), v - 0.09 if v > 0.1 else v + 0.09)
            axis.annotate(label, (u, v), xytext=text_position, textcoords="data", ha="center",
                          bbox={"facecolor": "white", "edgecolor": "#d06020", "pad": 2},
                          arrowprops={"arrowstyle": "-", "color": "#d06020"})
        axis.set(title=case.replace("-transactional-t8", "") + f" / frame {frame}", xlabel="u", ylabel="v")
    figure.suptitle("Cells containing sampled excess T - DOD > 0.25 px; x = frozen witnesses")
    figure.savefig(output / "excess-components.png", dpi=160)
    plt.close(figure)
    figure, axes = plt.subplots(1, 3, figsize=(15, 4.5), constrained_layout=True)
    for axis, case in zip(axes, (list(CASES)[0], list(CASES)[2], list(CASES)[3])):
        frame = CASES[case][0]
        series = [r for r in witnesses if r["case"] == case and r["sourceFrame"] == frame and r["witness"] == "0" and r["phase"] == "after"]
        x = [int(r["frame"]) for r in series]
        axis.plot(x, [float(r["futureError"]) for r in series], label=f"Fixed frame-{frame} projection", color="#bf581b")
        axis.plot(x, [float(r["error"]) if r["visible"] == "1" else np.nan for r in series], label="Current view (visible only)", color="#276c9d")
        axis.set(title=case.split("-b")[0] + " / witness 0", xlabel="Update opportunity", ylabel="Sample error (px)")
        axis.grid(alpha=0.2)
        axis.legend(fontsize=8)
    figure.suptitle("View-dependent apparent improvement and fixed-view geometry change are different")
    figure.savefig(output / "witness-timelines.png", dpi=160)
    plt.close(figure)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--raw", type=Path, required=True)
    parser.add_argument("--fer", type=Path, default=ROOT / "benchmark-output/experiment-infrastructure/fer-02")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = analyze(args.raw, args.fer, args.output)
    print("Matched", sum(case["frames"] for case in result["cases"].values()), "FER frames")
