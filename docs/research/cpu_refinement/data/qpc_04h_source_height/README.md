# QPC-04H 源高度消融数据

[完整结果与原因分析](../../qpc_04h_source_height_results.md)、[纸面推导](../../qpc_04h_source_height_derivation.md)。原始目录为 `benchmark-output/cpu-refinement/qpc-04h/run-01/`；程序、完整样本、源快照和原始perf/Tracy按规则忽略，本目录保留可审查归约。

| 文件 | 内容与边界 |
|---|---|
| [summary.json](summary.json) | 正常首测、定向复测、最终拒绝类别修正核对、旧路径兼容、局部/原生/批次审计、独立质量 |
| [timing.csv](timing.csv) | 两场景首测按移动、返回、静止及完整暖轨迹归约；均值/中位/P95 |
| [repeat.csv](repeat.csv) | 唯一一次政策定向复测及同窗口06E原程序；不覆盖首测 |
| [quality.csv](quality.csv) | 四帧绝对Emax/RMS/Hmax、实际N、Dmax和逐点损伤/改善分布 |
| [local-proposals.csv](local-proposals.csv) | 43项固定目录源高试值，双域进展/拒绝/局部最大值；不新增生产求解器 |
| [work.csv](work.csv) | 追溯进程的暖移动理由/工作计数；配对缓存命中会重复计理由，不等于独立认证次数 |
| [provenance.json](provenance.json) | 每次原始manifest、程序/源码/输入/命令身份、质量文件摘要与配置检查 |
| [完整时间与面数图](trajectory.png) | 首测逐帧曲线；排除前三帧冷启动，竖线区分返回与静止 |
| [视觉对照图](visual-contact.png) | 两场景P0/PS frame16/48实际平台捕获；原图身份见[visual-sources.json](visual-sources.json) |

仅Sierra PS做一次函数/时序诊断，窗口frame3～31：

- [全部有占比函数](perf/perf-functions.csv)、[采样调用边](perf/perf-call-edges.csv)、[完整采样路径](perf/perf-paths.csv.gz)。
- [Tracy全部区间与次数](tracy/tracy-functions.csv)、[派发/等待/长尾](tracy/tracy-dispatches.csv)、[逐任务线程与起止](tracy/tracy-tasks.csv)。

这些诊断在最终“Shape先于净面数检查”修正前采集。最终Windows正常轨迹有独立程序摘要和 `timing-final`：输出逐帧不变，但不能把旧诊断的精确调用数写成最终源码重采。拒绝类别修正前后数据均保留。

独立进程为统计单位。PS改变持续网格和实际工作，不是与P0同任务的多核加速实验；Linux诊断时间不换算Windows平台时间。线程区间和不是CPU时间，父子inclusive不能相加，perf使用cpu-clock权重而非硬件cycles。

评价使用已有独立C++ evaluator、原始U16+双线性reference、k=0固定采样。RMS是可见参数域样本等权，非屏幕面积加权；Emax是sampled maximum。目标0.5px只用于本冻结协议的分布统计，不是视觉不可感知阈值。

只读重建归约（需要本轮及06E/04G原始输入）：

```sh
/home/mcaswen/.cache/roam-experiments/venv/bin/python scripts/analyze_transactional_source_height.py --runs benchmark-output/cpu-refinement/qpc-04h/run-01 --output docs/research/cpu_refinement/data/qpc_04h_source_height
```

43项离线试值入口：

```sh
/home/mcaswen/.cache/roam-experiments/venv/bin/python scripts/analyze_transactional_source_height.py --local-inputs benchmark-output/cpu-refinement/qpc-04g/run-01 --output benchmark-output/cpu-refinement/qpc-04h/local-audit-reproduction
```

生产配置从06E Sierra/Canyon P仅增加 `receiverHeightPolicy: source-height`；P0省略该字段。正常、质量、视觉用共同实验runner分开运行；native witness/交换捕获复用 `--recovery-trace`。每条真实命令和程序SHA-256见provenance及原始commands，不能用今日工作区直接覆盖已冻结程序。

重建图表使用本机已有微软雅黑字体，不复制字体。原生缓存临时关闭CBT仅为CPU探针构建，收口时恢复原应用CBT开启/测试关闭选项；测量仍绑定保存的独立程序副本。
