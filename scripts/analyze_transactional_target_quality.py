"""从持续轨迹与独立有理证据归约04F，不把安全拒绝解释为恢复成功。"""

import argparse
from collections import Counter
from fractions import Fraction as F
import json
from pathlib import Path
import statistics
import struct
import time
import numpy as np

from run_transactional_platform import identity, write
from run_transactional_exchange_quality import rows
from transactional_exchange_quality import (
    rational, geometry, closed_population, source_height, screen_errors, evaluate_side,
    combine, excess, sum_interval, Unknown,
)


def read(path):
    return json.loads(path.read_text(encoding="utf-8"))


def public_side(side, source, config, denominator, work):
    """先复现实际float位置，再用有理坐标反变换；不以double反除近似替代。"""
    def f32(value):
        return F(struct.unpack("f", struct.pack("f", value))[0])

    result = {**side}
    for key in ("old", "new"):
        points = []
        for identity, u, v, h in side[key]["points"]:
            x = f32((u-.5)*float(config["terrainSize"]))
            z = f32((v-.5)*float(config["terrainSize"]))
            points.append([identity, x/config["terrainSize"]+F(1, 2),
                           z/config["terrainSize"]+F(1, 2), f32(h)])
        result[key] = dict(points=points, faces=side[key]["faces"])
    population = closed_population(geometry(result["old"]), source, denominator, work)
    result["samples"] = []
    for sid, (x, y) in population.items():
        q = F(x, denominator), F(y, denominator)
        ref = source_height(source, x, y, denominator, config["heightScale"])
        visible, _, _ = screen_errors(config, q, ref, ref, ref)
        result["samples"].append([sid, x, y, visible, None, None, None])
    return result


def condition(records, config, receiver):
    e = rational(config["qualityTargetPixels"])**2
    h = (config["heightScale"]*rational(config["qualityHeightRatio"]))**2
    bad_screen = [p["id"] for p in records.values() if p["visible"] and p["a1"] > max(p["a0"], e)]
    bad_height = [p["id"] for p in records.values() if p["u1"] > max(p["u0"], h)]
    low, high = sum_interval([excess(p["a0"], e)-excess(p["a1"], e)
                              for p in records.values() if p["visible"]])
    return dict(samples=len(records), screenViolations=bad_screen, heightViolations=bad_height,
                deltaLower=str(low), deltaUpper=str(high),
                accepted=not bad_screen and not bad_height and (not receiver or low > 0))


def audit(path, deadline, target_override=None):
    raw = read(path)
    if target_override is not None:
        raw.update(target_override)
    if raw["status"] != "complete":
        return dict(status=raw["status"])
    source = read(path.parent / "exchange-source.json")
    config = {**raw, "terrainSize": rational(raw["terrainSize"]), "heightScale": rational(raw["heightScale"]),
              "matrix": tuple(map(rational, raw["matrix"]))}
    work = Counter()
    results = []
    all_records = [[], []]
    for exchange in raw["exchanges"]:
        item = dict(index=exchange["index"], receiverKind=exchange["receiver"]["kind"], domains=[])
        try:
            for domain in (0, 1):
                checked = []
                for role in ("receiver", "donor"):
                    side = exchange[role]
                    if side is None:
                        continue
                    if domain:
                        side = public_side(side, source, config, raw["denominator"], work)
                    records = evaluate_side(side, source, config, raw["denominator"], work, deadline)
                    checked.append({"role": role, **condition(records, config, role == "receiver")})
                    all_records[domain].append(records)
                item["domains"].append(checked)
            item["status"] = "pass" if all(c["accepted"] for d in item["domains"] for c in d) else "violation"
        except Unknown as error:
            item.update(status="unknown", reason=str(error))
        results.append(item)
    shared = []
    for records in all_records:
        try:
            combined = combine(records, work)
            shared.append({"status": "pass", **condition(combined, config, bool(raw["exchanges"]))})
        except Unknown as error:
            shared.append(dict(status="unknown", reason=str(error)))
    return dict(status="complete", input=identity(path), approved=raw["approved"], exchanges=results,
                combined=shared, work=dict(work))


def timing(path):
    data = rows(path)
    numeric = {}
    for key in data[0]:
        if "ms" not in key.lower() and "cpu" not in key.lower():
            continue
        try:
            values = [float(row[key]) for row in data[3:]]
        except ValueError:
            continue
        numeric[key] = dict(mean=statistics.mean(values), maximum=max(values))
    # 事务数量保留冷启动后的所有机会，不用暖路径窗口丢掉早期改动
    counters = {key: sum(int(row[key]) for row in data) for key in
                ("exchanges", "free", "pairs", "conflicts", "touches")}
    stagnant = longest = 0
    for row in data:
        stagnant = stagnant + 1 if int(row["raw"]) > 0 and int(row["vertexWrites"]) == 0 else 0
        longest = max(longest, stagnant)
    return dict(frames=len(data), time=numeric, finalFaces=data[-1].get("faces"),
                counters=counters, finalRaw=int(data[-1]["raw"]), maxStagnant=longest)


