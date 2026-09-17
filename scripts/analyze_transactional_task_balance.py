"""核对根动态领取前后的行为、完整时间与线程长尾，不执行被测程序。"""

import argparse
from bisect import bisect_right
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import statistics
import time

from analyze_transactional_current_profile import comparison, perf_details, rows, tracy_details, write_csv
from profiling.tracy_report import contains, read_zones


def distribution(values):
    ordered = sorted(values)
    position = (len(ordered) - 1) * .95
    lower = int(position)
    upper = min(lower + 1, len(ordered) - 1)
    return {"mean": statistics.mean(ordered), "median": statistics.median(ordered),
            "p95": ordered[lower] + (position - lower) * (ordered[upper] - ordered[lower]),
            "max": ordered[-1]}


def compare_timing(before, after):
    check = comparison(before, after)
    if not check["equal"]:
        raise ValueError(check)
    events = {}
    for event in sorted({r["event"] for r in before if not int(r["warmup"])}):
        selected = {int(r["frame"]) for r in before if not int(r["warmup"]) and r["event"] == event}
        left = [r for r in before if int(r["frame"]) in selected]
        right = [r for r in after if int(r["frame"]) in selected]
        events[event] = {"frames": sorted(selected), "metrics": {}}
        for field in ("cpuMs", "viewMs", "receiverMs", "donorMs", "reservationMs", "sampleRepairMs", "uploadMs"):
            if field not in before[0] or field not in after[0]:
                continue
            # CPU适配器没有上传指标；空列表示不可用，不能伪造为零耗时。
            if all(not r[field] for r in left + right):
                continue
            a = distribution([float(r[field]) for r in left])
            b = distribution([float(r[field]) for r in right])
            events[event]["metrics"][field] = {"before": a, "after": b,
                "deltaMs": {key: b[key] - a[key] for key in a},
                "relative": {key: b[key] / a[key] - 1 if a[key] else None for key in a}}
    return {"comparison": check, "events": events}


def item_details(capture, selected, output):
    zones = read_zones((capture / "profile/zones.csv").read_text())
    frames = sorted((z for z in zones if z["name"] == "profile.frame"), key=lambda z: z["start"])
    starts = [f["start"] for f in frames]
    grouped = defaultdict(list)
    for zone in zones:
        index = bisect_right(starts, zone["start"]) - 1
        if index >= 0 and contains(frames[index], zone):
            frame_id = int(frames[index]["text"].split(":")[1])
            if frame_id in selected:
                grouped[frame_id].append(zone)
    records, summaries = [], []
    for frame_id, values in sorted(grouped.items()):
        frame = next(z for z in values if z["name"] == "profile.frame")
        items = [z for z in values if z["name"] == "gtp.item" and z["text"].startswith("receiver_stage/")]
        tasks = [z for z in values if z["name"] == "gtp.task" and z["text"].startswith("receiver_stage/")]
        local = []
        for item in items:
            parent = next(t for t in tasks if t["thread"] == item["thread"] and contains(t, item))
            record = {"frame": frame_id, "rootIndex": int(item["text"].split("/")[1]),
                      "task": parent["text"], "thread": item["thread"],
                      "startMs": (item["start"] - frame["start"]) / 1e6,
                      "endMs": (item["end"] - frame["start"]) / 1e6, "durationMs": item["duration"] / 1e6}
            for name, key in (("gtp.fit", "fit"), ("gtp.pointwise.certify", "pointwise")):
                children = [z for z in values if z["name"] == name and z["thread"] == item["thread"] and contains(item, z)]
                record[key + "Calls"] = len(children)
                record[key + "Ms"] = sum(z["duration"] for z in children) / 1e6
            local.append(record)
        indices = [r["rootIndex"] for r in local]
        assert sorted(indices) == list(range(160)), (frame_id, indices)
        records.extend(local)
        largest = max(local, key=lambda r: r["durationMs"])
        summaries.append({"frame": frame_id, "items": len(local),
                          "itemSumMs": sum(r["durationMs"] for r in local),
                          "maxItemMs": largest["durationMs"], "maxItemIndex": largest["rootIndex"],
                          "taskItems": {t["text"]: sum(r["task"] == t["text"] for r in local) for t in tasks}})
    write_csv(output / "tracy-items.csv", records)
    return {"frames": summaries, "largestItems": sorted(records, key=lambda r: -r["durationMs"])[:10]}


