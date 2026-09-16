"""QPC-01：冻结误差见证并编排有限追溯，不调整生产输入或质量规则。"""

import argparse
from collections import deque
import csv
import json
from pathlib import Path

import numpy as np

from run_transactional_platform import ROOT, identity, native_run, win, write


CASES = {
    "peking547-b50000-transactional-t8": (15,),
    "peking547-b200000-transactional-t8": (15,),
    "dem-canyon-b50000-transactional-t8": (48,),
    "dem-sierra-b50000-transactional-t8": (48, 95),
}


def sampling_coordinates(width, height):
    """复现 k=0 评价器的首次顶点/边/重心发射顺序，保留源 UV 的 float 精度。"""
    cells = (np.arange(height - 1)[:, None] * width + np.arange(width - 1)).ravel()
    triangles = np.stack((cells, cells + width, cells + 1,
                          cells + 1, cells + width, cells + width + 1), axis=1).reshape(-1, 3)
    vertices = triangles.ravel()
    following = np.roll(triangles, -1, axis=1).ravel()
    edges = (np.minimum(vertices, following).astype(np.uint64) << 32) | np.maximum(vertices, following).astype(np.uint64)
    first_vertex = np.unique(vertices, return_index=True)[1]
    first_edge = np.unique(edges, return_index=True)[1]
    present = np.zeros((len(triangles), 7), dtype=bool)
    present[first_vertex // 3, 2 * (first_vertex % 3)] = True
    present[first_edge // 3, 2 * (first_edge % 3) + 1] = True
    present[:, 6] = True
    u = (np.arange(width, dtype=np.float32) / np.float32(width - 1)).astype(float)
    v = (np.arange(height, dtype=np.float32) / np.float32(height - 1)).astype(float)
    points = np.stack((u[triangles % width], v[triangles // width]), axis=2)
    values = np.empty((len(triangles), 7, 2))
    values[:, 0:6:2] = points
    values[:, 1:6:2] = (points + np.roll(points, -1, axis=1)) * 0.5
    values[:, 6] = ((points[:, 0] + points[:, 1]) + points[:, 2]) / 3.0
    return values[present]


def components(occupied):
    """按 raster cell 的八邻域分组；不声称重建连续超额误差区域。"""
    labels = np.full(occupied.shape, -1, dtype=np.int32)
    count = 0
    rows, columns = occupied.shape
    for y, x in zip(*np.nonzero(occupied)):
        if labels[y, x] >= 0:
            continue
        labels[y, x] = count
        pending = deque([(y, x)])
        while pending:
            cy, cx = pending.popleft()
            for ny in range(max(0, cy - 1), min(rows, cy + 2)):
                for nx in range(max(0, cx - 1), min(columns, cx + 2)):
                    if occupied[ny, nx] and labels[ny, nx] < 0:
                        labels[ny, nx] = count
                        pending.append((ny, nx))
        count += 1
    return labels, count


def checked_frame(root, case, frame):
    directory = root / f"quality-{case}"
    index = json.loads((directory / "quality-index.json").read_text())
    entry = next(row for row in index["frames"] if row["frame"] == frame)
    for field in ("quality", "errors", "locations"):
        if identity(directory / entry[field])["sha256"] != entry[field + "Sha256"]:
            raise RuntimeError(f"Frozen {field} changed: {case}/{frame}")
    return directory / f"frame-{frame}", index, entry


def select(root, output):
    if (output / "selection.json").exists():
        raise RuntimeError("Refusing to overwrite frozen witness selection")
    result = {"rule": "T-DOD>0.25 px; raster-cell 8-connectivity; fixed before replay",
              "scope": "diagnostic components, not continuous error regions", "cases": {}}
    for case, frames in CASES.items():
        resolved = root / f"r1-{case}/inputs/resolved.json"
        config = json.loads(resolved.read_text())
        selected = []
        pages = []
        for frame in frames:
            directory, index, entry = checked_frame(root, case, frame)
            baseline, base_index, base_entry = checked_frame(root, case.replace("transactional", "dod"), frame)
            if (entry["sampleHash"], index["sourceSha256"], entry["poseHash"], entry["projectionHash"]) != (
                    base_entry["sampleHash"], base_index["sourceSha256"], base_entry["poseHash"], base_entry["projectionHash"]):
                raise RuntimeError("Quality comparison does not share Q/source/view")
            actual = np.fromfile(directory / "errors.f64", dtype="<f8")
            dod = np.fromfile(baseline / "errors.f64", dtype="<f8")
            if actual.shape != dod.shape or not np.array_equal(actual >= 0, dod >= 0):
                raise RuntimeError("Visible sample populations differ")
            uv = sampling_coordinates(config["width"], config["height"])
            # locations 是稀疏可视化记录，不能冒充完整 Q；逐条核对重建坐标
            locations = np.loadtxt(directory / "locations.csv", delimiter=",", skiprows=1, usecols=(0, 1, 2))
            if not np.array_equal(uv[locations[:, 0].astype(int)], locations[:, 1:]):
                raise RuntimeError("Reconstructed Q differs from evaluator locations")
            if len(uv) != len(actual):
                raise RuntimeError("Location count differs from frozen Q")
            excess = actual - dod
            ordinals = np.flatnonzero((actual >= 0) & (excess > 0.25))
            x = np.minimum((uv[:, 0] * (config["width"] - 1)).astype(int), config["width"] - 2)
            y = np.minimum((uv[:, 1] * (config["height"] - 1)).astype(int), config["height"] - 2)
            occupied = np.zeros((config["height"] - 1, config["width"] - 1), dtype=bool)
            occupied[y[ordinals], x[ordinals]] = True
            labels, count = components(occupied)
            membership = labels[y[ordinals], x[ordinals]]
            sizes = np.bincount(membership, minlength=count)
            best = np.full(count, -1, dtype=np.int64)
            for ordinal, label in zip(ordinals, membership):
                if best[label] < 0 or excess[ordinal] > excess[best[label]]:
                    best[label] = ordinal
            quality = json.loads((directory / "quality.json").read_text())
            choices = [("screen_max", int(quality["screenWitness"][0]))]
            if count:
                choices.append(("largest_component", int(best[int(np.argmax(sizes))])))
                choices.append(("maximum_excess_component", int(ordinals[int(np.argmax(excess[ordinals]))])))
            for role, ordinal in choices:
                existing = next((item for item in selected if item["frame"] == frame and item["ordinal"] == ordinal), None)
                if existing:
                    existing["roles"].append(role)
                else:
                    selected.append({"frame": frame, "returnFrame": 16 if len(frames) == 1 and "peking" in case else 80,
                                     "ordinal": ordinal, "u": float(uv[ordinal, 0]), "v": float(uv[ordinal, 1]),
                                     "roles": [role], "actualPx": float(actual[ordinal]), "dodPx": float(dod[ordinal]),
                                     "excessPx": float(excess[ordinal])})
            cluster_file = output / "selection" / f"{case}-frame-{frame}-clusters.csv"
            cluster_file.parent.mkdir(parents=True, exist_ok=True)
            with cluster_file.open("w", newline="") as stream:
                writer = csv.writer(stream)
                writer.writerow(["component", "samples", "representative", "u", "v", "maxExcessPx"])
                for label, ordinal in enumerate(best):
                    writer.writerow([label, int(sizes[label]), int(ordinal), *uv[ordinal], float(excess[ordinal])])
            np.savez_compressed(cluster_file.with_suffix(".npz"), labels=labels)
            pages.append({"frame": frame, "visibleSamples": int(np.count_nonzero(actual >= 0)),
                          "excessSamples": len(ordinals), "components": count,
                          "largestComponentSamples": int(max(sizes, default=0)),
                          "input": entry, "dodInput": base_entry, "sourceSha256": index["sourceSha256"]})
        if not 1 <= len(selected) <= 6:
            raise RuntimeError("Witness quota exceeded")
        table = output / "selection" / f"{case}.txt"
        table.write_text("".join(f"{s['frame']} {s['returnFrame']} {s['ordinal']} {s['u']:.17g} {s['v']:.17g}\n" for s in selected))
        result["cases"][case] = {"resolved": identity(resolved), "witnessTable": identity(table),
                                 "witnesses": selected, "pages": pages}
        print(case, len(selected), "witnesses frozen", flush=True)
    write(output / "selection.json", result)


def collect(root, output, exe):
    selection = json.loads((output / "selection.json").read_text())
    for case in CASES:
        frozen = selection["cases"][case]
        resolved = root / f"r1-{case}/inputs/resolved.json"
        table = output / "selection" / f"{case}.txt"
        if identity(table)["sha256"] != frozen["witnessTable"]["sha256"] or identity(resolved)["sha256"] != frozen["resolved"]["sha256"]:
            raise RuntimeError("Frozen replay input changed")
        native_run(output, case, exe, case, "transactional", 8, "trace", arguments=[
            "--recovery-trace", win(resolved), win(table), win(output / case)])


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=("select", "collect"))
    parser.add_argument("--fer", type=Path, default=ROOT / "benchmark-output/experiment-infrastructure/fer-02")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--exe", type=Path)
    args = parser.parse_args()
    if args.mode == "select":
        select(args.fer, args.output)
    elif args.exe:
        collect(args.fer, args.output, args.exe)
    else:
        parser.error("collect requires --exe")
