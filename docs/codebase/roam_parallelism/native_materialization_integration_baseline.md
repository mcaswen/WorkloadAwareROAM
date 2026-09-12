# 原生 DOD 目标物化接入边界事实

> 日期：2026-09-13；源码基线：`602ed48`；范围：细分拓扑阶段、续接状态及 CPU 网格消费接口。
> 本文是接入边界的定向事实记录，不代替整个 DOD 模块的完整架构清单。未重新审计图形上传实现或全部历史实验。

## 1. 来源与范围

已阅读[开发规范](../../standards/development_guidelines.md)、[规划规范](../../plans/plan_guideline.md)、[原型大规划](../../plans/roam_parallelism/target_materialization_prototype_plan.md)、MPR-01～03 小规划与结果，以及[五阶段事实](../formal_experiment/cpu_pass_boundary_and_workload_baseline.md)。历史状态以当前源码为准。

主要命名空间为 `ParallelRoam::Algorithms::DataOrientedRoam`。下文源码相对仓库根目录；表中同名前缀文件均位于 `src/algorithms/data_oriented_roam/`，除非另行标明。

| 当前文件 / 类型 | 本次核对的职责与关系 |
| --- | --- |
| `DataOrientedRoamPipeline.h/.cpp` | 独占状态与线程池；准备、五阶段执行、验证、帧尾历史维护；输出借用的 CPU 网格 |
| `DataOrientedRoamPassExecution.h/.cpp` | 准备帧及真实五阶段入口；观察器在阶段执行之前同步调用 |
| `DataOrientedRoamTopology.h/.cpp` | 严格串行根请求、强制递归、预算交换、原并行辅助、活动索引与队列维护；当前同时承担多项修改职责 |
| `DataOrientedRoamTopologyPlan.h/.cpp` | 当前安全连续前缀与分块候选计划；不是动态严格目标求解器 |
| `DataOrientedRoamTopologyExperiment.h` | 串行推进和操作观察等实验入口；实验观察不构成新的生产状态 |
| `DataOrientedRoamState.h/.cpp`、`StateOps.h` | SoA 节点池、长期索引/队列/网格、节点创建与重置；稳定物理下标 |
| `DataOrientedRoamQueues.h/.cpp` | 两个持久堆、反向位置、局部资格复核、整批评分和建堆 |
| `DataOrientedRoamScoring.h/.cpp`、`Variance.h` | 域二分、评分、迟滞、调试分类与误差树查询；调用共享 ROAM 几何/屏幕误差函数 |
| `DataOrientedRoamMeshState.h`、`MeshPlan.h/.cpp` | 槽位归属、脏集合、逐操作日志及日志消费；不执行图形 API |
| `DataOrientedRoamMeshEmit.h/.cpp` | 记录网格操作、计划槽位、生成 CPU 顶点/索引、提交范围收尾 |
| `DataOrientedRoamParallel.h`、`ThreadPool.h/.cpp` | DOD 已有同步线程执行能力；流水线持有线程池，状态只借用地址 |
| `DataOrientedRoamValidation.h/.cpp`、`PassInput.h/.cpp`、`PassEvidence.h/.cpp` | 状态不变量、完整阶段输入身份及实验结果证据；可遍历完整状态，不是免费热路径能力 |
| `DataOrientedRoamTerrainLodAlgorithm.h/.cpp`、`Types.h` | 公共参数适配、DOD 统计、CPU 网格包与借用生命周期 |
| `src/algorithms/ITerrainLodAlgorithm.h`、`TerrainLodPassTrace.h` | 公共设置、阶段策略、请求/实际动作及哈希；算法标识仍为 Classic 与 DOD |
| `src/experiment/roam_materialization/*` | 独立原型状态、直接补丁、验证器及 DOD 来源桥接；生产 DOD 当前不依赖它 |
| `src/gui/ImGuiLayer.*`、`src/app/Application.cpp`、`RuntimeBenchmarkConfig.h` | 面板、设置传递及运行时命令行覆盖 |
| `src/benchmark/TerrainLodBenchmark*`、`src/experiment/TerrainLodExperimentCsv.*` | 无窗口配置、实验编排和公共记录 |
| `src/render/TerrainRenderer.h/.cpp`、`D3D12TerrainRenderer.cpp` | 各后端向同一 LOD 设置传递 `RoamPassPolicy`，消费 CPU 网格包 |

## 2. 真实阶段控制流

**FACT**：`ExecuteDataOrientedRoamCpuPasses` 固定执行：

```text
PrepareDataOrientedRoamFrame
→ MergeScore → MergeTopology → SplitScore → SplitTopology → MeshEmit
→ 验证（可选）→ 统计/历史收尾 → 输出
```

