"""归约04F现存计时与工作账本；不运行算法，也不把任务墙钟折算成帧时间。"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import statistics
import time

ROOT = Path(__file__).resolve().parents[1]
RAW = ROOT / "benchmark-output/cpu-refinement/qpc-04f/run-01"
OUT = ROOT / "docs/research/cpu_refinement/data/qpc_04f_quality_cost"
SCENES = ("peking547", "dem-sierra", "dem-canyon")
STAGES = ("viewMs", "receiverMs", "donorMs", "reservationMs", "sampleRepairMs",
          "topologyPrepareMs", "topologyPublishMs", "meshPrepareMs", "continuationMs", "adapterMs")
COUNTS = ("raw", "examined", "receivers", "exchanges", "free", "pairs", "touches", "evaluations")
MATCH = ("frame", "hash", "faces", "raw", "examined", "receivers", "need", "feasible",
         "exchanges", "free", "pairs", "conflicts", "donorReuse", "touches")


def read_rows(path: Path, sources: dict) -> list[dict]:
    data = path.read_bytes()
    sources[str(path.relative_to(ROOT))] = hashlib.sha256(data).hexdigest()
    return list(csv.DictReader(data.decode("utf-8-sig").splitlines()))


def read_json(path: Path, sources: dict):
    data = path.read_bytes()
    sources[str(path.relative_to(ROOT))] = hashlib.sha256(data).hexdigest()
    return json.loads(data)


def statistics_for(rows: list[dict]) -> dict:
    means = {k: statistics.mean(float(row[k]) for row in rows) for k in ("cpuMs", *STAGES)}
    # 残差只表示未单列包络，不把其中的证书合并、收集、销毁等猜分给某个函数。
    means["unitemizedMs"] = means["cpuMs"] - sum(means[k] for k in STAGES)
    if means["unitemizedMs"] < -0.01:
        raise ValueError("阶段计时出现不可解释的重叠")
    times = sorted(float(row["cpuMs"]) for row in rows)
    return {"frames": len(rows), "means": means, "medianCpuMs": statistics.median(times),
            "p95CpuMs": times[math.ceil(.95 * len(times)) - 1],
            "counts": {k: sum(int(row[k]) for row in rows) for k in COUNTS}}


def work_for(rows: list[dict], selected: set[int]) -> dict:
    result = {"seconds": {}, "count": {}, "maximum": {}}
    for row in rows:
        if int(row["frame"]) not in selected:
            continue
        kind, key = row["type"], row["key"]
        value = float(row["value"]) if kind == "seconds" else int(row["value"])
        old = result[kind].get(key, 0)
        result[kind][key] = max(old, value) if kind == "maximum" else old + value
    return result


def analyze() -> dict:
    sources = {}
    summary = {"contract": "正常平台墙钟与诊断任务子计时分开；暖机会排除0/1/2；单进程历史快验",
               "cases": {}, "sources": sources}
    export_rows = []
    for scene in SCENES:
        case = scene + "-b50000-transactional-t8"
        arms, configs = {}, {}
        for arm in ("A", "B"):
            folder = RAW / (case + "-" + arm)
            configs[arm] = read_json(folder / "inputs/resolved.json", sources)
            arms[arm] = read_rows(folder / "run/frames.csv", sources)
            if [int(r["frame"]) for r in arms[arm]] != list(range(configs[arm]["maxFrames"])):
                raise ValueError("正常轨迹不完整")
            if any((int(r["frame"]) < 3) != bool(int(r["warmup"])) for r in arms[arm]):
                raise ValueError("原暖机标记与04F协议不同")
        # 路径随冻结目录变化；只有政策字段可以改变任务语义。
        excluded = {"heightMap", "cameraFile", "materialFile", "qualityPolicy",
                    "qualityTargetPixels", "qualityHeightRatio"}
        common = lambda c: {k: v for k, v in c.items() if k not in excluded}
        if common(configs["A"]) != common(configs["B"]):
            raise ValueError("A/B共同输入不同")
        seed_a = read_json(RAW / (case + "-A-seed-trace") / "seed-identity.json", sources)
        seed_b = read_json(RAW / (case + "-B-trace") / "seed-identity.json", sources)
        if seed_a != seed_b:
            raise ValueError("初始种子不同")
        trace = read_rows(RAW / (case + "-B-trace") / "frames.csv", sources)
        work = read_rows(RAW / (case + "-B-trace") / "pointwise-work.csv", sources)
        if len(trace) != len(arms["B"]):
            raise ValueError("诊断轨迹不完整")
        for normal, observed in zip(arms["B"], trace):
            if any(normal[k] != observed[k] for k in MATCH):
                raise ValueError(f"{scene}/{normal['frame']} 正常与诊断离散结果不同")
        warm = {int(r["frame"]) for r in arms["B"] if not int(r["warmup"])}
        case_result = {"seed": seed_a, "normalTraceMatchingFrames": len(trace), "arms": {}}
        for arm, rows in arms.items():
            selected = [row for row in rows if int(row["frame"]) in warm]
            case_result["arms"][arm] = statistics_for(selected)
            case_result["arms"][arm]["events"] = {
                event: statistics_for([r for r in selected if r["event"] == event])
                for event in sorted({r["event"] for r in selected})}
        a = case_result["arms"]["A"]["means"]
        b = case_result["arms"]["B"]["means"]
        delta = {k: b[k] - a[k] for k in a}
        if not math.isclose(delta["cpuMs"], sum(delta[k] for k in (*STAGES, "unitemizedMs")), abs_tol=1e-8):
            raise ValueError("阶段净增量不闭合")
        case_result["deltaMs"] = delta
        case_result["cpuRatio"] = b["cpuMs"] / a["cpuMs"]
        case_result["diagnosticWarm"] = work_for(work, warm)
        case_result["diagnosticAll"] = work_for(work, set(range(len(trace))))

        # 末端连续空批、同视图、同输出只定位重复工作；不假装已有通用缓存失效证明。
        suffix = []
        for normal, observed in reversed(list(zip(arms["B"], trace))):
            if int(normal["frame"]) not in warm or any(int(observed[k]) for k in ("exchanges", "free", "flips")):
                break
            if suffix and any(normal[k] != suffix[-1][k] for k in ("poseHash", "projectionHash", "hash")):
                break
            suffix.append(normal)
        suffix.reverse()
        if suffix:
            ids = {int(row["frame"]) for row in suffix}
            case_result["staticEmptySuffix"] = {
                "first": min(ids), "last": max(ids), "normal": statistics_for(suffix),
                "cpuSumMs": sum(float(r["cpuMs"]) for r in suffix),
                "viewSumMs": sum(float(r["viewMs"]) for r in suffix),
                "diagnostic": work_for(work, ids)}
        case_result["lastFrameWork"] = work_for(work, {len(trace) - 1})
        for normal in arms["B"]:
            frame = int(normal["frame"])
            diag = work_for(work, {frame})
            export_rows.append({"scene": scene, **{k: normal[k] for k in
                ("frame", "event", "warmup", "cpuMs", "receiverMs", "donorMs", "viewMs", "exchanges", "free", "examined")},
                **{k: diag["seconds"].get(k, 0) for k in
                   ("receiver_stage", "task_wall_sum_receiver_certification", "task_wall_sum_pointwise_quality",
                    "task_wall_sum_proposal", "pointwise_batch")},
                **{k: diag["count"].get(k, 0) for k in
                   ("fit_bound_failed", "shape_infeasible", "quality_samples", "quality_exact_samples")}})
        summary["cases"][scene] = case_result
    OUT.mkdir(parents=True, exist_ok=True)
    with (OUT / "frames.csv").open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(export_rows[0]))
        writer.writeheader()
        writer.writerows(export_rows)
    return summary


def plot(summary: dict):
    from experiment_infrastructure.asset_preview import configure_font, plt
    configure_font()
    figure, axes = plt.subplots(2, 2, figsize=(13, 8), layout="constrained")
    ax = axes[0, 0]
    labels, totals = [], []
    layers = [("receiverMs", "接收"), ("donorMs", "回收"), ("viewMs", "视图"),
              ("reservationMs", "预留"), ("sampleRepairMs", "样本修复")]
    values = []
    for scene, result in summary["cases"].items():
        for arm in ("A", "B"):
            labels.append(scene.replace("dem-", "") + " " + arm)
            values.append(result["arms"][arm]["means"])
            totals.append(values[-1]["cpuMs"])
    bottom = [0.] * len(values)
    for key, label in layers:
        heights = [v[key] for v in values]
        ax.bar(range(len(values)), heights, bottom=bottom, label=label)
        bottom = [a + b for a, b in zip(bottom, heights)]
    ax.bar(range(len(values)), [t - b for t, b in zip(totals, bottom)], bottom=bottom, label="其余包络")
    for i, total in enumerate(totals):
        ax.text(i, total + 1, f"{total:.1f}", ha="center", fontsize=9)
    ax.set_xticks(range(len(labels)), labels, rotation=20)
    ax.set(title="正常平台暖机会：完整成本组成", ylabel="毫秒 / 机会", ylim=(0, 120))
    ax.legend(fontsize=8, ncol=3)
    ax = axes[0, 1]
    for i, scene in enumerate(SCENES):
        d = summary["cases"][scene]["deltaMs"]
        increase = d["receiverMs"] + d["donorMs"]
        ax.bar(i - .2, increase, .2, label="接收＋回收增量" if i == 0 else None, color="#C44E52")
        ax.bar(i, d["cpuMs"] - increase, .2, label="其余阶段抵消" if i == 0 else None, color="#55A868")
        ax.bar(i + .2, d["cpuMs"], .2, label="完整净增量" if i == 0 else None, color="#4C72B0")
    ax.axhline(0, color="#777777", linewidth=.8)
    ax.set_xticks(range(3), [s.replace("dem-", "") for s in SCENES])
    ax.set(title="A→B净变化：新增成本被下游减少部分抵消", ylabel="毫秒 / 机会")
    ax.legend(fontsize=8)
    for ax, scene in zip(axes[1], SCENES[1:]):
        for arm, style in (("A", "--"), ("B", "-")):
            path = RAW / (scene + "-b50000-transactional-t8-" + arm) / "run/frames.csv"
            rows = list(csv.DictReader(path.open(encoding="utf-8")))
            rows = [r for r in rows if not int(r["warmup"])]
            ax.plot([int(r["frame"]) for r in rows], [float(r["cpuMs"]) for r in rows], style, label=arm)
        suffix = summary["cases"][scene]["staticEmptySuffix"]
        ax.axvspan(suffix["first"], suffix["last"], color="#999999", alpha=.15, label="B末端静止空批")
        ax.set(title=scene.replace("dem-", "") + "：逐机会正常平台CPU", xlabel="机会编号", ylabel="毫秒")
        ax.legend(fontsize=9)
    figure.suptitle("QPC-04F质量成本追溯：A=error-first；B=pointwise-target（单进程历史快验）")
    figure.savefig(OUT / "quality-cost.png", dpi=145)
    plt.close(figure)


def reduction(raw: Path, output: Path, arms: list[str]) -> dict:
    """显式输入的同政策版本对照，原04F默认归约和产物保持不变。"""
    sources = {}
    result = {"contract": "同政策单独进程快验；内部三次认证仅作内核定位", "cases": {}, "sources": sources}
    for scene in SCENES:
        case = scene + "-b50000-transactional-t8"
        records = {}
        baseline = None
        for arm in arms:
            frames = read_rows(raw / arm / case / "run/frames.csv", sources)
            if baseline is None:
                baseline = frames
            elif len(frames) != len(baseline) or any(
                any(a[k] != b[k] for k in MATCH) for a, b in zip(baseline, frames)):
                raise ValueError("等价优化改变离散结果")
            warm = [r for r in frames if not int(r["warmup"])]
            records[arm] = {"warm": statistics_for(warm), "events": {
                event: statistics_for([r for r in warm if r["event"] == event])
                for event in sorted({r["event"] for r in warm})}}
        result["cases"][scene] = records
    micro = {}
    comparison = {}
    for arm in arms:
        items = {}
        for file in sorted((raw / arm).glob("*-audit/proposal-*.json")):
            item = read_json(file, sources)
            key = file.parent.name + "/" + file.name
            logical = {k: item[k] for k in ("binding", "constructionReason", "oldReason", "pointwiseReason",
                                          "targetMicropixels", "fitHeight", "interval")}
            logical["witnesses"] = [{k: w[k] for k in ("height", "pointwiseReason", "legacyAccepts", "touches")}
                                    for w in item["verifiedWitnesses"]]
            if key in comparison and comparison[key] != logical:
                raise ValueError("同提案认证或见证结果不同：" + key)
            comparison[key] = logical
            for w in item["verifiedWitnesses"]:
                items[key] = {"medianMs": 1000*statistics.median(w["certifySeconds"]),
                              "repeatsSeconds": w["certifySeconds"]}
        micro[arm] = items
    result["sameProposalRecords"] = len(comparison)
    result["micro"] = micro
    result["programs"] = {arm: read_json(raw / arm / "program.json", sources) for arm in arms}
    # 物理工作从诊断账本归约；原生完整时间只来自上面的普通平台CSV
    result["diagnosticWork"] = {}
    for arm in arms:
        result["diagnosticWork"][arm] = {}
        for file in sorted((raw / arm).glob("*-audit/pointwise-work.csv")):
            data = read_rows(file, sources)
            result["diagnosticWork"][arm][file.parent.name] = work_for(data, {int(r["frame"]) for r in data})
    result["supplement"] = {}
    for label in ("L0", "P0-repeat", "P1-repeat"):
        records = {}
        for file in sorted((raw / label).glob("*/run/frames.csv")):
            data = read_rows(file, sources)
            warm = [r for r in data if not int(r["warmup"])]
            records[file.parents[1].name] = {"warm": statistics_for(warm), "events": {
                e: statistics_for([r for r in warm if r["event"] == e]) for e in sorted({r["event"] for r in warm})}}
        if records:
            result["supplement"][label] = records
    output.mkdir(parents=True, exist_ok=True)
    (output / "summary.json").write_text(json.dumps(result, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plot", action="store_true", help="只对已归约结果制图")
    parser.add_argument("--reduction-input", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--arms", nargs="+", default=["P0", "A1", "P1"])
    args = parser.parse_args()
    started = time.perf_counter()
    if args.reduction_input:
        if args.output is None:
            parser.error("版本对照必须指定独立输出目录")
        result = reduction(args.reduction_input.resolve(), args.output.resolve(), args.arms)
        print(json.dumps({s: {a: v['warm']['means']['cpuMs'] for a,v in r.items()}
                          for s,r in result['cases'].items()}))
    elif args.plot:
        plot(json.loads((OUT / "summary.json").read_text(encoding="utf-8")))
    else:
        result = analyze()
        result["analysisSeconds"] = time.perf_counter() - started
        result["scriptSha256"] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
        (OUT / "summary.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print(json.dumps({s: r["cpuRatio"] for s, r in result["cases"].items()}))
    print(f"处理时间：{time.perf_counter() - started:.3f}s")
