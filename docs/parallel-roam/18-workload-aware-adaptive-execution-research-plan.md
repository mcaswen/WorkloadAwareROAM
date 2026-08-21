# 研究假设与验证计划（v0.3）

> 暂定题目：面向动态不规则拓扑更新的 Workload-Aware ROAM：Pass 级 Execution Crossover 建模与自适应执行
> 初稿日期：2026-08-20；状态更新：2026-08-21
>
> 当前状态：Classic/DOD 双基线及统一 benchmark 已可运行；DOD 已具有持久双队列、并行优先级刷新、chunk topology commit、串行闭包收敛和增量 mesh emit，但各 pass 的策略开关、全量对照路径、workload 特征记录、离线 oracle 和在线自适应策略尚未实施。

## 核心贡献定位

本研究的核心贡献是：**提出一种面向动态地形拓扑细分的工作负载自适应执行，通过分析不同 topology update pass 的执行交叉边界，建立工作负载特征与执行策略之间的映射，并在实时约束下实现接近 oracle 的自适应策略选择。**

预期贡献分为三层：

1. **Pass-level crossover characterization。** 系统分析动态二叉三角树细分中不同 topology update pass 的 execution crossover，区分并行相对收益、串行尾部和实时不可行区。
2. **Workload-to-strategy model。** 建立决策前可观测 workload feature 到 pass-specific execution strategy 的映射模型，解释固定串行、固定并行和固定更新方式的适用边界。
3. **Adaptive execution framework。** 提出 pass 级自适应拓扑执行框架，在保证语义等价、拓扑正确和 frame deadline 的前提下接近 frame-level oracle，并在混合或动态 workload 上优于固定策略。

## 研究问题

### 领域知识中已经完成的工作

在本文建立的知识索引范围内，相关研究已经解决了动态层次化网格的若干基础问题：

- ROAM 类算法建立了基于二叉三角树或 diamond 关系的动态 split/merge、优先级队列、局部相容约束和帧间相干更新；
- 动态 LOD 和 terrain rendering 研究已经证明，利用时间相干性复用上一帧状态、只更新变化区域，能够减少重复的网格生成和数据传输；
- 并行 LOD 研究已经覆盖 tile、空间分块、CPU/GPU/cluster 资源映射、任务划分和负载均衡；
- 不规则并行算法研究已经总结了任务粒度、同步、调度、缓存和负载不均衡带来的并行开销；
- 一般并行运行时和调度理论已经讨论过：当任务具有串行实现和工作低效但可并行实现时，调度器需要根据任务规模或运行状态选择执行方式。

这些工作说明动态拓扑、增量维护和并行资源利用各自具有成熟基础，但研究粒度主要是完整算法、空间 tile、硬件阶段或抽象任务，并没有直接给出本文所需的 pass-level execution crossover 定义。

### 与已有并行地形和拓扑工作的关系

本文与已有工作的区别在于研究**多个合法执行模型之间的运行时选择问题**：

| 研究方向 | 主要问题 |
|---|---|
| GPU terrain / GPU LOD | How to maximize parallel throughput? |
| CBT / concurrent topology structures | How to design a concurrent topology representation and memory system? |
| Parallel terrain / ROAM-like refinement | How to distribute refinement workload? |
| 本文 Workload-Aware ROAM | Given multiple valid execution models, when should each model be selected? |

已有 GPU terrain 工作强调吞吐和硬件并发，CBT 类工作强调并发拓扑结构和容量管理，Parallel ROAM/LOD 工作强调细分任务分配；本文假定这些执行模型已经存在，进一步研究它们在不同拓扑变化 workload 和 deadline 约束下的选择边界。

### 现有知识尚未解决的问题

在上述知识基础上，本文关注的研究空白不是“ROAM 能否并行”，而是以下尚未被系统回答的问题：

1. **缺少 pass 级 crossover characterization。** 现有结果通常报告完整 terrain/LOD pipeline 的加速比，或报告某个 GPU/CPU 分块方案的总体吞吐，缺少对 Decision、Topology Mutation 和 State Maintenance 中具体 pass 分别测量串行/并行及合法增量/全量策略的交叉点。
2. **缺少动态拓扑 workload 的统一特征空间。** 候选数量之外，拓扑变化还包含有效独立度、chunk 不均衡、interior/boundary 比例、强制闭包深度、dirty ratio 和预算压力；现有知识没有形成这些特征到执行策略的可验证映射。
3. **缺少完整 pass 成本分析。** 时间相干研究关注增量复用，并行研究关注任务划分，但较少把 candidate preparation、parallel commit、queue maintenance 和 serial closure tail 共同计入 Merge topology / Split topology 的 crossover。
4. **缺少状态化 topology pipeline 的在线策略。** 一般串并行调度理论的任务通常可以抽象为独立 job；动态二叉三角树的 pass 共享队列、拓扑、缓存和闭包状态，局部决策是否能接近整帧 oracle 尚未明确。
5. **缺少实时可行域分析。** 并行版本可能相对更快，但仍可能因为固定同步成本、串行尾部、内存占用或 frame deadline 而不可用。现有研究很少同时报告相对 crossover 和绝对可行性边界。
6. **缺少跨 workload 的自适应验证。** 固定并行、固定串行或固定 dirty update 策略可能只在某一类地形和相机轨迹上成立，尚缺少按完整 workload 划分的留出验证和相对 offline oracle 的 regret 分析。

