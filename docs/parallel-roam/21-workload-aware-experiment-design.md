# Workload-Aware ROAM：实验设计

本文档定义 crossover 测量、特征模型、自适应对照、泛化测试、统计口径、go/no-go 标准和实施顺序。策略和 pass 的定义分别见[策略定义](20-workload-aware-strategy-definition.md)和[具体问题定义](19-workload-aware-problem-definition.md)。

## 两层实验结构

研究分为两个层次，第一层回答“现象是否存在”，第二层回答“能否在线预测和利用”。

### 第一层：二维矩阵与现象刻画

对每个具体 pass 固定输入状态，扫描 workload volume 与有效并行度或 dirty ratio，输出：

- 该 pass 合法 action 的完整实测时间，例如拓扑的 `SerialImmediate/ParallelAssisted`，或网格提交的 dirty/full；
- crossover 边界；
- winner 与 feasible mask；
- 参考 frame budget 下的 winner、tie 和 feasibility 区域。

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

模型只使用决策前可获得的特征，输出当前 pass 的一个合法策略以及 deadline feasibility。对比候选数量单阈值、二维 lookup table 和低维可解释模型，主要指标是时间 regret 和 deadline miss，而不是单纯分类准确率。

## 核心实验

### Experiment 1：Crossover characterization

检验核心 pass 中是否至少三个出现稳定且位置不同的 crossover，并给出 deadline-aware workload 图。Merge 拓扑与 Split 拓扑同时报告 planning、parallel commit、maintenance 和 serial tail 的内部成本拆分。

### Experiment 2：Feature model

检验 pre-decision + during-planning 的 pass-bound multi-feature model 是否比候选数量单阈值更接近 per-pass oracle。post-execution feature 只用于解释误差和内部成本归因。

### Experiment 3：Adaptive vs baselines

比较 adaptive、fixed serial、maximum-safe-parallel、best static、greedy pass oracle 和 frame-level oracle。除总体时间外，必须直接报告 greedy oracle 与 frame oracle 的差距，以及 16.6 ms deadline miss rate。

### Experiment 4：Generalization

使用完整 terrain 或完整 trajectory 留出测试，检验模型是否只记住单一地形或相机路径。主文不要求同时完成跨 CPU 架构泛化。

## 实验规模边界

主文实验固定在一台参考 CPU、一个主要线程配置、两个代表性 terrain 和三条 workload trajectory 上。CPU pass 以 headless benchmark 为主；涉及上传/渲染的结果先固定当前已验证的 OpenGL 后端，D3D12 在重新通过构建与 smoke 后作为交叉检查。线程数扩展实验、第二 CPU 架构、10/20 ms deadline、额外合法 action、完整 hysteresis sweep 和更多 DEM 放入补充材料或后续工作。

只有在四个核心实验成立后才扩展实验规模，避免 terrain × trajectory × 线程 × CPU × strategy 的全笛卡尔积。

## 公平性与正确性

所有配对实验必须使用：

- 相同高度图、terrain size、height scale、max depth 和三角形预算；
- 相同 split/merge threshold、相机矩阵和视锥；
- 相同冻结拓扑状态、queue 内容、候选集合与候选排序；
- 相同结果验证器和 mesh 质量口径；
- 相同构建配置、CPU 频率策略和后台负载条件。

每个目标帧先保存或可重建输入状态，再分别运行所有策略。配对结果至少比较：

- active leaf 集合或规范化拓扑哈希；
- split、forced split 和 merge 结果；
- 三角形预算与 persistent queue invariant；
- mesh vertex/index 内容或规范化 mesh hash；
- crack、T-junction、invalid neighbor 和无效拓扑数量。

若并行提交允许多种合法顺序但最终 cut 不唯一，则必须定义规范化等价条件并证明质量与预算一致；否则该 pass 不满足纯 execution crossover 实验要求。

## Workload 构造

核心 workload 只保留足以跨越主要 regime 的最小集合：

- **Trajectory A：** 静止或缓慢移动，产生低候选/dirty workload；
- **Trajectory B：** 稳定接近或远离高曲率区域，产生持续的 split-heavy 或 merge-heavy workload；
- **Trajectory C：** 快速转向与预算重入，产生突发、高不均衡和 mixed workload；
- **Terrain A：** 当前 Hm_Terrain_Test_129 或等价受控地形，用于可重复的矩阵扫描；
- **Terrain B：** 当前 Hm_Terrain_Peking_513，用于真实形态和留出验证。

主文固定参考 CPU 和主要线程配置。1/2/4/8 线程数扩展实验、更多程序化地形、额外 DEM、第二 CPU 架构及更完整的相机运动类型只作为补充实验。若两个 terrain 已不足以形成 held-out 测试，再增加一个真实 DEM，而不是预先展开完整组合。

## 统计方法

- 每组先 warm-up，不记录线程池创建、shader 编译或冷启动成本；
- serial/parallel 采用交替或随机配对顺序，避免温度和频率漂移；
- 每个矩阵点重复多轮，主报告使用 p50、p95 和 bootstrap confidence interval；样本量足够时再报告 p99；
- 对差异落在计时分辨率或置信区间内的点标记为 tie，不强制分类；
- 训练/标定与测试按完整地形或完整相机轨迹划分，不能随机拆分相邻帧造成时间泄漏；
- 策略选择、特征提取和 fallback 的时间必须计入 adaptive 总成本。

