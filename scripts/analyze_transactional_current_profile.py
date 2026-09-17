"""归约当前质量政策的函数采样与时序，保留跨平台代表性和观测扰动边界。"""

import argparse
from bisect import bisect_right
from collections import Counter, defaultdict
import csv
import gzip
import hashlib
import json
from pathlib import Path
import statistics
import time

from profiling.report import read_windows, samples, summarize_perf
from profiling.tracy_report import contains, read_zones


LOGIC = (
    "frame", "sample", "poseHash", "projectionHash", "faces", "budget", "workers", "sequence", "hash", "split", "merge",
    "status", "updated", "cold", "seedFaces", "samples", "raw", "examined", "receivers", "need",
    "feasible", "exchanges", "free", "pairs", "conflicts", "donorReuse", "touches", "evaluations",
    "vertexWrites", "indexWrites",
)
METRICS = (
    "cpuMs", "viewMs", "receiverMs", "donorMs", "reservationMs", "sampleRepairMs",
    "raw", "examined", "receivers", "need", "feasible", "exchanges", "free", "pairs", "conflicts", "touches",
)


def rows(path):
    with path.open(newline="") as stream:
        return list(csv.DictReader(stream))


def write_csv(path, records):
    if not records:
        return
    opener = gzip.open if path.suffix == ".gz" else Path.open
    mode = "wt" if path.suffix == ".gz" else "w"
    with opener(path, mode, newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(records[0]), lineterminator="\n")
        writer.writeheader()
        writer.writerows(records)


def comparison(a, b):
    if len(a) != len(b):
        return {"equal": False, "lengths": [len(a), len(b)]}
    differences = []
    total = 0
    for left, right in zip(a, b):
        fields = {k: [left[k], right[k]] for k in LOGIC if left[k] != right[k]}
        if fields:
            total += 1
            if len(differences) < 3:
                differences.append({"frame": left["frame"], "fields": fields})
    return {"equal": total == 0, "differentFrames": total, "firstDifferences": differences}


def statistics_for(data):
    result = {"frames": len(data)}
    for key in METRICS:
        if not data or key not in data[0]:
            continue
        values = [float(r[key]) for r in data]
        result[key] = {"sum": sum(values), "mean": statistics.mean(values),
                       "median": statistics.median(values), "max": max(values)}
    return result


def perf_details(path, selected, output):
    windows = [w for w in read_windows((path / "run/windows.csv").read_text()) if w["round"] in selected]
    text = (path / "profile/perf-script.txt").read_text()
    summary = summarize_perf(text, windows)
    starts = [w["start_ns"] for w in windows]
    own, inclusive, counts, edges, stacks = Counter(), Counter(), Counter(), Counter(), Counter()
    numeric_contexts = Counter()
    for sample in samples(text):
        index = bisect_right(starts, sample["time_ns"]) - 1
        if index < 0 or sample["time_ns"] >= windows[index]["end_ns"]:
            continue
        chain = sample["frames"] or ["[missing stack]"]
        weight = sample["period"]
        own[chain[0]] += weight
        counts[chain[0]] += 1
        for name in set(chain):
            inclusive[name] += weight
        for child, parent in set(zip(chain, chain[1:])):
            edges[(parent, child)] += weight
        stacks[" <- ".join(chain)] += weight
        # 按互斥上层路径划分数值叶热点；不是相加父子inclusive百分比
        if "__nextafter" in chain[0]:
            context = "其他"
            for needle, label in (("TransactionalPointwiseQuality::Certify", "逐点认证"),
                                  ("TransactionalCertification::Fit", "旧Fit"),
                                  ("TransactionalProposals::Donor", "回收")):
                if any(needle in name for name in chain):
                    context = label
                    break
            numeric_contexts[context] += weight
    total = summary["roi_event_weight"]
    assert sum(own.values()) == total
    functions = [{"function": name, "selfPercent": 100 * own[name] / total,
                  "inclusivePercent": 100 * inclusive[name] / total, "selfSamples": counts[name],
                  "selfWeight": own[name], "inclusiveWeight": inclusive[name]}
                 for name in sorted(set(own) | set(inclusive), key=lambda n: (-own[n], n))]
    calls = [{"caller": a, "callee": b, "weight": weight, "percent": 100 * weight / total}
             for (a, b), weight in edges.most_common()]
    write_csv(output / "perf-functions.csv", functions)
    write_csv(output / "perf-call-edges.csv", calls)
    write_csv(output / "perf-paths.csv.gz", [{"path": name, "weight": weight, "percent": 100 * weight / total}
                                        for name, weight in stacks.most_common()])
    # 明细已经完整导出，摘要只保留样本质量与分母，避免重复巨型模板符号。
    for key in ("self", "inclusive", "paths"):
        summary.pop(key)
    summary["functionCount"] = len(functions)
    summary["nextafterContextsPercent"] = {k: 100 * v / total for k, v in numeric_contexts.items()}
    summary["lostHeader"] = [line for line in (path / "profile/perf-report.txt").read_text().splitlines()
                             if "Lost Samples" in line]
    return summary


