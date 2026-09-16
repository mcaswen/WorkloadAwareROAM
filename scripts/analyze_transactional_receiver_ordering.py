"""QPC-04D：独立核对前缀，分开归约质量、实际工作和同政策线程成本。"""

import argparse
import json
import math
import struct
from pathlib import Path

import numpy as np

from run_transactional_receiver_ordering import CASES, FRAMES, references, historical, BOUNDARY, ROOT
from run_transactional_boundary_integration import read, rows, compare, STAGES
from run_transactional_boundary_audit import statistics_for
from run_transactional_platform import identity, write
from run_transactional_recovery_trace import sampling_coordinates, components


def priority_audit(path):
    records = rows(path)
    limit = int(records[0]["prefixLimit"])
    eligible = []
    for row in records:
        p, e, density, threshold = (float(row[k]) for k in
                                    ("prioritySquared", "errorSquared", "densitySquared", "thresholdPx"))
        if p != max(e, density):
            raise RuntimeError("评分分解未复现 P")
        if math.isfinite(p) and p > threshold * threshold:
            eligible.append(row)
    error = sorted(eligible, key=lambda row: (-float(row["errorSquared"]), int(row["root"]), int(row["slot"])))
    composite = sorted(eligible, key=lambda row: (-float(row["prioritySquared"]), int(row["root"]), int(row["slot"])))
    actual = {row["slot"] for row in records if row["inPrefix"] == "1"}
    if actual != {row["slot"] for row in error[:limit]} or len(eligible) != int(records[0]["rawCount"]):
        raise RuntimeError("新索引不等于同人口误差前缀")
    ranks = {row["slot"]: i + 1 for i, row in enumerate(composite)}
    return {"eligible": len(eligible), "prefix": len(actual), "exact": True,
            "prefixNoVisible": sum(int(row["visibleSamples"]) == 0 for row in error[:limit]),
            "compositePrefixNoVisible": sum(int(row["visibleSamples"]) == 0 for row in composite[:limit]),
            "top": [{"root": int(row["root"]), "error": math.sqrt(float(row["errorSquared"])),
                     "compositeRank": ranks[row["slot"]]} for row in error[:5]]}


def distribution(directory, frame):
    quality = read(directory / f"frame-{frame}/quality.json")
    if any(quality[key] for key in ("missing", "ambiguous", "invalidGeometry", "invalidProjection")):
        raise RuntimeError("独立质量存在无效几何或投影")
    values = np.fromfile(directory / f"frame-{frame}/errors.f64", dtype="<f8")
    if len(values) != quality["q"] or not np.isfinite(values).all():
        raise RuntimeError("逐样本质量文件无效")
    visible = values >= 0
    return quality, values, {str(cut): int(np.count_nonzero(values[visible] > cut)) for cut in (.1, .25, .5, 1, 2, 4)}


def cluster(values, baseline, config):
    uv = sampling_coordinates(config["width"], config["height"])
    x = np.minimum((uv[:, 0] * (config["width"] - 1)).astype(int), config["width"] - 2)
    y = np.minimum((uv[:, 1] * (config["height"] - 1)).astype(int), config["height"] - 2)
    selected = (values >= 0) & (values - baseline > .25)
    cells = np.zeros((config["height"] - 1, config["width"] - 1), dtype=bool)
    cells[y[selected], x[selected]] = True
    labels, count = components(cells)
    sizes = np.bincount(labels[y[selected], x[selected]], minlength=count)
    return {"samples": int(selected.sum()), "visible": int((values >= 0).sum()),
            "components": count, "largest": int(sizes.max()) if count else 0}


