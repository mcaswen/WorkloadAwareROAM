"""冻结的翻边 B/C 对照：正常计时、历史、实际帧与独立质量分别采集。"""

import argparse
from array import array
from collections import Counter
import json
import math
from pathlib import Path
import statistics

from run_transactional_platform import ROOT, identity, native_run, quality, win, write
from run_transactional_quality_audit import CORE_FIELDS
from transactional_platform_report import SEMANTICS, STAGES, compare, rows

OLD = ROOT / "benchmark-output/cpu-refinement/pq-04/run-01/after-default"
QUALITY_B = ROOT / "benchmark-output/cpu-refinement/pq-01/run-01/opengl-peking-transactional8-export"
QUALITY_DOD = ROOT / "benchmark-output/cpu-refinement/tpi-05/run-02/opengl-peking-dod8-export"
FLIP_FIELDS = ("flipTriggered", "flipAttempts", "flipCertified", "flipConflicts", "flips")
KEYFRAMES = (0, 2, 15, 16, 23)


def collect(output, app, probe):
    inventory = {"app": identity(app), "probe": identity(probe), "protocol": "pq05-v1"}
    freeze = output / "measured.json"
    if freeze.exists() and json.loads(freeze.read_text()) != inventory:
        raise RuntimeError("Measured program changed; preserve the previous attempt")
    write(freeze, inventory)
    for label, enabled in (("b-provenance", False), ("c-provenance", True)):
        args = [win(output / label), "immutable", "--witness", ".97985345125198364", ".94871795177459717"]
        if enabled:
            args.append("--flip-recovery")
        if native_run(output, label, probe, "peking", "transactional", 8, "audit", arguments=args)["status"] != "ok":
            raise RuntimeError("Provenance failed: " + label)
    # 默认路径必须先与旧原始文件匹配，才能继续消费新政策的自然证据
    for name in ("frames.csv", "transactions.jsonl", "witnesses.csv", "recovery.jsonl",
                 *(f"mesh-{f}.bin" for f in KEYFRAMES)):
        if (output / "b-provenance" / name).read_bytes() != (OLD / name).read_bytes():
            raise RuntimeError("Default evidence differs: " + name)
    if (output / "b-provenance/seed.json").read_bytes() != (output / "c-provenance/seed.json").read_bytes():
        raise RuntimeError("B/C seed differs")
    for name, workers, mode, enabled in (
        ("b8-normal", 8, "normal-immutable", False),
        ("c8-normal", 8, "normal-immutable", True),
        ("c1-normal", 1, "normal-immutable", True),
        ("opengl-c-export", 8, "visual-immutable", True),
        ("opengl-b-export", 8, "visual-immutable", False),
    ):
        args = ["--transactional-platform-replay", "peking", "transactional", str(workers), win(output/name), mode]
        if enabled:
            args.append("--flip-recovery")
        if native_run(output, name, app, "peking", "transactional", workers, mode, arguments=args)["status"] != "ok":
            raise RuntimeError("Platform run failed: " + name)


def evaluate(output, probe):
    # B沿用同一实际float输出的既有质量；没有哈希对应时不能引用旧评价
    b = rows(output / "b8-normal/frames.csv")
    old = rows(QUALITY_B / "frames.csv")
    compare(b, old, CORE_FIELDS, "B quality source")
    for frame in KEYFRAMES:
        for origin, name in ((QUALITY_B, "opengl-b-export"),):
            if (origin/f"mesh-{frame}.bin").read_bytes() != (output/name/f"mesh-{frame}.bin").read_bytes():
                raise RuntimeError("Actual B mesh differs")
            link = output/name/f"quality-{frame}"
            if not link.exists():
                link.symlink_to(origin/f"quality-{frame}", target_is_directory=True)
    quality(output, probe)


def distribution(values):
    values = sorted(values)
    return {"mean": statistics.mean(values), "median": statistics.median(values),
            "p95": values[math.ceil(.95*len(values))-1], "max": values[-1]}