def tracy_details(path, selected, output):
    zones = read_zones((path / "profile/zones.csv").read_text())
    frames = sorted((z for z in zones if z["name"] == "profile.frame"), key=lambda z: z["start"])
    assert len(frames) == 96
    starts = [f["start"] for f in frames]
    grouped = defaultdict(list)
    for zone in zones:
        index = bisect_right(starts, zone["start"]) - 1
        if index >= 0 and contains(frames[index], zone):
            frame_id = int(frames[index]["text"].split(":")[1])
            if frame_id in selected:
                grouped[frame_id].append(zone)
    aggregate = defaultdict(lambda: {"calls": 0, "inclusiveMs": 0, "selfMs": 0})
    tasks, frame_records, critical = [], [], []
    for frame_id, values in sorted(grouped.items()):
        frame = next(z for z in values if z["name"] == "profile.frame")
        by_thread = defaultdict(list)
        for z in values:
            entry = aggregate[z["name"]]
            entry["calls"] += 1
            entry["inclusiveMs"] += z["duration"] / 1e6
            entry["selfMs"] += z["duration"] / 1e6
            by_thread[z["thread"]].append(z)
        for sequence in by_thread.values():
            stack = []
            for z in sorted(sequence, key=lambda z: (z["start"], -z["end"])):
                while stack and z["start"] >= stack[-1]["end"]:
                    stack.pop()
                if stack:
                    assert contains(stack[-1], z)
                    aggregate[stack[-1]["name"]]["selfMs"] -= z["duration"] / 1e6
                stack.append(z)
        for dispatch in (z for z in values if z["name"] == "gtp.dispatch"):
            work = [z for z in values if z["name"] == "gtp.task" and contains(dispatch, z)
                    and z["text"].rsplit("/", 1)[0] == dispatch["text"]]
            if not work:
                continue
            maximum = max(work, key=lambda z: z["duration"])
            wait = sum(z["duration"] for z in values if z["name"] == "pool.wait" and contains(dispatch, z)
                       and z["thread"] == dispatch["thread"])
            entry = {"frame": frame_id, "phase": dispatch["text"], "frameMs": frame["duration"] / 1e6,
                     "dispatchMs": dispatch["duration"] / 1e6, "taskSumMs": sum(z["duration"] for z in work) / 1e6,
                     "taskMaxMs": maximum["duration"] / 1e6, "tasks": len(work),
                     "threads": len({z["thread"] for z in work}), "waitMs": wait / 1e6,
                     "afterLastTaskMs": (dispatch["end"] - max(z["end"] for z in work)) / 1e6}
            entry["balanceRatio"] = entry["taskSumMs"] / (entry["tasks"] * entry["taskMaxMs"])
            frame_records.append(entry)
            for task in work:
                tasks.append({"frame": frame_id, "phase": dispatch["text"], "task": task["text"],
                              "thread": task["thread"], "startMs": (task["start"] - frame["start"]) / 1e6,
                              "endMs": (task["end"] - frame["start"]) / 1e6, "durationMs": task["duration"] / 1e6})
            if dispatch["text"] == "receiver_stage":
                record = {**entry, "longestTask": maximum["text"]}
                for name in ("gtp.fit", "gtp.measure", "gtp.pointwise.certify", "gtp.receivers"):
                    children = [z for z in values if z["name"] == name and z["thread"] == maximum["thread"]
                                and contains(maximum, z)]
                    record[name] = {"calls": len(children), "ms": sum(z["duration"] for z in children) / 1e6,
                                    "maxCallMs": max((z["duration"] for z in children), default=0) / 1e6}
                critical.append(record)
    functions = [{"zone": name, **entry} for name, entry in
                 sorted(aggregate.items(), key=lambda pair: -pair[1]["inclusiveMs"])]
    assert all(r["selfMs"] > -1e-6 for r in functions)
    write_csv(output / "tracy-functions.csv", functions)
    write_csv(output / "tracy-dispatches.csv", frame_records)
    write_csv(output / "tracy-tasks.csv", tasks)
    phases = {}
    for phase in sorted({r["phase"] for r in frame_records}):
        records = [r for r in frame_records if r["phase"] == phase]
        phases[phase] = {"dispatches": len(records), **{key: statistics.mean(r[key] for r in records)
            for key in ("dispatchMs", "taskSumMs", "taskMaxMs", "waitMs", "afterLastTaskMs", "balanceRatio", "threads")}}
    return {"functions": functions, "phases": phases,
            "slowReceivers": sorted(critical, key=lambda r: -r["dispatchMs"])[:5]}


