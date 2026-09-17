"""源高度消融的只读归约；冻结提案与持续轨迹分别解释，不混作同任务加速。"""

import argparse
from collections import Counter
from fractions import Fraction
import json
from pathlib import Path
import time
import numpy as np

from transactional_proposal_feasibility import exact_config, evaluate_height, geometry, shape_failure, Unknown
from analyze_transactional_current_profile import rows, comparison, write_csv, perf_details, tracy_details
from analyze_transactional_task_balance import distribution
from run_transactional_platform import identity


def source_value(source, u, v, scale):
    """按生产binary64表达式生成试值；质量判定仍由独立有理模型完成。"""
    x, y = u * (source["width"] - 1), v * (source["height"] - 1)
    ix, iy = min(int(x), source["width"] - 2), min(int(y), source["height"] - 2)
    base = iy * source["width"] + ix
    tx, ty = x - ix, y - iy
    values = source["values"]
    lower = values[base] * (1 - tx) + values[base + 1] * tx
    upper = values[base + source["width"]] * (1 - tx) + values[base + source["width"] + 1] * tx
    return ((1 - ty) * lower + ty * upper) * scale / 65535


def local_inputs(previous, output):
    output.mkdir(parents=True, exist_ok=False)
    records = []
    started = time.monotonic()
    deadline = started + 1200
    for capture in sorted(previous.glob("*-capture")):
        source = json.loads((capture / "feasibility-source.json").read_text())
        for path in sorted(capture.glob("proposal-*.json")):
            raw = json.loads(path.read_text())
            data = raw["input"]
            work = Counter()
            row = {key: data[key] for key in ("frame", "root", "ordinal", "kind")}
            row.update(capture=capture.name, path=str(path), oldReason=raw["oldReason"], work=work,
                       oldFitHeight=raw["fitHeight"], sampleCount=len(raw["samples"]))
            try:
                if time.monotonic() > deadline:
                    raise Unknown("local_audit_time_cap")
                if raw["status"] != "complete":
                    row["status"] = "censored"
                elif shape_failure(geometry(data["new"])):
                    row["status"] = "shape_rejected"
                elif raw["constructionReason"]:
                    row["status"] = "construction_rejected"
                else:
                    point = next(p for p in data["new"]["points"] if p[0] == data["newVertex"])
                    height = source_value(source, point[1], point[2], data["config"]["heightScale"])
                    row["sourceHeight"] = height
                    # B不属于替换范围，保留为原固定高度控制而非源高分支收益
                    if data["kind"] == "B":
                        height = raw["fitHeight"]
                        row["unchangedBoundaryControl"] = True
                    domains = evaluate_height(raw, source, exact_config(data["config"]), height, work, deadline)
                    row["domains"] = domains
                    row["status"] = "accepted" if all(d["accepted"] for d in domains) else "quality_rejected"
            except (Unknown, ValueError, ZeroDivisionError) as error:
                row.update(status="unknown", reason=str(error))
            records.append(row)
            (output / f"{capture.name}-{path.stem}.json").write_text(
                json.dumps(row, ensure_ascii=False, indent=2) + "\n")
            print(capture.name, path.name, row["status"], flush=True)
    result = dict(records=records, counts=dict(Counter(r["status"] for r in records)),
                  seconds=time.monotonic() - started, scope="冻结旧提案的源高度单点消融")
    (output / "summary.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n")
    return result


