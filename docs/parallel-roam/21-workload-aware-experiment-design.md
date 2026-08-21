# Workload-Aware ROAM：实验设计

本文档定义二维 crossover characterization、feature model、adaptive 对照、泛化测试、统计口径、指标、go/no-go 标准和实施顺序。策略和 pass 的定义分别见[策略定义](20-workload-aware-strategy-definition.md)和[具体问题定义](19-workload-aware-problem-definition.md)。

## 两层实验结构

研究分为两个层次，第一层回答“现象是否存在”，第二层回答“能否在线预测和利用”。

### 第一层：二维矩阵与现象刻画

对每个具体 pass 固定输入状态，扫描 workload volume 与有效并行度或 dirty ratio，输出：

- 该 pass 合法策略对的实测时间，例如 topology commit 的 serial/parallel，或 mesh emit 的 dirty/full；
- crossover boundary；
- winner 与 feasible mask；
- 16.6 ms deadline 下的 serial optimal、parallel optimal、both feasible、only serial feasible、only parallel feasible 和 none feasible 区域。

这一层只建立经验事实和可视化，不训练 adaptive model。执行后特征可以用于解释矩阵形状，但不能被描述为在线输入。

### 第二层：预测模型与在线策略

预测模型的接口必须明确：

~~~text
Input:
  passId
  preDecisionFeatures
  duringPlanningFeatures
  frameBudgetSlack

Output:
  effectiveStrategyId in A_pass
  predictedPassTime
  predictedFeasibility
~~~

模型只使用决策前可获得的特征，输出当前 pass 的一个合法策略以及 deadline feasibility。对比 candidate-count 单阈值、二维 lookup table 和低维可解释模型，主要指标是时间 regret 和 deadline miss，而不是单纯分类准确率。

## 核心实验

### Experiment 1：Crossover characterization

证明核心 pass 中至少三个具有不同 crossover，并给出 deadline-aware 二维矩阵。Merge topology 与 Split topology 同时报告 planning、parallel commit、maintenance 和 serial tail 的内部成本拆分。

### Experiment 2：Feature model

证明使用 pre-decision + during-planning 的 pass-bound multi-feature model 比 candidate-count single threshold 更接近 per-pass oracle。post-execution feature 只用于解释误差和内部成本归因。

### Experiment 3：Adaptive vs baselines

比较 adaptive、fixed serial、maximum-safe-parallel、best static、greedy pass oracle 和 frame-level oracle。除总体时间外，必须直接报告 greedy oracle 与 frame oracle 的差距，以及 16.6 ms deadline miss rate。

### Experiment 4：Generalization

使用完整 terrain 或完整 trajectory 留出测试，证明模型没有只记住单一地形或单一相机路径。主文不要求同时完成跨 CPU 架构泛化。

## 实验规模边界

主文实验固定在一台参考 CPU、一个主要 worker 配置、两个代表性 terrain 和三条 workload trajectory 上。worker sweep、第二 CPU 架构、10/20 ms deadline、全部 incremental/full 组合、完整 hysteresis sweep 和更多 DEM 放入补充材料或后续工作。

只有在四个核心实验成立后才扩展实验规模，避免 terrain × trajectory × worker × CPU × strategy 的全笛卡尔积。

## 公平性与正确性

所有配对实验必须使用：

- 相同高度图、terrain size、height scale、max depth 和 triangle budget；
- 相同 split/merge threshold、相机矩阵和视锥；
- 相同冻结拓扑状态、queue 内容、候选集合与候选排序；
- 相同结果验证器和 mesh 质量口径；
- 相同构建配置、CPU 频率策略和后台负载条件。

每个目标帧先保存或可重建输入状态，再分别运行所有策略。配对结果至少比较：

- active leaf 集合或规范化 topology hash；
- split、forced split 和 merge 结果；
- triangle budget 与 persistent queue invariant；
- mesh vertex/index 内容或规范化 mesh hash；
- crack、T-junction、invalid neighbor 和 invalid topology count。

若并行提交允许多种合法顺序但最终 cut 不唯一，则必须定义规范化等价条件并证明质量与预算一致；否则该 pass 不满足纯 execution crossover 实验要求。

## Workload 构造

核心 workload 只保留足以跨越主要 regime 的最小集合：

- **Trajectory A：** 静止或缓慢移动，产生低 candidate/dirty workload；
- **Trajectory B：** 稳定接近或远离高曲率区域，产生持续的 split-heavy 或 merge-heavy workload；
- **Trajectory C：** 快速转向与预算重入，产生突发、高不均衡和 mixed workload；
- **Terrain A：** 当前 Hm_Terrain_Test_129 或等价受控地形，用于可重复的矩阵扫描；
- **Terrain B：** 当前 Hm_Terrain_Peking_513，用于真实形态和留出验证。

主文固定参考 CPU 和主要 worker 配置。1/2/4/8 worker sweep、更多程序化地形、额外 DEM、第二 CPU 架构及更完整的相机运动类型只作为补充实验。若两个 terrain 已不足以形成 held-out 测试，再增加一个真实 DEM，而不是预先展开完整组合。

## 统计方法

