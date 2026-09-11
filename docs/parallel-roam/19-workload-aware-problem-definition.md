# Workload-Aware ROAM：具体问题定义

> 历史定义（2026-09-12）：本页保留原阶段与在线决策问题。当前研究问题、贡献和待完成工作见[新定义页](../plans/formal_experiment/cpu_roam_research_definition.md)；现有实现契约仍须遵守，不由定位调整放宽正确性。

本文档定义研究所比较的 ROAM Update Pipeline、pass 边界、pass-specific 合法策略、workload 扫描维度和在线决策可用的特征。二维矩阵描述 workload 空间，不表示每个 pass 都有统一的四策略组合。

## Pass 定义

ROAM Update Pipeline 分为三类研究 pass，并包含三个共享阶段：

### 1. Decision Passes

- **Merge 评分**：更新 merge queue priority，筛选并形成 merge 候选。
- **Split 扫描/评分**：更新 split queue priority，完成 heap/候选扫描并形成 split 候选。

### 2. 拓扑修改 Pass

- **Merge 拓扑**：执行 merge 候选的排序、分块、提交、邻域/队列维护和串行收敛。
- **Split 拓扑**：执行 split 候选的提交、forced closure、budget crossover 和串行收敛。

### 3. State Maintenance Passes

- **网格提交**：将拓扑修改记录重放到 mesh slots，生成 dirty ranges，并准备 renderer 使用的 mesh 数据。

### Shared stages

- **Prepare**：处理输入、缓存、variance 和拓扑重置。
- **Finalize**：汇总 leaf、queue、统计和输出 packet。
- **CPU 上传**：将 mesh 或 dirty ranges 上传到 renderer。

### 执行顺序

~~~text
Prepare
  -> Merge 评分
  -> Merge 拓扑
  -> Split 扫描/评分
  -> Split 拓扑
  -> 网格提交
  -> Finalize
  -> CPU 上传
~~~

不同实现可以在相邻阶段之间合并计算，但策略切换的边界必须保持在上述 pass 内。一个 pass 的 parallel 版本必须包含该 pass 的 planning、提交、维护和串行尾部，不能只拿某个内部函数的耗时作为 crossover。

### 合法策略集合

| Pass | 主实验策略 | 不进入主实验的模式 |
|---|---|---|
| Merge 评分 | `SerialRefresh` / `ParallelRefresh` | 普通相机变化下不提供 incremental score refresh |
| Split 扫描/评分 | `SerialRefresh` / `ParallelRefresh` | 同上；持久 membership 不等于增量 score |
| Merge 拓扑 | `SerialImmediate` / `ParallelAssisted` | 不提供全量拓扑 |
| Split 拓扑 | `SerialImmediate` / `ParallelAssisted` | 不提供全量拓扑；forced closure 保持串行 |
| 网格提交 | `SerialDirty` / `ParallelDirty` / `SerialFull`；验证后再加入 `ParallelFull` | 不改变拓扑或 slot ownership |
| CPU 上传 | `DirtyRange` / `FullBuffer` | 不宣称并行上传调用 |

Prepare 和 Finalize 不选择策略，但必须计入 frame oracle 和 deadline feasibility。拓扑 pass 的 planning、chunk build、commit、结果合并、queue/index 维护和串行收敛必须整体计时；`ParallelAssisted` 不是只截取线程 kernel 的时间。

同一 pass 的候选策略必须从同一冻结输入开始，使用相同 score、threshold、候选资格、预算和 closure 规则，并产生相同 active cut/mesh 或满足预先定义的规范化等价条件。无法满足该条件的实现不能作为同一 crossover 点的候选策略。

## 二维 Workload Matrix

矩阵以六个研究 pass 为单位，并按 Decision、拓扑修改、State Maintenance 和 Shared stages 组织。Prepare 和 Finalize 只计入 frame budget，不建立策略矩阵。

### Decision Passes

| Pass | 横轴 | 纵轴 | 附加特征 |
|---|---|---|---|
| Merge 评分 | Q_m entry 数 | 有效线程数 | score/heapify 占比、merge pressure |
| Split 扫描/评分 | Q_s entry 数 | 有效线程数 | score/heapify 占比、split pressure |

两个评分 pass 都对当前 queue entries 执行 full score refresh，分别比较 serial/parallel 评分；主线程 heapify 和必要 planning 必须计入 pass 时间。主文固定一个线程配置时可以报告一维 queue-size crossover，线程数扩展实验放入补充材料，不为形式上的二维矩阵引入无意义变量。

### 拓扑修改 Pass

| Pass | 横轴 | 纵轴 | 附加特征 |
|---|---|---|---|
| Merge 拓扑 | merge 候选数量或 requested merge volume | independent interior ratio | non-empty chunks、imbalance、边界比例、closure demand |
| Split 拓扑 | split 候选数量或 requested split volume | independent interior ratio | non-empty chunks、imbalance、边界比例、closure demand、budget pressure |

