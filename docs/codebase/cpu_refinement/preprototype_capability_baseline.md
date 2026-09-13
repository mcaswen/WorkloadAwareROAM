# 全局优先级事务化候选：原型前能力基线

> 日期：2026-09-14；源码/推导基线：`8af1a41`。供[原型前大规划](../../plans/cpu_refinement/greedy_multipass_preprototype_plan.md)使用。FACT 表示已读取实现，PLANNED 表示缺口，不把纸面构造写成运行能力。

## 1. 研究与实现的边界

FACT：[独立 greedy 候选](../../research/cpu_refinement/greedy_local_exchange_candidate.md)、自由高度、有界补丁和一般回收附页已有局部证明、反例及 Python 有限核查。一般内部 d 价回收为 d→d−2；局部 DP 的完整费用包含几何判断、样本查询、三角形成本和递推，不能只写 O(d³)。

FACT：已有 16 面预算交换正例，以及形状/质量不可回收反例。没有自然输入兑现率、完整全局事务 planner、持续多阶段新算法或该候选的真实多线程性能结果。既有研究自审未批准原型实现。

FACT：[现有研究定义](../../plans/formal_experiment/cpu_roam_research_definition.md)仍有效。多阶段、数据布局、评分并行、阶段特化和端到端对照已有实现与实验基础；不能把采用这些词本身当作首次提出。

## 2. 输入捕获

| 位置 | FACT | 复用限制 |
| --- | --- | --- |
| `src/algorithms/data_oriented_roam/DataOrientedRoamPipeline.h/.cpp` | `BuildWithPassObserver` 可在指定阶段边界观察来源状态 | 来源推进与捕获属于离线输入准备，不是新算法目标发现 |
| `tests/RoamMaterializationProbe.cpp` | 能恢复两个 sample14 来源及独立 `budget-orbit64` 压力协议 | 该探针还运行物化与固定重复，不适合作为新 Gate 直接入口 |
| `src/experiment/roam_materialization/MaterializationDodBridge.cpp` | `Capture` 导入来源，复制 DOD 状态，执行旧 SplitTopology 后提取目标差分 | 不能把整个 `Capture` 包装为新算法只读输入或便宜目标来源；`Import` 当前为私有接口 |
| `src/algorithms/data_oriented_roam/DataOrientedRoamState.h` | 活动叶、节点域、层次关系、队列成员和历史由 DOD 所有 | 捕获当前叶面应从阶段状态生成，不能默认先前 mesh 输出与当前拓扑同步 |
| `src/terrain/TerrainMeshBuilder.h` | `TerrainMeshData` 持有顶点属性和索引 | 渲染值类型不是通用邻接/持久候选状态；重复输出顶点不能直接当拓扑逻辑身份 |

PLANNED：只导出当前几何、逻辑顶点/面身份、相机与资产身份的薄适配器。不得导出 Legacy 最终 J 作为新 planner 的答案。新候选允许一般连接，不能假定可导入依赖二叉层次的 MPR/DOD 状态。

## 3. Priority 与独立质量

FACT：`src/algorithms/data_oriented_roam/DataOrientedRoamScoring.cpp` 的节点评分采用层次 `GeometricError`；几何描述符重载仍通过 `DomainToWorld` 从 HeightMap 求三个世界顶点。误差来自二叉域上的 variance/wedgie 构造。

FACT：`src/algorithms/RoamScreenError.h` 的公共计算接受实际三角形和外部 `WorldError`，取保守投影失真与加权屏幕长边的最大值。公共投影公式可以复用；它不负责为任意重剖分/自由拟合后的面生成可信误差界。路径、深度与历史抑制规则也不自动适用于这些面。

因此 **不能把任意新面的旧 hierarchy variance 或 source 高度当成其当前几何**。新 priority 的定义、代价及稳定排序契约仍为 PLANNED。

FACT：[最小质量评价器](../ordering_relaxation/mesh_quality_evaluation.md)已提供原始高度样本加双线性主参考、固定采样层和最大误差位置。`MeshQualityEvaluator.h/.cpp` 的公共结果包含 sampled screen maximum、sampled height maximum、输入/投影异常和计时。

FACT：当前没有公共 RMS、配对 pointwise excess、局部三角形证书接口或完整连续屏幕误差证明。整网格入口会建立查询索引，不能按每个事务反复调用并把成本视为局部常数。复用参考与投影语义，不假定全部质量设施已完成。

## 4. 冻结输入与证据用途

| 输入 | 世界尺度 / 高度比例 | 预算 / 深度 | 本次可复用内容 |
| --- | --- | --- | --- |
| `test129-a-b4096` / sample14 | 30 / 4 | 4096 / 14 | 轨迹 A、相机、资产摘要与来源恢复协议 |
| `peking547-a-b20000` / sample14 | 80 / 12 | 20000 / 20 | 同上；资产实际 547×547 |

