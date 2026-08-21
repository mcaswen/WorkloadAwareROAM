# Workload-Aware ROAM：具体问题定义

本文档定义研究所比较的 ROAM Update Pipeline、pass 边界、合法策略维度、二维 workload matrix 和在线决策可用的 workload feature。

## Pass 定义

ROAM Update Pipeline 分为三类研究 pass 和两个共享阶段：

### 1. Decision Passes

- **Merge mark**：更新 merge queue priority，筛选和形成 merge candidates。
- **Split scan/mark**：更新 split queue priority，完成 heap/候选扫描并形成 split candidates。

### 2. Topology Mutation Passes

- **Merge topology**：执行 merge candidate 的排序、分块、提交、邻域/队列维护和串行收敛。
- **Split topology**：执行 split candidate 的提交、forced closure、budget crossover 和串行收敛。

### 3. State Maintenance Passes

- **Mesh emit**：将 topology edits 重放到 mesh slots，生成 dirty ranges，并准备 renderer 使用的 mesh 数据。

### Shared stages

- **Prepare**：处理输入、缓存、variance 和 topology reset。
- **Finalize**：汇总 leaf、queue、统计和输出 packet。
- **CPU upload**：将 mesh 或 dirty ranges 上传到 renderer。

### 执行顺序

~~~text
Prepare
  -> Merge mark
  -> Merge topology
  -> Split scan/mark
  -> Split topology
  -> Mesh emit
  -> Finalize
  -> CPU upload
~~~

不同实现可以在相邻阶段之间合并计算，但策略切换的边界必须保持在上述 pass 内。一个 pass 的 parallel 版本必须包含该 pass 的 planning、提交、维护和串行尾部，不能只拿某个内部函数的耗时作为 crossover。

### 策略绑定

| Pass | Serial/parallel 维度 | Incremental/full 维度 | 研究角色 |
|---|---|---|---|
| Merge mark | serial vs parallel queue refresh/mark | incremental/deferred refresh vs full refresh，仅在候选语义等价时开放 | 核心 crossover pass |
| Merge topology | serial queue consumption vs parallel-assisted interior commit | 不单设 full topology mode | 核心 crossover pass |
| Split scan/mark | serial vs parallel queue refresh/mark | incremental/deferred scan vs full scan，仅在候选语义等价时开放 | 核心 crossover pass |
| Split topology | serial queue consumption vs parallel-assisted interior commit | 不单设 full topology mode | 核心 crossover pass |
| Mesh emit | serial vs parallel emit | dirty-slot incremental emit vs full active-mesh emit | 核心 crossover pass |
| CPU upload | upload dispatch 不强行设置并行模式 | dirty-range upload vs full-buffer upload | 共享阶段中的策略 pass |

Prepare 和 Finalize 不暴露策略维度，但必须计入 frame oracle 和 deadline feasibility。Merge topology 和 Split topology 的 candidate snapshot、chunk build、queue invalidation、parallel commit、result merge、queue/index refresh、forced closure 和 serial convergence 都属于对应 topology pass 的完整成本，不另立 pass。

同一 pass 的候选策略必须从同一冻结输入开始，使用相同 score、threshold、candidate eligibility、预算和 closure 规则，并产生相同 active cut/mesh 或满足预先定义的规范化等价条件。无法满足该条件的实现不能作为同一 crossover 点的候选策略。

## 二维 Workload Matrix

矩阵以六个研究 pass 为单位，并按 Decision、Topology Mutation、State Maintenance 和 Shared stages 组织。Prepare 和 Finalize 只计入 frame budget，不建立策略矩阵。

### Decision Passes

| Pass | 横轴 | 纵轴 | 附加特征 |
|---|---|---|---|
| Merge mark | Q_m entries 或 scanned entries | eligible merge ratio / changed eligibility | merge pressure、worker count |
| Split scan/mark | Q_s entries 或 scanned entries | eligible split ratio / changed priority | split pressure、worker count |

