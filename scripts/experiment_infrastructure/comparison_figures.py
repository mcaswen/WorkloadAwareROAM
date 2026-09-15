"""按资产和设备边界绘制统一实验图，不从画图结果重新选择实验。"""
import numpy as np
import matplotlib.pyplot as plt
from .figures import COLORS, LABELS, STAGE_LABELS
from .analysis import STAGES, PASS


def label(config):
    case = config["case"]
    if case["algorithm"] == "cbt":
        return f"CBT a={case['cbtArea']:g}"
    return f"{LABELS[case['algorithm']]} {case['budget']//1000}k / {case['workers']}t"


def mean(config, key, group="warm"):
    return config["groups"][group][key]["mean"]


def cost_chart(writer, terrain, configurations):
    cpu = [config for config in configurations if config["device"] == "cpu"]
    gpu = [config for config in configurations if config["device"] == "gpu"]
    columns = 2 if cpu and gpu else 1
    height = max(4.6, .4 * max(len(cpu), len(gpu)) + 1.5)
    fig, axes = plt.subplots(1, columns, figsize=(12 if columns == 2 else 9, height), squeeze=False)
    groups = [(cpu, "cpuMs", "完整CPU更新 / ms")] if cpu else []
    if gpu:
        groups.append((gpu, "gpuComputeMs", "GPU计算阶段和 / ms"))
    for axis, (items, metric, title) in zip(axes[0], groups):
        for y, config in enumerate(items):
            values = config["groups"]["warm"][metric]
            if values["mean"] is None:
                continue
            axis.barh(y, values["mean"], color=COLORS[config["case"]["algorithm"]])
            axis.errorbar(values["mean"], y,
                xerr=[[values["mean"] - values["minimum"]], [values["maximum"] - values["mean"]]],
                fmt="none", color="black", capsize=3)
        axis.set(yticks=range(len(items)), yticklabels=[label(config) for config in items], xlabel=title)
        axis.tick_params(axis="y", labelsize=8)
    fig.subplots_adjust(left=.22)
    writer.emit(fig, terrain + "-runtime", terrain + "：独立进程正常成本",
        {"configurations": [config["key"] for config in configurations], "group": "warm"},
        "CPU与GPU独立坐标/计时边界；误差线为进程均值范围，非CI，不表示同质量加速。")


def stage_page(writer, terrain, configurations, by_id, page):
    items = [config for config in configurations if config["processCount"]]
    columns = min(3, len(items))
    rows = (len(items) + columns - 1) // columns
    fig, axes = plt.subplots(rows, columns, figsize=(6 * columns, 5 * rows), squeeze=False)
    for axis in axes.flat:
        axis.axis("off")
    for axis, config in zip(axes.flat, items):
        axis.axis("on")
        if config["device"] == "gpu":
            run = by_id[config["representativeRun"]]
            group = run["gpu"]["groups"]["warm"]["compute"]
            names = run["gpu"]["stageNames"]
            pairs = [(name, group.get(f"stage{i}Ms", {}).get("mean")) for i, name in enumerate(names)]
        else:
            keys = STAGES if config["case"]["algorithm"] == "transactional" else PASS
            pairs = [(STAGE_LABELS[key], mean(config, key)) for key in (*keys, "unattributedCpuMs")]
        pairs = [(name, value) for name, value in pairs if value is not None and value > 0]
        axis.barh([name for name, value in pairs], [value for name, value in pairs],
                  color=COLORS[config["case"]["algorithm"]])
        axis.set(title=label(config), xlabel="ms / 暖来源机会")
        axis.tick_params(axis="y", labelsize=8)
    fig.subplots_adjust(left=.2)
    writer.emit(fig, terrain + f"-stages-{page}", terrain + f"：阶段成本边界（{page}）",
        {"configurations": [config["key"] for config in items]},
        "GPU阶段显示固定第一正常进程，绘制另列；CPU阶段为进程均值。两种设备计时不能堆叠。")


