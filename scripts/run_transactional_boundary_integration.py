"""QPC-04B：固定三轨迹的政策对照、实际输出质量和有限成本归约。"""

import argparse
import csv
import json
from pathlib import Path
import time

import numpy as np

from run_transactional_platform import ROOT, identity, native_run, win, write
from run_transactional_boundary_audit import CASES, PRIOR, statistics_for
from run_transactional_recovery_trace import sampling_coordinates, components
from experiment_infrastructure import runner
from experiment_infrastructure.quality import evaluate, pointwise_pair


HISTORY = ROOT / "benchmark-output/experiment-infrastructure/fer-02"
FRAMES = {case: ((2, 48, 80, 95) if "canyon" in case else (2, 15, 16, 23)) for case in CASES}
SEMANTICS = ("frame", "hash", "faces", "raw", "examined", "receivers", "need", "feasible", "exchanges", "free",
             "pairs", "conflicts", "donorReuse", "touches")
STAGES = ("cpuMs", "uploadMs", "frameMs", "viewMs", "receiverMs", "donorMs", "reservationMs",
          "topologyPrepareMs", "topologyPublishMs", "sampleRepairMs", "meshPrepareMs", "continuationMs")


def read(path):
    return json.loads(path.read_text(encoding="utf-8"))


def rows(path):
    with path.open(encoding="utf-8-sig", newline="") as stream:
        return list(csv.DictReader(stream))


def compare(a, b, fields=SEMANTICS):
    if len(a) != len(b):
        raise RuntimeError("机会数不同")
    for left, right in zip(a, b):
        for field in fields:
            if left[field] != right[field]:
                raise RuntimeError(f"frame {left['frame']} 的 {field} 不同：{left[field]} / {right[field]}")


def limited(started):
    if time.monotonic() - started > 45 * 60:
        raise RuntimeError("本轮采集/评价已达硬时间边界，保留完成部分")


