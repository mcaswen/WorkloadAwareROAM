# 提案面证据复用归约数据

[报告与推导](../../qpc_06e_face_evidence_results.md)。原始目录为`benchmark-output/cpu-refinement/qpc-06e/run-01/`，正常前测复用06D；二进制、源快照、perf.data和Tracy原始捕获按仓库规则忽略，本目录提交完整归约与图。

- [summary.json](summary.json)：四配置两系统三事件的均值/中位/P95/最大值、逻辑对照、诊断结果、面计数、测试输出与原始文件SHA-256。
- [timing-comparison.csv](timing-comparison.csv)：首轮正常前后全部指标。
- [timing-recheck.csv](timing-recheck.csv)：仅Linux Canyon P的一对触发复测，不覆盖首测。
- [verification.json](verification.json)：13进程96帧、数值专项、门槛、计数/根覆盖、权重及源身份核查。
- [diagnostic-source-difference.patch](diagnostic-source-difference.patch)：先构建FP与后构建Tracy的唯一业务源差异，只有显式零初始化和诊断聚合事件；不把不同源快照冒充同一个版本身份。
- [同根费用图](root-cost.png)：Sierra P第28帧前缀0～159，保留未改善根，索引不是稳定面ID。

仅Sierra P新增诊断，全部明细针对暖移动frame3～31：

| 内容 | 文件 |
|---|---|
| 全部187个有占比采样符号 | [perf-functions.csv](dem-sierra-P/perf-functions.csv) |
| 全部采样调用边 | [perf-call-edges.csv](dem-sierra-P/perf-call-edges.csv) |
| 完整采样栈（压缩） | [perf-paths.csv.gz](dem-sierra-P/perf-paths.csv.gz) |
| Tracy全部函数区间、次数、自身/含调用时间 | [tracy-functions.csv](dem-sierra-P/tracy-functions.csv) |
| 每帧阶段派发、等待、长尾 | [tracy-dispatches.csv](dem-sierra-P/tracy-dispatches.csv) |
| 每任务线程、起止及时间 | [tracy-tasks.csv](dem-sierra-P/tracy-tasks.csv) |
| 4,640根及内部Fit/逐点次数、时间 | [tracy-items.csv](dem-sierra-P/tracy-items.csv) |
| 1,357次认证的7项面计数 | [face-work.csv](dem-sierra-P/face-work.csv) |

面计数字段依次为记录数、区间/精确构造、区间/精确边查询、区间/精确高度查询。原式固定减法`2e+6h`是以同一实际查询集合按源码计算的反事实工作量，新式为`8b`；并非原程序指令计数。函数inclusive不相加，线程时长和不是CPU时间，采样调用边不是调用次数。

独立进程为统计单位，首轮8个正常+3个诊断/控制，触发复测2个。帧分布不充当独立重复；Linux与Windows绝对时间不换算。诊断追加聚合事件的源码版本与先构建FP分别留有源快照；正常业务表达式相同，身份差异不抹平。

重建归约（Ubuntu，需要保留本轮原始目录及06D归约/原始数据）：

```sh
/home/mcaswen/.cache/roam-experiments/venv/bin/python scripts/analyze_transactional_face_evidence.py --input benchmark-output/cpu-refinement/qpc-06e/run-01 --output docs/research/cpu_refinement/data/qpc_06e_face_evidence --plot
```

该入口不运行被测程序，最近归约约6秒；图表使用Windows现有中文字体，不拷贝字体到仓库。