def analyze(output, target):
    deadline = time.monotonic() + 1200
    collection = read(output / "collection.json")
    frozen = read(output / "freeze.json")
    result = dict(protocol=frozen["protocol"], collection=collection, cases=[])
    for item in frozen["cases"]:
        case = item["case"]
        record = dict(case=case, timing={}, audits=[], quality=None)
        for arm in ("A", "B"):
            path = output / (case+"-"+arm+"/run/frames.csv")
            if path.exists():
                record["timing"][arm] = timing(path)
        trace = output / (case+"-B-trace")
        workfile = trace / "pointwise-work.csv"
        if workfile.exists():
            counts = Counter()
            costs = Counter()
            for row in rows(workfile):
                if row["type"] == "count": counts[row["key"]] += int(row["value"])
                if row["type"] == "seconds": costs[row["key"]] += float(row["value"])
            record.update(counts=dict(counts), seconds=dict(costs))
        for frame in item["auditFrames"]:
            path = trace / f"exchange-quality-{frame}.json"
            if path.exists():
                record["audits"].append({"frame": frame, **audit(path, deadline)})
        quality = output / ("quality-"+case) / "quality-index.json"
        if quality.exists():
            index = read(quality)
            prior = output.parents[1] / "qpc-04d/run-01" / ("quality-" + case)
            record["quality"] = []
            for frame in index["frames"]:
                if frame["status"] != "ok":
                    record["quality"].append(frame)
                    continue
                a = read(prior / frame["quality"])
                b = read(quality.parent / frame["quality"])
                x = np.fromfile(quality.parent / frame["errors"], dtype="<f8")
                y = np.fromfile(prior / frame["errors"], dtype="<f8")
                assert x.shape == y.shape and np.array_equal(x < 0, y < 0)
                mask = x >= 0
                difference = x[mask] - y[mask]
                record["quality"].append(dict(frame=frame["frame"], A=a, B=b,
                    Dmax=float(difference.max()), excessOver025=int((difference > .25).sum()),
                    visible=int(mask.sum()), targetExceeded=int((x[mask] > .5).sum()),
                    squaredExcess=float(np.maximum(x[mask]**2-.25, 0).sum())))
        result["cases"].append(record)
        write(target / "summary.json", result)
    write(target / "freeze.json", frozen)



def audit_known_counterexamples(output, target):
    """只读复用两个冻结坏批，不生成第四条自然轨迹。"""
    prior = output.parents[1] / "qpc-04e/run-01"
    targets = dict(qualityTargetPixels=.5, qualityHeightRatio=1/256)
    result = {}
    deadline = time.monotonic() + 1200
    for name, frame in (("canyon", 24), ("canyon-composite", 8)):
        result[name] = audit(prior/name/f"exchange-quality-{frame}.json", deadline, targets)
    write(target/"known-counterexamples.json", dict(targets=targets, results=result))

def append_diagnostics(output, target):
    """保存阶段账本和冻结见证；同一失败可能在配对中重复计数，不当成独立提案数。"""
    summary = read(target / "summary.json")
    witness_result = {}
    from run_transactional_receiver_ordering import references
    from experiment_infrastructure.quality import pointwise_pair
    for case in summary["cases"]:
        name = case["case"]
        trace = output / (name + "-B-trace")
        data = rows(trace / "pointwise-work.csv")
        comparison = output / (name + "-vs-DOD.json")
        if not comparison.exists():
            pointwise_pair(output / ("quality-" + name), references(name)["dod"], comparison)
        paired = {r["frame"]: r for r in read(comparison)["frames"]}
        for q in case["quality"]:
            frame = q["frame"]
            q["DOD"] = read(references(name)["dod"] / f"frame-{frame}/quality.json")
            q["DmaxVsDOD"] = paired[frame]["DmaxSamplePx"]
            x = np.fromfile(output / ("quality-"+name) / f"frame-{frame}/errors.f64", dtype="<f8")
            y = np.fromfile(references(name)["dod"] / f"frame-{frame}/errors.f64", dtype="<f8")
            q["excessOver025VsDOD"] = int(((x >= 0) & (x-y > .25)).sum())
        last = case["timing"]["B"]["frames"] - 1
        case["maximumRationalBits"] = max(int(r["value"]) for r in data if r["type"] == "maximum")
        case["lastReasons"] = {r["key"]: int(r["value"]) for r in data
                               if r["type"] == "count" and int(r["frame"]) == last}
        groups = []
        for file in trace.glob("frame-*/witnesses.csv"):
            frame = int(file.parent.name[6:])
            recovery = [json.loads(line) for line in file.with_name("recovery.jsonl").read_text().splitlines()]
            groups.append(dict(group=file.parent.name,
                witnesses=[r for r in rows(file) if r["phase"] == "seed" or
                           (int(r["frame"]) in (frame, last) and r["phase"] == "after")],
                recovery=[r for r in recovery if r["frame"] in (frame, last)]))
        witness_result[name] = groups
    write(target / "summary.json", summary)
    write(target / "witness-summary.json", witness_result)


