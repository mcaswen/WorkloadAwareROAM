# QPC-06F 精简证据

对应[完整报告](../../qpc_06f_height_rejection_results.md)。原始资料在忽略目录 `benchmark-output/cpu-refinement/qpc-06f/run-01/`；本目录保存可审阅的归约，不提交程序、perf.data或Tracy原捕获。

- `summary.json`：全部普通计时统计、轨迹比较、提示复遇、诊断工作及采样/时序摘要。
- `timing.csv`：Windows两场景、S0/S1/S2/P0、移动/返回/静止各阶段均值/中位/P95/最大。每变体一个进程，帧不是独立重复。
- `work.csv`：Sierra S1/S2诊断；影子工作不混入生产计数，但计时含明确标出的影子重放，不能当性能基准。
- `perf/`：214个函数self/inclusive、调用边及压缩完整路径。分母为移动ROI cpu-clock事件权重，父子inclusive不能相加。
- `tracy/`：函数区间、派发与任务。跨线程区间和不是CPU时间。
- `trajectory.png`：同政策S0/S1/S2轨迹及不同生成器P0成本参照。
- `provenance.json`：程序清单、输入/输出清单和关键原始CSV的SHA256；完整命令保留原始`commands/`及各run manifest。
- `verification.json`：定向测试日志、原认证重放数量、96帧对照、源码身份、日常构建恢复及文档链接核查。

冻结基线：父提交`c00ca8a4b80111dcb6ce13705f988ce86cef6dfd`加未提交04H；完整修改前源码归档SHA256 `a9dd8f829f7f3d5f9727776f6b1b950b6c60366fead49bf1b729ddaaf935733c`。S0程序`116f3b898bac74ba4e4b238791a015943f242ca311d8f2a8735e4fd49d9d7120`；S1 `5da6464f9c6df77151774bfbbd10a61f051c95a441e85f93dca88c577df9a67e`；S2 `2f1664df62a507212701df4a7f956cc38246f4df7cb07f33ba54dce7571bd89d`。这些为冻结CBT-OFF测量程序，不是最后恢复CBT-ON日常构建的程序身份。

归约命令（Ubuntu）：

```text
python3 scripts/analyze_transactional_height_rejection.py benchmark-output/cpu-refinement/qpc-06f/run-01 --details docs/research/cpu_refinement/data/qpc_06f_height_rejection --plot
```

作图依赖matplotlib；本机通过`/home/mcaswen/.cache/roam-profiling/report-plot`的既有安装提供。无需作图时省略`--plot`，其余分析为标准库及既有项目解析器。`summary.json`记录与Windows输出等价的Linux捕获；不能把不同平台的毫秒拼成一张收益表。
