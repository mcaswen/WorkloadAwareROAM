"""复用既有帧、perf和Tracy归约，比较面证据复用前后的同结果与成本。"""

import argparse
from bisect import bisect_right
import hashlib
import json
from pathlib import Path
import time

from analyze_transactional_current_profile import comparison, perf_details, rows, tracy_details, write_csv
from analyze_transactional_task_balance import compare_timing, item_details
from profiling.tracy_report import contains, read_zones


def face_work(capture, selected, destination):
    zones = read_zones((capture / "profile/zones.csv").read_text())
    frames = sorted((z for z in zones if z["name"] == "profile.frame"), key=lambda z: z["start"])
    starts = [z["start"] for z in frames]
    fields = ("records", "boundsBuilds", "exactBuilds", "boundsSides", "exactSides", "boundsHeights", "exactHeights")
    records = []
    for zone in zones:
        if zone["name"] != "gtp.face_evidence":
            continue
        index = bisect_right(starts, zone["start"]) - 1
        if index < 0 or not contains(frames[index], zone):
            continue
        frame = int(frames[index]["text"].split(":")[1])
        if frame not in selected:
            continue
        values = list(map(int, zone["text"].split("/")))
        assert len(values) == len(fields)
        records.append({"frame": frame, "thread": zone["thread"], **dict(zip(fields, values))})
    write_csv(destination / "face-work.csv", records)
    total = {field: sum(r[field] for r in records) for field in fields}
    total["certifications"] = len(records)
    # 依据原表达式计固定坐标减法：每边两次；高度的面积四次、w1两次。
    # 这是同查询集合下的旧公式工作推算，不冒充旧程序硬件指令计数。
    for prefix in ("bounds", "exact"):
        total[prefix + "OriginalFixedSubtractions"] = 2 * total[prefix + "Sides"] + 6 * total[prefix + "Heights"]
        total[prefix + "PreparedFixedSubtractions"] = 8 * total[prefix + "Builds"]
    return total


def analyze(raw, output):
    output.mkdir(parents=True, exist_ok=True)
    prior = raw.parent.parent / "qpc-06d/run-01"
    result = {"scope": "面证据同结果工程快验；独立进程为单位；双系统不换算", "cases": {}}
    records, repeated = [], []
    for scene in ("dem-sierra", "dem-canyon"):
        for policy in ("L", "P"):
            name = f"{scene}-{policy}"
            case = {}
            for system, mode in (("linux", "fp-timing"), ("windows", "windows")):
                current = raw / name / mode / "run/frames.csv"
                if not current.exists():
                    continue
                case[system] = compare_timing(rows(prior / name / mode / "run/frames.csv"), rows(current))
            selected = case["linux"]["events"]["reveal"]["frames"]
            dest = output / name
            dest.mkdir(exist_ok=True)
            case["profileBehavior"] = {}
            for mode in ("perf", "tracy", "tracy-timing"):
                directory = raw / name / mode
                if not directory.exists():
                    continue
                check = comparison(rows(raw / name / "fp-timing/run/frames.csv"), rows(directory / "run/frames.csv"))
                if not check["equal"]:
                    raise ValueError((name, mode, check))
                case["profileBehavior"][mode] = check
                if mode == "perf":
                    case["perf"] = perf_details(directory, selected, dest)
                if mode == "tracy":
                    case["tracy"] = tracy_details(directory, selected, dest)
                    case["items"] = item_details(directory, selected, dest)
                    case["faceWork"] = face_work(directory, selected, dest)
                    case["captureOverhead"] = compare_timing(rows(raw / name / "tracy-timing/run/frames.csv"),
                                                             rows(directory / "run/frames.csv"))
            case["recheck"] = {}
            for system in ("linux", "windows"):
                repeat = raw / "recheck" / name / system
                if (repeat / "after/run/frames.csv").exists():
                    case["recheck"][system] = compare_timing(rows(repeat / "before/run/frames.csv"),
                                                            rows(repeat / "after/run/frames.csv"))
            for source, target in ((case, records), (case["recheck"], repeated)):
                for system in ("linux", "windows"):
                    if system not in source:
                        continue
                    for event, values in source[system]["events"].items():
                        for metric, statistics in values["metrics"].items():
                            for key in ("mean", "median", "p95", "max"):
                                target.append({"case": name, "system": system, "event": event, "metric": metric,
                                               "statistic": key, "beforeMs": statistics["before"][key],
                                               "afterMs": statistics["after"][key], "deltaMs": statistics["deltaMs"][key],
                                               "relative": statistics["relative"][key]})
            result["cases"][name] = case
    write_csv(output / "timing-comparison.csv", records)
    write_csv(output / "timing-recheck.csv", repeated)
    result["checks"] = json.loads((raw / "checks.json").read_text())
    result["freeze"] = json.loads((raw / "freeze.json").read_text())
    result["sources"] = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(raw.rglob("*"))
                         if p.name in ("manifest.json", "frames.csv", "profile-result.json", "perf-script.txt", "zones.csv")}
    (output / "summary.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return result


def plot(output):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib import font_manager

    font = Path("/mnt/c/Windows/Fonts/msyh.ttc")
    if font.exists():
        font_manager.fontManager.addfont(str(font))
        plt.rcParams["font.family"] = font_manager.FontProperties(fname=str(font)).get_name()
    figure, axis = plt.subplots(figsize=(10, 4), layout="constrained")
    for source, label in ((output.parent / "qpc_06d_task_balance", "修改前"), (output, "面证据复用")):
        values = [r for r in rows(source / "dem-sierra-P/tracy-items.csv") if int(r["frame"]) == 28]
        values.sort(key=lambda r: int(r["rootIndex"]))
        axis.plot([int(r["rootIndex"]) for r in values], [float(r["durationMs"]) for r in values], label=label)
    axis.set(title="Sierra P 第28帧：同一根前缀的认证时间（Linux Tracy）", xlabel="前缀索引", ylabel="单根时间（ms）")
    axis.legend()
    axis.grid(alpha=.2)
    figure.savefig(output / "root-cost.png", dpi=150)
    plt.close(figure)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="面证据复用成本归约")
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--plot", action="store_true")
    args = parser.parse_args()
    started = time.monotonic()
    report = analyze(args.input.resolve(), args.output.resolve())
    if args.plot:
        plot(args.output.resolve())
    for name, case in report["cases"].items():
        for system in ("linux", "windows"):
            if system in case:
                print(name, system, {key: {"before": value["before"]["mean"], "after": value["after"]["mean"],
                                          "relative": value["relative"]["mean"]}
                                    for key, value in case[system]["events"]["reveal"]["metrics"].items()})
    print(f"归约耗时 {time.monotonic() - started:.3f}s")
