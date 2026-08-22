# Workload-Aware ROAM：源码改造规划

本文档把研究计划落实到 Classic/DOD 源码。目标不是把所有 pass 强行改成四种组合，而是先确认每个 pass 当前的执行形态，再为具有语义等价性的维度增加显式策略开关和配对实验。

## 改造原则

1. **先区分 pass 与 pass 内部子阶段。** 一个拓扑 pass 只要包含并行预提交、主线程队列维护和串行 closure/convergence，就只能称为 parallel-assisted 拓扑 pass，不能称为 fully parallel。
2. **Serial/parallel 和 incremental/full 不做全局笛卡尔积。** 每个 pass 声明自己的合法策略集合；不具备语义等价实现的维度不暴露开关。
3. **先保证同一冻结输入和结果契约，再比较时间。** 策略切换不能改变 score、候选资格、预算、forced closure、队列不变量或最终 active cut；允许不同合法顺序时必须比较规范化拓扑/mesh hash。
4. **Classic 作为外部端到端基线，DOD serial 作为 DOD 内部 pass 对照。** Classic 的对象式数据结构和 DOD 的 SoA 数据结构不同，不能把 Classic 与 DOD 的同名计时直接解释成同一实现的 serial/parallel 消融。
5. **先做可观测性和 replay，再做 adaptive。** 没有每个 pass 的 requested/effective action、workload feature、fallback reason 和结果 hash，后续 crossover 不能作为可靠证据。

## 当前源码执行判定

源码入口是 [`DataOrientedRoamPipeline::BuildInternal`](../../src/algorithms/data_oriented_roam/DataOrientedRoamPipeline.cpp)：先 merge，再 split，随后增量网格提交；CPU 上传位于 renderer。Classic 入口是 [`ClassicRoamMeshBuilder::Build`](../../src/algorithms/classic_roam/ClassicRoamMeshBuilder.cpp)。

| 研究 pass | 当前执行形态 | 主线合法 action | 优先级 |
|---|---|---|---|
| Merge 评分 | 刷新 Q_m 全部当前 entry 的 score；评分可并行，heapify 串行；membership 由拓扑局部维护 | `SerialRefresh` / `ParallelRefresh`，两者均为 full score refresh | P0 |
| Split 扫描/评分 | 刷新 Q_s 全部当前 entry 的 score；评分可并行，heapify/snapshot/chunk build 串行 | `SerialRefresh` / `ParallelRefresh`；snapshot 是否执行单独记录 | P0 |
| Merge 拓扑 | 安全 interior 候选可并行预提交；结果合并、active index/Q_m 更新和 convergence 在主线程 | `SerialImmediate` / `ParallelAssisted` | P1 |
| Split 拓扑 | 安全且可复用 child 的候选可并行预提交；预算、forced closure 和 convergence 在主线程 | `SerialImmediate` / `ParallelAssisted` | P1 |
| 网格提交 | 拓扑修改 replay 和 slot/range 维护串行；dirty slot 写入达到阈值时可并行 | `SerialDirty` / `ParallelDirty` / `SerialFull`；验证后再加入 `ParallelFull` | P1 |
| CPU 上传 | renderer 在 full buffer 与 dirty ranges 之间选择；API 仍由渲染线程提交 | `DirtyRange` / `FullBuffer` | P1 |

以下统计字段不是独立策略 pass：budget leaf collect 和 final leaf collect 当前为 0 ms；预算直接使用持久活动视图，最终输出直接复用 active leaf/slot view。独立 Error Eval 也为 0，屏幕误差计算属于 Q_s/Q_m refresh。它们只记录来源、数量和正确性，不增加 action。

Classic 作为外部实现基线：Q_s/Q_m score refresh、拓扑 transaction 和网格提交主要串行；它不作为 DOD 内部 serial/parallel 的配对 kernel。

## 必须先修正的源码语义

### 1. 拆开 `EnableParallelSplit` 的含义

当前 `TerrainLodSettings::EnableParallelSplit` 传入 `DataOrientedRoamSettings::EnableParallelSplit`，但它只控制 `RefineWithSplitQueue` 是否执行候选 snapshot、chunk build 和 parallel split pre-commit；`RefreshPersistentSplitQueuePriorities` 仍根据 `ErrorEvaluationWorkerCount` 自动并行。因此当前开关不能作为“Split pass 串行/并行”的实验标签。

