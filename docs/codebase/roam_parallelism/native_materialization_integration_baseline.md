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

**INFERENCE**：只复制初始排序列表不能保持动态严格语义。新孩子、新合并资格、被强制移除的任意条目和阻塞分数都要求可变逻辑队列。已有堆是可借用的入口；初始扫描时没有稀疏覆盖层，后续实现另记于 §10。

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

**UNCERTAIN**：原生内部缓存关系的局部生产恢复与完整细分阶段收益仍待后续验证。严格规划当前已测得较高成本，具体结果见 §11，不再将它仅列为未测量假设。

**PLANNED / FACT 边界**：[原生接入大规划](../../plans/roam_parallelism/native_materialization_plan.md)的全部生产接入尚未实现。初始扫描后，字段审计见 §9，工作区见 §10，NMP-01 完整规划与验收见 §11；后续物化/开关不追认为当前能力。

## 9. NMP-01 步骤 1 补充事实

规划提交 `c2cd2d8` 之后的第一批改动增加三个测试文件与无窗口 CPU 构建入口，当时尚未修改 `src/`。本节保留字段审计过程；后续工作区见 §10。实现范围与复现参数见[小规划 §11](../../plans/roam_parallelism/native_materialization_01_plan.md#11-结果与审查回填)。

**FACT**：`DataOrientedRoamNativeTargetTests.cpp` 从深度 3 的一致细分出发，以人工优先级构造 120 对合法请求历史。相同最终叶集合下，内部左右邻接有 72 处差异，调用原生邻域收集得到 48 处集合差异；两端通过原生拓扑、队列与预算核查。内部底边关系在此矩阵中未出现差异。

**FACT**：`PrepareSplitNodeState` 清空复用孩子三邻接之前，`SplitNodeImpl` 已收集并失效合并邻域；“重新激活时覆盖”不足以证明休眠关系从未被读取。

**FACT（隔离续接）**：`ReplaceInternalSides` 以 A→B 为完整来源，只替换 8/9 两个内部左右字段；替换再原址还原后的完整 `StateSnapshot` 相等。`ContinueTogether` 对五种配置逐步对照实际原生严格循环及逻辑状态，均一致，包含强制细分、成功合并、预算交换和预算阻塞。原始 hash/物理堆排列不作为续接等价标准。

**FACT（见证局部恢复）**：对同一批见证父节点，先确认两个孩子均为活动叶，再用孩子 Base 重算父 Left/Right；相对 A→B 一个字段变化，另一个原本一致。相同五条续接仍成立。目标孩子的 Base 是恢复值来源，另一历史只标定被审计的节点。

**FACT（读取与写入）**：`EvaluateMergeCandidate` 和 `IsMergeableTopology` 不读内部 Left/Right；`ReplaceNeighborReference` 对三个字段分别比较和写入。`MergeSingleNodeImpl` 在父变成叶前以孩子 Base 覆盖父 Left/Right；`LinkSplitNeighbors` 使用待细分叶的左右关系。内部历史 Left/Right 的实际查询用途是合并邻域收集，不能称为无人读取。

**INFERENCE（有前提的幂等）**：拓扑、轮次、视图不变，逻辑 Qm 完整且分数有效时，额外失效/刷新未变化代表可恢复同一成员、伙伴和分数；只可能增加维护和改变物理堆布局。`AuditNeighborhoodRefresh` 对 8/9 各做一次往返，8 次实际插入后逻辑投影一致。人为删除一个仍合法候选后，刷新会补回，明确展示完整性前提；不是自然失败合并证据。

**INFERENCE（合并失败边界）**：严格细分循环以最大有限分数调用实际合并；完整拓扑候选及有限重算分数足以通过其复核。当前五种配置每步检查 Qm 所有候选均可通过该复核，失败合并为 0。不能据此删除其他入口或异常队列的防御性失败分支。

**FACT（深层与休眠续接补充）**：`NormalizeNonLeafRelations` 对活动内部节点写 `Left=Base(leftChild)`、`Right=Base(rightChild)`，对休眠节点清空三邻接，不改活动叶及活动内部 Base。`AuditDeepAndDormantNormalization` 从深度 5 状态开始，30 个深层内部记录参与恢复，48 个字段改变，38 个成对合并续接步一致；随后清空 124 个休眠缓存的关系，改变 320 个字段，76 个成对细分续接步一致，124 个旧缓存重新激活。`RequireLegal` 对活动内部 Base 另检查同深度反射伙伴或边界空值。快照隔离及逐步投影仍保留。

**PLANNED（采用的恢复协议）**：小规划 §11.9 已确定首版不准入 `InternalRelationChange`；实际物化应从目标边、固定伙伴和孩子 Base 恢复关系，休眠关系可清空。Planner 决策工作区继续保留精确临时关系。有限例子不能证明所有状态，协议实际局部性与切换续接仍由 NMP-02 验收；不存在已完成的原生物化/mesh 消费能力。

## 10. NMP-01 稀疏规划工作区

### 10.1 文件、调用者与依赖

下列路径均相对 `src/algorithms/data_oriented_roam/`；namespace 为 `ParallelRoam::Algorithms::DataOrientedRoam::Materialization`，纯堆原语位于 DOD 的 `IndexedHeap` namespace。

| 文件 | 当前真实职责 |
| --- | --- |
| `materialization/NativeMaterializationTypes.h` | 工作区字段/条目/计数，以及正常 `NativeTargetPlan`、净事件、具名 Γ 和纯求值缓存；见 §11 |
| `materialization/NativePlanningView.h/.cpp` | 只读来源借用、字段覆盖、虚拟缓存、稳定身份查询和剩余预算 |
| `materialization/NativePlanningQueues.h/.cpp` | 两个私有堆的逻辑长度、槽位与反向位置覆盖、合并代表/伙伴覆盖 |
| `DataOrientedRoamIndexedHeap.h` | 静态 `SiftUp/SiftDown/Restore`，以比较/交换回调操作索引，不持有容器 |

**FACT**：工作区组件测试与 `NativeTargetPlanner` 构造这些对象，四个 materialization `.cpp` 均已注册进共同 DOD 清单。Legacy 增加编译期轻量观察，评分增加纯描述入口；旧 `Queues.cpp` 没有改用新堆或覆盖视图。不存在生产模式开关、并行任务、线程共享或跨帧持有。

### 10.2 View 的所有权与行为

**FACT**：`NativePlanningView` 持有 `const DataOrientedRoamState& _source`，禁止复制；调用方须保证来源及借用的高度图等资源在同步工作区寿命内有效且不变。构造只检查深度上限 20、节点容量和 membership 长度，初始化剩余预算，不枚举 N 个缓存或 Q 个候选。输入拓扑合法性仍由外部诊断负责，构造器不充当完整验证器。

`_overrides` 是 `map<Node, array<optional<uint64_t>, FieldCount>>`。关系、轮次、forced、IsSplit、Activity、阻塞轮次及 CurrentSplitPath 分字段覆盖，显式无效关系与没有覆盖不同。`Read` 优先读覆盖，再借用来源；`Write` 恢复基值时清除该字段的 optional，但保留曾写节点记录，累计覆盖不随净差分缩小。Activity 独立于缓存 IsSplit。

`Path/Parent/Depth/Domain/GeometricError/VarianceTree/VarianceIndex/CreatedBuild` 从来源或虚拟记录返回值。`FindPath` 校验双根路径编码后沿已有孩子下降，不建立全池身份字典，不为查找缺失身份创建节点。`CreateChildren` 要求两个孩子都未缓存，以已有域二分、路径和 variance 函数追加一对虚拟静态记录，然后只写私有父孩子字段；新节点仍休眠，创建并非成功细分。

`TryReserveBudget/ReleaseBudget` 只改私有标量；`TouchedNodes/Metrics` 只枚举覆盖容器。`Initial` 独立读取初始字段并计费，供净出口默认值比较。控制器完成所有根后以 Qs 长度检查活动叶数与预算账本，求值缓存/正常出口见 §11；虚拟下标和关系不进入返回值。

### 10.3 Queues 的状态转换

**FACT**：`NativePlanningQueues` 借用 View，禁止复制。每堆持有 `Length/SourceVisible`、`Cells`、`Reverse` 和计数，合并另有 `_representatives/_partners`。构造只借用原堆长度；`At` 检查逻辑长度，优先覆盖，其次读仍可见的来源前缀。

`Remove` 将尾条目填入洞、缩短 Length 和 SourceVisible，以显式无效反向覆盖表示删除，再调用静态 `Restore`。旧私有尾记录留存用于累计计费，但超出 Length 不可读取；`Upsert` 每次追加完整写槽，避免截断后复活旧尾。分数必须有限，细分最大分优先、合并最小分优先，平分按最小 Path。

`RemoveMerge` 可从任一侧解析代表并清除该组；`UpsertMerge` 只接收调用方已确定的规范代表/伙伴，改组前撤销双方旧组。它不判断拓扑资格。`Validate` 诊断枚举堆，检查成员唯一、反向位置、堆序和代表关联；不证明成员是合法可合并菱形，也不应进入计时包络。没有完整物理堆导出 API。

### 10.4 成本、诊断与验证

**INFERENCE**：构造是常数规模；普通覆盖查询承担有序容器查找成本，堆修复中的多次槽位查询因此不能当成原数组常数访问。路径查询下降不超过 H 层，各层仍含覆盖查询成本。`TouchedNodes` 分配 O(a) 结果，`Metrics` 枚举已存覆盖，销毁计入 Tplan。三个输入的实际覆盖已记录于 §11 及小规划 §11.12，不证明所有输入的稀疏性。

**FACT**：普通模式仍累加操作计数，`collectReadCoverage=false` 时不建立不同旧读集合。诊断模式另用 set 保存旧节点/槽位读取覆盖，包含额外查找与分配。节点累计覆盖、最终非空字段、新虚拟节点、旧/新增堆槽位、反向记录及代表关系分别计数；计数不是总字节量，不能据此省略容器开销。

`DataOrientedRoamNativePlanningTests.cpp` 在诊断开/关下核对：构造未物化来源、路径与虚拟生命周期、字段恢复不抹掉累计覆盖、预算、平分队首、任意删除/尾部重用、伙伴改组及来源完整快照。队首 oracle 从独立成员映射中选取，不复用堆修复。任意伙伴改组明确仅是存储夹具。

组件测试在 Ubuntu Release 通过。工作区阶段原 Legacy 短测曾出现 test129 未解释计时偏移，见小规划 §11.10；完整规划器后来已测 Tplan，见 §11.12，尚无完整算法加速结论。

## 11. NMP-01 独立严格规划与验收

以下保留 `2f04f48` 基线事实；当前工作区的存储、计费与候选维护变化以 §12 为准。

### 11.1 新文件与执行路径

**FACT**：`materialization/NativeTargetPlanner.h/.cpp` 实现 `BuildNativeSplitTarget(source, audit)`；`NativeRefinementSimulation.h/.cpp` 持有 View/Queues 借用、实际求值缓存及失败合并处置集合。两者均只在同步调用内使用。Simulation 不可复制，负责 `Split/Merge/Block/RemoveFailedMerge` 与局部候选维护，不选择全局根；Planner 冻结迭代上限、执行严格循环、生成净结果，不写生产池/活动索引/mesh。

普通路径：调用方只读 M₀ → `Build<false>` 创建 View/Queues/Simulation → 严格循环 → `Extract` → 冻结计数 → 销毁局部容器 → 返回 `NativeTargetPlan`。当前上层调用者只有 `DataOrientedRoamNativePlannerTests.cpp` 和 `DataOrientedRoamNativeMaterializationProbe.cpp`；未接到生产 `CommitScoredSplitTopology` 的新动作。

轻量诊断路径：`Build<true>` 发独立决策事件，外部可在每步或最终回调读取私有逻辑投影；回调引用不得逃逸。微型测试使用逐步回调，自然诊断只做最终全量投影。计数先于 Finished 回调冻结。不同读集合受 `CollectReadCoverage` 控制，普通模式不建立集合，仍有标量计数和少量条件判断。

Legacy 路径：原 `RunStrictSplitStep<false,false>` 仍执行生产 mutator；新 `AdvanceStrictSplitIterationWithDecisions` 使用 `<false,true>` 与 `DecisionSerialCommitPolicy`，不运行每根克隆/完整哈希。`DataOrientedRoamDecisionTrace.h` 仅定义固定事件、同步接收器、当前递归身份游标及不可复制的 `DecisionAttemptScope`；观察关闭实例不发事件。原 Stop 枚举从 Experiment 头移动到此纯值头，名称/数值保持。

`Scoring.h/.cpp` 新增 `(state, domain, geometricError)` 和 `(state, depth, path, score)` 纯入口；旧节点外壳委托它们。新旧高层控制和局部逻辑保持独立，未用共同 controller 自证。新模块没有 `AdvanceStrictSplitIteration/RunStrictSplitStep/SplitNodeImpl`、生产 AddNode/mesh writer、完整输入哈希或 MPR Bridge 调用。

### 11.2 私有逻辑与正常结果

**FACT**：Simulation 精确维护临时三邻接、活动资格、缓存孩子与轮次标记。每根先预留自身预算，再沿当前底边递归，成功前置后重新查询底边；根失败只释放当前未完成预留，已完成前置保留。候选维护取修改前/后邻域，先失效再按当前资格插入；已存在代表不额外刷新，失败处置在重入时撤销。合并资格和实际 primitive 复核分开，原 guard 达界与停止头部规则未改变。

`NativeTargetPlan` schema 1 保存 BuildSequence、来源规模/预算、最终叶数/余量/停止/迭代摘要、AddedEvents/RemovedEvents、Obligations、Evaluations 及 Metrics。来源标量只说明同一同步调用的环境，不是任意跨状态事务认证。没有来源引用、虚拟下标、最终邻接、活动数组、堆补丁、slots 或 mesh。

`Extract` 仅枚举 `TouchedNodes`、新虚拟区间和失败处置集合，完整 J 不展开。事件由入口/最终活动内部资格的差形成。历史默认值从来源和净活动转移得到，超出默认的命名例外进入 Γ；重复键不会因多个操作重复输出。移除内部事件的默认激活/合并轮次为当前轮，新增内部默认细分轮次为当前轮，休眠变活动默认激活为当前轮；确定重新激活/合并者的 forced 默认 false，其余字段保留来源默认。实际强制激活例外单列，阻塞不以 Δ 代替。

Γ 枚举为 `SplitBlockedBuild/FailedMergeRemoval/ActivatedBuild/SplitBuild/MergeBuild/ForcedActivation/CacheBirth`。失败移除只有最终仍具规范候选资格但不存在时输出；目标活动层次隐含的新缓存不重复登记，额外休眠新缓存只登记存在性/创建轮次。当前类型没有关系 Γ；`CurrentSplitPaths` 私有模拟保持精确，但帧尾重建前无续接后果的追加不导出。

纯求值缓存仅保存 Path/Score，包含实际求值但不含极值抑制分。首版每个原逻辑求值仍实际计算，再覆盖缓存；没有借缓存偷偷减少被计工作。导出按路径排序，只在当前冻结环境内可复用。工作区 map/virtual vector/堆/缓存析构计入 Tplan，返回结果清理另计。

**FACT（前置条件）**：当前入口拒绝局部共形约束关闭、评分镜像开启、预算小于当前活动叶数的输入，不更改来源设置。该范围符合本轮冻结实验设置，不构成所有生产配置兼容。全面合法性由外部来源诊断负责，普通入口不扫描认证全状态。

### 11.3 测试事实与费用

`DataOrientedRoamNativePlannerTestSupport.h` 定义外部完整缓存/逻辑堆投影、百万事件容量上限和独立 Γ 提取器，只由测试和探针引用。它全量枚举 Legacy 结果，不采用 Planner 的触及集合或输出义务键；比较稳定身份和字段位值，不要求物理堆排列相同。source snapshot 验证容量、地址和全部来源内容不变。

**FACT**：11 个解析配置逐循环相符，覆盖合法细化/粗化/缓存复用/预算交换/平分停止及两类显式故障注入。预算加 2 的合法配置有一次根失败保留成功前置；非叶前置故障得到 k=0/h=1，仅用于防御分支。原迭代上限后增边界另做组件检查，没有宣称自然输入耗尽上限。

**FACT**：固定三点分别完整比较 69、413、156720 个决策事件；最终活动叶、完整缓存关系/历史、逻辑堆、Δ/Γ、预算和停止一致，诊断关闭结果一致。三点 k/h/g 为 12/6/12、78/44/84、27783/16610/25290；当前自然 Γ 全部为 forced 标记例外。根、forced、primitive、失败与预算计数另列，不混为净差分。

**FACT**：两个普通点旧节点写覆盖 0.528%/0.625%；压力点 8.393%，旧节点读取覆盖 13.598%。压力细分/合并堆写覆盖 37.802%/57.594%，读覆盖 55.783%/76.035%。不能宣称整个工作区均非常稀疏。r 的诊断值含轨迹身份查询，全量最终投影不计入这些覆盖。

**FACT**：一组独立进程短测中，Legacy/Planner 均值 ms 分别为 0.046012/0.102063、0.491022/0.995450、61.629698/1088.565186。Tplan 已含私有容器销毁，尚无 Tmaterialize；压力费用比约 17.66，不是完整更新加速比。实际存在有序覆盖查询、重复 heap 维护、求值缓存和出口工作，具体时间占比未用 profiler 分离。

**INFERENCE**：当前实现不能直接带来生产替代收益；但这些成本不能反证语义解耦。压力 heap 较广是独立于节点局部性的结构观察，需要在下一实现决策前 Review。有限三点不证明一般 O(k) 或所有输入的性能性质。

### Unresolved / Uncertain

- NMP-01 已完成；原生 materializer、生产开关、mesh 消费与全部生产设置兼容尚未实现。
- 当前覆盖表示的各项耗时占比及更换容器后的可获得收益未测量，压力堆覆盖不能称为 u≪Q。
- 关系恢复协议的原生局部实现与新旧切换续接仍需 NMP-02 验证。
- test129 短计时偏移的版本因果与离群值来源未解释，已达到本轮复测上限。

## 12. NMP-01P 当前中间实现

**FACT（2026-09-13，尚未提交）**：Planner 接口、独立控制器、只读来源、结果 Δ/封闭 Γ 及同任务契约保持。§10 的 map 覆盖、optional 字段、交换式修复与 §11 的每次实际求值描述已被本节更新。性能优化尚未结束，NMP-02 没有启动；成本见[小规划 §9](../../plans/roam_parallelism/native_planner_performance_plan.md#9-实施过程)。

### 12.1 存储和节点访问

**FACT**：新增 `NativePlanningStorage.h`，模板参数 `Key/Value/Paged`，追加式连续 `Record{first,second}`。Find/Ensure/InsertMissing 返回稳定数值记录号，At/RecordAt 的引用随 vector 扩容失效；没有物理删除。显式无效值由业务负责，源回退与无效覆盖不混同。记录几何增长，32位编码零代表缺失，编码容量耗尽会抛异常。

哈希实例使用完整键加32位记录编码、固定0.75负载上限及逐步倍增；重散列只扫描已存记录。当前只有View节点与评分缓存使用`Paged=true`，两堆与成员已改为下述独立连续表示。哈希宽键组件仍保留独立测试，不根据案例名或运行时负载切换存储。

**FACT**：新增 `NativePlanningPageIndex.h`，256个uint32编码组成一页，独占页指针目录按最高已写页号增长。Find检查缺失目录/页且不分配；Prepare拒绝超过32位的写键，几何扩目录、初始化中间空项、按需分配零页。页只保存记录编号，不从来源复制节点或队列。销毁遍历目录并释放页，均属于Tplan。

计量分开记录实际records、记录capacity/移动、索引capacity/字节、分配/清零、目录size/capacity/初始化/移动和峰值。哈希探测与页索引层数不是相同指标，后者不得解释成CPU字段load。峰值按请求字节计，不含分配器元数据；八表峰值和只能作为整体上界。正常计时不累计索引探测，详细成本或覆盖诊断才累计。

**FACT**：View Fields由13个optional先改为值数组，再压缩为五个uint32关系、四个uint64轮次、uint8标记及uint16存在位图，Fields56字节、含节点键的记录64字节。轮次保留全部64位，Activity占两位且不等同于IsSplit。Write委托WriteFields按原顺序处理相邻赋值，一次定位、逐字段比较旧值/校验/计数；恢复基值仍保留存在位和累计触及。最终ChangedFields由Metrics比较入口值，Baseline和收尾读取均计入Queries/SourceQueries。

固定Field读取实例与运行时读取共享来源映射，保留节点/字段检查和读计数，没有改变优化器或浮点选项。Inspect返回仅在无View写入区间有效的ReadCursor，保存节点/记录号而非指针；MergeRepresentative与AppendNeighborhood在局部无写区间复用它，不跨递归或修改缓存资格。Extract用TouchedCount/TouchedNode/ReadTouched按已定位记录读取，TouchedNodes仍供组件调用。虚拟孩子仍是独立静态求值记录。

**FACT**：新增`NativePlanningHeapSlots.h`，首次真实修复复制该决策堆的Score/Node及从View查询的Path键；复制前At借用来源。私有条目16字节，逐槽曾写标记另存uint8数组。Prepare在修复前准备长度，SetPrepared只访问已验证下标；截断保留物理高水位，后来追加完整覆盖。按来源Q复制、正常几何增长，容量/搬移/标记初始化/销毁均在Tplan。

**FACT**：新增`NativePlanningMembership.h`，队列拥有四字段成员投影：SplitPosition、MergePosition、Representative、Partner与曾写位图，每行20字节。首次旧节点写时复制N行；新虚拟节点使用独立增长尾部，虚拟写不强制旧域复制。触及列表和四字段累计写数分开计量，不将初始化行数冒充逻辑修改。它不复制ActiveLeaf/InternalPosition，活动资格仍归View。全部状态只活在本次规划，非完整State副本。

**FACT**：八个Storage历史位置中只有节点、细分槽、共享成员、合并槽和评分五项非空，共享字段空位为零，不能重复相加。试验性的七字段拓扑投影已撤回，源码没有`NativePlanningTopologyFields`；其O(N)拓扑初始化及较慢结果只作为§9.10历史证据保留。

### 12.2 堆修复与封闭候选维护

**FACT**：`DataOrientedRoamIndexedHeap.h::FillHole`接收调用方已持有的值，沿洞搬移并在末尾落位，无需先写槽再读回；旧Restore未改，Legacy不调用新原语。Queues修复入口准备私有存储并验证待填索引/身份，封闭内核复用Heap引用，保留逐次读写/成员/比较计数。条目携带不变Path用于平分比较，外部Validate核查缓存身份。修复期间不重入或查询暂未落位成员，返回前反向位置恢复；Upsert仅在分数位值相同时跳过修复，仍区分正负零。

Queues的BeginMergeMaintenance/InvalidateMerge/FinishMergeMaintenance仅用于原Simulation Invalidate→修改→Refresh区间。Invalidate立即清除代表/伙伴映射并记录旧代表，暂留heap条目；Refresh保持原候选顺序和评分时机，重新接纳原代表时更新原条目；Finish删除未恢复代表并清空待清理列表。一个旧代表只能因首次逻辑失效进入一次列表。

新增 bool `_mergeMaintenance` 和vector `_deferredMerge`归Queues独占。Merge Top、Validate和Metrics拒绝未关闭区间；该区间内没有根选择/外部回调/forced递归。正常刷新末尾和原防御失败出口都完成清理；异常终止整次Planner并由RAII释放私有状态，不尝试继续使用半维护队列。物理heap排列可与Legacy不同，唯一score/Path全序与逻辑成员保持。

计数分别给出DeferredRemovals/RestoredEntries/DeferredErases/UnchangedUpserts/DeferredPeak/DeferredCapacity，不把逻辑失效数当作物理删堆数。当前压力42977次失效、25115次恢复、17862次最终删堆；待清理列表峰值5、容量8。内存与清理费用在调用内。

### 12.3 评分、计费与生命周期

**FACT**：Simulation在一次固定视图/几何/设置调用内复用纯评分值，默认cacheScores=true；诊断可禁用，但不是生产/GUI策略开关。Requests=Evaluations+Hits，压力85900=51667+34233。动态活动资格、轮次抑制及CanMerge仍在原位置复核；±最大有限数等抑制结果不写入纯缓存。不同调用无缓存继承，输出仍按Path排序。

`NativePlanningAudit::Costs`同步借用有限计时接收结构；不能同时开启事件/回调/读覆盖。`NativePlanningCosts/CostScope`在Types中以嵌套活动区间累加互斥自耗时，Finish幂等并恢复父区间。普通调用不读时钟；详细模式按语义操作计时，不逐字段计时。诊断开销不通过事后扣减伪装成精确普通时间。

Build中的三个私有拥有者改为就地optional，只为显式控制构造/销毁区间，不额外分配对象。顺序仍View→Queues→Simulation；Extract/指标冻结→外部最终审计→Simulation/Queues/View逆序reset。返回值不引用工作区，普通Tplan外包络包含所有reset，返回结果清理由探针另计。

### 12.4 入口、验证和剩余边界

**FACT**：现有Probe增精确冻结case选择、all/timing/diagnostic/cost、反转方法顺序和no-score-cache；不允许改工作负载参数。普通计时一次预热+两次记录，Legacy副本在其方法结束即销毁，复位在Legacy计时外；新旧核心比较使用同一新包络。原探针保留副本至Planner之后，相关历史数据不可当作相同包络。

**FACT**：当前组件覆盖哈希碰撞/页边界/目录增长/稳定记录/显式无效/宽键拒绝，以及洞修复、连续尾部重用、封闭维护中的存活/失效/伙伴改组/未关闭观察拒绝。11个规划配置在原完整逐步投影和事件对照下通过，缓存开启/关闭分别核对分数位值与续接义务。

当前`local-access/natural.csv`三个输入完整匹配69/413/156720事件、完整目标/续接投影、Γ、预算、停止及来源快照。均值Legacy/Planner(ms)为0.050051/0.050191、0.764228/0.529029、61.801414/133.891678；压力Planner两次131.344162/136.439194ms。历史各组和失败试验保留，不挑最快结果；同进程两次不是两个独立统计单位。

**FACT**：当前压力五项预留合计36642456字节，分项峰值和44669251字节仅为上界。节点/评分页仍初始化732928/686336个编码；堆复制195367/32856条、成员投影复制745072行。View不同旧节点读273990并不包含成员投影的全N行来源复制；节点写记录62537和虚拟写记录25290保持。不能再把整个Planner来源读取称为稀疏。

**INFERENCE**：当前费用除了严格目标发现的候选/失败/预算/O(log Q)堆维护外，还含显式O(N+Q)准备、页/目录初始化、记录移动和O(z log z)输出排序，不是给定目标MPR的O(k)物化界。压力费用比2.166，仍未达80%门槛。当前粗成本134.839ms包含循环114.357、提取14.224、汇总5.618、销毁0.633ms；仅优化收尾不足以达标。

**UNCERTAIN**：紧凑字段、内联、缓存、局部定位的独立收益未由正式统计分离；组合版本是当前开发证据。详细互斥遍224.547ms受计时扰动，不得按比例归因给普通133.892ms。后续跨操作合并维护或资格缓存尚未实现，必须先证明观察时刻/失效契约，不能据本轮结果保证50%目标或反证优化空间。