def witness_audit(case, trace, frames):
    old = (ROOT / "benchmark-output/cpu-refinement/qpc-01/run-01" / case if "sierra" in case
           else BOUNDARY / (case + "-trace-on"))
    result = {}
    for directory in sorted(trace.glob("frame-*")):
        current = rows(directory / "witnesses.csv")
        previous = rows(old / directory.name / "witnesses.csv")
        seed_keys = ("witness", "u", "v", "reference", "height", "face", "futureError", "returnError")
        a = [{k: row[k] for k in seed_keys} for row in previous if row["phase"] == "seed"]
        b = [{k: row[k] for k in seed_keys} for row in current if row["phase"] == "seed"]
        if a != b: raise RuntimeError("冻结见证的种子几何不同")
        source = int(directory.name.split("-")[1])
        roots = [json.loads(line) for line in (directory / "roots.jsonl").read_text().splitlines()]
        decisions = [json.loads(line) for line in (directory / "recovery.jsonl").read_text().splitlines()]
        selected = {source, frames[-1]}
        entry = {"seedWitnessesEqual": True,
                 "A": [row for row in previous if row["phase"] == "after" and int(row["frame"]) in selected],
                 "B": [row for row in current if row["phase"] == "after" and int(row["frame"]) in selected],
                 "roots": [row for row in roots if row["phase"] == "before" and row["frame"] in selected],
                 "decisions": [row for row in decisions if row["frame"] in selected], "changes": []}
        for line in (directory / "transactions.jsonl").read_text().splitlines():
            row = json.loads(line)
            for witness in row["witnesses"]:
                if witness["heightBefore"] != witness["heightAfter"]:
                    entry["changes"].append({"frame": row["frame"], "kind": row["kind"],
                        "root": row["receiverRoot"], **witness})
        result[directory.name] = entry
    return result