def collect(output, app, probe):
    """政策与见证在新运行前冻结，恢复执行只复用身份相同的既有产物。"""
    started = time.monotonic()
    selection = read(PRIOR / "selection.json")["cases"]
    freeze = {"protocol": "qpc04b-source-midpoint-v1", "app": identity(app), "probe": identity(probe), "cases": {}}
    for case in CASES:
        old = selection[case]
        resolved = Path(old["resolved"]["path"])
        if identity(resolved)["sha256"] != old["resolved"]["sha256"]:
            raise RuntimeError("历史输入改变：" + case)
        config = read(ROOT / "configs/experiments/formal/fer_02" / (case + ".json"))
        config["boundaryRefinement"] = True
        config_file = output / (case + ".json")
        write(config_file, config)
        witnesses = [dict(item) for item in old["witnesses"]]
        u, v = ((.86328125, .9990234375) if "canyon" in case else (.9404761904761905, .9981684981684982))
        witnesses.append({"frame": witnesses[0]["frame"], "returnFrame": witnesses[0]["returnFrame"],
                          "ordinal": 0, "u": u, "v": v, "roles": ["04a-added-degradation"]})
        table = output / (case + ".txt")
        table.write_text("".join(f"{w['frame']} {w['returnFrame']} {w['ordinal']} {w['u']:.17g} {w['v']:.17g}\n"
                                  for w in witnesses), encoding="utf-8")
        freeze["cases"][case] = {"oldResolved": identity(resolved), "config": identity(config_file),
                                  "witnessTable": identity(table), "witnesses": witnesses, "qualityFrames": FRAMES[case]}
    frozen = output / "freeze.json"
    # JSON 往返统一 tuple/list，再比较避免恢复时误判同一冻结协议
    freeze = json.loads(json.dumps(freeze))
    if frozen.exists() and read(frozen) != freeze:
        raise RuntimeError("程序或输入改变，不能覆盖已有实验")
    write(frozen, freeze)
    resolved = Path(freeze["cases"][CASES[0]]["oldResolved"]["path"])
    if native_run(output, "platform-after-off", app, "peking", "transactional", 8, "timing",
                  arguments=["--experiment-run", win(resolved), win(output / "platform-after-off")])["status"] != "ok":
        raise RuntimeError("关闭政策平台运行失败")
    compare(rows(output / "platform-before/frames.csv"), rows(output / "platform-after-off/frames.csv"))
    for case, item in freeze["cases"].items():
        limited(started)
        for enabled in (False, True):
            if enabled:
                for mode in ("timing", "visual"):
                    destination = output / (case + "-" + mode)
                    if not (destination / "manifest.json").exists():
                        runner.run(output / (case + ".json"), destination, app, mode=mode)
                    manifest = read(destination / "manifest.json")
                    if manifest["status"] != "ok" or manifest["binary"]["sha256"] != freeze["app"]["sha256"]:
                        raise RuntimeError("正常运行未完成或二进制不同：" + str(destination))
                    old_manifest = read(HISTORY / ("r1-" + case) / "manifest.json")
                    if manifest["workloadId"] != old_manifest["workloadId"]:
                        raise RuntimeError("自然输入身份改变")
                runner.compare_modes([output / (case + "-" + mode) for mode in ("timing", "visual")])
                resolved = output / (case + "-timing/inputs/resolved.json")
            else:
                resolved = Path(item["oldResolved"]["path"])
            name = case + ("-trace-on" if enabled else "-trace-off")
            result = native_run(output, name, probe, case, "transactional", 8, "audit",
                                arguments=["--recovery-trace", win(resolved), win(output / (case + ".txt")), win(output / name)])
            if result["status"] != "ok":
                raise RuntimeError("续接追溯失败：" + name)
            if not enabled:
                for field in ("frames.csv", "boundary.csv"):
                    if (output / name / field).read_bytes() != (PRIOR / case / field).read_bytes():
                        raise RuntimeError("关闭政策改变历史轨迹：" + case + "/" + field)
            else:
                compare(rows(output / name / "frames.csv"), rows(output / (case + "-timing/run/frames.csv")))
        print("completed", case, flush=True)
    # 优先 Peking 50k；若未触及 B，再增加首个含 B 的既采输入
    serial_cases = [CASES[0]]
    counts = {}
    for case in CASES:
        data = rows(output / (case + "-timing/run/boundary-refinement.csv"))
        counts[case] = sum(int(row["freeExecuted"]) + int(row["pairedExecuted"]) for row in data)
    if counts[CASES[0]] == 0:
        extra = next((case for case in CASES[1:] if counts[case]), None)
        if extra:
            serial_cases.append(extra)
    for case in serial_cases:
        limited(started)
        config = read(output / (case + ".json"))
        config["workers"] = 1
        config_file = output / (case + "-t1.json")
        write(config_file, config)
        destination = output / (case + "-serial")
        if not destination.exists():
            runner.run(config_file, destination, app)
        compare(rows(destination / "run/frames.csv"), rows(output / (case + "-timing/run/frames.csv")))
        compare(rows(destination / "run/boundary-refinement.csv"), rows(output / (case + "-timing/run/boundary-refinement.csv")),
                tuple(rows(destination / "run/boundary-refinement.csv")[0]))
    write(output / "collection.json", {"seconds": time.monotonic() - started, "serialCases": serial_cases,
                                       "boundaryExecuted": counts})


def quality(output, probe):
    started = time.monotonic()
    for case in CASES:
        limited(started)
        target = output / ("quality-" + case)
        if not target.exists():
            evaluate(output / (case + "-visual"), target, probe, FRAMES[case])
        for label, reference in (("off", case), ("dod", case.replace("transactional", "dod"))):
            pair = output / (case + "-vs-" + label + ".json")
            if not pair.exists():
                pointwise_pair(target, HISTORY / ("quality-" + reference), pair)
        print("quality completed", case, flush=True)