def analyze(raw, output):
    output.mkdir(parents=True, exist_ok=True)
    earlier = raw.parent.parent / "qpc-06c/run-01"
    native = raw.parent.parent / "qpc-06b/run-01"
    report = {"scope": "独立进程工程快验；同政策前后；跨系统分表", "cases": {}}
    flat, repeated = [], []
    for scene in ("dem-sierra", "dem-canyon"):
        for policy, arm in (("L", "L1"), ("P", "P2")):
            name = f"{scene}-{policy}"
            dest = output / name
            dest.mkdir(exist_ok=True)
            current = raw / name
            before_cpu = rows(earlier / name / "fp-timing/run/frames.csv")
            after_cpu = rows(current / "fp-timing/run/frames.csv")
            result = {"linux": compare_timing(before_cpu, after_cpu)}
            result["profileBehavior"] = {}
            for mode in ("perf", "tracy-timing", "tracy"):
                captured = current / mode / "run/frames.csv"
                if captured.exists():
                    check = comparison(after_cpu, rows(captured))
                    if not check["equal"]:
                        raise ValueError((name, mode, check))
                    result["profileBehavior"][mode] = check
            if (current / "windows/run/frames.csv").exists():
                before = rows(native / arm / f"{scene}-b50000-transactional-t8/run/frames.csv")
                after = rows(current / "windows/run/frames.csv")
                result["windows"] = compare_timing(before, after)
            if (current / "tracy").exists():
                selected = result["linux"]["events"]["reveal"]["frames"]
                result["tracy"] = tracy_details(current / "tracy", selected, dest)
                result["items"] = item_details(current / "tracy", selected, dest)
                result["captureOverhead"] = compare_timing(rows(current / "tracy-timing/run/frames.csv"),
                                                           rows(current / "tracy/run/frames.csv"))
            if (current / "perf").exists():
                result["perf"] = perf_details(current / "perf", selected, dest)
            rechecks = {}
            for system in ("linux", "windows"):
                repeat = raw / "recheck" / name / system
                if (repeat / "after/run/frames.csv").exists():
                    rechecks[system] = compare_timing(rows(repeat / "before/run/frames.csv"),
                                                     rows(repeat / "after/run/frames.csv"))
            result["recheck"] = rechecks
            report["cases"][name] = result
            for source, records in ((result, flat), (rechecks, repeated)):
                for system in ("linux", "windows"):
                    if system not in source:
                        continue
                    for event, event_data in source[system]["events"].items():
                        for metric, values in event_data["metrics"].items():
                            for statistic in ("mean", "median", "p95", "max"):
                                records.append({"case": name, "system": system, "event": event, "metric": metric,
                                                "statistic": statistic, "beforeMs": values["before"][statistic],
                                                "afterMs": values["after"][statistic], "deltaMs": values["deltaMs"][statistic],
                                                "relative": values["relative"][statistic]})
    write_csv(output / "timing-comparison.csv", flat)
    write_csv(output / "timing-recheck.csv", repeated)
    report["sources"] = {str(p): hashlib.sha256(p.read_bytes()).hexdigest()
                         for p in sorted(raw.rglob("*")) if p.name in
                         ("manifest.json", "profile-result.json", "frames.csv", "zones.csv", "perf-script.txt")}
    (output / "summary.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return report


def plot(report, output):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib import font_manager

    font = Path("/mnt/c/Windows/Fonts/msyh.ttc")
    if font.exists():
        font_manager.fontManager.addfont(str(font))
        plt.rcParams["font.family"] = font_manager.FontProperties(fname=str(font)).get_name()

    figure, axes = plt.subplots(2, 2, figsize=(14, 8), sharex="col", layout="constrained")
    previous = output.parent / "qpc_06c_current_profile"
    for column, (name, frame_id) in enumerate((("dem-sierra-P", 25), ("dem-canyon-P", 5))):
        for row, label in enumerate(("修改前：固定连续块", "修改后：按根动态领取")):
            axis = axes[row, column]
            path = previous if row == 0 else output
            tasks = [r for r in rows(path / name / "tracy-tasks.csv") if int(r["frame"]) == frame_id and r["phase"] == "receiver_stage"]
            origin = min(float(t["startMs"]) for t in tasks)
            for index, task in enumerate(sorted(tasks, key=lambda r: r["task"])):
                axis.broken_barh([(float(task["startMs"]) - origin, float(task["durationMs"]))],
                                 (index - .35, .7), facecolors="#7d9cb3")
                if row == 1:
                    items = [r for r in rows(output / name / "tracy-items.csv") if int(r["frame"]) == frame_id and r["task"] == task["task"]]
                    for item in items:
                        axis.broken_barh([(float(item["startMs"]) - origin, float(item["durationMs"]))],
                                         (index - .28, .56), facecolors=plt.cm.tab20(int(item["rootIndex"]) % 20))
            axis.set_title(f"{name}，第{frame_id}帧\n{label}（Linux Tracy）")
            axis.set_ylabel("派发任务编号")
            axis.set_xlabel("从首个任务开始的时间（ms）")
            axis.set_yticks(range(8))
            axis.grid(axis="x", alpha=.2)
    figure.savefig(output / "receiver-task-timeline.png", dpi=150)
    plt.close(figure)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="接收根动态领取前后归约")
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--plot", action="store_true")
    args = parser.parse_args()
    started = time.monotonic()
    result = analyze(args.input.resolve(), args.output.resolve())
    if args.plot:
        plot(result, args.output.resolve())
    for name, case in result["cases"].items():
        print(name, {system: {key: case[system]["events"]["reveal"]["metrics"][key]
                             for key in ("cpuMs", "receiverMs")}
                     for system in ("linux", "windows") if system in case})
    print(f"归约耗时 {time.monotonic() - started:.3f}s")