改造为 pass-specific policy：

```text
MergeMarkExecution = Serial | Parallel
SplitMarkExecution = Serial | Parallel
MergeTopologyExecution = Serial | ParallelAssisted
SplitTopologyExecution = Serial | ParallelAssisted
MeshEmitExecution = Serial | Parallel
    MeshEmitAction = SerialDirty | ParallelDirty | SerialFull | ParallelFull
    CpuUploadAction = DirtyRange | FullBuffer
```

`EnableParallelSplit` 在兼容期可映射为 `SplitTopologyExecution`，但 benchmark 和源码统计必须改用新的 requested/effective 字段。

### 2. 不再用一个线程参数控制所有 pass

当前 `ErrorEvaluationWorkerCount` 同时影响 Q_s/Q_m priority refresh、拓扑 commit 和网格提交。它适合作为旧 benchmark 的线程上限，但不能支持 pass-level crossover。增加 pass-specific action 和线程设置，并记录 requested/effective action 与 fallback reason；保留旧字段只用于兼容旧命令。

### 3. 把环境变量诊断开关迁移到显式策略

`PARALLEL_ROAM_DOD_PARALLEL_COMMIT_PHASE`、`PARALLEL_ROAM_DOD_PARALLEL_COMMIT_BUILD` 和并行候选阈值目前位于 `DataOrientedRoamTopology.cpp`，适合临时诊断，不适合作为论文实验的唯一控制面。最终 benchmark 应通过 settings/命令行写入 action，并把解析后的 effective action 输出到 CSV；环境变量只保留为调试覆盖。

## 分阶段改造计划

### Phase 0：冻结当前实现并建立 pass 证据

**状态：已完成（2026-08-22）**

目标是先回答“当前到底是什么”，不改变算法结果。

1. 在 DOD `BuildInternal`、Classic `Build` 和 renderer 上传周围保存统一 `PassTrace`：pass id、requested/effective action、线程数量、候选数量、dirty 数量、fallback reason、wall time。
2. 将已有字段重新标注来源：`CpuMergeCandidateMarkMilliseconds` 对应 Merge 评分；`CpuSplitCandidateMarkMilliseconds` 对应 Split 扫描/评分；六段拓扑字段只计入对应拓扑 pass；`CpuMeshEmitMilliseconds` 不包含上传。
3. 输出三类事实：`membershipUpdate=Incremental`、`priorityRefresh=Full/AllCurrentEntries`、mesh/上传的增量或全量状态，避免把 queue membership 的增量维护误写成 score refresh 的增量。
4. 为 Classic 和 DOD 每个目标帧保存拓扑哈希、active leaf hash、mesh hash、budget、queue invariant 和 validation counters。
5. 为目标帧提供“保存状态”或“由固定 trajectory 确定性重建状态”中的至少一种 replay 入口；没有 replay 的 pass 不进入配对性能结论。

Phase 0 的结果是当前实现状态表和回归基线，不引入 adaptive。

当前实现结果：

- Classic、DOD 和渲染器共用固定大小的 `TerrainLodPassTrace`，记录请求/实际方式、线程数量、候选数量、脏数据数量、回退原因和包络耗时
- Q_s/Q_m 明确输出 `membershipUpdate=incremental` 与 `priorityRefresh=fullAllCurrentEntries`，网格和上传单独输出增量、全量或混合状态
- 研究基准按需计算重放输入、拓扑、活动叶和网格哈希，并保存预算、队列不变量、验证计数与证据采集耗时
- `pass-trace-replay` 在算法 `Reset` 后确定性重建同一固定轨迹，逐帧比较上述输入和结果哈希
- 证据全量扫描只在基准测试中开启，普通交互帧不承担队列检查与完整网格哈希成本
- 阶段验收分别运行默认 600 点路径与预算饱和 64 点压力路径，压力路径可通过 `--runtime-benchmark-path budget-saturation` 自动选择
- 2026-08-22 的阶段 0 验收中，两种算法完整通过默认路径和压力路径；所有帧的预算越界、队列不变量错误、资源验证失败、非法邻接和 T 形裂缝数量均为零

### Phase 1：建立 pass policy 和可重复串行路径