def summarize(raw, output):
    output.mkdir(parents=True, exist_ok=True)
    report = {"scope": "不同生成政策；独立进程快验；不是同任务并行加速", "cases": {}}
    timing = []
    for scene in ("dem-sierra", "dem-canyon"):
        baseline = rows(raw / f"{scene}-P0/timing/run/frames.csv")
        candidate = rows(raw / f"{scene}-PS/timing/run/frames.csv")
        historical = rows(raw.parent.parent / f"qpc-06e/run-01/{scene}-P/windows/run/frames.csv")
        case = {"oldPathCompatibility": comparison(historical, baseline), "events": {}}
        if not case["oldPathCompatibility"]["equal"]:
            raise ValueError(case["oldPathCompatibility"])
        for event in sorted({r["event"] for r in baseline} | {"complete-warm"}):
            groups = [[r for r in values if not int(r["warmup"]) and
                       (event == "complete-warm" or r["event"] == event)] for values in (baseline, candidate)]
            fields = ("cpuMs", "receiverMs", "viewMs", "donorMs", "reservationMs", "sampleRepairMs", "uploadMs",
                      "faces", "receivers", "exchanges", "free", "pairs", "touches", "evaluations")
            case["events"][event] = {}
            for field in fields:
                a, b = [distribution([float(r[field]) for r in group]) for group in groups]
                case["events"][event][field] = {"P0": a, "PS": b, "ratio": b["mean"] / a["mean"] if a["mean"] else None}
                timing.append(dict(scene=scene, event=event, metric=field, P0mean=a["mean"], PSmean=b["mean"],
                                   P0median=a["median"], PSmedian=b["median"], P0p95=a["p95"], PSp95=b["p95"],
                                   delta=b["mean"] - a["mean"]))
        case["totalActivity"] = {arm: {field: sum(int(r[field]) for r in values) for field in ("exchanges", "free")}
                                 for arm, values in (("P0", baseline), ("PS", candidate))}
        case["finalFaces"] = {"P0": int(baseline[-1]["faces"]), "PS": int(candidate[-1]["faces"])}
        case["observedRuns"] = {}
        for arm, expected in (("P0", baseline), ("PS", candidate)):
            for mode in ("quality", "visual", "timing-final"):
                path = raw / f"{scene}-{arm}/{mode}/run/frames.csv"
                if path.exists():
                    case["observedRuns"][f"{arm}-{mode}"] = comparison(expected, rows(path))
        report["cases"][scene] = case
        print(scene, "moving", case["events"]["reveal"]["cpuMs"], case["totalActivity"], case["finalFaces"], flush=True)
    write_csv(output / "timing.csv", timing)
    local = json.loads((raw / "local-audit-01/summary.json").read_text())
    # 提交归约只保留结论和计数；完整有理见证留在带身份的原始目录。
    report["localAudit"] = {key: local[key] for key in ("counts", "seconds", "scope")}
    local_rows = []
    for record in local["records"]:
        item = {k: v for k, v in record.items() if k not in ("domains", "work")}
        for index, domain in enumerate(record.get("domains", [])):
            for field in ("accepted", "visible", "deltaLower", "deltaUpper", "oldMaxSquared", "newMaxSquared"):
                item[f"domain{index}_{field}"] = domain[field]
            item[f"domain{index}_screenViolations"] = len(domain["screenViolations"])
            item[f"domain{index}_heightViolations"] = len(domain["heightViolations"])
        local_rows.append(item)
    fields = list(dict.fromkeys(key for row in local_rows for key in row))
    write_csv(output / "local-proposals.csv", [{k: row.get(k, "") for k in fields} for row in local_rows])
    accepted = [r for r in local["records"] if r["status"] == "accepted"]
    report["localProgress"] = {
        "unchangedMax": sum(r["domains"][0]["oldMaxSquared"] == r["domains"][0]["newMaxSquared"] for r in accepted),
        "tinyProgressBelow1e-12": sum(float(Fraction(r["domains"][0]["deltaLower"])) < 1e-12 for r in accepted)}
    capture = raw / 'dem-sierra-PS'
    for mode, function in (("perf", perf_details), ("tracy", tracy_details)):
        if (capture / mode / "profile-result.json").exists():
            destination = output / mode
            destination.mkdir(exist_ok=True)
            report[mode] = function(capture / mode, set(range(3, 32)), destination)
    for scene in ("dem-sierra", "dem-canyon"):
        quality = {}
        for arm in ("P0", "PS"):
            base = raw / f"{scene}-{arm}/evaluation"
            if (base / "quality-index.json").exists():
                index = json.loads((base / "quality-index.json").read_text())
                quality[arm] = [{**r, "values": json.loads((base / r["quality"]).read_text()) if "quality" in r else None}
                                for r in index["frames"]]
        if quality:
            report["cases"][scene]["quality"] = quality
    supplement(raw, output, report)
    (output / "summary.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    return report


def supplement(raw, output, report):
    """统计独立质量、单次复测和工作账本；不重写几何评价器。"""
    quality_rows, repeat_rows, work_rows = [], [], []
    for scene, case in report["cases"].items():
        case["repeat"] = {}
        for arm in ("before", "P0", "PS"):
            path = raw / f"{scene}-{arm}/timing-repeat/run/frames.csv"
            if not path.exists():
                continue
            values = rows(path)
            expected = rows(raw / f'{scene}-{"P0" if arm == "before" else arm}/timing/run/frames.csv')
            record = {"logic": comparison(expected, values), "moving": {}}
            for key in ("cpuMs", "receiverMs", "viewMs", "donorMs", "reservationMs", "sampleRepairMs"):
                value = distribution([float(r[key]) for r in values if 3 <= int(r["frame"]) <= 31])
                record["moving"][key] = value
                repeat_rows.append(dict(scene=scene, arm=arm, metric=key, **value))
            case["repeat"][arm] = record
        if "quality" in case and set(case["quality"]) == {"P0", "PS"}:
            # 先沿已有身份校验结果配对，再只归约同一可见域中的误差分布。
            paired = json.loads((raw / f"{scene}-PS-P0-dmax.json").read_text())
            for left, right, pair in zip(case["quality"]["PS"], case["quality"]["P0"], paired["frames"]):
                assert left["frame"] == right["frame"] == pair["frame"] and pair["status"] == "paired"
                arrays = [np.fromfile(raw / f"{scene}-{arm}/evaluation" / row["errors"], dtype="<f8")
                          for arm, row in (("P0", right), ("PS", left))]
                mask = arrays[0] >= 0
                assert np.array_equal(mask, arrays[1] >= 0)
                a, b = [x[mask] for x in arrays]
                delta = b - a
                record = dict(scene=scene, frame=left["frame"], visible=len(a), Dmax=pair["DmaxSamplePx"])
                for arm, row, values in (("P0", right, a), ("PS", left, b)):
                    record[arm + "Faces"] = row["faces"]
                    for metric in ("screenMax", "heightMax", "terrainSampleRms"):
                        record[arm + metric] = row["values"][metric]
                    record[arm + "ErrorP95"] = float(np.quantile(values, .95))
                    record[arm + "AboveTargetCount"] = int(np.count_nonzero(values > .5))
                for threshold in (.1, .25, .5):
                    record[f"ExcessAbove{threshold}Count"] = int(np.count_nonzero(delta > threshold))
                    record[f"ImprovementAbove{threshold}Count"] = int(np.count_nonzero(delta < -threshold))
                quality_rows.append(record)
        trace = raw / f"{scene}-PS-trace/pointwise-work.csv"
        if trace.exists():
            counts = Counter()
            for row in rows(trace):
                if row["type"] == "count" and 3 <= int(row["frame"]) <= 31:
                    counts[row["key"]] += int(row["value"])
            case["movingTraceCounts"] = dict(counts)
            work_rows.extend(dict(scene=scene, key=key, value=value) for key, value in counts.items())
    write_csv(output / "quality.csv", quality_rows)
    write_csv(output / "repeat.csv", repeat_rows)
    write_csv(output / "work.csv", work_rows)
    report["qualityDistribution"] = quality_rows
    if (raw / "final-check.json").exists():
        report["finalCheck"] = json.loads((raw / "final-check.json").read_text())
        for scene in report["cases"]:
            values = rows(raw / f"{scene}-PS/timing-final/run/frames.csv")
            report["finalCheck"]["cases"][scene]["movingCpu"] = distribution(
                [float(r["cpuMs"]) for r in values if 3 <= int(r["frame"]) <= 31])
    exchange = raw / "exchange-audit.json"
    if exchange.exists():
        report["exchangeAudit"] = json.loads(exchange.read_text())
    report["nativeWitnesses"] = []
    for path in sorted((raw / "native-source-witness").glob("proposal-*.json")):
        data = json.loads(path.read_text())
        for witness in data.get("verifiedWitnesses", []):
            report["nativeWitnesses"].append(dict(input=identity(path), kind=data["input"]["kind"], **witness))
    provenance = []
    for pattern in ("**/manifest.json", "**/quality-index.json", "commands/*.json", "*programs.json", "freeze.json",
                    "config-tests.json", "native-cache-restore.json", "final-check.json"):
        for path in sorted(raw.glob(pattern)):
            provenance.append(dict(file=identity(path), record=json.loads(path.read_text())))
    (output / "provenance.json").write_text(json.dumps(provenance, ensure_ascii=False, indent=2) + "\n")
    draw(raw, output)


def draw(raw, output):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib import font_manager
    from matplotlib.ticker import MaxNLocator
    font = Path("/mnt/c/Windows/Fonts/msyh.ttc")
    if font.exists():
        font_manager.fontManager.addfont(str(font))
        plt.rcParams["font.family"] = font_manager.FontProperties(fname=str(font)).get_name()
    figure, axes = plt.subplots(2, 2, figsize=(12, 7), layout="constrained")
    for column, scene in enumerate(("dem-sierra", "dem-canyon")):
        for arm in ("P0", "PS"):
            values = rows(raw / f"{scene}-{arm}/timing/run/frames.csv")[3:]
            axes[0, column].plot([int(r["frame"]) for r in values], [float(r["cpuMs"]) for r in values], label=arm)
            axes[1, column].plot([int(r["frame"]) for r in values], [int(r["faces"]) for r in values], label=arm)
        axes[0, column].set(title=scene, ylabel="完整 CPU 时间（ms）")
        axes[1, column].set(xlabel="帧序号", ylabel="实际三角形数")
        axes[1, column].ticklabel_format(axis="y", style="plain", useOffset=False)
        axes[1, column].yaxis.set_major_locator(MaxNLocator(integer=True))
        for axis in axes[:, column]:
            axis.axvline(32, color="grey", linestyle=":")
            axis.axvline(64, color="grey", linestyle=":")
            axis.legend()
            axis.grid(alpha=.2)
    figure.savefig(output / "trajectory.png", dpi=140)
    plt.close(figure)
    figure, axes = plt.subplots(4, 2, figsize=(12, 14), layout="constrained")
    visual_sources = []
    for row, (scene, frame) in enumerate((('dem-sierra', 16), ('dem-sierra', 48),
                                         ('dem-canyon', 16), ('dem-canyon', 48))):
        for column, arm in enumerate(("P0", "PS")):
            path = raw / f"{scene}-{arm}/visual/run/frame-{frame}.png"
            if path.exists():
                axes[row, column].imshow(plt.imread(path))
                visual_sources.append(identity(path))
            axes[row, column].set_title(f"{scene} / frame{frame} / {arm}")
            axes[row, column].axis("off")
    figure.savefig(output / "visual-contact.png", dpi=120)
    plt.close(figure)
    (output / "visual-sources.json").write_text(json.dumps(visual_sources, indent=2) + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="源高候选的有限离线审计")
    parser.add_argument("--local-inputs", type=Path)
    parser.add_argument("--runs", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.local_inputs:
        result = local_inputs(args.local_inputs, args.output)
        print(result["counts"], result["seconds"])
    elif args.runs:
        summarize(args.runs, args.output)
    else:
        parser.error("需要--local-inputs或--runs")
