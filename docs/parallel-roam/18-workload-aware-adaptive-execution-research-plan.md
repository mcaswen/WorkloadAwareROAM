# 研究假设与验证计划（v0.4）

> 暂定题目：面向动态不规则拓扑更新的 Workload-Aware ROAM：Pass 级 Execution Crossover 建模与自适应执行
> 初稿日期：2026-08-20；状态更新：2026-08-29
>
> 当前状态：Classic/DOD 双基线、阶段独立策略、固定轨迹重放、冻结拓扑配对、统一结果哈希和跨实现结果契约均已可运行。DOD 已具有持久双队列、并行优先级刷新、并行辅助分块提交、串行闭包收敛和增量网格提交。尚未实现的是胜负反转实验矩阵、离线最优参考和在线策略。

## 核心贡献定位

本研究考察一个具体问题：**同一条状态化 ROAM 更新流水线中，不同 pass 的合法实现何时发生性能胜负反转，能否只用决策前可获得的低成本特征在线选择实现。** 

预期贡献分为三层：

1. **Pass-level crossover characterization。** 分别测量评分、拓扑、网格提交和上传的合法策略，完整计入 planning、维护和串行尾部。
2. **可在线使用的决策模型。** 比较单阈值、二维查表和低维可解释模型，明确特征获取时间和决策开销。
3. **端到端自适应验证。** 在拓扑、预算和 mesh 结果一致的前提下，与固定策略、greedy pass oracle 和 frame oracle 比较。

本文使用四个核心术语：`action` 是某个 pass 的一个具体实现；`crossover` 是两个 action 的性能胜负随 workload 发生反转；`oracle` 是离线重放后得到的最快合法选择；`regret` 是在线结果相对 oracle 多花的时间比例。

## 研究问题

### 领域知识中已经完成的工作

项目当前整理的论文和参考实现已经覆盖以下基础问题：

- ROAM 类算法建立了基于二叉三角树或 diamond 关系的动态 split/merge、优先级队列、局部相容约束和帧间相干更新；
- 动态 LOD 和 terrain rendering 研究已经证明，利用时间相干性复用上一帧状态、只更新变化区域，能够减少重复的网格生成和数据传输；
- 并行 LOD 与不规则并行算法研究已经讨论空间分块、任务粒度、同步、缓存和负载不均衡；
- 一般调度研究已经讨论串行实现与工作低效并行实现之间的运行时选择。

这些工作为动态拓扑、增量维护和并行调度提供了基础。当前尚未找到同时覆盖“状态化 ROAM、完整 pass 成本、异构合法策略集合和在线选择”的直接方案。

### 与已有并行地形和拓扑工作的关系

本文与已有工作的区别在于研究**多个合法执行模型之间的运行时选择问题**：

| 研究方向 | 主要问题 |
|---|---|
| GPU terrain / GPU LOD | 如何提高并行吞吐 |
| CBT / 并发拓扑结构 | 如何设计并发拓扑结构与内存系统 |
| Parallel terrain / ROAM-like refinement | 如何划分和分配细分工作 |
| 本文 Workload-Aware ROAM | 多个合法实现都存在时，何时选择哪一个 |

已有 GPU terrain 工作强调吞吐和硬件并发，CBT 类工作强调并发拓扑结构和容量管理，Parallel ROAM/LOD 工作强调细分任务分配；本文假定这些执行模型已经存在，进一步研究它们在不同拓扑变化 workload 和 deadline 约束下的选择边界。

### 本文需要验证的缺口

本文验证当前知识索引中仍缺少直接答案的四个问题：

1. **不同 pass 是否真的具有不同 crossover。** 不能只报告整帧加速比，也不能把某个并行 kernel 的时间当作完整 pass 时间。
2. **候选数量是否足以做决策。** 拓扑还受安全 interior 比例、chunk 不均衡、边界/closure demand 和预算压力影响；输出阶段还受 dirty ratio 和 range fragmentation 影响。
3. **局部最优能否组成整帧最优。** 状态、队列和串行闭包会耦合相邻 pass，需要比较 greedy pass oracle 与 frame oracle。
4. **在线选择是否有净收益。** 特征提取、策略决策和切换成本必须计入，并在完整 terrain/trajectory 留出测试中验证。

### 本文拟研究的问题

本研究拟回答：**能否根据决策前可观测的 workload feature，从每个 pass 自己的合法策略集合中在线选择实现，使 CPU update 接近 frame oracle，同时保持拓扑、预算和 mesh 正确？**

具体问题为：

- **问题 1：Pass-specific crossover。** 六个 pass 实例中，哪些存在稳定的策略胜负反转，哪些始终由单一策略支配？
- **问题 2：Feature availability。** 只使用 pre-decision 和计入开销的 during-planning feature，能否比候选数量单阈值更准确地选择策略？
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

- **H1 Crossover：** 至少三个代表性 pass 实例存在可重复的策略胜负反转；若不足三个，停止“通用 pass-level adaptive”主张并转向更窄的问题。
- **H2 可决策特征：** 只使用 pre-decision 和计入开销的 during-planning feature，能够比候选数量单阈值降低留出 workload 上的 oracle regret。
- **H3 自适应净收益：** 计入特征、决策和切换成本后，adaptive 在混合轨迹上优于 fixed serial、maximum-safe-parallel 和 best static，并量化 greedy oracle 与 frame oracle 的差距。
- **H4 正确性与泛化：** 配对策略满足统一结果契约；模型在留出 terrain 或完整 trajectory 上仍保持收益，拓扑错误与预算违规为零。

## 策略定义

策略接口、合法策略集合、基线、oracle 和消融对照见[策略定义](20-workload-aware-strategy-definition.md)。

## 实验设计

二维 crossover、预测模型、adaptive 对照、泛化实验、规模边界和验证标准见[实验设计](21-workload-aware-experiment-design.md)。

## 预期论文结果结构

1. 与 GPU terrain、CBT 和 Parallel ROAM/LOD 的问题边界；
2. pass 的功能分类、执行顺序和 pass-specific valid strategy set；
3. Experiment 1：pass-specific crossover 与 deadline feasibility；
4. Experiment 2：pre-decision/during-planning feature model；
5. Experiment 3：adaptive、fixed baselines、greedy oracle 与 frame oracle；
6. Experiment 4：留出 terrain/trajectory 泛化；
7. 正确性、feature/decision 开销、失败区间和外部有效性讨论；
8. 线程数、第二 CPU 和 hysteresis 作为补充结果。

## 参考

- [ROAMing Terrain: Real-time Optimally Adapting Meshes, 1997](https://doi.org/10.1109/VISUAL.1997.663860)
- [Concurrent Binary Trees (with application to longest edge bisection), 2020](https://doi.org/10.1145/3406186)
- [Concurrent Binary Trees for Large-Scale Game Components, 2024](https://doi.org/10.1145/3675371)
- [Tile-based Level of Detail for the Parallel Age, 2007](https://doi.org/10.1109/TVCG.2007.70587)
- [Hardware-Based Adaptive Terrain Mesh Using Temporal Coherence, 2019](https://doi.org/10.3390/su11072137)
- [Scheduling Jobs with Work-Inefficient Parallel Solutions, SPAA 2024](https://arxiv.org/abs/2405.11986)