分别比较两个 pass 的合法 serial/parallel queue refresh/mark。incremental/deferred 与 full 只有在候选集合和 priority 语义等价时才加入。

### Topology Mutation Passes

| Pass | 横轴 | 纵轴 | 附加特征 |
|---|---|---|---|
| Merge topology | merge candidate count 或 requested merge volume | independent interior ratio | non-empty chunks、imbalance、boundary ratio、closure demand |
| Split topology | split candidate count 或 requested split volume | independent interior ratio | non-empty chunks、imbalance、boundary ratio、closure demand、budget pressure |

Topology pass 的测量时间必须包含 candidate planning、chunk build、parallel commit、result merge、queue/index maintenance、forced closure 和 serial convergence。

### State Maintenance Pass

| Pass | 横轴 | 纵轴 | 附加特征 |
|---|---|---|---|
| Mesh emit | active triangle count 或 output bytes | dirty triangle ratio / dirty byte ratio | dirty range count、range length、slot fragmentation |

比较 serial/parallel emit 以及 dirty-slot incremental/full active-mesh emit。

### Shared stage pass

| Pass | 横轴 | 纵轴 | 附加特征 |
|---|---|---|---|
| CPU upload | upload bytes | dirty-range ratio 或 range fragmentation | range count、buffer capacity |

CPU upload 的 dirty-range/full-buffer 是输出路径策略，serial/parallel 只在后端存在语义等价的 preparation 实现时开放。Prepare 和 Finalize 作为共享阶段计入 frame budget，不参与 winner matrix。

### Deadline-aware 标签

每个矩阵点同时保存 winner 与 deadline feasibility：

| 区域 | 定义 |
|---|---|
| serial optimal | serial 更快且满足 deadline |
| parallel optimal | parallel 更快且满足 deadline |
| both feasible | 两者都满足 deadline，差异处于 tie/置信区间 |
| only serial feasible | 只有 serial 满足 deadline |
| only parallel feasible | 只有 parallel 满足 deadline |
| none feasible | 两者都超过 deadline，需要降低质量或工作量 |

对于 incremental/full 比较，使用同样的 winner + feasibility 表达，不强行套用 serial/parallel 标签。主实验使用 16.6 ms frame deadline，10 ms 和 20 ms 作为敏感性分析；Prepare、Finalize 和 CPU upload 的时间必须计入剩余 frame budget。

## Workload feature 候选

feature 必须按照决策时序分类，否则模型会把执行结果泄漏到输入中。每个 feature 都要记录 available time 和获取成本。

feature 必须绑定具体 pass；同名的 candidateCount 也不能在 Merge mark、Split scan/mark、Merge topology 和 Split topology 之间直接复用：

| Pass 分类 | 主要 workload feature |
|---|---|
| Decision Passes | evaluated/scanned entries、view/priority delta、eligible ratio、split/merge pressure |
| Topology Mutation Passes | requested edits、non-empty chunks、estimated imbalance、boundary ratio、closure demand、budget pressure |
| State Maintenance Passes | active leaves/triangles、dirty ratio、dirty range count、range fragmentation、output bytes |

### Pre-decision feature

在具体 pass dispatch 之前已经存在，或者由上一帧状态和输入直接得到：

- `queueSize`、`activeTriangleCount`、`candidateCount`（候选集合已由前一阶段维护时）；
- `dirtyTriangleRatio`、`dirtyByteRatio`、上一帧 topology delta；
- `cameraDisplacement`、`cameraAngularVelocity`、split/merge pressure；
- `triangleBudgetSlack`、`frameBudgetSlack`、worker count 和 CPU 配置；
- pass 的固定元数据，例如数据类型、元素大小和已分配的 buffer capacity。

这些特征可以直接作为在线策略输入，但不能依赖当前策略已经执行后的结果。

### During-planning feature

在 dispatch 前通过一次轻量 scan、计数或分块规划得到，成本必须单独计时并纳入 adaptive overhead：

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
