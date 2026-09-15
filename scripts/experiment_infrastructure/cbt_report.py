"""读取冻结的CBT比较证据；质量按实际N解释，GPU时间按采样代去重。"""
from pathlib import Path
import csv
import statistics

import numpy as np
import matplotlib.pyplot as plt

from .catalog import load_json, content_hash
from .result_adapters import load_run
from .runner import save
from .figures import FigureWriter
from .gpu_observations import gpu_samples
from .quality import pointwise_excess


COLORS = {"cbt": "#6b4da2", "dod": "#256eb0", "transactional": "#ce7130"}
LABELS = {"cbt": "GPU CBT", "dod": "DOD8", "transactional": "Transactional8"}


def mean(values):
    values = [value for value in values if value is not None]
    return statistics.mean(values) if values else None


def nearest_count_reference(left, candidates):
    """先按数量选参考，再检查质量，避免用有效性挑选更有利的比较点。"""
    return min(candidates, key=lambda row: (abs(row["N"] - left["N"]), row["parameter"]))


def collect(root):
    protocol = load_json(root / "protocol.json")
    observations = []
    timings = []
    for name in protocol["cases"]:
        case = load_json(root / "cases" / (name + ".json"))
        visual = load_run(root / "runs" / name / "visual")
        timing = load_run(root / "runs" / name / "timing")
        warm = [row for row in timing["frames"] if not row["warmup"]]
        times = {"case": name, "terrain": case["terrain"], "algorithm": case["algorithm"],
            "cpuMs": mean([r["cpuMs"] for r in warm]), "frameMs": mean([r["frameMs"] for r in warm]),
            "uploadMs": mean([r["uploadMs"] for r in warm]), "opportunities": len(warm),
            "computeMs": None, "drawMs": None, "computeSamples": 0, "drawSamples": 0}
        if case["algorithm"] == "cbt":
            for generation, column, label in (("timingGeneration", "computeMs", "compute"),
                ("drawGeneration", "drawMs", "draw")):
                values = gpu_samples(timing["cbt"], generation, column, protocol["warmup"])
                times[column] = mean(values)
                times[label + "Samples"] = len(values)
        timings.append(times)
        directory = root / "quality" / name
        index = load_json(directory / "quality-index.json")
        if index["runManifestSha256"] != content_hash(root / "runs" / name / "visual/manifest.json"):
            raise ValueError("质量索引与运行清单不同")
        for item in index["frames"]:
            if not all(item.get(field) for field in ("quality", "errors", "locations")):
                raise ValueError("质量证据文件缺失，无法审计")
            for field in ("quality", "errors", "locations"):
                if content_hash(directory / item[field]) != item[field + "Sha256"]:
                    raise ValueError("质量证据内容改变")
            quality = load_json(directory / item["quality"])
            valid = item["status"] == "ok" and not any(quality[key] for key in
                ("missing", "ambiguous", "invalidGeometry", "invalidProjection"))
            frame = int(item["frame"])
            record = visual["frames"][frame]
            observations.append({"case": name, "terrain": case["terrain"], "algorithm": case["algorithm"],
                "parameter": case.get("cbtArea", case["budget"]), "frame": frame, "event": record["event"],
                "N": int(record["faces"]), "valid": valid, "ambiguous": quality["ambiguous"],
                "missing": quality["missing"], "Emax": quality["screenMax"] if valid else None,
                "Hmax": quality["heightMax"] if valid else None,
                "RMS": quality["terrainSampleRms"] if valid else None, "witnessU": quality["screenWitness"][1],
                "witnessV": quality["screenWitness"][2], "sourceHash": index["sourceSha256"],
                "sampleHash": item["sampleHash"], "sampleCount": item["sampleCount"],
                "poseHash": item["poseHash"], "projectionHash": item["projectionHash"],
                "meshHash": item["meshHash"], "errors": str(directory / item["errors"]),
                "locations": str(directory / item["locations"]),
                "image": str(root / "runs" / name / "visual/run" / Path(record["image"]).with_suffix(".png"))})
    pairs = []
    for left in observations:
        if left["algorithm"] != "transactional":
            continue
        compatible = [r for r in observations if r["terrain"] == left["terrain"] and r["frame"] == left["frame"]]
        cbt = nearest_count_reference(left, (r for r in compatible if r["algorithm"] == "cbt"))
        dod = next(r for r in compatible if r["algorithm"] == "dod" and r["parameter"] == left["parameter"])
        for right in (cbt, dod):
            value = None
            if left["valid"] and right["valid"]:
                value = pointwise_excess(left, right, np.fromfile(left["errors"], dtype="<f8"),
                    np.fromfile(right["errors"], dtype="<f8"))
            ratio = left["N"] / right["N"]
            pairs.append({"terrain": left["terrain"], "frame": left["frame"], "left": left["case"],
                "right": right["case"], "leftN": left["N"], "rightN": right["N"], "NRatio": ratio,
                "nearCount": abs(ratio - 1) <= protocol["nearCountRelativeTolerance"],
                "status": "paired" if value else "invalid-quality",
                "Dmax": value["Dmax"] if value else None,
                "witnessOrdinal": value["witnessOrdinal"] if value else None})
    return {"protocol": protocol, "observations": observations, "timings": timings, "pairs": pairs}