## 主要指标

| 类别 | 主指标 | 诊断字段 |
|---|---|---|
| 性能 | `passWallTimeMs`、`cpuUpdateMs`、p50、p95 | `frameTimeMs`、p99 |
| 实时性 | `deadlineMissRate` | `frameBudgetSlack` |
| 策略质量 | `frameOracleRegret`、`greedyFrameOracleGap` | prediction accuracy、switch 数量 |
| 在线开销 | feature collection、policy decision wall time | fallback 数量/reason |
| Workload | 候选/dirty 数量、non-empty chunks、边界/dirty ratio | imbalance、closure depth、线程利用率 |
| 正确性 | 拓扑/mesh hash、validation failures、budget violations | queue invariant counters |

其中主要优化指标为时间 regret，而不是单纯分类准确率：

~~~text
regret = (T_adaptive - T_frame_oracle) / T_frame_oracle
oracle_gap = (T_greedy_oracle - T_frame_oracle) / T_frame_oracle
~~~

两个 action 耗时落在 tie 区间时，分类错误不应与明显跨越 crossover/deadline 的错误同等处罚。CSV 必须用不同前缀区分 pre-decision、during-planning 和 post-execution feature，防止把事后统计误用为在线输入。

## 验证与决策标准

初始继续条件：

- Experiment 1：至少三个代表性 pass 实例观察到可重复的 crossover，并形成不同 winner/feasibility 区域；
- Experiment 2：仅使用 pre-decision/during-planning feature 的多特征模型，相较候选数量单阈值显著降低 held-out workload 上的 per-pass oracle regret；
- Experiment 3：adaptive 在混合轨迹上降低 CPU update p95 或 16.6 ms deadline miss rate，并接近 frame-level oracle；目标暂定中位 regret 不超过约 5%，P95 regret 不超过约 10%；
- Experiment 3 同时给出 greedy oracle 与 frame oracle 的 gap：小 gap 支持 pass decoupling，大 gap 则必须由 coordinated policy 解释；
- Experiment 4：在留出 terrain 或完整 trajectory 上保持收益，而不是只在训练路径成立；
- feature collection 与策略决策总开销不超过 CPU update 的 5%，且拓扑错误和预算违规始终为零。

停止或转向条件：

- 同一个固定策略在绝大多数核心 pass 和 workload 上持续占优，其他策略差异长期落在测量噪声内；
- crossover 只能由 post-execution feature 解释，pre-decision/during-planning feature 无法获得净收益；
- 简单候选数量阈值已经达到与复杂模型相近的 regret；
- 某个具体 action 几乎始终被支配，则从最终策略空间删除，不为保持策略数量而保留；
- adaptive policy 的决策、切换或缓存扰动长期抵消其选择收益；
- 模型只适用于单一高度图或单一相机路径，留出测试无法复现趋势；
- 在核心 workload 中 fixed serial 或 maximum-safe-parallel 始终满足 deadline 且占优，无法形成有意义的策略选择问题。

上述数值是前置实验阶段的暂定 go/no-go 标准，不是提前写死的论文结论。完成计时噪声分析和硬件标定后再冻结。

## 实施顺序

1. **已完成：** 冻结 Classic/DOD 基线，确认持久双队列、增量 mesh、统一拓扑 validation 和 runtime benchmark 可用。
2. **已完成：** 为 DOD Merge 拓扑 / Split 拓扑拆分内部计时，并记录候选数、非空 chunk、线程和六段拓扑时间。
3. **并行完成文献核验：** 按 `pass-level scheduling`、`work-inefficient parallelism`、`dynamic topology maintenance` 和 `adaptive LOD` 四组原样检索词补全检索与对照表；若发现直接覆盖相同问题和实验设计的工作，先收窄贡献再继续实现。
4. **当前工程工作：** 增加 pass-specific requested/effective action、线程数量、fallback reason 和统一 `PassTrace`，保持默认行为不变。
5. **先建立可重复输入：** 实现目标帧/目标 pass 的冻结状态 replay、拓扑/active leaf/mesh hash 和 queue invariant；没有 replay 不进入性能配对。
6. **实现最小策略集：** 依次补齐评分 serial/parallel、拓扑 serial/parallel-assisted、mesh dirty/full 与上传 dirty/full，并逐项通过等价性测试。
7. **先做 Experiment 1 与 go/no-go：** 扩展 CSV，完成最小 workload 扫描；若不足三个 pass 实例出现稳定 crossover，停止通用 adaptive 主线并转向更窄的 adaptive batching 或单 pass 问题。
8. **生成 oracle 标签：** 在选定目标帧上计算 greedy pass oracle 和 frame oracle，先量化 pass coupling，再确定 per-pass policy 是否成立。
9. **再做模型与在线策略：** 完成 Experiment 2 和 Experiment 3，比较候选数量阈值、查表和低维模型，禁止 post-execution leakage。
10. **主结果成立后：** 使用留出 terrain/trajectory 完成 Experiment 4；线程数扩展实验、第二 CPU、更多 DEM 和完整 hysteresis 实验按篇幅进入补充材料。