来源：[冻结场景清单](../../parallel-roam/cpu-pilot-scenarios-v1.csv)、[输入契约](../formal_experiment/cpu_pilot_input_contracts.md)。两个输入仅支持原型现实核查，不构成 workload 泛化。不同新 priority 或事务规则下的结果不能沿用 P5 严格等价配对标签。

PLANNED（评审修订）：GMP-03 将使用上述两个场景、各 2～3 个预先指定时刻的小面板，具体 index 和理由在运行任何新后端前冻结。上表仍是已用历史来源事实，不表示新的 4～6 个快照已经选定、捕获或验证；没有新增自然结果。

## 5. 架构复用判断

- **Reuse**：冻结输入、只读观察入口、原始高度场/相机语义、已有精确小夹具与几何核查函数。
- **Wrap**：来源状态到中立快照的适配、公共投影数学；适配器单向依赖 DOD，研究内核不依赖 DOD 控制器。
- **Extend**：有限局部检查覆盖事务组合和新语义反例，保留历史结果，不复跑整套证明矩阵。
- **Create**：新执行契约、兑现率分母与诊断账本；需要自然核查时再创建小型离线审计工具。

本轮只记录事实，没有修改源码、捕获新输入或运行性能测试。

## 6. DOD 五阶段与公共接入边界（2026-09-14 补充）

FACT：`DataOrientedRoamPassExecution.cpp` 固定执行 `MergeScore → MergeTopology → SplitScore → SplitTopology → MeshEmit`。观察器在各阶段开始前调用，回调返回后才开始该阶段计时；回调只能同步借用状态。因此 SplitTopology 前的来源已经包含本帧旧 MergeTopology 的结果。

| 阶段 / 入口 | 当前实际行为与依赖 |
| --- | --- |
| `PrepareDataOrientedRoamFrame` | 更新视图/设置，处理重置、层次 variance、节点容量与增量网格生命周期；不是只复制通用参数 |
| `MergeScore` | `RefreshPersistentMergeQueuePriorities` 操作 DOD 持久合并队列与原生资格/评分状态 |
| `MergeTopology` | `CommitScoredMergeTopology` 应用原合法节点/菱形合并并维护原生状态 |
| `SplitScore` | `RefreshPersistentSplitQueuePriorities` 操作本帧合并后的持久细分队列 |
| `SplitTopology` | `CommitScoredSplitTopology` 包含细分、强制兼容与预算交换；其内部可以执行合并，不是只增加面 |
| `MeshEmit` | `ApplyIncrementalMeshUpdates` 加 `FinalizeIncrementalMeshUpdate`；消费节点与槽位映射及增量记录 |
| `BuildInternal` 收尾 | 原生拓扑/网格检查、统计、`CollectActiveSplitPaths` 与下一帧 `PreviousSplitPaths`；不能对一般网格直接套用 |

FACT：`DataOrientedRoamMeshEmit.cpp::WriteDomainTriangle` 根据节点 `TriangleDomain` 调用 `SampleTerrainWorld` 取得 Position/Height，法线从 source 梯度采样，并填写预分配三角形槽位。它不读取一般网格当前拟合高度。`DirtySlots` 和 `SlotOwners` 由 DOD 网格状态持有，不能把新面 ID 填入旧节点槽位后直接调用。

FACT：`DataOrientedRoamThreadPool` 的 `ParallelFor(workerCount, callback)` 仅负责线程任务与完成同步，不读取拓扑。线程池由 DOD pipeline 所有，状态仅借用地址。可在后续实验适配层复用调度能力；它不提供新事务正确性或与 DOD 热路径共享同一活状态的理由。

FACT：公共 `ITerrainLodAlgorithm::BuildRenderData` 接受 `TerrainLodBuildInput` 并返回 `TerrainLodRenderPacket`。DOD 适配层返回自有 `TerrainMeshData` 的借用指针，寿命为 `UntilNextBuildOrReset`，并给出 generation、全量标志和顶点/索引更新区间。OpenGL 与 D3D12 渲染器各自创建算法实例并消费此公共 CPU 输出；后端资源与上传实现保持各自管理。

FACT：公共 `TerrainLodPassPolicy` 和现有阶段统计带有五阶段语义，已有 `ParallelAssisted` 指原生拓扑执行方式。不能把具有不同候选/拓扑契约的新算法静默放在该 action 下，并继续宣称 P5 的同阶段严格等价。

PLANNED：新状态引擎通过公共算法输入/输出接入；评分、提交与线程调度可复用执行技术及独立纯函数，原生队列/层次/历史/槽位不混用。具体建议及阶段对应见大规划 §4.1～4.5；目前没有实现新的算法 ID、开关或适配器。