1. 在 `DataOrientedRoamTypes.h` 增加 pass policy enum/struct；在 `TerrainLodSettings`、benchmark scenario 和 render packet 中传递它。
2. 将 Q_s/Q_m refresh 的线程解析改为显式读取 `MergeMarkAction`/`SplitMarkAction`；`SerialRefresh` 必须使用同一 score 函数、同一 queue members 和同一 heapify。
3. 将网格提交的线程解析改为读取 `MeshEmitAction`；先实现 `SerialDirty`，确保与当前并行 dirty 写入结果一致，再恢复 `ParallelDirty`。
4. 增加 `SerialFull`：以当前 `SlotOwners`/active leaf 为唯一输出集合，生成完整 update range；不重建拓扑、不改变 slot ownership。
5. 对 CPU 上传增加显式 `DirtyRange`/`FullBuffer` 请求，并记录因 buffer capacity 或 frame-slot backlog 触发的 effective fallback。

Phase 1 的验收是所有策略在固定输入下拓扑、预算和规范化 mesh 一致，且 serial mode 确实为单线程。

### Phase 2：固定 Decision pass 的真实语义

普通 camera/view 变化会使全部屏幕 score 失效，因此主线只实现 `FullScoreRefresh` 的 serial/parallel 对照，不把持久 queue membership 写成 incremental score。

1. Q_s/Q_m serial 与 parallel action 使用相同 entry 集合、score 函数和 heapify。
2. 记录 score entry 数量、score wall time、heapify wall time、snapshot wall time 和有效线程数量。
3. Q_s/Q_m membership 继续由拓扑 transaction 局部维护，并单独记录 membership update 数量/cost。
4. 候选 snapshot 属于拓扑 planning；若模型使用 snapshot 后才能获得的特征，必须把 snapshot 成本计入决策开销。
5. Incremental score refresh 不属于主重构。只有未来出现“view 不变且可证明部分 score 未失效”的独立研究场景时再立项，不能作为当前论文的必做 action。

### Phase 3：拆分拓扑修改的串行与并行辅助

1. 以 `RefineWithSplitQueue` 和 `MergeWithDiamondQueue` 为 pass 外壳，将候选 snapshot、chunk build、queue invalidation、parallel commit、result merge、index/queue refresh 和 serial convergence 统一包络计时。
2. `SerialCommit` 路径必须跳过 parallel chunk 线程，但仍执行同一候选资格、预算、forced closure、queue invariant 和 mesh edit 记录。
3. `ParallelAssistedCommit` 只提交 `SafeInterior*` 候选；`SplitWouldNeedForcedNeighbor`、不可复用 child、跨 chunk 边界、预算闭包和所有动态失败候选回到串行尾部。
4. 保留 `SerialTopologyCommitPolicy` 和 `ParallelTopologyCommitPolicy` 的共享事务逻辑；线程只写局部 counters/result，join 后由主线程更新 active indexes 和持久队列。
5. 不实现全量拓扑：拓扑修改是对持久状态的增量事务，重新遍历并重建完整拓扑会改变研究对象和执行语义。
6. 对同一冻结候选快照分别运行 serial 和 parallel-assisted；若合法 tie 顺序导致 active cut 不唯一，使用规范化等价条件，不用“最终三角形数量相同”替代拓扑等价。

### Phase 4：Classic 对照和统一结果契约

Classic 不能直接复用 DOD 的 chunk parallel implementation，但必须提供同一 pass trace：

| Classic 路径 | 研究映射 | 对照用途 |
|---|---|---|
| `RefreshPersistentQueuePriorities` 的 Q_m loop + heapify | Merge 评分 serial/full score refresh | DOD serial/parallel 评分的外部实现基线 |
| `RefreshPersistentQueuePriorities` 的 Q_s loop + heapify | Split 扫描/评分 serial/full score refresh | DOD serial/parallel 评分的外部实现基线 |
| `OptimizeWithPersistentDualQueues` 中 `MergeNodeOrDiamond` | Merge 拓扑 `SerialImmediate` | Classic 对象式拓扑参考；包含 dual-queue crossover 期间 merge |
| `OptimizeWithPersistentDualQueues` 中 `SplitNode`/forced closure | Split 拓扑 `SerialImmediate` | Classic 串行闭包和预算语义参考 |
| `ApplyIncrementalMeshUpdates` + `FinalizeIncrementalMeshUpdate` | 网格提交 `SerialDirty` | DOD dirty 提交的 serial reference |
| `TerrainRenderer::UploadMeshData` | CPU 上传 dirty/full | Classic/DOD 共享 renderer 上传对照 |