- 每组先 warm-up，不记录线程池创建、shader 编译或冷启动成本；
- serial/parallel 采用交替或随机配对顺序，避免温度和频率漂移；
- 每个矩阵点重复多轮，报告 p50、p95、p99、均值、方差或 bootstrap confidence interval；
- 对差异落在计时分辨率或置信区间内的点标记为 tie，不强制分类；
- 训练/标定与测试按完整地形或完整相机轨迹划分，不能随机拆分相邻帧造成时间泄漏；
- 策略选择、特征提取和 fallback 的时间必须计入 adaptive 总成本。

## 主要指标

~~~text
passWallTimeMs
cpuUpdateMs
frameTimeMs
p50 / p95 / p99
deadlineMissRate
greedyOracleRegret
frameOracleRegret
greedyFrameOracleGap
strategyPredictionAccuracy
featureCollectionOverheadMs
policyDecisionOverheadMs
strategySwitchCount
effectiveWorkerCount
candidateCount
nonEmptyChunkCount
estimatedChunkImbalance
estimatedBoundaryRatio
actualClosureDepth
actualWorkerUtilization
dirtyTriangleRatio
dirtyRangeCount
topologyValidationFailures
budgetViolationCount
~~~

其中主要优化指标为时间 regret，而不是单纯分类准确率：

~~~text
regret = (T_adaptive - T_frame_oracle) / T_frame_oracle
oracle_gap = (T_greedy_oracle - T_frame_oracle) / T_frame_oracle
~~~

把两个耗时接近的策略分类错，影响可能很小；把明显跨越 crossover 或 deadline boundary 的点分类错，才应受到更大惩罚。pre-decision、during-planning 和 post-execution feature 必须在 CSV 中使用不同前缀，避免后续分析误把事后特征放入模型。

## 验证与决策标准

初始继续条件：

- Experiment 1：至少三个代表性 pass 观察到可重复的 crossover，并形成 winner 与 deadline feasibility 不同的区域；
- Experiment 2：仅使用 pre-decision/during-planning feature 的多特征模型，相较 candidate-count 单阈值显著降低 held-out workload 上的 per-pass oracle regret；
- Experiment 3：adaptive 在混合轨迹上降低 CPU update p95 或 16.6 ms deadline miss rate，并接近 frame-level oracle；目标暂定中位 regret 不超过约 5%，P95 regret 不超过约 10%；
- Experiment 3 同时给出 greedy oracle 与 frame oracle 的 gap：小 gap 支持 pass decoupling，大 gap 则必须由 coordinated policy 解释；
- Experiment 4：在留出 terrain 或完整 trajectory 上保持收益，而不是只在训练路径成立；
- feature collection 与策略决策总开销不超过 CPU update 的 5%，且拓扑错误和预算违规始终为零。

停止或转向条件：

- 同一个固定策略在绝大多数核心 pass 和 workload 上持续占优，其他策略差异长期落在测量噪声内；
- crossover 只能由 post-execution feature 解释，pre-decision/during-planning feature 无法获得净收益；
- 简单 candidate-count 阈值已经达到与复杂模型相近的 regret；
- 某个具体 pass 的 incremental/full 路径几乎始终被支配，则从该 pass 的最终策略空间删除该维度，而不是保留全局四策略组合；
- adaptive policy 的决策、切换或缓存扰动长期抵消其选择收益；
- 模型只适用于单一高度图或单一相机路径，留出测试无法复现趋势；
- 在核心 workload 中 fixed serial 或 maximum-safe-parallel 始终满足 deadline 且占优，无法形成有意义的策略选择问题。

上述数值是前置实验阶段的暂定 go/no-go 标准，不是提前写死的论文结论。完成计时噪声分析和硬件标定后再冻结。

## 实施顺序

1. **已完成：** 冻结 Classic/DOD 基线，确认持久双队列、增量 mesh、统一 topology validation 和 runtime benchmark 可用。
2. **已完成：** 为 DOD Merge topology / Split topology 拆分内部计时，并记录候选数、非空 chunk、worker 和六段 topology 时间。
3. **当前工作：** 将核心 pass 的 requested/effective `ExecutionMode`、`UpdateMode`、worker count 和 fallback reason 解耦，保持默认行为不变。
4. **策略接口完成后：** 为 serial/parallel 建立共享 immutable snapshot 与共享 kernel，补充 topology hash、mesh hash 和逐帧配对等价测试。
5. **等价性通过后：** 补齐 Merge mark、Merge topology、Split scan/mark、Split topology、Mesh emit 和 CPU upload 的合法策略；Prepare、Finalize 只作为共享阶段计入总时间。
6. **实验控制面完成后：** 按 pre-decision、during-planning、post-execution 分类扩展 CSV，先完成 Experiment 1 的 pass-specific deadline-aware 二维矩阵。
7. **Crossover 稳定后：** 完成 Experiment 2，比较 candidate-count threshold 与多特征模型，禁止 post-execution leakage。
8. **模型验证后：** 实现冻结状态 replay、greedy pass oracle 和 frame-level oracle，完成 Experiment 3 的 adaptive/baseline/oracle 对比。
9. **主结果成立后：** 使用留出 terrain/trajectory 完成 Experiment 4；worker sweep、第二 CPU、更多 DEM 和完整 hysteresis 实验按篇幅进入补充材料。