def visuals(output, target):
    """实际截图配合同尺度全Q热图；局部裁剪只定位，不参与质量判定。"""
    from PIL import Image
    from experiment_infrastructure.asset_preview import configure_font, plt
    from run_transactional_recovery_trace import sampling_coordinates
    from run_transactional_target_quality import PRIOR
    configure_font()
    target.mkdir(parents=True, exist_ok=True)
    manifest = []
    for case_index, item in enumerate(read(output / "freeze.json")["cases"]):
        case = item["case"]
        frames = (15, 23) if case_index == 0 else (48, 95)
        runs = (PRIOR / (case+"-B-visual"), output / (case+"-B-visual"))
        roots = (PRIOR / ("quality-"+case), output / ("quality-"+case))
        config = read(runs[1] / "inputs/resolved-portable.json")
        uv = sampling_coordinates(config["width"], config["height"])
        ix = np.minimum((uv[:, 0]*(config["width"]-1)).astype(int), config["width"]-2)
        iy = np.minimum((uv[:, 1]*(config["height"]-1)).astype(int), config["height"]-2)
        maximum = max(read(root/f"frame-{frame}/quality.json")["screenMax"]
                      for root in roots for frame in frames)
        fig, axes = plt.subplots(2, 4, figsize=(16, 8), layout="constrained")
        detail, crops = plt.subplots(2, 2, figsize=(10, 6), layout="constrained")
        for row, frame in enumerate(frames):
            a, b = (rows(run / "run/frames.csv")[frame] for run in runs)
            assert all(a[key] == b[key] for key in ("poseHash", "projectionHash"))
            q = read(roots[1]/f"frame-{frame}/quality.json")
            locations = rows(roots[1]/f"frame-{frame}/locations.csv")
            witness = next(r for r in locations if int(r["ordinal"]) == q["screenWitness"][0])
            x, y = float(witness["pixelX"]), float(witness["pixelY"])
            for column, (label, run, quality) in enumerate(zip(("A 旧政策", "B 逐点政策"), runs, roots)):
                image_path = run / "run" / f"frame-{frame}.png"
                image = Image.open(image_path)
                axes[row, column].imshow(image)
                axes[row, column].set_title(f"{label} / 帧 {frame} / 平台实际输出")
                axes[row, column].axis("off")
                values = np.fromfile(quality/f"frame-{frame}/errors.f64", dtype="<f8")
                visible = values >= 0
                cells = np.full((config["height"]-1, config["width"]-1), np.nan)
                np.fmax.at(cells, (iy[visible], ix[visible]), values[visible])
                heat = axes[row, column+2].imshow(cells, origin="lower", extent=(0, 1, 0, 1),
                                                   cmap="magma", vmin=0, vmax=maximum)
                axes[row, column+2].set_title(f"{label} / 完整Q按栅格最大值归约")
                ax = crops[row, column]
                ax.imshow(image)
                ax.set_xlim(max(0, x-100), min(1280, x+100))
                ax.set_ylim(min(720, y+80), max(0, y-80))
                ax.plot([x], [y], "r+", ms=9)
                ax.set_title(f"{label} / 帧 {frame} / 同一B最大见证")
                ax.set_axis_off()
                manifest.append(dict(case=case, frame=frame, label=label, image=identity(image_path),
                                     quality=identity(quality/f"frame-{frame}/quality.json"),
                                     witness=q["screenWitness"], pixel=[x, y]))
        fig.colorbar(heat, ax=axes[:, 2:].ravel().tolist(), label="px；同场景A/B与两帧共用范围")
        fig.suptitle(case)
        detail.suptitle(case+" / 局部见证，红十字定位")
        fig.savefig(target/(case+"-visual.png"), dpi=120)
        detail.savefig(target/(case+"-witness.png"), dpi=120)
        plt.close(fig)
        plt.close(detail)
    write(target/"visual-evidence.json", manifest)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--target", type=Path, required=True)
    parser.add_argument("--visuals-only", action="store_true")
    args = parser.parse_args()
    if args.visuals_only:
        visuals(args.output.resolve(), args.target.resolve())
    else:
        analyze(args.output.resolve(), args.target.resolve())
        append_diagnostics(args.output.resolve(), args.target.resolve())
        audit_known_counterexamples(args.output.resolve(), args.target.resolve())
