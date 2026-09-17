# 接收根任务均衡归约数据

来源为`benchmark-output/cpu-refinement/qpc-06d/run-01/`；修改前Linux复用06C、Windows复用06B，各自原始目录没有覆盖。原始二进制、源码快照、perf.data、Tracy捕获和运行输出按仓库规则忽略；本目录提交完整归约与图。结果解释见[报告](../../qpc_06d_receiver_task_balance_results.md)。

- [summary.json](summary.json)：所有正常/复测事件的均值、中位、插值P95及最大值、行为核对、各模式一致性、线程与根摘要、采样质量、原始文件SHA-256。
- [timing-comparison.csv](timing-comparison.csv)：首次正常前后数据，含两个系统、四配置、三事件和完整/阶段指标。
- [timing-recheck.csv](timing-recheck.csv)：仅触发项的旧/新独立复测，保留首测，不择快替换。
- [verification.json](verification.json)：21进程/96帧、主门槛、每帧唯一根、perf权重核对，以及残余分析使用的30个空机会分布。
- [时序图](receiver-task-timeline.png)：Sierra P第25帧与Canyon P第5帧，前后同列共用时间轴；新版本彩色段为单根。

| 明细 | Sierra P | Canyon P |
|---|---|---|
| Tracy所有函数调用/自身/含调用 | [CSV](dem-sierra-P/tracy-functions.csv) | [CSV](dem-canyon-P/tracy-functions.csv) |
| 每帧每阶段派发、等待、任务和、长尾 | [CSV](dem-sierra-P/tracy-dispatches.csv) | [CSV](dem-canyon-P/tracy-dispatches.csv) |
| 每个任务线程、起止及时间 | [CSV](dem-sierra-P/tracy-tasks.csv) | [CSV](dem-canyon-P/tracy-tasks.csv) |
| 每根前缀索引、所属任务、时间、Fit/逐点认证次数 | [CSV](dem-sierra-P/tracy-items.csv) | [CSV](dem-canyon-P/tracy-items.csv) |
| 所有perf函数占比 | [CSV](dem-sierra-P/perf-functions.csv) | 复用06C，不重采 |
| 所有采样调用边 | [CSV](dem-sierra-P/perf-call-edges.csv) | 同上 |
| 所有完整采样栈 | [CSV.gz](dem-sierra-P/perf-paths.csv.gz) | 同上 |

明细暖窗口为frame3～31，共29帧；每个P的根文件恰4,640行。帧时间以对应frame起点为零，前缀索引不是稳定面ID。采样调用边不是调用次数，任务和不是CPU时间，父子inclusive不可相加。L只做正常前后测量，不扩充重复的采样矩阵。

独立进程是统计单位，帧分布是描述性统计，不用于假装独立重复。两个系统绝对时间不换算。首次13进程，定向复测8进程；全部行为对照包含完整96帧。Canyon P静止残余没有删除或用其他事件的收益冲掉。

重建命令（Ubuntu，需保留原始目录与06B/06C产物）：

```sh
/home/mcaswen/.cache/roam-experiments/venv/bin/python scripts/analyze_transactional_task_balance.py --input benchmark-output/cpu-refinement/qpc-06d/run-01 --output docs/research/cpu_refinement/data/qpc_06d_task_balance --plot
```

生成器仅归约，不启动被测程序；最近一次完整归约约10.9秒，位于所有运行计时边界之外。Windows微软雅黑字体存在时供图表使用，不将字体复制入仓库。