def report(output):
    """全域质量、逐点分布与固定投影见证分账，不用均值抵消坏帧。"""
    freeze = read(output / "freeze.json")
    result = {"protocol": freeze["protocol"], "cases": {}, "performance": {}}
    for name in ("platform-before", "platform-after-off"):
        warm = [row for row in rows(output / name / "frames.csv") if row["warmup"] == "0" and row["cold"] == "0"]
        result["performance"][name] = {key: statistics_for(warm, key) for key in STAGES}
    for case in CASES:
        normal = rows(output / (case + "-timing/run/frames.csv"))
        off = rows(output / (case + "-trace-off/frames.csv"))
        on = rows(output / (case + "-trace-on/frames.csv"))
        budgets = rows(output / (case + "-trace-on/boundary-refinement.csv"))
        entry = {"quality": [], "witnesses": {}, "counts": {}, "frames": normal, "budgets": budgets}
        for label, data in (("off", off), ("on", on)):
            entry["counts"][label] = {key: sum(int(row[key]) for row in data) for key in
                                      ("exchanges", "free", "flips", "pairs", "conflicts")}
        entry["counts"]["boundary"] = {key: sum(int(row[key]) for row in budgets) for key in
            ("attempts", "certified", "resolutionRejected", "conflicts", "freeExecuted", "pairedExecuted", "releasedFaces")}
        entry["counts"]["boundary"].update(initialVertices=int(budgets[0]["boundaryVertices"]) -
            int(budgets[0]["freeExecuted"]) - int(budgets[0]["pairedExecuted"]), finalVertices=int(budgets[-1]["boundaryVertices"]))
        warm = [row for row in normal if row["warmup"] == "0" and row["cold"] == "0"]
        entry["performance"] = {key: statistics_for(warm, key) for key in STAGES}
        off_timing = output / (case + "-off-timing/frames.csv")
        if case == CASES[0]:
            off_timing = output / "platform-after-off/frames.csv"
        if off_timing.exists():
            current_off = rows(off_timing)
            compare(current_off, off)
            warm_off = [row for row in current_off if row["warmup"] == "0" and row["cold"] == "0"]
            entry["currentOffPerformance"] = {key: statistics_for(warm_off, key) for key in STAGES}
        serial = output / (case + "-serial/run/frames.csv")
        if serial.exists():
            serial_rows = rows(serial)
            compare(serial_rows, normal)
            warm_serial = [row for row in serial_rows if row["warmup"] == "0" and row["cold"] == "0"]
            entry["serialPerformance"] = {key: statistics_for(warm_serial, key) for key in STAGES}
        entry["coldPerformance"] = {key: float(normal[0][key]) for key in (*STAGES, "seedMs", "initializeMs")}
        index = read(output / ("quality-" + case) / "quality-index.json")
        old_index = read(HISTORY / ("quality-" + case) / "quality-index.json")
        if index["probe"]["sha256"] != old_index["probe"]["sha256"]:
            raise RuntimeError("评价器与基线不同")
        for frame in FRAMES[case]:
            quality_row = {"frame": frame}
            errors = {}
            for label, directory in (("on", output / ("quality-" + case)), ("off", HISTORY / ("quality-" + case)),
                                     ("dod", HISTORY / ("quality-" + case.replace("transactional", "dod")))):
                q = read(directory / f"frame-{frame}/quality.json")
                if any(q[key] for key in ("missing", "ambiguous", "invalidGeometry", "invalidProjection")):
                    raise RuntimeError("独立质量含无效评价")
                if label != "dod" and q["meshHash"] != (normal if label == "on" else off)[frame]["hash"]:
                    raise RuntimeError("旧质量与本轮实际网格不同")
                quality_row[label] = q
                errors[label] = np.fromfile(directory / f"frame-{frame}/errors.f64", dtype="<f8")
            visible = errors["on"] >= 0
            if any(not np.array_equal(values >= 0, visible) for values in errors.values()):
                raise RuntimeError("共同可见域不同")
            quality_row["above"] = {label: {str(cut): int(np.count_nonzero(values[visible] > cut))
                                                for cut in (.1, .25, .5, 1)} for label, values in errors.items()}
            quality_row["Dmax"] = {label: float(np.max(errors["on"][visible] - errors[label][visible]))
                                   for label in ("off", "dod")}
            if "canyon" in case and frame == 48:
                config = read(output / (case + "-timing/inputs/resolved-portable.json"))
                uv = sampling_coordinates(config["width"], config["height"])
                x = np.minimum((uv[:, 0] * (config["width"] - 1)).astype(int), config["width"] - 2)
                y = np.minimum((uv[:, 1] * (config["height"] - 1)).astype(int), config["height"] - 2)
                quality_row["clusters"] = {}
                for label in ("off", "on"):
                    selected = visible & (errors[label] - errors["dod"] > .25)
                    occupied = np.zeros((config["height"] - 1, config["width"] - 1), dtype=bool)
                    occupied[y[selected], x[selected]] = True
                    labels, count = components(occupied)
                    sizes = np.bincount(labels[y[selected], x[selected]], minlength=count)
                    quality_row["clusters"][label] = {"samples": int(selected.sum()), "visible": int(visible.sum()),
                        "components": count, "largest": int(sizes.max()) if count else 0}
            entry["quality"].append(quality_row)
        for label in ("off", "on"):
            directory = output / (case + "-trace-" + label)
            entry["witnesses"][label] = {p.parent.name: rows(p) for p in directory.glob("frame-*/witnesses.csv")}
        result["cases"][case] = entry
    write(output / "analysis.json", result)
    summary = {"protocol": result["protocol"], "freeze": freeze, "performance": result["performance"],
               "collection": read(output / "collection.json"), "cases": {}, "evidence": {}}
    for case, entry in result["cases"].items():
        compact = {key: value for key, value in entry.items() if key not in ("frames", "budgets", "witnesses")}
        compact["faceRange"] = [min(int(row["faces"]) for row in entry["frames"]),
                                 max(int(row["faces"]) for row in entry["frames"])]
        compact["finalFaces"] = int(entry["frames"][-1]["faces"])
        compact["witnesses"] = {label: {group: [row for row in data if row["phase"] == "after" and
            int(row["frame"]) in FRAMES[case]] for group, data in groups.items()} for label, groups in entry["witnesses"].items()}
        summary["cases"][case] = compact
        for relative in (case + "-timing/manifest.json", case + "-visual/manifest.json",
                         "quality-" + case + "/quality-index.json", case + "-trace-on/frames.csv",
                         case + "-trace-on/boundary-refinement.csv", case + "-trace-off/frames.csv"):
            summary["evidence"][relative] = identity(output / relative)
    write(output / "summary.json", summary)
    for case, item in result["cases"].items():
        print(case, item["counts"]["boundary"], "CPU", item["performance"]["cpuMs"]["mean"])
        for q in item["quality"]:
            print(q["frame"], "Emax", q["off"]["screenMax"], q["on"]["screenMax"],
                  "Hmax", q["off"]["heightMax"], q["on"]["heightMax"], "Dmax", q["Dmax"])


