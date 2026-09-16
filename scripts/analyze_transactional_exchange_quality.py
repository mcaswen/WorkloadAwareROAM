"""同一局部几何计算六个预注册操作点，输出保留、阻断、请求缺口与费用。"""

import argparse
import csv
import json
import math
from pathlib import Path
import resource
import statistics
import time

from run_transactional_exchange_quality import read, rows
from run_transactional_platform import write, identity
from transactional_exchange_quality import inspect_batch, OPERATING_POINTS


def request_gaps(path):
    population = rows(path)
    return [dict(e=float(e), allRoots=len(population),
                 qualityRoots=sum(float(row["errorSquared"]) > float(e*e) for row in population),
                 ineligibleQualityRoots=sum(float(row["errorSquared"]) > float(e*e) and row["eligible"] == "0" for row in population),
                 prefixQualityRoots=sum(float(row["errorSquared"]) > float(e*e) and row["inPrefix"] == "1" for row in population))
            for e in sorted({e for e, _ in OPERATING_POINTS})]


def timing(path):
    values = sorted(float(row["cpuMs"]) for row in rows(path) if row["warmup"] == "0")
    return dict(count=len(values), mean=statistics.mean(values), median=statistics.median(values),
                p95=values[max(0, math.ceil(.95*len(values))-1)])


def retention(batches):
    result = []
    for index, (e, ratio) in enumerate(OPERATING_POINTS):
        per_batch = []
        for batch in batches:
            complete = [entry for entry in batch["exchanges"] if entry["status"] == "complete"]
            ops = [entry["operatingPoints"][index] for entry in complete]
            accepted = [op for op in ops if op["accepted"]]
            positive = sum(max(0, op["deltaPsiLower"]) for op in ops)
            retained = sum(op["deltaPsiLower"] for op in accepted)
            per_batch.append(dict(name=batch["name"], frame=batch["frame"], approved=batch["approved"],
                                  known=len(complete), retained=len(accepted), screenRejected=sum(not op["screenPass"] for op in ops),
                                  heightRejected=sum(not op["heightPass"] for op in ops),
                                  positive=sum(op["progress"] == "positive" for op in ops),
                                  retainedPsi=retained, positivePsi=positive,
                                  progressFraction=retained/positive if positive else None))
        result.append(dict(e=float(e), hRatio=str(ratio), batches=per_batch,
                           opportunityGate=sum(b["retained"] >= 2 for b in per_batch) >= 2))
    return result


def write_tables(directory, summary):
    tables = {
        "retention.csv": [dict(e=r["e"], hRatio=r["hRatio"], **b) for r in summary["retention"] for b in r["batches"]],
        "request-gaps.csv": [dict(name=b["name"], frame=b["frame"], **r) for b in summary["batches"] for r in b["requestGaps"]],
        "work.csv": [dict(name=b["name"], frame=b["frame"], **b["work"]) for b in summary["batches"]],
    }
    for name, data in tables.items():
        fields = list(dict.fromkeys(key for row in data for key in row))
        with (directory / name).open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=fields)
            writer.writeheader()
            writer.writerows(data)


def figures(output, batches):
    from experiment_infrastructure.asset_preview import configure_font, plt
    configure_font()
    chosen = []
    for batch in batches:
        if batch["name"] == "canyon" and batch["frame"] == 24:
            entry = next((e for e in batch["exchanges"] if e["donorCenter"] == 5928 and e["status"] == "complete"), None)
            if entry is not None:
                chosen.append((batch, entry))
        if batch["name"] == "canyon-composite":
            # 历史坏例位置固定，不根据本轮接受结果另挑可展示事务
            q = (.86328125, .9990234375)
            point_file = output / "analysis" / f"{batch['name']}-{batch['frame']}-points.csv"
            if not point_file.exists():
                continue
            samples = rows(point_file)
            hit = min(samples, key=lambda p: (float(p["u"])-q[0])**2+(float(p["v"])-q[1])**2)
            chosen.append((batch, next(e for e in batch["exchanges"] if e["index"] == int(hit["exchange"]))))
    fig, axes = plt.subplots(max(1, len(chosen)), 3, figsize=(14, 4.4*max(1, len(chosen))), squeeze=False, layout="constrained")
    if not chosen:
        for ax in axes[0]:
            ax.axis("off")
        axes[0][1].text(.5, .5, "已知坏例证据缺失或未知，未生成局部对照", ha="center")
    for row, (batch, entry) in zip(axes, chosen):
        raw = read(output / batch["name"] / f"exchange-quality-{batch['frame']}.json")
        exchange = raw["exchanges"][entry["index"]]
        side = exchange["donor"] if entry["donorCenter"] == 5928 else exchange["receiver"]
        for label, color in (("old", "#4575b4"), ("new", "#d73027")):
            points = {p[0]: p[1:3] for p in side[label]["points"]}
            for face in side[label]["faces"]:
                xy = [points[v] for v in face]
                row[0].plot(*zip(*(xy+xy[:1])), color=color, alpha=.7, linewidth=1, label=label)
        handles, labels = row[0].get_legend_handles_labels()
        unique = dict(zip(labels, handles))
        row[0].legend(unique.values(), unique.keys())
        row[0].set(title=f"{batch['name']} f{batch['frame']} / exchange {entry['index']}", xlabel="U", ylabel="V", aspect="equal")
        ids = {row[0] for row in side["samples"]}
        samples = [r for r in rows(output / "analysis" / f"{batch['name']}-{batch['frame']}-points.csv")
                   if int(r["exchange"]) == entry["index"] and int(r["sample"]) in ids]
        for ax, old, new, limit, title in ((row[1], "oldScreen", "newScreen", 6, "当前视图屏幕误差（px；仅可见）"),
                                          (row[2], "oldResidual", "newResidual", 1.25, "全闭支持高度残差（world）")):
            data = [r for r in samples if old != "oldScreen" or r["visible"] == "True"]
            ax.scatter([float(r[old]) for r in data], [float(r[new]) for r in data], s=14, alpha=.65)
            ax.plot([0, limit], [0, limit], "--", color="gray")
            ax.set(xlabel="旧", ylabel="新", xlim=(0, limit), ylim=(0, limit), title=title)
            if not data:
                ax.text(.5, .5, "无当前可见样本", transform=ax.transAxes, ha="center")
    fig.suptitle("QPC-04E｜原轨迹局部证据；不是新政策持续运行结果")
    fig.savefig(output / "analysis/known-counterexamples.png", dpi=160)
    plt.close(fig)
    table = retention(batches)
    fig, ax = plt.subplots(figsize=(12, 4.5), layout="constrained")
    ax.axis("off")
    columns = [f"{b['name']}\nf{b['frame']} ({b['approved']})" for b in batches]
    cells = [[str(x["retained"]) for x in r["batches"]] for r in table]
    labels = [f"E={r['e']} px; H/scale={r['hRatio']}" for r in table]
    widget = ax.table(cellText=cells, rowLabels=labels, colLabels=columns, loc="center", cellLoc="center")
    widget.auto_set_font_size(False)
    widget.set_fontsize(9)
    widget.scale(1, 2)
    ax.set_title("保留的正进展完整交换数｜括号为原批准数；空批保留为零")
    fig.savefig(output / "analysis/retention.png", dpi=160)
    plt.close(fig)


