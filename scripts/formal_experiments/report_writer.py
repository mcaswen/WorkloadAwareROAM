"""将同一份探索统计写为中文 Markdown 与 CSV，不在模板中重新计算胜负。"""

import csv
import json
from pathlib import Path


def display(value):
    if value is None:
        return "不可定义"
    if isinstance(value, float):
        return f"{value:.6g}"
    if isinstance(value, (dict, list)):
        return json.dumps(value, ensure_ascii=False, sort_keys=True)
    return str(value).replace("|", "\\|").replace("\n", " ")


def write_csv(path, rows, empty_columns):
    columns = list(rows[0]) if rows else list(empty_columns)
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=columns)
        writer.writeheader()
        for row in rows:
            writer.writerow({key: json.dumps(value, ensure_ascii=False, sort_keys=True)
                             if isinstance(value, (dict, list)) else value for key, value in row.items()})


def table(lines, headings, rows):
    lines.extend(["", "| " + " | ".join(headings) + " |", "| " + " | ".join("---" for _ in headings) + " |"])
    for row in rows:
        lines.append("| " + " | ".join(display(value) for value in row) + " |")


def write_report(output, analysis):
    output = Path(output)
    for filename, key in (("paired-targets.csv", "pairs"), ("workload-strata.csv", "strata"),
                          ("crossover-signals.csv", "signals"), ("run-consistency.csv", "consistency"),
                          ("short-run-stability.csv", "shortRunStability"), ("failed-attempts.csv", "failedAttempts"),
                          ("target-costs.csv", "targetCosts")):
        write_csv(output / filename, analysis[key], ("runId", "status"))
    lines = ["# CPU 阶段配对探索报告", "", "数据用途：`exploratory`。测量协议版本为 1，配对记录为 v2，描述性噪声规则版本为 1。",
             "", "每个策略时间来自同源副本的一次连续生产阶段调用。复制、输入检查、线程准备和结果验证在计时外；"
             "该协议不能直接代表冷缓存生产延迟。标定值只作噪声参照，没有从策略时间中扣除。",
             "", "先在完整块中计算 `d = tA - tB`，再取中位数。负值表示 A 更快，正值表示 B 更快。"
             "相对收益使用配对中位差除以 A 的中位时间；A 为零时不可定义。",
             "", "判断阈值为 `max(0.01 ms, 0.03 × 较快策略中位时间, 前后空计时/无工作 P99 最大值, 3 × 配对差 MAD)`。"
             "只有差值严格超过阈值才标记方向；MAD 不表示置信区间。相邻相机点、重复块和复制的尝试目录都不是新的独立运行。"]
    if analysis["failedAttempts"]:
        lines.extend(["", "## 失败与未完成尝试", "", "以下尝试仅作失败审计，其局部完整目标没有进入胜负统计。"])
        table(lines, ("目录", "原因"), [(row["directory"], row["failure"]) for row in analysis["failedAttempts"]])
    lines.extend(["", "## 运行与标定"])
    for run in analysis["runs"]:
        lines.extend(["", f"### 运行 `{run['runId']}`", "", f"后端：{run['backend']}；来源目录：`{run['directory']}`。"
                      f"有效完成目标：{run['targetCount']}；预热行：{run['summary']['warmupRowCount']}；"
                      f"正式行：{run['summary']['measuredRowCount']}。"])
        environment = "空计时满足上限" if run["calibration"]["environmentValid"] else "空计时超出上限，停止胜负解释"
        drift = "短运行漂移未满足要求，停止胜负解释" if run["shortRunBlocked"] else "本次未被已提供的短运行漂移证据排除"
        lines.extend(["", f"{environment}；{drift}。"])
        if run["environmentChanged"]:
            lines.extend(["", "前后环境配置发生变化，本次全部策略对停止胜负解释；原始量级与环境记录仍保留。"])
        lines.extend(["", f"源码摘要：`{run['sourceSha256']}`。完整构建文件身份见此运行的 `run-metadata.json`。",
                      f"C++ 内部总计：{display(run['cppTotalMs'])} ms；子进程墙钟：{display(run['processWallSeconds'])} s；"
                      f"整个尝试起止间隔：{display(run['attemptElapsedSeconds'])} s。"])
        table(lines, ("标定批次", "数量", "P50（ms）", "P95（ms）", "P99（ms）", "名义分辨率（ns）"),
              [(name, row["count"], row["p50Ms"], row["p95Ms"], row["p99Ms"], row["clockResolutionNs"])
               for name, row in sorted(run["calibration"]["batches"].items())])
    lines.extend(["", "## 三次独立短运行漂移"])
    if not analysis["shortRunStability"]:
        lines.extend(["", "未提供符合 W=2/R=10/P=8 和两个中预算场景首目标条件的三次独立短运行。"
                      "当前结果不能证明跨进程环境稳定，仍需按工程验收协议补充漂移检查。"])
    else:
        labels = {"stable": "最后一轮满足 5% 上限", "repeat_required": "需冷却并整轮重测",
                  "unstable": "第二轮仍不稳定", "incomplete": "独立短运行数量不足"}
        table(lines, ("场景", "阶段", "采样", "动作", "状态", "各轮原值统计"),
              [(row["scenarioId"], row["passId"], row["sampleIndex"], row["action"], labels[row["status"]], row["rounds"])
               for row in analysis["shortRunStability"]])
        lines.extend(["", "漂移按同一目标与动作的三个运行级中位数检查。第二轮必须保留完整三次新运行和第一次结果；"
                      "超过 5% 不能通过丢弃不利进程来修正。近零样本同时保留绝对极差。"])
    lines.extend(["", "## 目标配对与实际执行"])
    winners = {"A": "A 占优", "B": "B 占优", "tie": "平局或噪声不足", "excluded": "排除胜负解释"}
    for run in analysis["runs"]:
        lines.extend(["", f"### 运行 `{run['runId']}`"])
        selected = sorted((row for row in analysis["pairs"] if row["runId"] == run["runId"]),
                          key=lambda row: (row["scenarioId"], row["passId"], row["primaryWorkValue"],
                                           row["sampleIndex"], row["actionA"], row["actionB"]))
        table(lines, ("场景/阶段/采样", "冻结层", "原始工作量", "A / B", "A 中位（ms）", "B 中位（ms）",
                      "配对差（ms）", "B 相对收益", "阈值（ms）", "实际线程 A / B", "结论", "排除原因"),
              [(f"{r['scenarioId']} / {r['passId']} / {r['sampleIndex']}", r["selectionStratum"], r["primaryWorkValue"],
                f"{r['actionA']} / {r['actionB']}", r["medianAMs"], r["medianBMs"], r["medianDifferenceMs"],
                r["relativeGainB"], r["thresholdMs"],
                f"{r['actualWorkersMinA']}–{r['actualWorkersMaxA']} / {r['actualWorkersMinB']}–{r['actualWorkersMaxB']}",
                winners[r["winner"]], r["exclusionReasons"]) for r in selected])
    lines.extend(["", "## 目标重建与计时外成本", "", "以下合计包含预热与正式策略行，单位为 ms；各项中位数另存 `target-costs.csv`。"
                  "重建是来源流水线到达目标边界的累计耗时，扣除了此前测量观察器时间，不能跨目标相加。"
                  "统一线程准备与每行准备分开记录；这些成本没有从策略 wallMs 中扣除，也不进入策略胜负判断。"
                  "CSV 的 inputFeatureProbeMs 只记录一次目标输入匹配探测，不将复制到各行的数值重复相加。"])
    table(lines, ("运行", "场景/阶段/采样", "策略行", "累计重建", "复制合计", "输入核对合计", "结果验证合计", "统一/逐行线程准备"),
          [(r["runId"], f"{r['scenarioId']} / {r['passId']} / {r['sampleIndex']}", r["strategyRowCount"],
            r["rebuildToTargetMs"], r["stateCloneMsSum"], r["inputCheckMsSum"], r["validationMsSum"],
            f"{display(r['targetWorkerPreparationMs'])} / {display(r['workerPreparationMsSum'])}") for r in analysis["targetCosts"]])
    lines.extend(["", "## 冻结工作量分层", "", "low/middle/high/coverage 沿用发现阶段的固定分层，coverage 补点独立列出。"
                  "没有按测得时间重新分箱；以下计数以目标为单位。"])
    table(lines, ("运行", "场景/阶段", "A / B", "层", "有效/全部目标", "工作量范围", "A 优势", "B 优势", "平局", "排除"),
          [(r["runId"], f"{r['scenarioId']} / {r['passId']}", f"{r['actionA']} / {r['actionB']}", r["stratum"],
            f"{r['validTargetCount']}/{r['targetCount']}", f"{display(r['minimumWork'])}–{display(r['maximumWork'])}",
            r["winsA"], r["winsB"], r["ties"], r["excluded"]) for r in analysis["strata"]])
    lines.extend(["", "## 各次运行的描述性信号"])
    table(lines, ("运行", "场景/阶段", "A / B", "有效目标", "观察"),
          [(r["runId"], f"{r['scenarioId']} / {r['passId']}", f"{r['actionA']} / {r['actionB']}",
            r["validTargetCount"], r["signal"]) for r in analysis["signals"]])
    lines.extend(["", "## 跨运行方向核对"])
    lines.extend(["", "仅在相同后端、源码、构建、冻结输入、环境和 W/R/P 配置组内核对方向，组的完整身份及 W/R/P 见 `paired-targets.csv`。"])
    table(lines, ("比较组", "场景/阶段/采样", "A / B", "独立运行数", "各次方向", "有方向样本是否一致"),
          [(r["comparisonGroup"][:12], f"{r['scenarioId']} / {r['passId']} / {r['sampleIndex']}", f"{r['actionA']} / {r['actionB']}", r["runCount"],
            r["winners"], "未建立" if r["directionAgreement"] is None else ("一致" if r["directionAgreement"] else "不一致"))
           for r in analysis["consistency"]])
    lines.extend(["", "## 覆盖不足与限制"])
    if analysis["unavailableGroups"]:
        table(lines, ("运行", "场景", "阶段", "原因"),
              [(r["runId"], r["scenarioId"], r["passId"], r["failure"]) for r in analysis["unavailableGroups"]])
    lines.extend(["", "任一块发生回退时，整个目标中涉及该动作的策略对都排除胜负解释；"
                  "网格的两种串行策略若仍完整有效，可以继续比较。预热没有进入正式统计。",
                  "", "双向描述性信号仅表示不同冻结目标出现相反方向且差值超过预定噪声阈值。"
                  "本报告没有执行 bootstrap、Holm 校正、正式模型拟合或研究继续判定；后续 pilot gate 仍需审阅覆盖、稳定性和预测价值。",
                  ""])
    (output / "report.md").write_text("\n".join(lines), encoding="utf-8")