def excess(left, right, frame):
    a = json.loads((left/f"quality-{frame}/quality.json").read_text())
    b = json.loads((right/f"quality-{frame}/quality.json").read_text())
    if (a["sampleHash"], a["q"], a["visible"]) != (b["sampleHash"], b["q"], b["visible"]):
        raise RuntimeError("Pointwise sample identity mismatch")
    x, y = array("d"), array("d")
    x.frombytes((left/f"quality-{frame}/errors.f64").read_bytes())
    y.frombytes((right/f"quality-{frame}/errors.f64").read_bytes())
    if len(x) != a["q"] or len(y) != len(x):
        raise RuntimeError("Pointwise evidence size mismatch")
    best, ordinal = -math.inf, None
    for i, (u, v) in enumerate(zip(x, y)):
        if not math.isfinite(u) or not math.isfinite(v) or (u >= 0) != (v >= 0):
            raise RuntimeError("Pointwise visibility mismatch")
        if u >= 0 and u-v > best:
            best, ordinal = u-v, i
    return {"DmaxPx": best, "ordinal": ordinal, "deltaEmaxPx": a["screenMax"]-b["screenMax"],
            "deltaHmax": a["heightMax"]-b["heightMax"]}


def flip_history(output):
    flips = [json.loads(line) for line in (output/"c-provenance/transactions.jsonl").read_text().splitlines()
             if json.loads(line)["kind"] == "R"]
    last, reversals = {}, []
    for f in flips:
        g = f["geometry"]
        old = [tuple(v[0] for v in face[1:]) for face in g["oldFaces"]]
        def diagonal(faces):
            counts = Counter(tuple(sorted((face[i], face[(i+1)%3]))) for face in faces for i in range(3))
            return next(e for e, n in counts.items() if n == 2)
        key = tuple(sorted(p[0] for p in f["points"]))
        f["oldEdge"], f["newEdge"] = diagonal(old), diagonal(g["newFaces"])
        old_f = last.get(key)
        if old_f and old_f["oldEdge"] == f["newEdge"] and old_f["newEdge"] == f["oldEdge"]:
            reversals.append({"from": old_f["frame"], "to": f["frame"], "vertices": key,
                              "sameView": old_f["matrix"] == f["matrix"],
                              "sameGeometry": old_f["points"] == f["points"]})
        if any(p[3] != p[4] for p in f["points"]) or f["free"] or g["newPoint"] is not None:
            raise RuntimeError("Flip changed persistent vertex geometry")
        last[key] = f
    return {"count": len(flips), "reversals": reversals, "transactions": flips}


def report(output):
    paths = {name: rows(output/name/"frames.csv") for name in ("before-normal", "b8-normal", "c8-normal", "c1-normal",
                                                           "opengl-b-export", "opengl-c-export")}
    compare(paths["before-normal"], paths["b8-normal"], SEMANTICS, "default before/after")
    compare(paths["c1-normal"], paths["c8-normal"], SEMANTICS+FLIP_FIELDS, "C1/C8")
    compare(paths["b8-normal"], paths["opengl-b-export"], SEMANTICS+FLIP_FIELDS, "B visual")
    compare(paths["c8-normal"], paths["opengl-c-export"], SEMANTICS+FLIP_FIELDS, "C visual")
    for label in ("b", "c"):
        compare(rows(output/f"{label}-provenance/frames.csv"), paths[f"{label}8-normal"], CORE_FIELDS, label+" core/platform")
    result = {"runs": {}, "quality": {}, "pairs": {}, "flipHistory": flip_history(output),
              "q2": {label: [r for r in rows(output/f"{label}-provenance/witnesses.csv")
                             if r["witness"] == "2" and r["phase"] == "after"] for label in ("b", "c")}}
    for name, data in paths.items():
        if not name.endswith("normal"):
            continue
        groups = {"all": data, "cold": data[:1], "warm": data[1:], "moving": data[3:16], "return": data[16:]}
        result["runs"][name] = {group: {field: distribution([float(r[field]) for r in subset])
                                        for field in ("cpuMs", "uploadMs", "frameMs", *STAGES)}
                               for group, subset in groups.items()}
        result["runs"][name]["counts"] = {field: sum(int(r.get(field, 0)) for r in data)
                                               for field in ("exchanges", "free", "pairs", *FLIP_FIELDS)}
    for frame in KEYFRAMES:
        for label, directory in (("b", QUALITY_B), ("c", output/"opengl-c-export"), ("dod", QUALITY_DOD)):
            q = json.loads((directory/f"quality-{frame}/quality.json").read_text())
            if q["meshHash"] != rows(directory/"frames.csv")[frame]["hash"]:
                raise RuntimeError("Quality output identity mismatch")
            result["quality"].setdefault(label, {})[frame] = q
        result["pairs"][frame] = {"C-B": excess(output/"opengl-c-export", QUALITY_B, frame),
                                  "C-DOD": excess(output/"opengl-c-export", QUALITY_DOD, frame)}
    write(output/"flip-analysis.json", result)
    print("flips", result["flipHistory"]["count"], "reversals", result["flipHistory"]["reversals"])
    for label in ("b", "c"):
        print(label, [(r["frame"], r["futureError"]) for r in result["q2"][label] if r["frame"] in ("15", "23")])