def analyze(output, make_figures=False):
    started = time.monotonic()
    deadline = started + 1200
    resource.setrlimit(resource.RLIMIT_AS, (8*1024**3, 8*1024**3))
    freeze = read(output / "freeze.json")
    checker = identity(Path(__file__).with_name("transactional_exchange_quality.py"))
    batches = []
    for case in freeze["cases"]:
        directory = output / case["name"]
        for frame in case["frames"]:
            if not (directory / "exchange-source.json").exists() or not (directory / f"exchange-quality-{frame}.json").exists():
                batches.append(dict(name=case["name"], frame=frame, approved=None, exchanges=[], work={"seconds": 0},
                                    status="not-collected", requestGaps=[], captureWork={}))
                continue
            source = read(directory / "exchange-source.json")
            target = output / "analysis" / f"{case['name']}-{frame}.json"
            if target.exists():
                result = read(target)
                if result.get("checker", {}).get("sha256") != checker["sha256"]:
                    raise RuntimeError("数值检查器改变，不能静默复用旧判断")
                if result["input"]["sha256"] != identity(directory / f"exchange-quality-{frame}.json")["sha256"]:
                    raise RuntimeError("原始证据改变，不能复用旧判断")
                if result["source"]["sha256"] != identity(directory / "exchange-source.json")["sha256"]:
                    raise RuntimeError("源高度改变，不能复用旧判断")
            else:
                raw = read(directory / f"exchange-quality-{frame}.json")
                result, points = inspect_batch(raw, source, deadline)
                result.update(name=case["name"], checker=checker, input=identity(directory / f"exchange-quality-{frame}.json"),
                              source=identity(directory / "exchange-source.json"), captureWork=raw["work"],
                              requestGaps=request_gaps(directory / f"exchange-roots-{frame}.csv"))
                write(target, result)
                if points:
                    with target.with_name(target.stem+"-points.csv").open("w", newline="") as stream:
                        writer = csv.DictWriter(stream, fieldnames=points[0].keys())
                        writer.writeheader()
                        writer.writerows(points)
            batches.append(result)
            print(case["name"], frame, result["approved"], dict(result["work"]), flush=True)
            if time.monotonic() > deadline:
                break
    summary = dict(batches=batches, retention=retention(batches), analysisSeconds=time.monotonic()-started,
                   checker=checker, evidenceEvaluationSeconds=sum(b["work"]["seconds"] for b in batches),
                   peakRssBytes=resource.getrusage(resource.RUSAGE_SELF).ru_maxrss*1024,
                   before=timing(output / "normal-before/frames.csv"), after=timing(output / "normal-after/frames.csv"),
                   sumSemantics="exact rational per sample; outward dyadic 128-bit finite-sum enclosure")
    if (output / "normal-before-repeat/frames.csv").exists():
        summary["repeat"] = {label: timing(output / f"normal-{label}-repeat/frames.csv") for label in ("before", "after")}
    write(output / "analysis/summary.json", summary)
    write_tables(output / "analysis", summary)
    if make_figures:
        figures(output, batches)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--figures", action="store_true")
    args = parser.parse_args()
    analyze(args.output.resolve(), args.figures)