def write_table(path, rows):
    with path.open("x", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def make_figures(output, data):
    writer = FigureWriter(output / "figures", output / "analysis.json")
    for asset in data["protocol"]["assets"]:
        name = asset["id"]
        points = [row for row in data["observations"] if row["terrain"] == name]
        fig, axes = plt.subplots(2, 2, figsize=(11, 8))
        for axis, frame in zip(axes.flat, asset["qualityFrames"]):
            for algorithm in COLORS:
                values = sorted((r for r in points if r["frame"] == frame and r["algorithm"] == algorithm and r["valid"]), key=lambda r: r["N"])
                axis.plot([r["N"] for r in values], [r["Emax"] for r in values], "o-",
                    label=LABELS[algorithm], color=COLORS[algorithm])
            axis.set(xscale="log", yscale="log", xlabel="实际三角形数量 N", ylabel="采样最大屏幕误差 / px",
                title=f"机会 {frame}（同一视图）")
            axis.legend(fontsize=8)
        writer.emit(fig, name + "-quality-n", name + "：质量与实际数量",
            {"asset": name, "frames": asset["qualityFrames"]},
            "同一机会内比较；无效质量点不绘制（见失败表），不插值补齐，容量不是硬预算。")
        plt.close(fig)

        fig, axes = plt.subplots(2, 1, figsize=(11, 7))
        for case in sorted(set(r["case"] for r in points)):
            values = sorted((r for r in points if r["case"] == case), key=lambda r: r["frame"])
            style = "--" if values[0]["algorithm"] == "cbt" else "-"
            label = case.removeprefix(name + "-")
            axes[0].plot([r["frame"] for r in values], [r["Emax"] for r in values], style + "o", label=label)
            axes[1].plot([r["frame"] for r in values], [r["N"] for r in values], style + "o", label=label)
        axes[0].set(yscale="log", ylabel="采样最大屏幕误差 / px")
        axes[1].set(yscale="log", ylabel="实际三角形数量 N", xlabel="更新机会")
        axes[0].legend(ncol=3, fontsize=8)
        writer.emit(fig, name + "-trajectory", name + "：冷启动与持续质量",
            {"asset": name, "frames": asset["qualityFrames"]}, "仅显示预冻结关键机会；早期低N是来源初始化，不增加隐藏收敛轮次。")
        plt.close(fig)

        last = asset["qualityFrames"][-1]
        representatives = [next(r for r in points if r["frame"] == last and r["algorithm"] == algorithm and
            r["parameter"] == (8 if algorithm == "cbt" else 50000)) for algorithm in ("dod", "transactional", "cbt")]
        fig, axes = plt.subplots(1, 3, figsize=(15, 4.6))
        for axis, row in zip(axes, representatives):
            axis.imshow(plt.imread(row["image"]))
            quality_text = f"Emax={row['Emax']:.3f}px" if row["valid"] else "质量不完整"
            axis.set_title(f"{LABELS[row['algorithm']]}\nN={row['N']}, {quality_text}", fontsize=10)
            axis.axis("off")
        writer.emit(fig, name + "-actual-views", name + f"：机会 {last} 实际画面",
            {"cases": [r["case"] for r in representatives], "frame": last},
            "使用完整原截图；CBT展示预定中间面积8，数量不一致，不能凭画面宣称同预算胜出。")
        plt.close(fig)

        fig, axes = plt.subplots(1, 3, figsize=(12, 4.8))
        vmax = max(r["Emax"] for r in representatives if r["valid"])
        for axis, row in zip(axes, representatives):
            if not row["valid"]:
                axis.text(.5, .5, f"质量不完整\n歧义样本={row['ambiguous']}\n保留原始证据，不画有效误差图",
                    ha="center", va="center", transform=axis.transAxes)
                axis.set_title(LABELS[row["algorithm"]])
                axis.axis("off")
                continue
            with open(row["locations"]) as stream:
                locations = [r for r in csv.DictReader(stream) if int(r["visible"])]
            image = axis.scatter([float(r["u"]) for r in locations], [float(r["v"]) for r in locations],
                c=[float(r["screenError"]) for r in locations], s=2, vmin=0, vmax=vmax, cmap="magma")
            axis.scatter([row["witnessU"]], [row["witnessV"]], marker="x", s=50, color="cyan")
            axis.set(xlabel="u", ylabel="v", title=LABELS[row["algorithm"]], aspect="equal", xlim=(0, 1), ylim=(0, 1))
            fig.colorbar(image, ax=axis, label="屏幕误差 / px", fraction=.045)
        writer.emit(fig, name + "-witness", name + f"：机会 {last} 几何误差见证",
            {"cases": [r["case"] for r in representatives], "frame": last},
            "统一色标；显示抽样位置和实际全Q最大点（青色叉），不是完整误差场或感知结论。")
        plt.close(fig)
    for item in writer.items:
        # 此报告增加GPU参考；沿用字体/导出器，但显示规则必须反映实际图例
        item["colors"] = COLORS
        item["trajectoryColors"] = "Matplotlib per-case cycle; legend records each frozen parameter"
        save(output / "figures" / (item["id"] + ".spec.json"), item)
    save(output / "figure-index.json", writer.items)


def format_number(value):
    return "—" if value is None else f"{value:.4f}"


def write_report(root, output, data):
    valid_count = sum(row["valid"] for row in data["observations"])
    invalid_count = len(data["observations"]) - valid_count
    lines = ["# CBT 接入后的首轮有限比较", "", "本报告来自预冻结的两个资产和14个配置，每配置一次独立timing/visual进程；帧不是独立统计重复。",
        "", f"28个运行完成；56个关键帧中{valid_count}个质量评价有效，{invalid_count}个CBT canyon点因覆盖歧义无效。有限比较已收口，不能解释为全部质量门禁通过。",
        "", "平台接入与同代GPU质量证据已建立。下表描述实际N与采样几何误差，不把槽池容量当预算，不将不同N自动判成公平性能胜出。",
        "", "## 协议与来源", "", f"原始证据：{root}。协议及程序SHA见 protocol.json；CSV和图可由保存的原始结果重建。",
        "", "CPU固定使用CBI-02程序，CBT使用CBI-03程序；CBI-03的CPU工程回归仍OPEN。当前性能列是不同执行/二进制的描述，不能用于论文speedup主张。",
        "", "CBT面积16/8/4，动态512K，off/modified；CPU预算50k/200k，DOD8与固定旧点+flip恢复的Transactional8。D3D12 1280×720，路线/尺度沿原FER。",
        "", "## 末机会质量与数量", "", "| 资产 | 配置 | 实际 N | Emax / px | RMS / px | Hmax | 最坏 UV / 状态 |", "|---|---|---:|---:|---:|---:|---|"]
    for asset in data["protocol"]["assets"]:
        for row in data["observations"]:
            if row["terrain"] == asset["id"] and row["frame"] == asset["qualityFrames"][-1]:
                witness = f"({row['witnessU']:.6f}, {row['witnessV']:.6f})" if row["valid"] else "质量不完整"
                lines.append(f"| {row['terrain']} | {row['case']} | {row['N']} | {format_number(row['Emax'])} | {format_number(row['RMS'])} | {format_number(row['Hmax'])} | {witness} |")
    lines += ["", "## 无效质量证据", "", "以下点保留原始输出，质量指标留空，不进入Dmax或质量优势判断。没有放宽评价器容差，也没有调参数替换失败点。",
        "", "| 配置 | 机会 | 实际N | 歧义样本 | 缺失样本 |", "|---|---:|---:|---:|---:|"]
    for row in data["observations"]:
        if not row["valid"]:
            lines.append(f"| {row['case']} | {row['frame']} | {row['N']} | {row['ambiguous']} | {row['missing']} |")
    lines += ["", "有限定位见 vertex-coherence-audit.json：对canyon末机会三个CBT网格，按实际世界XZ归并后，未发现同位置不同高度、同位置不同UV、边重数大于2或未配对内部边。它排除了这几种直接输出破损现象，不能证明完整共形性或评价器有误。歧义来自几何、参数域还是数值判定仍为UNCERTAIN；本轮不改shader和容差。",
        "", "最近数量参考若无效，Dmax仍留空；不会改选另一个有效但更远的CBT点。",
        "", "完整四关键机会数值见 observations.csv。最大误差是固定Q上的sampled maximum；RMS是可见参数域样本等权，不是屏幕面积加权。",
        "", "## 逐点退化与数量匹配", "", "Dmax定义为同一可见参考域上逐点误差差的最大值，正值说明至少一个点Transactional更差；负值才说明所有可见采样点均更好。它不替代绝对Emax。",
        "", "| 资产/机会 | Transactional配置 | 参考配置 | N比值 | 近数量≤10% | Dmax / px |", "|---|---|---|---:|---|---:|"]
    for row in data["pairs"]:
        lines.append(f"| {row['terrain']}/{row['frame']} | {row['left']} | {row['right']} | {row['NRatio']:.3f} | {'是' if row['nearCount'] else '否'} | {format_number(row.get('Dmax'))} |")
    lines += ["", "CBT参考仅按同机会实际N最近选择，与误差结果无关；数量不近时保留不匹配，不插值或追加阈值。DOD参考固定同CPU预算。",
        "", "## 分边界时间", "", "| 配置 | CPU update ms | 帧包络 ms | CPU上传 ms | GPU计算 ms | GPU绘制 ms | GPU计算/绘制样本 |", "|---|---:|---:|---:|---:|---:|---:|"]
    for row in data["timings"]:
        lines.append(f"| {row['case']} | {format_number(row['cpuMs'])} | {format_number(row['frameMs'])} | {format_number(row['uploadMs'])} | {format_number(row['computeMs'])} | {format_number(row['drawMs'])} | {row['computeSamples']}/{row['drawSamples']} |")
    lines += ["", "去3预热；GPU样本按资源/采样代去重并映射其实际机会，尾部缺失不补轮次。计算与绘制不跨代相加。CBT CPU update只包含主机命令记录，不能除以CPU算法时间称并行加速。捕获/质量成本不计入timing。",
        "", "## 结果解释", "",
        "### Peking：返回视图存在近数量优势，转向帧存在反例", "",
        "机会23，Transactional为49,999面、Emax=0.441886px；CBT面积16为45,844面、Emax=0.927970px。前者多约9.06%的三角形、最大误差低约52.38%，属于预声明的近数量描述点。它不是精确同N证明，也不是逐点支配：对应Dmax仍为+0.328234px。",
        "", "机会15不能省略：Transactional两预算点的Emax均为1.996557px，CBT三个面积点为1.069818px，DOD约0.42/0.19px。当前Transactional没有跨视图统一质量优势；返回后误差下降也不能仅凭全局Emax解释成困难区域已经恢复。",
        "", "200k末机会的0.209150px低于CBT面积4的0.407000px，但双方实际N为200,000和140,913，相差41.93%，不足以判断同数量优势。Transactional与DOD末帧Emax相同仍不代表逐点质量相同：50k/200k的Dmax分别为+0.101125/+0.074878px。也不能把相同Emax完全归因于新事务算法，初始DOD种子是持续状态的一部分。",
        "", "### Canyon：有效比较覆盖不足，保留质量失败与实际预算利用率", "",
        "面积16末机会34,973面、9.069726px；CPU 50k点为50,000面、2.348193px。数量相差约42.97%，不能据此宣布同预算优于CBT。面积8/4的末机会质量无效，尤其最接近Transactional 200k配置的面积8点不能参与胜负判断。",
        "", "CPU的200k是预算上限：末机会DOD实际75,764面，Transactional实际61,278面，利用率约37.88%和30.64%。两者Emax为1.856884和1.941665px，不能把差异全部解释为排序策略；数量利用不同必须一并报告。机会32，Transactional约9px而有效CBT点约6～8px、DOD约1.8px，同样不支持持续质量普遍占优。",
        "", "### 时间：GPU参考已有低计算成本，CPU完整收益仍未成立", "",
        "有效GPU计算样本均值约0.074～0.096ms，绘制另计；这建立了参考的实际设备成本。CPU主机记录、GPU完成时间和帧包络不同，不能直接互除获得同任务加速比。",
        "", "本轮CPU描述也不是统一正结果：Peking 50k的Transactional/DOD约34.85/42.92ms，200k则约706.42/196.86ms。后者表明现有大配置仍很昂贵，本阶段没有新增归因探针，不能从总时间推定全部由reservation造成。CBI-03 CPU工程回归独立保持OPEN；不同程序、单次进程及不同质量/实际N共同限制性能结论。",
        "", "## 图与视觉证据", ""]
    for asset in data["protocol"]["assets"]:
        for suffix in ("quality-n", "trajectory", "actual-views", "witness"):
            name = asset["id"] + "-" + suffix
            lines += [f"![{name}](figures/{name}.png)", ""]
    lines += ["## 状态与适用范围", "", "- 平台持续接入：通过有限设备/生命周期检查。",
        "- 同代实际GPU质量证据：通过，未用CPU重建替代。",
        "- 有限面板执行与报告：完成；51/56质量点有效，5点明确无效。",
        "- Canyon部分CBT采样覆盖：FAIL，原因待查，不扩张为全部CBT输出失效。",
        "- CPU工程性能：CBI-03回归待查，不能宣布无回归。",
        "- 跨算法同质量性能与论文竞争性：未由本轮证明。",
        "", "CBT与CPU的初始化、目标函数和预算语义不同；早期收敛/末帧质量必须共同阅读。单轮结果不支持总体显著性、人眼可接受阈值或连续曲面误差保证。"]
    (output / "report.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def report(root):
    root = Path(root).resolve()
    output = root / "analysis"
    output.mkdir(exist_ok=False)
    data = collect(root)
    save(output / "analysis.json", data)
    for key in ("observations", "timings", "pairs"):
        write_table(output / (key + ".csv"), data[key])
    make_figures(output, data)
    write_report(root, output, data)
    return output / "report.md"