def report(output):
    result = {"freeze": read(output / "freeze.json"), "collection": read(output / "collection.json"),
              "default": {}, "cases": {}, "evidence": {}}
    for label in ("platform-before", "platform-after-off"):
        warm = [r for r in rows(output / label / "frames.csv") if r["warmup"] == "0" and r["cold"] == "0"]
        result["default"][label] = {stage: statistics_for(warm, stage) for stage in STAGES}
    for case in CASES:
        entry = {"quality": [], "work": {}, "performance": {}, "coldPerformance": {}, "priority": {}, "witnesses": {}}
        for label in ("A", "B", "B-t1"):
            directory = output / (case + "-" + label)
            if not directory.exists(): continue
            data = rows(directory / "run/frames.csv")
            warm = [r for r in data if r["warmup"] == "0" and r["cold"] == "0"]
            entry["performance"][label] = {stage: statistics_for(warm, stage) for stage in STAGES}
            entry["coldPerformance"][label] = {key: float(data[0][key]) for key in (*STAGES, "seedMs", "initializeMs")}
            entry["work"][label] = {key: sum(int(r[key]) for r in data) for key in
                ("raw", "examined", "receivers", "need", "feasible", "exchanges", "free", "pairs", "conflicts", "touches")}
            flip_rows = rows(directory / "run/flip-recovery.csv")
            entry["work"][label]["flips"] = sum(int(row["executed"]) for row in flip_rows)
            entry["work"][label].update(finalFaces=int(data[-1]["faces"]),
                                       faceRange=[min(int(r["faces"]) for r in data), max(int(r["faces"]) for r in data)])
            result["evidence"][case + "-" + label] = identity(directory / "manifest.json")
        b_rows = rows(output / (case + "-B/run/frames.csv"))
        a_rows = rows(output / (case + "-A/run/frames.csv"))
        for frame in FRAMES[case]:
            qrow = {"frame": frame, "above": {}, "Dmax": {}}
            arrays = {}
            for label, directory in {**references(case), "B": output / ("quality-" + case)}.items():
                q, values, counts = distribution(directory, frame)
                qrow[label] = q
                qrow["above"][label] = counts
                arrays[label] = values
                if label in ("A", "B") and q["meshHash"] != (a_rows if label == "A" else b_rows)[frame]["hash"]:
                    raise RuntimeError("复用质量不是当前实际输出")
            visible = arrays["B"] >= 0
            for label in ("A", "off", "dod"):
                if not np.array_equal(arrays[label] >= 0, visible):
                    raise RuntimeError("共同可见域不同")
                qrow["Dmax"][label] = float(np.max(arrays["B"][visible] - arrays[label][visible]))
            qrow["visible"] = int(visible.sum())
            if "canyon" in case:
                config = read(output / (case + "-B/inputs/resolved-portable.json"))
                qrow["clusters"] = {label: cluster(arrays[label], arrays["dod"], config) for label in ("A", "B", "off")}
            entry["quality"].append(qrow)
        trace = output / (case + "-B-trace")
        for path in sorted(trace.glob("priority-[0-9]*.csv")):
            entry["priority"][path.stem] = priority_audit(path)
        for path in trace.glob("frame-*/witnesses.csv"):
            entry["witnesses"][path.parent.name] = rows(path)
        entry["witnessAudit"] = witness_audit(case, trace, FRAMES[case])
        nonregression = all(q["B"][key] <= q["A"][key] for q in entry["quality"]
                            for key in ("screenMax", "heightMax", "terrainSampleRms"))
        nonregression &= all(q["above"]["B"][cut] <= q["above"]["A"][cut] for q in entry["quality"]
                             for cut in q["above"]["A"])
        retained = False
        for audit in entry["witnessAudit"].values():
            a = {row["witness"]: float(row["futureError"]) for row in audit["A"] if int(row["frame"]) == FRAMES[case][-1]}
            b = {row["witness"]: float(row["futureError"]) for row in audit["B"] if int(row["frame"]) == FRAMES[case][-1]}
            retained |= any(a[key] - b[key] >= .01 for key in a)
        entry["finiteQualityExit"] = {"frozenMetricsNonregressing": nonregression, "fixedViewProgressRetained": retained,
                                     "relativeA": "finite-positive" if nonregression and retained else "mixed-or-unrealized"}
        result["cases"][case] = entry
        result["evidence"][case + "-trace"] = identity(trace / "frames.csv")
        result["evidence"][case + "-quality"] = identity(output / ("quality-" + case) / "quality-index.json")
    write(output / "summary.json", result)
    for case, data in result["cases"].items():
        print(case, "CPU", {label: value["cpuMs"]["mean"] for label, value in data["performance"].items()})
        print("work", data["work"])
        for q in data["quality"]:
            print(q["frame"], "Emax", {p: q[p]["screenMax"] for p in ("A", "B", "off", "dod")}, "Dmax", q["Dmax"])