拓扑 pass 的测量时间必须包含候选规划、chunk build、parallel commit、result merge、queue/index maintenance、forced closure 和 serial convergence。

### State Maintenance Pass

| Pass | 横轴 | 纵轴 | 附加特征 |
|---|---|---|---|
| 网格提交 | 活动三角形数量或 output bytes | dirty 三角形比例 / dirty byte ratio | dirty range 的数量、range length、slot fragmentation |

比较上述合法 mesh action；`ParallelFull` 只有在 `SerialFull` 和 `ParallelDirty` 分别通过等价性验证后才加入。

### Shared stage pass

| Pass | 横轴 | 纵轴 | 附加特征 |
|---|---|---|---|
| CPU 上传 | 上传字节数 | dirty-range ratio 或 range fragmentation | range 的数量、buffer capacity |

CPU 上传只比较 dirty-range/full-buffer。Prepare 和 Finalize 计入 frame budget，但不参与策略 winner matrix。

### Deadline-aware 标签

每个矩阵点同时保存 winner 与 deadline feasibility：

| 区域 | 定义 |
|---|---|
| action A optimal | A 更快且满足 deadline |
| action B optimal | B 更快且满足 deadline |
| tie / both feasible | 两者都满足 deadline，差异落在 tie/置信区间 |
| only A feasible | 只有 A 满足 deadline |
| only B feasible | 只有 B 满足 deadline |
| none feasible | 两者都超过 deadline，需要降低质量或工作量 |

主实验以 16.6 ms 作为参考 frame budget；单个 pass 的可行性使用扣除 Prepare、Finalize、渲染/上传等固定成本后的剩余预算，不能把“pass 小于 16.6 ms”直接写成整帧可行。10 ms 和 20 ms 只作敏感性分析。

## Workload feature 候选

feature 必须按照决策时序分类，否则模型会把执行结果泄漏到输入中。每个 feature 都要记录 available time 和获取成本。

feature 必须绑定具体 pass；同名的 `candidateCount` 也不能在 Merge 评分、Split 扫描/评分、Merge 拓扑和 Split 拓扑之间直接复用：

| Pass 分类 | 主要 workload feature |
|---|---|
| Decision Passes | queue entries、有效线程数量、上一帧 score/heapify 成本、split/merge pressure |
| 拓扑修改 Pass | requested edits、non-empty chunks、estimated imbalance、边界比例、closure demand、budget pressure |
| State Maintenance Passes | active leaves/三角形、dirty ratio、dirty range 数量、range fragmentation、output bytes |

### Pre-decision feature

在具体 pass dispatch 之前已经存在，或者由上一帧状态和输入直接得到：

- `queueSize`、`activeTriangleCount`、`candidateCount`（仅当候选集合已由前一阶段维护）；
- 当前 edit log 已知时的 `dirtyTriangleRatio`、`dirtyByteRatio`，以及上一帧拓扑 delta；
- `cameraDisplacement`、`cameraAngularVelocity`、split/merge pressure；
- `triangleBudgetSlack`、`frameBudgetSlack`、线程数量和 CPU 配置；
- pass 的固定元数据，例如数据类型、元素大小和已分配的 buffer capacity。

这些特征可以直接作为在线策略输入，但不能依赖当前策略已经执行后的结果。

### During-planning feature

在 dispatch 前通过一次轻量扫描、计数或分块规划得到，成本必须单独计时并纳入 adaptive overhead：

- `candidateCount`（若不是由前一阶段直接维护）；
- `chunkCount`、`nonEmptyChunkCount`；
- `estimatedChunkImbalance`、`estimatedIndependentRatio`；
- `estimatedBoundaryRatio`、`estimatedClosureDemand`；
- `estimatedDirtyRangeCount`、`estimatedRangeFragmentation`；
- 预计的输出字节数、任务粒度和同步次数。

planning feature 可以进入模型，但只能使用选择策略之前已经完成的轻量工作。不能为了给 parallel 模型提供输入而先执行完整的 parallel pass。

### Post-execution analysis feature

只能用于解释、训练标签、oracle 分析和模型误差诊断，不进入同一次在线决策：

- `actualClosureDepth`、`actualConvergenceIterations`；
- `actualWorkerUtilization`、`actualChunkImbalance`；
- `actualFallbackReason`、`actualSynchronizationTime`；
- 实际 dirty range 数、实际上传字节数和最终 pass wall time。

这些特征对于说明“为什么某个策略赢或输”很重要，但将其放入同一决策的输入会造成 post-execution leakage。模型训练应使用历史帧的 post-execution 数据生成标签，在线推理只使用 pre-decision 和 during-planning 特征。

最终实验结果必须同时给出三类 feature 的获取时间、额外开销和是否进入模型，证明策略是在决策前可实现的，而不是用事后统计重现最优答案。