`PrepareDataOrientedRoamFrame` 将深度规范化到 0～20、预算规范化到至少 2，限制合并阈值不超过细分阈值。递增 `BuildSequence`，冻结本帧视图，清空本轮记录并开始网格更新。正常视点变化复用状态；高度图、尺度、预算或降低深度等情况按 `NeedsTopologyReset` 重置。

**FACT**：`CommitScoredSplitTopology` 根据现有 `SplitTopology` 动作，选择是否快照候选、规划原安全前缀、尝试并行辅助；之后同步剩余预算并执行 `RunSplitSerialConvergence`。`SerialImmediate` 不复制候选、不做原分块计划。

**FACT**：原细分拓扑阶段内还会执行合并。`RunStrictSplitStep` 的顺序为：

1. 若合并队首有效且分数 **严格小于** `MergeThreshold`，先尝试合并。
2. 否则读取细分队首；`ShouldSplitWithScore` 不允许时停止，不能越过头部找更低优先级请求。
3. 尝试普通根细分，内部可能产生强制细分。
4. 失败后，使用预算拒绝计数判断是否因预算失败；重新读取当前合并队首。
5. 若预算失败且原根分数高于当前合并分数，尝试合并以交换预算；否则按预算阻塞停止，或将非预算失败根标为本帧阻塞。

前置合并阶段的阈值比较使用 `<=`，不能和步骤 1 的 `<` 合并成一个比较。

`InitializeSplitIteration` 在入口冻结最大迭代次数 `max(1024, 8B + 4*Nodes.size())`。缓存节点规模因此会参与下一轮控制条件；循环次数也不等于最终事件差分数量。

## 3. 强制细分与失败的状态影响

**FACT**：`SplitNodeImpl` 先为当前节点预留一个叶名额，再沿当前底边关系递归展开前置；前置成功后重新读取底边关系。深度、`forcedFrom` 和循环保护都参与停止。

**FACT**：后续失败仅释放当前尚未提交节点的预留。此前成功的强制细分已经更新生产状态，不执行整条根请求回滚。把根请求视为“完整闭包全成或全败”会改变结果。

节点提交涉及：父 `IsSplit`/`SplitBuildId`、子节点激活轮次与强制来源、子邻接重置和互连、外部反向邻接、活动索引、两队列、预算、统计、`CurrentSplitPaths`、网格日志。合并保留已创建的缓存孩子，恢复父与外部邻接，设置 `MergeBuildId` 等状态。

**INFERENCE**：最终叶差分相同，不足以证明原生续接行为相同。至少还要解释本帧阻塞、发生过的细分/合并、缓存孩子和内部节点关系。原型的抽象历史契约不能原样代替这些字段。

## 4. 身份、活动索引与持久堆

**FACT**：`AddNode` 追加 SoA 数组，同时追加 `NodeMembership`、`NodeSlots`、`SplitQueueBlockedBuildIds`。节点物理下标不搬移，但数组扩容会使元素地址/引用失效。缓存池可显著大于活动预算。

活动叶和内部节点使用稠密向量与反向位置，删除采用末尾填洞。顺序不是全局优先级。

**FACT**：队列使用分数与稳定 `PathId` 排序，平分由 `PathId` 打破，不依赖物理槽位。`Q_s` 支持按节点删除并立即修堆；其成员仍包含最大深度或本帧阻塞节点，通过极低分数停止。当前轮合并标记也会压制细分分数。

`Q_m` 根据活动内部节点、孩子叶状态、底边关系和对侧资格确定成员。完整菱形以较小 `PathId` 为代表，两侧保存代表/伙伴关系；分数取两侧最大值，本帧已细分节点受到禁止立即合并的评分处理。

`AppendPersistentMergeQueueNeighborhood` 访问当前节点、父子、三邻居，以及这圈节点的父和底边关系；修改前失效，修改后重新插入。`RefreshPersistent*QueuePriorities` 对现有堆成员整批评分并线性建堆；拓扑中的局部成员更新还可能计算新条目的分数。

**INFERENCE**：只复制初始排序列表不能保持动态严格语义。新孩子、新合并资格、被强制移除的任意条目和阻塞分数都要求可变逻辑队列。已有堆是可借用的入口，尚不存在稀疏规划视图或堆覆盖层。

## 5. 网格消费与跨帧义务

**FACT**：`BeginDataOrientedRoamMeshPlan` 推进网格代号，清空本帧日志/脏集合；保留上轮调试过渡叶以刷新一次性着色。需要初始化时不记录普通操作。

