# Work–Inflation Gate 小规划

> 日期：2026-09-12；类型：复用既有配对设施的小规划。
> 状态：**能力评估与契约草案完成，待用户 Review；未修改代码、未启动新性能采集**。
> 后续安排：**暂缓实施，先编写参数化成本语义 Gate 小规划**。下文保存测量方案，不构成当前插桩或采样授权。
> 前置：[必要依赖审计](../../research/roam_parallelism/necessary_dependency_audit.md)已获用户认可；测量口径集中在[测量契约](../../research/roam_parallelism/work_inflation_measurement_contract.md)。

## 1. 目标与范围

用最小证据检查 split scoring、split topology、mesh commit 的逻辑工作与执行成本能否解释墙钟行为。产物是一份分阶段工作账本和有边界的解释报告，不要求统一假说成立，也不要求全列都有数值。

只复用既有生产算法、冻结输入、阶段配对和运行 Probe；允许在现有实验边界导出已经维护的计数，并可选采样进程累计 CPU 时间。CPU-CBT 只读归档结果。首批六目标及有界扩展条件见契约第 6 节。

不改拓扑、评分、网格算法或线程池；不优化回退、不做新批量提交、不重构 profiler、不重新建 DAG、不恢复已关闭支线、不增加 GPU 工作。缺测可以成为出口，规划是最大范围，不是必须完成的流水线。

## 2. 前置阅读与架构判断

已阅读开发规范、规划规范、审查规范、现有 CPU ROAM 研究定义、必要依赖审计、CPU 配对/普通性能事实及 CPU-CBT-02 规划与审查；定向扫描阶段执行、配对计量、计数映射、候选计划、网格写入、线程池与进程采样代码。

判断：既有 `生产阶段 → 实验采样值 → 正式记录/CSV → Python 校验和分析` 的单向依赖足够，无须新增运行时子系统。工作解释属于离线报告，计量属于现有实验边界，不能为了账本侵入生产数组访问或在热循环加入逐项原子计数。

关键事实与缺口见契约第 2 节。尤其应防止把共同 `planning_*` 特征当作原生串行已执行的计划，或把进程全生命周期 CPU 时间当阶段 CPU 时间。

## 3. 文件与职责边界

以下是确认后最多允许涉及的文件；条件项未触发则不修改。保留现有目录，没有新的生产模块或公共算法接口。

| 决策 | 文件 | 职责与变更边界 |
| --- | --- | --- |
| Reuse | DOD 的 Queues、Topology、TopologyPlan、MeshEmit、ThreadPool；`TerrainLodProfiling.h` | 只读解释生产分支；复用现有公开 CPU 采样，不修改生产算法、线程调度或热路径统计 |
| Extend | `src/algorithms/data_oriented_roam/DataOrientedRoamPassExperiment.h`、`DataOrientedRoamPassMeasurement.cpp` | 样本补实际工作计数与阶段增量；可选 CPU 诊断配置默认关闭，只在实验包络读进程计数 |
| Extend | `src/experiment/formal/FormalCpuPairRecords.h`、`FormalCpuPairCsv.cpp`、`src/benchmark/formal/FormalCpuPassBenchmark.cpp` | 映射并导出上述值、缺测状态与诊断模式；不在导出层重建生产工作或决定策略 |
| Extend，CPU 预检启用时 | `src/experiment/formal/FormalExperimentTypes.h`、`src/benchmark/TerrainLodBenchmarkCommandLine.cpp`、`scripts/run_cpu_pilot.py` | 沿已有选择配置传递一个默认关闭的 `--pass-cpu-sample` 开关；不增加新 profile、命令族或调度器 |
| Extend | `scripts/cpu_pilot_artifacts.py` | 新记录完整性/版本校验；旧 P5 输入协议不变，历史记录不改写 |
| Create | `scripts/analyze_work_inflation.py` | 有限离线汇总：校验身份、建立带单位的账本、按独立进程汇总；独立存在是因为问题不同于既有 crossover 胜负分类，不承担运行调度 |
| Extend / Create | `tests/DataOrientedRoamPassExperimentTests.cpp`、`tests/FormalCpuPairTests.cpp`；条件涉及 `tests/FormalExperimentCommandLineTests.cpp`、`tests/test_cpu_pilot_pairing.py`；新增 `tests/test_work_inflation_analysis.py` | 只覆盖新增计量边界、计数差值、协议和汇总风险；Python 测试可直接运行，不为单项分析新建构建平台 |
| Create，结束时 | `docs/research/roam_parallelism/work_inflation_gate_report.md`、`docs/reviews/roam_parallelism/work_inflation_gate_review.md` | 分别保存证据/阶段出口和规划符合性审查；同一 Agent 自审，不称独立同行评审 |
| Extend | 本规划、测量契约、`docs/research/README.md` | 回填实际执行、停止条件与索引，不重写研究主线 |

数据流为 `冻结阶段输入 → 同步生产调用 → 实际计数/原始 CPU 增量 → 有版本记录 → 离线账本`。复制、所有权和结果核查继续由原实验模块负责。CPU 诊断与干净墙钟分别运行，默认执行路径不增加系统采样调用。无需 Wrap、观察者框架或新的设计模式。