Classic 和 DOD 当前都已填充 `MeshFullRebuildCount`、`MeshUpdatedTriangleCount`、`MeshReusedTriangleCount` 和 `MeshDirtyRangeCount`。比较时排除首帧/reset 强制全量发布，并结合 update ranges 判断 dirty/full；不能只凭字段名或单个零值推断实现模式。

### Phase 5：基准和验证矩阵

实验顺序固定为：

1. **当前实现基线：** Classic；DOD default；DOD `EnableParallelSplit=false`；记录真实 effective action，特别验证该开关并未关闭 Q_s parallel refresh。
2. **Decision pass crossover：** DOD `SerialRefresh/ParallelRefresh`，只比较 FullScoreRefresh；Classic 作为串行外部基线。
3. **拓扑 crossover：** DOD `SerialImmediate/ParallelAssisted`；分别扫描候选数量、non-empty chunk、边界比例和 closure demand。
4. **Mesh crossover：** Classic/DOD 各自 `SerialDirty`、`ParallelDirty`、`SerialFull`；只有实现并验证后才加入 `ParallelFull`。
5. **上传 crossover：** 同一 mesh packet 分别执行 dirty-range 和 full-buffer 上传。先用已验证的 OpenGL 后端记录 bytes、range 的数量、capacity fallback 和 wall time；D3D12 重新通过构建与 smoke 后再做交叉检查。
6. **端到端对照：** Classic、DOD fixed serial、DOD maximum-safe-parallel、DOD best static、greedy pass oracle、frame oracle、adaptive。

每个点固定 height map、camera/view、threshold、三角形预算、max depth、线程配置和冻结输入；先做 warm-up，再交替运行策略。每个策略都检查：拓扑哈希、active leaf hash、mesh hash、三角形预算、persistent queue invariant、crack/T-junction/invalid neighbor 和无效拓扑。

## 预计文件改造范围

| 文件 | 改造内容 |
|---|---|
| `src/algorithms/ITerrainLodAlgorithm.h` | 增加 pass policy、策略有效性和 trace 所需公共设置/结果字段；保留旧 `EnableParallelSplit` 兼容映射 |
| `src/algorithms/data_oriented_roam/DataOrientedRoamTypes.h` | DOD pass action、effective action、fallback 和 feature/trace 字段 |
| `DataOrientedRoamTerrainLodAlgorithm.cpp` | 公共设置到 DOD policy 的映射，禁止一个线程字段控制所有 pass |
| `DataOrientedRoamQueues.cpp` | Q_s/Q_m serial/parallel full score refresh 和评分计时 |
| `DataOrientedRoamTopology.cpp` | 拓扑 mode 显式选择、parallel-assisted 包络、serial tail 和候选冻结 replay |
| `DataOrientedRoamMeshEmit.cpp` | serial/parallel dirty 提交、full active-mesh 提交、update range 统计 |
| `ClassicRoamQueues.cpp` / `ClassicRoamMeshEmit.cpp` | 输出同一 pass trace；增加 full 提交对照，不改 Classic 默认路径 |
| `TerrainRenderer.cpp` / `D3D12TerrainRenderer.cpp` | dirty/full 上传 policy、capacity fallback、上传 bytes/ranges 统计 |
| `src/benchmark/TerrainLodBenchmark.cpp` / `src/app/RuntimeBenchmark.cpp` | action 参数、effective action、feature、trace、hash 和配对实验输出 |
| `tests/` | frozen-state replay、合法 action 等价性和 Classic/DOD contract tests |

## 完成判据

- 能明确回答每个 pass 当前是 serial、parallel-assisted 还是 fully parallel；当前源码中不存在 fully parallel 拓扑 pass。
- 能明确回答每个 pass 当前是 incremental、full refresh 还是两者都不是；当前 Q_s/Q_m membership 和 mesh/拓扑修改是增量，priority score 默认全 refresh，budget/final leaf/error eval 不是独立 pass。
- 每个可切换 action 都有 requested/effective 记录，fallback 不会静默改变实验标签。
- Classic 和 DOD 在同一输入、同一结果契约下可以进行 pass trace 和端到端对照。
- 任何性能结论都同时给出 pass wall time、线程/chunk/dirty 特征、CPU 上传成本和 correctness 结果。