def analyze(raw, output):
    output.mkdir(parents=True, exist_ok=True)
    report = {"contract": "暖移动窗口函数与时序；采样不是精确计时，跨平台比例不迁移", "cases": {}}
    for scene in ("dem-sierra", "dem-canyon"):
        for policy, arm in (("L", "L1"), ("P", "P2")):
            name = f"{scene}-{policy}"
            case = raw / name
            dest = output / name
            dest.mkdir(exist_ok=True)
            data = {mode: rows(case / mode / "run/frames.csv") for mode in ("fp-timing", "perf", "tracy-timing", "tracy")}
            reference = rows(raw.parent.parent / "qpc-06b/run-01" / arm /
                             (scene + "-b50000-transactional-t8") / "run/frames.csv")
            events = {event: [int(r["frame"]) for r in data["fp-timing"] if not int(r["warmup"]) and r["event"] == event]
                      for event in sorted({r["event"] for r in data["fp-timing"] if not int(r["warmup"])})}
            selected = events["reveal"]
            detail = {"events": events, "comparisons": {mode: comparison(data["fp-timing"], frames)
                      for mode, frames in data.items()}, "windowsComparison": comparison(data["fp-timing"], reference),
                      "timing": {mode: {event: statistics_for([r for r in frames if int(r["frame"]) in ids])
                                for event, ids in events.items()} for mode, frames in {**data, "windows": reference}.items()}}
            if not all(v["equal"] for v in detail["comparisons"].values()):
                raise ValueError(f"{name}采集模式或构建改变结果，先处理代表性")
            detail["perf"] = perf_details(case / "perf", selected, dest)
            detail["tracy"] = tracy_details(case / "tracy", selected, dest)
            report["cases"][name] = detail
    report["sources"] = {str(p): hashlib.sha256(p.read_bytes()).hexdigest()
                         for p in sorted(raw.rglob("*")) if p.name in
                         ("manifest.json", "profile-result.json", "windows.csv", "frames.csv", "zones.csv", "perf-script.txt")}
    (output / "summary.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return report


def plot_report(report, raw, output):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    figure = plt.figure(figsize=(14, 9), layout="constrained")
    grid = figure.add_gridspec(2, 2, height_ratios=[1, 1.2])
    axis = figure.add_subplot(grid[0, :])
    names = list(report["cases"])
    bottom = [0.0] * len(names)
    stages = (("viewMs", "View"), ("receiverMs", "Receiver"), ("donorMs", "Donor"),
              ("reservationMs", "Reservation"), ("sampleRepairMs", "Sample repair"))
    for key, label in stages:
        values = [report["cases"][n]["timing"]["windows"]["reveal"][key]["mean"] for n in names]
        axis.bar(names, values, bottom=bottom, label=label)
        bottom = [a + b for a, b in zip(bottom, values)]
    total = [report["cases"][n]["timing"]["windows"]["reveal"]["cpuMs"]["mean"] for n in names]
    assert all(a <= b + 0.001 for a, b in zip(bottom, total))
    axis.bar(names, [b - a for a, b in zip(bottom, total)], bottom=bottom, label="Other")
    for index, value in enumerate(total):
        axis.text(index, value + 1, f"{value:.2f}", ha="center")
    axis.set_ylabel("Mean CPU-ready (ms)")
    axis.set_title("Native Windows baseline: warm moving frames only (29 frames per case)")
    axis.legend(ncol=6, loc="upper left")
    axis.set_ylim(0, max(total) * 1.2)
    for column, name in enumerate(("dem-sierra-P", "dem-canyon-P")):
        axis = figure.add_subplot(grid[1, column])
        case = report["cases"][name]
        slow = case["tracy"]["slowReceivers"][0]
        zones = read_zones((raw / name / "tracy/profile/zones.csv").read_text())
        frame = next(z for z in zones if z["name"] == "profile.frame" and z["text"] == f'0:{slow["frame"]}')
        dispatch = next(z for z in zones if z["name"] == "gtp.dispatch" and z["text"] == "receiver_stage" and contains(frame, z))
        tasks = sorted((z for z in zones if z["name"] == "gtp.task" and
                        z["text"].startswith("receiver_stage/") and contains(dispatch, z)), key=lambda z: z["text"])
        for row, task in enumerate(tasks):
            axis.broken_barh([((task["start"] - dispatch["start"]) / 1e6, task["duration"] / 1e6)],
                             (row - .34, .68), facecolors="#d4dbe2")
            for zone_name, color in (("gtp.fit", "#e69f00"), ("gtp.pointwise.certify", "#0072b2")):
                pieces = [((z["start"] - dispatch["start"]) / 1e6, z["duration"] / 1e6) for z in zones
                          if z["name"] == zone_name and z["thread"] == task["thread"] and contains(task, z)]
                axis.broken_barh(pieces, (row - .3, .6), facecolors=color)
        axis.set_yticks(range(len(tasks)), [t["text"].split("/")[-1] for t in tasks])
        axis.set_ylabel("Chunk (20 roots each)")
        axis.set_xlabel("Time since receiver dispatch (ms)")
        axis.set_title(f'{name}, frame {slow["frame"]}: Linux Tracy\norange=Fit, blue=pointwise, gray=other')
        axis.grid(axis="x", alpha=.2)
    figure.savefig(output / "moving-cost-and-tasks.png", dpi=150)
    plt.close(figure)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="当前移动负载函数与时序归因")
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--plot", action="store_true")
    args = parser.parse_args()
    started = time.monotonic()
    result = analyze(args.input.resolve(), args.output.resolve())
    if args.plot:
        plot_report(result, args.input.resolve(), args.output.resolve())
    print(json.dumps({name: {"samples": c["perf"]["roi_samples"], "windowsEqual": c["windowsComparison"]["equal"]}
                      for name, c in result["cases"].items()}, ensure_ascii=False))
    print(f"归约耗时 {time.monotonic() - started:.3f}s")