def quality_charts(writer, terrain, configurations):
    items = [config for config in configurations if config["quality"]]
    if not items:
        return
    frames = sorted({row["frame"] for config in items for row in config["quality"]})
    fig, axes = plt.subplots((len(frames) + 1) // 2, 2, figsize=(12, 8), squeeze=False)
    for axis, frame in zip(axes.flat, frames):
        plotted = []
        for algorithm in COLORS:
            values = sorted([row for config in items if config["case"]["algorithm"] == algorithm
                             for row in config["quality"] if row["frame"] == frame and row["valid"]], key=lambda row: row["N"])
            if not values:
                continue
            plotted.extend(row["Emax"] for row in values)
            axis.plot([row["N"] for row in values], [row["Emax"] for row in values], "o-",
                      label=LABELS[algorithm], color=COLORS[algorithm])
        axis.set(xlabel="实际三角形数量 N", ylabel="采样最大屏幕误差 / px", title=f"机会 {frame}", xscale="log")
        if not plotted:
            axis.text(.5, .5, "无有效质量点；见失败表", transform=axis.transAxes, ha="center")
        elif min(plotted) > 0 and max(plotted) / min(plotted) > 10:
            axis.set_yscale("symlog", linthresh=.001)
        else:
            axis.set_ylim(0, max(max(plotted) * 1.2, .001))
        if plotted:
            axis.legend(fontsize=8)
    writer.emit(fig, terrain + "-quality-n", terrain + "：同视图质量与实际数量",
        {"frames": frames}, "仅有效质量点；连线不是插值或Pareto证明。容量不是预算，数量不匹配仍保留。")

    fig, axes = plt.subplots(2, 1, figsize=(12, 8))
    for config in items:
        values = sorted(config["quality"], key=lambda row: row["frame"])
        style = "--o" if config["device"] == "gpu" else "-o"
        axes[0].plot([row["frame"] for row in values], [row["Emax"] for row in values], style, label=label(config))
        axes[1].plot([row["frame"] for row in values], [row["N"] for row in values], style)
    axes[0].set(ylabel="采样最大屏幕误差 / px")
    axes[0].set_yscale("symlog", linthresh=.001)
    axes[1].set(xlabel="更新机会", ylabel="实际N", yscale="log")
    axes[0].legend(fontsize=8, ncol=3)
    writer.emit(fig, terrain + "-quality-trajectory", terrain + "：质量与数量轨迹",
        {"frames": frames}, "只评价预冻结关键机会；无效质量留空不连线。不同view的误差下降不自动代表几何恢复。")

    comparable = [config for config in items if mean(config, "frameMs") is not None and all(row["valid"] for row in config["quality"])]
    if comparable:
        fig, axis = plt.subplots(figsize=(11, 5))
        for config in comparable:
            observed = [row["Emax"] for row in config["quality"] if row["valid"]]
            if not observed:
                continue
            cost = mean(config, "frameMs")
            axis.scatter(cost, max(observed), color=COLORS[config["case"]["algorithm"]])
            axis.annotate(label(config), (cost, max(observed)), xytext=(4, 4), textcoords="offset points", fontsize=7)
        axis.set(xlabel="暖主机帧包络 / ms", ylabel="已有效观测关键帧的最大Emax / px", xscale="log", yscale="log")
        writer.emit(fig, terrain + "-quality-cost", terrain + "：配置级成本与观测质量",
            {"configurations": [config["key"] for config in comparable]},
            "GPU异步帧包络非完成时间；配置关联不假定跨进程mesh相同。质量覆盖不足见失败表，不据此排名。")


def timeline(writer, terrain, configurations, by_id):
    fig, axes = plt.subplots(2, 1, figsize=(12, 8))
    for config in configurations:
        if not config["representativeRun"]:
            continue
        run = by_id[config["representativeRun"]]
        frames = run["frames"]
        axes[0].plot([frame["frame"] for frame in frames], [frame["frameMs"] for frame in frames], label=label(config))
        if config["case"]["algorithm"] == "transactional":
            axes[1].plot([frame["frame"] for frame in frames], [frame["pairs"] for frame in frames], label=label(config))
    axes[0].set(ylabel="主机帧包络 / ms")
    axes[0].set_yscale("symlog", linthresh=.01)
    axes[1].set(xlabel="更新机会", ylabel="Transactional配对检查数")
    axes[0].legend(fontsize=7, ncol=3)
    axes[1].legend(fontsize=7)
    writer.emit(fig, terrain + "-timeline", terrain + "：固定第一进程的时序与工作量",
        {"runs": [config["representativeRun"] for config in configurations]},
        "包含冷启动；CPU/GPU实际设备不同，主机帧包络不是GPU完成延迟。完整重复和工作计数在数表。")


def build(writer, data, statistics):
    by_id = {run["id"]: run for run in data["runs"]}
    terrains = sorted({config["case"]["terrain"] for config in statistics["configurations"]})
    for terrain in terrains:
        configurations = [config for config in statistics["configurations"] if config["case"]["terrain"] == terrain]
        measured = [config for config in configurations if config["processCount"]]
        if measured:
            cost_chart(writer, terrain, measured)
            for offset in range(0, len(measured), 3):
                stage_page(writer, terrain, measured[offset:offset + 3], by_id, offset // 3 + 1)
            if data.get("study"):
                timeline(writer, terrain, measured, by_id)
        quality_charts(writer, terrain, configurations)