## 可选 Phase 6：FrontierMaintenance 一致性屏障

本阶段是主重构、正确性验证和核心 crossover 实验完成后的可选实现，不属于前述完成判据，也不阻塞论文主线。只有现有 pass 实验表明 active index 和持久队列的局部维护成本在高变化率 workload 中成为稳定瓶颈时，才进入本阶段。

`FrontierMaintenance` 不是放在整帧 Merge/Split 拓扑之后的统一延迟 pass。`ActiveLeafNodes`、`ActiveInternalNodes`、Q_s/Q_m membership 和剩余 split budget 会被同一 Build 内的串行 closure、双队列 crossover 和后续评分立即消费；若在这些消费者运行后才恢复 frontier，会破坏预算、队列完整性和同帧连续 merge/split 语义。

合法边界只位于冻结候选 batch 完成之后、任何 frontier/queue 消费之前：

```text
候选 snapshot
  -> SerialBatchCommit 或 ParallelBatchCommit
  -> FrontierMaintenance 一致性屏障
  -> Budget synchronization
  -> 原有 SerialClosure / SerialConvergence
  -> 网格提交
```

batch commit 到一致性屏障完成之间属于 frontier dirty 区间，禁止读取 Q_s、Q_m、`ActiveLeafNodes` 和 `ActiveInternalNodes`。串行 forced closure 与 queue-driven convergence 继续使用当前即时增量维护，不为接口统一而强制生成 edit log 或延迟索引更新。

### 可选策略

| 策略 | 语义 | 使用位置 |
|---|---|---|
| `ImmediateIncremental` | 每次串行拓扑事务立即更新 active indexes、Q_s/Q_m 和预算相关状态 | 当前默认路径与串行收敛基线 |
| `BatchDelta` | batch commit 只修改权威 node 拓扑并记录紧凑 `TopologyEditLog`；屏障阶段按 edit 局部更新 frontier 和队列邻域 | frozen serial/parallel batch |
| `BatchFullRebuild` | 使用与 `BatchDelta` 相同的已提交拓扑，从两个 root 重建 active indexes、membership 和 Q_s/Q_m，再统一 heapify | 高变化率实验对照 |

首轮不实现 `ParallelIncremental`。局部 membership、反向位置和 heap 修复具有写冲突；只有 profiler 证明 `BatchDelta` 本身成为主要瓶颈，且 edit 能形成确定的无冲突分区时才增加该策略。

### 正确性契约

1. `Nodes` 中的 split 状态、parent/child 和 neighbor 是权威拓扑；两种 batch maintenance 必须从同一提交结果开始。
2. 屏障后 active leaf/internal 集合、Q_s/Q_m membership、三角形预算、blocked-build 状态和 queue invariant 必须一致。
3. queue comparator 必须以 score 加稳定 `PathId`/canonical representative 形成全序，不能让 vector 插入顺序或线程完成顺序改变预算边界的消费结果。
4. full rebuild 只能重建派生索引，不能清除 `SplitQueueBlockedBuildIds`、merge/activation build id、hysteresis 或其他当前 Build 状态。
5. active/mesh 顺序允许不同时，使用按 `PathId` 规范化的拓扑、active leaf 和 mesh hash；不能只比较三角形数量。
6. mesh edit 必须从同一 `TopologyEditLog` 派生，禁止旧 index-transition 路径与新 maintenance 路径重复记录 split/merge。

### 成本与进入条件

`BatchDelta` 的主要成本近似为拓扑修改数量乘局部邻域和 heap 修复成本；`BatchFullRebuild` 的主要成本是活动拓扑遍历、全量 membership/score 生成和线性 heapify。低变化率下 full rebuild 预期必然更慢；只有 edit 数量、邻域失效和随机 heap 更新接近活动拓扑规模时，连续全量重建才可能出现 crossover。

实现时复用当前 parallel commit 的线程 join、`CommittedSplit`/`CommittedMerge` 结果和跨帧保留容量，避免额外逐事务分配。默认 `ImmediateIncremental` 不创建新日志，`BatchFullRebuild` 只在显式 benchmark/policy 请求下执行。若配对实验中 full rebuild 在所有有效 workload 上均稳定劣于 delta maintenance，停止本阶段并删除生产路径开关，只保留实验结论。