def visuals(output):
    """显示真实平台截图和独立误差图，图像差异不当作感知质量结论。"""
    from PIL import Image
    from experiment_infrastructure.asset_preview import configure_font, plt
    configure_font()
    analysis = read(output / "analysis.json")
    labels = ("Peking 50k", "Peking 200k", "Canyon 50k")
    evidence = []
    fig, axes = plt.subplots(3, 2, figsize=(12, 12), layout="constrained")
    for row, (case, title) in enumerate(zip(CASES, labels)):
        entry = analysis["cases"][case]
        for label, color in (("off", "#647789"), ("on", "#bc5938")):
            axes[row, 0].plot(FRAMES[case], [q[label]["screenMax"] for q in entry["quality"]], "o-", label=label, color=color)
            group = next(iter(entry["witnesses"][label].values()))
            witness = [r for r in group if r["phase"] == "after" and r["witness"] == "0"]
            axes[row, 1].plot([int(r["frame"]) for r in witness], [float(r["futureError"]) for r in witness], label=label, color=color)
        axes[row, 0].plot(FRAMES[case], [q["dod"]["screenMax"] for q in entry["quality"]], "x--", label="DOD", color="#3b856d")
        axes[row, 0].set_title(title + "：实际 float 输出的 sampled Emax")
        axes[row, 1].set_title(title + "：原最大见证，固定来源帧投影")
        for ax in axes[row]:
            ax.set_xlabel("机会 / frame")
            ax.set_ylabel("px")
            ax.grid(alpha=.2)
            ax.legend()
    fig.savefig(output / "quality-witnesses.png", dpi=150)
    plt.close(fig)
    for case, title in zip(CASES, labels):
        fig, axes = plt.subplots(4, 2, figsize=(12, 14), layout="constrained")
        before = HISTORY / ("visual-" + case)
        after = output / (case + "-visual")
        before_rows, after_rows = rows(before / "run/frames.csv"), rows(after / "run/frames.csv")
        compare(before_rows, rows(output / (case + "-trace-off/frames.csv")))
        for row, frame in enumerate(FRAMES[case]):
            if any(before_rows[frame][key] != after_rows[frame][key] for key in ("poseHash", "projectionHash")):
                raise RuntimeError("截图视图不同")
            for column, (label, directory) in enumerate((("off", before), ("on", after))):
                path = directory / "run" / f"frame-{frame}.png"
                pixels = np.asarray(Image.open(path).convert("RGB"))
                axes[row, column].imshow(pixels)
                axes[row, column].set_title(f"{title} / {label} / frame {frame}")
                axes[row, column].axis("off")
                evidence.append({"case": case, "frame": frame, "policy": label, "image": identity(path)})
        fig.savefig(output / (case + "-actual.png"), dpi=150)
        plt.close(fig)
    case = CASES[2]
    config = read(output / (case + "-timing/inputs/resolved-portable.json"))
    uv = sampling_coordinates(config["width"], config["height"])
    x = np.minimum((uv[:, 0] * (config["width"] - 1)).astype(int), config["width"] - 2)
    y = np.minimum((uv[:, 1] * (config["height"] - 1)).astype(int), config["height"] - 2)
    field = []
    for base in (HISTORY, output):
        values = np.fromfile(base / ("quality-" + case) / "frame-48/errors.f64", dtype="<f8")
        cells = np.full((config["height"] - 1, config["width"] - 1), np.nan)
        visible = values >= 0
        np.fmax.at(cells, (y[visible], x[visible]), values[visible])
        field.append(cells)
    fig, axes = plt.subplots(1, 3, figsize=(14, 4.5), layout="constrained")
    for ax, values, title in zip(axes[:2], field, ("关闭", "开启")):
        image = ax.imshow(values, origin="lower", extent=(0, 1, 0, 1), vmin=0, vmax=10, cmap="magma")
        ax.set_title(title + "：每栅格单元采样最大值")
    fig.colorbar(image, ax=axes[:2], label="px")
    delta = axes[2].imshow(field[1] - field[0], origin="lower", extent=(0, 1, 0, 1), vmin=-5, vmax=5, cmap="coolwarm")
    axes[2].set_title("单元最大值之差（不是 Dmax）")
    fig.colorbar(delta, ax=axes[2], label="px")
    fig.suptitle("Canyon / frame48：完整共同可见采样域，不只展示原最大点")
    fig.savefig(output / "canyon-error-distribution.png", dpi=150)
    plt.close(fig)
    write(output / "visual-sources.json", {"captures": evidence, "userAcceptance": "pending"})


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
        quality(args.output.resolve(), args.probe.resolve())
    elif args.mode == "report":
        report(args.output.resolve())
    else:
        visuals(args.output.resolve())