def visuals(output):
    from PIL import Image
    import numpy as np
    from experiment_infrastructure.asset_preview import configure_font, plt
    configure_font()
    data = json.loads((output/"flip-analysis.json").read_text())
    fig, axes = plt.subplots(1, 3, figsize=(15, 4))
    for label, color in (("b", "#b65a44"), ("c", "#176b9a")):
        q2 = data["q2"][label]
        axes[0].plot([int(x["frame"]) for x in q2], [float(x["futureError"]) for x in q2], label=label.upper(), color=color)
        axes[1].plot(KEYFRAMES, [data["quality"][label][str(f)]["screenMax"] for f in KEYFRAMES], "o", label=label.upper(), color=color)
    axes[1].plot(KEYFRAMES, [data["quality"]["dod"][str(f)]["screenMax"] for f in KEYFRAMES], "x", label="DOD", color="#777777")
    axes[0].axvline(8, color="#777777", linestyle="--", linewidth=1)
    axes[0].set_title("q2：固定 frame15 投影");axes[0].set_ylabel("误差 px")
    axes[1].set_title("实际 float 输出：仅评价五帧")
    for name, label in (("b8-normal", "B8"), ("c8-normal", "C8"), ("c1-normal", "C1")):
        rs = rows(output/name/"frames.csv")
        axes[2].plot([int(r["frame"]) for r in rs[1:]], [float(r["cpuMs"]) for r in rs[1:]], label=label)
    axes[2].set_title("CPU-ready：暖路径，单进程轨迹");axes[2].set_ylabel("ms")
    for ax in axes:
        ax.set_xlabel("机会 / frame");ax.grid(alpha=.2);ax.legend()
    fig.tight_layout();fig.savefig(output/"quality-and-cost.png", dpi=160);plt.close(fig)

    capture_frames = (0, 7, 8, 15, 16, 23)
    fig, axes = plt.subplots(len(capture_frames), 3, figsize=(16, 18))
    captures = []
    for row, frame in enumerate(capture_frames):
        images = []
        for col, label in enumerate(("b", "c")):
            path = output/f"opengl-{label}-export/frame-{frame}.ppm"
            image = Image.open(path).convert("RGB");image.save(path.with_suffix(".png"))
            pixels = np.asarray(image);images.append(pixels)
            axes[row, col].imshow(pixels);axes[row, col].set_title(f"{label.upper()} / frame {frame}")
        delta = np.max(np.abs(images[1].astype(int)-images[0].astype(int)), axis=2)
        axes[row, 2].imshow(delta, vmin=0, vmax=8, cmap="magma")
        axes[row, 2].set_title(f"像素绝对差 max={int(delta.max())}/255；色阶0..8")
        captures.append({"frame": frame, "changedPixels": int(np.count_nonzero(delta)), "maxChannelDelta": int(delta.max())})
        for ax in axes[row]:
            ax.axis("off")
    fig.tight_layout();fig.savefig(output/"actual-frames.png", dpi=140);plt.close(fig)

    # 使用导出矩阵把参数域见证定位到实际画面，不将离线重绘当成平台截图
    import struct
    frame = 15
    binary = (output/f"opengl-c-export/mesh-{frame}.bin").read_bytes()
    matrix = np.array(struct.unpack_from("<16f", binary, 44)).reshape(4, 4)
    nv, ni = struct.unpack_from("<QQ", binary, 8)
    vertices = np.frombuffer(binary, dtype="<f4", count=nv*13, offset=108).reshape(nv, 13)
    indices = np.frombuffer(binary, dtype="<u4", count=ni, offset=108+nv*52).reshape(-1, 3)
    triangles = vertices[indices].astype(float)
    def height_at(u, v):
        uv = triangles[:, :, 6:8]
        a, b, c = uv[:, 0], uv[:, 1], uv[:, 2]
        det = (b[:, 1]-c[:, 1])*(a[:, 0]-c[:, 0])+(c[:, 0]-b[:, 0])*(a[:, 1]-c[:, 1])
        w0 = ((b[:, 1]-c[:, 1])*(u-c[:, 0])+(c[:, 0]-b[:, 0])*(v-c[:, 1]))/det
        w1 = ((c[:, 1]-a[:, 1])*(u-c[:, 0])+(a[:, 0]-c[:, 0])*(v-c[:, 1]))/det
        # 此容差仅为截图标记定位，独立质量求值仍使用原严格评价器
        matches = np.flatnonzero((w0 >= -1e-10) & (w1 >= -1e-10) & (w0+w1 <= 1+1e-10))
        if not len(matches):
            raise RuntimeError("Visual witness has no covering face")
        i = matches[0]
        return w0[i]*triangles[i, 0, 1]+w1[i]*triangles[i, 1, 1]+(1-w0[i]-w1[i])*triangles[i, 2, 1]
    worst = data["quality"]["c"][str(frame)]["screenWitness"]
    witnesses = (("q2", .97985345125198364, .94871795177459717), ("C 新最大点", worst[1], worst[2]))
    fig, axes = plt.subplots(2, 2, figsize=(10, 8))
    for row, (name, u, v) in enumerate(witnesses):
        h = height_at(u, v)
        clip = matrix @ np.array([(u-.5)*80, h, (v-.5)*80, 1])
        x, y = (clip[0]/clip[3]+1)*640, (1-clip[1]/clip[3])*360
        for col, label in enumerate(("b", "c")):
            pixels = np.asarray(Image.open(output/f"opengl-{label}-export/frame-{frame}.png"))
            axes[row, col].imshow(pixels)
            axes[row, col].set_xlim(max(0, x-110), min(1280, x+110));axes[row, col].set_ylim(min(720, y+90), max(0, y-90))
            axes[row, col].plot(x, y, marker="+", color="#ff4b6e", markersize=9)
            axes[row, col].set_title(f"{label.upper()} frame15 / {name}")
    fig.tight_layout();fig.savefig(output/"witness-crops.png", dpi=160);plt.close(fig)
    write(output/"visual-capture-checks.json", {"captures": captures, "note": "Pixel differences are diagnostic, not a perceptual quality threshold"})


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=("collect", "quality", "report", "visuals"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--app", type=Path)
    parser.add_argument("--probe", type=Path)
    args = parser.parse_args()
    if args.mode == "collect":
        collect(args.output.resolve(), args.app.resolve(), args.probe.resolve())
    elif args.mode == "quality":
        evaluate(args.output.resolve(), args.probe.resolve())
    elif args.mode == "report":
        report(args.output.resolve())
    else:
        visuals(args.output.resolve())