正常更新由 `RecordMeshSplit/Merge` 记录逐操作日志，`ApplyDataOrientedRoamMeshPlan` 顺序消费。细分让左子继承父槽，右子追加；合并删除子槽并用尾部填洞。日志不一致时当前实现完整初始化；首次初始化本来就扫描最终活动叶。

**FACT**：本帧合并阶段早于细分阶段，故细分入口的 `TopologyEdits` 可能非空。只替换细分阶段时不能清空此前日志。新算法若只提供最终叶集合，当前消费接口不能直接接收局部叶差分。

`NormalizeDirtyMeshSlots` 排序去重脏槽；最终范围和借用数据通过公共 CPU 网格包交给后端。物理槽位顺序可以不同，但必须保持反向对应及范围完整性。

**FACT**：`BuildInternal` 帧尾仍执行 `AccumulateLeafStats`、`CollectActiveSplitPaths`，并赋值 `PreviousSplitPaths = CurrentSplitPaths`。前者读取活动叶，后者沿活动拓扑重建路径集合。本次替换细分阶段不会自动消除这些全局工作。

## 6. 开关和计量接入

**FACT**：`TerrainLodPassPolicy` 目前只有阶段动作、线程数量和工作量阈值，没有原实现/直接物化实现选择。GUI 目前提供 DOD 的“并行 Split”，不能据此区分算法实现。

`Application`、两个 `TerrainRenderer` 和 DOD 公共适配器均传递整份 `PassPolicy`。后端设置比较包含该策略，变化可触发下一次构建；DOD 自身的拓扑重置条件不包含执行策略。适配器仅在 `EnableParallelSplit=false` 且细分动作为 `Automatic` 时将其转成串行。

**FACT**：`ExecuteDataOrientedRoamCpuPasses` 具有直接阶段外层计时；`FinalizePassTraces` 的拓扑墙钟记录当前由若干子计时相加，且预算交换合并另有统计归属。接入新路径时不能只填物化子项，也不能把交换合并漏掉或重复算入总时间。

`HashDataOrientedRoamPassInput` 编码节点、活动向量、堆顺序及网格元数据；它不是忽略布局的续接等价判断。`NormalizedMeshHash` 忽略槽位顺序及部分调试属性，单独使用不足以覆盖原生调试/历史契约。

## 7. 原型能力及不能直接复用的部分

**FACT**：`MaterializationPatch::Apply` 原地修改独立原型状态。全状态复制来自来源捕获和对照输入恢复，不是该函数的必要步骤。

`MaterializationDodBridge::Capture` 导入旧状态并在 DOD 副本上运行串行阶段取得目标，再生成完整差分。生产使用它会引入全量导入/复制和旧阶段执行。

原型使用稳定身份、有序容器、独立队列及 Pending 端点模型。它没有恢复原生 SoA 缓存布局、原生内部旧邻接、原生逐操作网格日志的完整契约。

`MaterializationHierarchy` 的路径、域、同层伙伴及整数边键计算是纯层次能力；当前位于实验目录并依赖实验类型。可以讨论向共享算法层提取，但当前尚未提取。

## 8. 已有验证与本轮未知

**FACT**：已核对 `DataOrientedRoamSplitPrefixTests` 的平分、停止头部、迟滞、预算及合并优先夹具；`DataOrientedRoamPassExecutionTests` 覆盖五阶段边界与多帧状态重建；`RoamMaterializationTests`/探针提供原型局部物化和续接证据。当前没有原生直接物化的测试或生产路径。

**FACT（NMP-01 规划补充扫描）**：`TopologyExperiment.h` 的 `TopologySplitStep` 保存单循环根选择、成败和预算/阻塞摘要；`AdvanceStrictSplitIteration` 复用原严格循环体。`ExecuteObservedSplitRoot` 每根调用 `HashDataOrientedRoamPassInput`，而 `AnalyzeSplitOperation` 还在外层复制整份状态；当前观察不能直接当作低成本完整决策轨迹。`SplitNodeImpl` 的 guard 达界结束 while 后继续执行后续判断，并没有独立的“guard 达界即失败”分支。

**FACT**：MPR 自然探针选取的是 `test129-a-b4096` 与 `peking547-a-b20000` 的 sample14；前者预算是 4096。压力样本为独立 `budget-orbit64` 几何轨迹，不能与 `peking547-a-b200000` 的轨迹 A 混称。

**UNCERTAIN**：严格目标规划所需稀疏状态是否足够便宜、原生内部缓存关系能否以局部补丁完整恢复，以及完整细分阶段是否有净收益，均需新阶段实际验证。

**PLANNED**：对应[原生接入大规划](../../plans/roam_parallelism/native_materialization_plan.md)定义新增职责；本文不将其列为当前已实现能力。本轮仅阅读源码与编写文档，未运行新实验。
