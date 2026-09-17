# 当前函数与时序归约数据

来源：`benchmark-output/cpu-refinement/qpc-06c/run-01/`，基线`5a18cb1`。原始perf.data、Tracy trace、输入/源码快照与二进制沿用仓库忽略规则；本目录提交完整派生数据和图，不提交大二进制。生成器为`scripts/analyze_transactional_current_profile.py`。

`summary.json`保存四组模式/Windows结果一致性、各事件统计、采样质量、线程/窗口分母、数值路径分摊、Tracy函数及长尾摘要和原始输入文件SHA-256。`moving-cost-and-tasks.png`区分Windows正常阶段均值与Linux采集时序。

| 数据 | Sierra L | Sierra P | Canyon L | Canyon P |
|---|---|---|---|---|
| 所有函数自身/含调用权重与自身样本数 | [CSV](dem-sierra-L/perf-functions.csv) | [CSV](dem-sierra-P/perf-functions.csv) | [CSV](dem-canyon-L/perf-functions.csv) | [CSV](dem-canyon-P/perf-functions.csv) |
| 所有调用边采样权重 | [CSV](dem-sierra-L/perf-call-edges.csv) | [CSV](dem-sierra-P/perf-call-edges.csv) | [CSV](dem-canyon-L/perf-call-edges.csv) | [CSV](dem-canyon-P/perf-call-edges.csv) |
| 所有完整采样栈 | [CSV.gz](dem-sierra-L/perf-paths.csv.gz) | [CSV.gz](dem-sierra-P/perf-paths.csv.gz) | [CSV.gz](dem-canyon-L/perf-paths.csv.gz) | [CSV.gz](dem-canyon-P/perf-paths.csv.gz) |
| Tracy函数调用/包含与自身时间 | [CSV](dem-sierra-L/tracy-functions.csv) | [CSV](dem-sierra-P/tracy-functions.csv) | [CSV](dem-canyon-L/tracy-functions.csv) | [CSV](dem-canyon-P/tracy-functions.csv) |
| 每帧每阶段派发、等待、任务和/最长块/尾部 | [CSV](dem-sierra-L/tracy-dispatches.csv) | [CSV](dem-sierra-P/tracy-dispatches.csv) | [CSV](dem-canyon-L/tracy-dispatches.csv) | [CSV](dem-canyon-P/tracy-dispatches.csv) |
| 每个任务的线程、起止和时长 | [CSV](dem-sierra-L/tracy-tasks.csv) | [CSV](dem-sierra-P/tracy-tasks.csv) | [CSV](dem-canyon-L/tracy-tasks.csv) | [CSV](dem-canyon-P/tracy-tasks.csv) |

明细分母为29个暖移动机会，完整96机会的其他事件统计在summary；未混入初始化、返回和空批静止时间。任务起止以对应frame起点为零。采样调用边不是实际调用次数；Tracy任务时长和不是CPU时间，inclusive父子不能相加。gz仅压缩未裁剪全栈文本。

L/P不是同任务速度对照，Linux/Windows行为一致不意味着时间可以换算。Sierra L仅1811个ROI样本，保留有限精度标记。完整解释和测量限制见[报告](../../qpc_06c_current_hotspot_results.md)。