def visuals(output):
    from PIL import Image
    from experiment_infrastructure.asset_preview import configure_font, plt
    configure_font()
    evidence = []
    for case in CASES:
        frames = FRAMES[case][1::2]
        fig, axes = plt.subplots(2, 4, figsize=(16, 8), layout="constrained")
        config = read(output / (case + "-B/inputs/resolved-portable.json"))
        uv = sampling_coordinates(config["width"], config["height"])
        x = np.minimum((uv[:, 0] * (config["width"] - 1)).astype(int), config["width"] - 2)
        y = np.minimum((uv[:, 1] * (config["height"] - 1)).astype(int), config["height"] - 2)
        maxima = [distribution(directory, frame)[0]["screenMax"] for frame in frames
                  for directory in (references(case)["A"], output / ("quality-" + case))]
        scale = max(maxima)
        for row, frame in enumerate(frames):
            left_rows = rows(historical(case, True) / "run/frames.csv")
            right_rows = rows(output / (case + "-B-visual/run/frames.csv"))
            for key in ("poseHash", "projectionHash"):
                if left_rows[frame][key] != right_rows[frame][key]: raise RuntimeError("视觉视图身份不同")
            for column, (label, run) in enumerate((("A", historical(case, True)), ("B", output / (case + "-B-visual")))):
                path = run / "run" / f"frame-{frame}.png"
                axes[row, column].imshow(Image.open(path))
                axes[row, column].set_title(f"{label} / frame {frame} / actual platform")
                axes[row, column].axis("off")
                evidence.append({"case": case, "frame": frame, "policy": label, "image": identity(path)})
                directory = references(case)["A"] if label == "A" else output / ("quality-" + case)
                _, values, _ = distribution(directory, frame)
                visible = values >= 0
                cells = np.full((config["height"] - 1, config["width"] - 1), np.nan)
                np.fmax.at(cells, (y[visible], x[visible]), values[visible])
                heat = axes[row, column + 2].imshow(cells, origin="lower", extent=(0, 1, 0, 1),
                    cmap="magma", vmin=0, vmax=scale)
                axes[row, column + 2].set_title(f"{label} / frame {frame} / sampled cell max")
        fig.colorbar(heat, ax=axes[:, 2:].ravel().tolist(), label="px; same scale for A/B and frames")
        fig.suptitle(case)
        fig.savefig(output / (case + "-visual-review.png"), dpi=130)
        plt.close(fig)
        # 裁剪中心来自独立参考和实际导出矩阵；仅用于定位，不参与质量求值
        from experiment_infrastructure.catalog import source_samples
        source = source_samples(Path(config["heightMap"])).astype(float) * config["heightScale"] / 65535
        fig, axes = plt.subplots(4, 2, figsize=(10, 12), layout="constrained")
        frozen = read(output / "freeze.json")["cases"][case]["witnesses"][0]
        for group, frame in enumerate(frames):
            q = read(output / ("quality-" + case) / f"frame-{frame}/quality.json")
            points = (("original witness", frozen["u"], frozen["v"]), ("B current maximum", *q["screenWitness"][1:]))
            artifact = output / (case + "-B-visual/run") / f"mesh-{frame}.bin"
            with artifact.open("rb") as stream:
                header = stream.read(108)
            if header[:8] != b"TPIMSH01": raise RuntimeError("未知视觉矩阵格式")
            matrix = np.array(struct.unpack_from("<16f", header, 44)).reshape(4, 4)
            for local, (name, u, v) in enumerate(points):
                fx, fy = u * (config["width"] - 1), v * (config["height"] - 1)
                ix, iy = min(int(fx), config["width"] - 2), min(int(fy), config["height"] - 2)
                tx, ty = fx - ix, fy - iy
                h = (source[iy, ix] * (1 - tx) + source[iy, ix + 1] * tx) * (1 - ty) + \
                    (source[iy + 1, ix] * (1 - tx) + source[iy + 1, ix + 1] * tx) * ty
                clip = matrix @ np.array([(u - .5) * config["terrainSize"], h, (v - .5) * config["terrainSize"], 1])
                x, y = (clip[0] / clip[3] + 1) * 640, (1 - clip[1] / clip[3]) * 360
                for column, (label, run) in enumerate((("A", historical(case, True)), ("B", output / (case + "-B-visual")))):
                    ax = axes[2 * group + local, column]
                    ax.set_title(f"{label} / frame {frame} / {name}")
                    if clip[3] <= 0 or not (0 <= x <= 1280 and 0 <= y <= 720):
                        ax.text(.5, .5, "reference point outside this view", ha="center")
                        ax.axis("off")
                        continue
                    ax.imshow(Image.open(run / "run" / f"frame-{frame}.png"))
                    ax.set_xlim(max(0, x - 100), min(1280, x + 100))
                    ax.set_ylim(min(720, y + 80), max(0, y - 80))
                    ax.plot(x, y, "+", color="#ff4b6e")
        fig.suptitle(case + " / actual platform crops; marker = source position")
        fig.savefig(output / (case + "-witness-crops.png"), dpi=130)
        plt.close(fig)
    write(output / "visual-sources.json", {"captures": evidence, "userAcceptance": "pending"})


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=("report", "visuals"))
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    (report if args.mode == "report" else visuals)(args.output.resolve())