### 本文拟研究的问题

本研究拟回答：**在上述 ROAM Update Pipeline 中，能否根据决策前可观测的 workload feature，预测六个研究 pass 的合法执行策略 crossover，并在线选择策略，使 CPU update 接近 frame-level oracle，同时保持拓扑、预算和 mesh 正确？**

具体问题为：

- **问题 1：Pass-specific crossover。** Merge mark、Merge topology、Split scan/mark、Split topology、Mesh emit 和 CPU upload 是否具有显著不同的 crossover？
- **问题 2：Feature availability。** 只使用 pre-decision 和计入开销的 during-planning feature，能否比 candidate-count 单阈值更准确地选择策略？
- **问题 3：Pass coupling。** Greedy pass oracle 与按上述执行顺序重放的 frame oracle 差距多大？
- **问题 4：Adaptive execution。** 在线策略能否在混合 workload 和 frame deadline 下接近 oracle，并优于 fixed serial、maximum-safe-parallel 和 best static？

形式上，对 pass p、workload feature x 和合法策略 a ∈ A_p，记录：

~~~text
T(p, a, x) = pass p 在策略 a 下的墙钟时间
a*(p, x) = argmin T(p, a, x)
~~~

A_p 是每个 pass 自己的合法策略集合。Offline oracle 必须从同一冻结状态执行 A_p 中的策略；任何改变候选集合、priority 语义、closure、预算或最终拓扑的路径都不能作为同一 pass 的 crossover 候选。

## 具体问题定义

Pass 边界、执行顺序、二维 workload matrix 和三类 feature 的完整定义见[具体问题定义](19-workload-aware-problem-definition.md)。

## 可证伪假设

- **H1 Crossover：** Merge mark、Merge topology、Split scan/mark、Split topology、Mesh emit 和 CPU upload 中至少三个存在可重复、统计稳定且彼此不同的合法策略 crossover；使用一个全局 worker 设置或统一候选阈值会在部分 workload 区间产生可测量退化。
- **H2 可决策特征：** 只使用 pre-decision 和计入开销的 during-planning feature，多特征模型仍能比单一 candidate-count threshold 更准确地选择策略并降低 oracle regret；post-execution feature 只增强解释力，不是在线决策成立的必要条件。
- **H3 Pass-specific update mode：** Mesh emit 的 dirty/full output path 存在 crossover；Decision Passes 的 deferred/full 只有在候选和 score 语义等价后才作为附加假设检验，Topology Mutation Passes 不强行设置 full mode。
- **H4 Pass 解耦：** greedy pass oracle 与 frame-level oracle 的差距足够小，说明各 pass 在策略选择上近似解耦；若差距显著，则需要 frame-level coordinated policy。
- **H5 固定策略局限：** fixed serial 与 maximum-safe-parallel 都不能在全部 workload 区间占优；在包含静止、小幅移动、快速转向和预算重入的混合轨迹中，至少一种固定策略会出现显著的平均、P95 或 deadline miss 退化。
- **H6 Deadline-aware 自适应性：** adaptive framework 在混合 workload 上能够以低决策开销接近 frame oracle，降低 16.6 ms deadline miss rate，并优于 fixed serial、maximum-safe-parallel 和最佳固定策略。
- **H7 正确性与泛化：** 等价策略产生一致拓扑、mesh 和预算结果；由训练 workload 标定的模型在留出 terrain/trajectory 上仍保持低 regret，且拓扑错误与预算违规为零。

## 策略定义

策略接口、合法策略集合、基线、oracle 和消融对照见[策略定义](20-workload-aware-strategy-definition.md)。

## 实验设计

二维 crossover、预测模型、adaptive 对照、泛化实验、规模边界和验证标准见[实验设计](21-workload-aware-experiment-design.md)。

## 预期论文结果结构

1. 与 GPU terrain、CBT 和 Parallel ROAM/LOD 的问题边界；
2. pass 的功能分类、执行顺序和 pass-specific valid strategy set；
3. Experiment 1：deadline-aware 二维 crossover matrix；
4. Experiment 2：pre-decision/during-planning feature model；
5. Experiment 3：adaptive、fixed baselines、greedy oracle 与 frame oracle；
6. Experiment 4：留出 terrain/trajectory 泛化；
7. 正确性、feature/decision 开销、失败区间和外部有效性讨论；
8. worker 数、第二 CPU、更多策略组合和 hysteresis 作为补充结果。

## 参考

- [ROAMing Terrain: Real-time Optimally Adapting Meshes, 1997](https://doi.org/10.1109/VISUAL.1997.663860)
- [Concurrent Binary Trees (with application to longest edge bisection), 2020](https://doi.org/10.1145/3406186)
- [Concurrent Binary Trees for Large-Scale Game Components, 2024](https://doi.org/10.1145/3675371)
- [Tile-based Level of Detail for the Parallel Age, 2007](https://doi.org/10.1109/TVCG.2007.70587)
- [Hardware-Based Adaptive Terrain Mesh Using Temporal Coherence, 2019](https://doi.org/10.3390/su11072137)
- [Scheduling Jobs with Work-Inefficient Parallel Solutions, SPAA 2024](https://arxiv.org/abs/2405.11986)