记录模式建议 `off / process_cpu`，原始字段含 `processCpuMs` 与 `cpuSampleStatus`；缺测用空值及原因，不能用零伪装。实际工作字段包括候选/分类数量及基础 split、forced split、merge、拒绝和预算拒绝的阶段增量；评分和网格复用已有实际计数。输出 schema 升版，干净墙钟测量边界不变；诊断模式单独标识，不把历史 schema 2 的缺列补零后混算。

若实现发现必须改生产接口、加入内部访问计数或增加另一套驱动才能得到结论，停止该扩展，记录缺口并重新 Review。不能把本表解释为必须完成所有条件项的授权。

## 4. 实施步骤和局部出口

### 步骤 1：用已有证据形成账本

验证六目标来源、旧动作/回退、字段语义及完整更新记录，建立共有工作、并行新增工作、设施开销、未知四栏。CPU-CBT 分开读取 R/S/P 的并行占比与阶段成本。

先判断是否已有足够解释：DOD 拓扑若仍全部回退，限定为准备后回退成本；CPU-CBT 若主要是并行部分极小，不为其补测线程曲线。缺少必要来源时停止对应案例，不重开历史复现。

本步骤可以结束一个案例，不要求为它继续实现 C。报告不得提前把候选统一解释写成事实。

### 步骤 2：补最少的阶段记录，检查 C 是否可用

在实验执行前保存既有累计计数，执行后、验证前计算本阶段增量并读取实际分支计数。只复制标量，不改变生产调用。实际工作与计时外共同特征分列。

CPU 可用性仅按契约第 5 节做高工作量目标的一次有界预检。使用现有 `CaptureTerrainLodCpuSample`，不直接依赖其内部 `Detail` 接口；记录原始 CPU 差值、包络及有效性，干净路径保持原计时方式。若不可分辨，写明 C 缺测并停止该项，不自动改接周期计数或重新设计批量重复器。

### 步骤 3：有限同期配对

先执行六目标的 S/请求 8 线程，沿用原配对顺序、验证与独立进程契约。成本诊断可用才采集该项；真实路径不匹配、结果不同或来源不完整时不能进入加速统计。

只有首批存在具体未解释的真实并行行为，才补相应高工作量目标的一线程入口及 2/4 线程；完整更新也仅在旧证据不能回答整体边界时复用现有两场景 Probe。所有条件扩展前在日志写明触发证据，不扩大输入和算法范围。

### 步骤 4：有限归因与审查

按契约第 7 节给出支持、部分支持或解释不足；将逻辑数量、累计 CPU 成本、直接墙钟及工作占比分开。不能证明固定并行税时写“规模相关，机制尚未完全分离”；没有可靠 C 时不能宣称 work-time inflation 已定量验证。

审查输出检查计数双算、混用边界、错误线程语义、样本伪重复、旧新数据混用、缺测补零、相加子计时及过度因果表述。回填本规划，结束 Gate；未来优化或额外 profiler 必须另行讨论。

## 5. 验证与性能契约

本次仅写文档：核对本地链接、来源数值、目录职责和 `git diff --check`，不构建、不跑性能或全量测试。

确认后的实现阶段，定向验证：

- 配对测试覆盖已有统计非零的阶段输入，确认增量不会夹带来源帧计数；forced split 不重复计入总 split。原结果哈希和默认计量边界保持。
- CPU 诊断测试覆盖关闭、有效、失败/不可分辨状态及执行前后包络；短采样不要求必须得到正值。无需添加精确计时性能断言。
- CSV/分析测试覆盖历史缺列、诊断混入干净墙钟、失败动作、回退、分母为零、独立进程聚合与单位。按实际修改选择相关测试，不运行所有后端和 CTest。

改代码前保存基线，改后同配置复测：使用契约中的三个高工作量目标及受影响的默认关闭路径；需要完整边界时复用两场景 Probe，不重复已有仍有效的报告。基线/改后均以独立进程为单位，默认一次预热、五次计量，保留结果及中位数/P95。

工程回归按[开发规范 7.3](../../standards/development_guidelines.md)的 `max(0.05 ms, 5% × 基线, 基线五进程极差)`，仅越线项最多完整复测一次；持续越线单独注明原因和分析，交由用户决定新增修复规划或修改后续规划。该门槛不用于证明研究胜负，也不能代替 CPU 采样有效性检查；不追究不可分辨的微秒差值。

原始数据存入新的 `benchmark-output/work-inflation/<attempt>/`，沿用忽略/归档规则，报告记录源码、构建、资产和数据摘要及复现入口，不留下无归属的跟踪外原始文件。未经用户明确许可不提交 Git。

## 6. 当前完成情况

- 已完成：能力盘点、CPU-CBT 竞争解释核对、不可替代缺口、最小样本和测量边界的草案。
- 文档核查：20 个本地引用均可解析；六目标身份及实际动作与 P5 原始配对记录相符，两个网格案例的脏三角形/区间数已直接核对；`git diff --check` 无空白错误。既有未提交审计文件保持原状，本轮仅新增两份文档并更新研究索引。
- 未执行：任何 C++/Python 修改、新运行、CPU 预检或新的机制结论。
- 待 Review：本规划的实验边界与条件扩展；用户确认后按步骤推进，不把本轮评估视为实现授权。
