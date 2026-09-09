# CPU 阶段边界与工作负载规划事实基线

> 本文保留 PREP-03 的实现事实与当时的配对入口基线。PREP-04 对测量、证据和配对入口的最新调整见 [CPU 阶段配对契约](cpu_pass_pairing_contracts.md)，历史入口描述不代表当前实现。

> 日期：2026-09-09\
> 源码基线：`4c78cb6`，扫描开始时工作区干净\
> 范围：PREP-03 所需的 DOD 阶段编排、候选规划、网格规划和发现输入边界\
> 上游事实：[CPU 策略与回放](cpu_policy_and_replay_baseline.md)、[CPU pilot 输入与记录](cpu_pilot_input_contracts.md)\
> 对应规划：[PREP-03 小规划](../../plans/formal_experiment/prep_03_workload_discovery_plan.md)

> 更新说明：第 1～7 节保留 `4c78cb6` 实施前基线；第 8 节起记录本次已实现的当前行为，最终验收状态见对应小规划

## 1. 当前模块与所有权

FACT：`DataOrientedRoamPipeline` 在 `src/algorithms/data_oriented_roam`，拥有跨帧 `DataOrientedRoamState` 与线程池。`Build` 只提供整帧更新，`State()` 返回只读引用，没有逐阶段观察接口。状态副本深复制容器，但借用源高度图与线程池；借用期间来源必须存活，同一源不得同时推进。

FACT：`benchmark/formal/FormalExperimentRunner` 当前只准备输入。`experiment/formal` 已有六场景、每场景 64 个相机点、目标引用及最小发现记录，没有实际发现生产者或目标选择器。发现不能调用现有配对运行器来获取候选，否则会同时执行策略计时。

## 2. 文件与职责索引

以下路径均相对仓库根目录；这里只列本次阶段边界涉及的文件，不代替整个 DOD 模块事实。

| 文件 | 当前符号与职责 |
| --- | --- |
| `src/algorithms/data_oriented_roam/DataOrientedRoamPipeline.h/.cpp` | `BuildInternal` 处理帧准备、合并/细分/网格与收尾；`FinalizePassTraces`、`CollectPassEvidence` 解释实际统计 |
| `src/algorithms/data_oriented_roam/DataOrientedRoamPassExperiment.h/.cpp` | `PrepareNextFrameState` 复制并设置下一帧输入；`RunDataOrientedRoamPassExperiment` 另写五阶段推进，调用策略测量与串行推进 |
| `src/algorithms/data_oriented_roam/DataOrientedRoamTopology.h/.cpp` | `MergeWithDiamondQueue`、`RefineWithSplitQueue` 同时刷新评分和提交拓扑；文件内还包含安全分类、分块、事务、串行收敛及冻结回放 |
| `src/algorithms/data_oriented_roam/DataOrientedRoamQueues.h/.cpp` | 持久队列刷新/建堆、成员维护、只读候选快照与队列不变量检查 |
| `src/algorithms/data_oriented_roam/DataOrientedRoamMeshEmit.h/.cpp` | 修改记录、槽位变换、脏槽位规范化、顶点/索引写入、区间与调试过渡状态收尾 |
| `src/algorithms/data_oriented_roam/DataOrientedRoamState.h/.cpp` | 节点下标/位置类型、候选、节点 SoA、状态复制；`DataOrientedRoamIncrementalMesh` 在 State.h 内定义，并无独立 MeshState 文件 |
| `src/algorithms/data_oriented_roam/DataOrientedRoamTypes.h` | 基础领域值、设置、统计和 `DataOrientedRoamMeshUpdateRange` |
| `src/algorithms/data_oriented_roam/DataOrientedRoamValidation.cpp` | 活动拓扑和增量网格诊断，直接消费槽位映射与实际数据 |
| `src/algorithms/data_oriented_roam/DataOrientedRoamParallel.h` | 数量解析及同步派发；无线程池时顺序遍历任务编号 |
| `src/experiment/formal/FormalExperimentTypes.h` | `TargetStateRef`、场景/相机、`FormalInputRequest`；当前目标 schema 为 v1 |
| `src/experiment/formal/FormalExperimentRecords.h`、`FormalExperimentCsv.h/.cpp` | 发现记录目前只有通用执行前/规划工作量和身份、耗时、状态；写入 schema v1 |
| `src/experiment/formal/FormalExperimentManifest.h/.cpp` | 严格清单及完整相机引用校验；目标接受 1..63、每组连续序号 0..7 |
| `src/benchmark/formal/FormalExperimentRunner.h/.cpp` | 独占目录、输入读写和成功/失败摘要；没有发现分发 |
| `src/benchmark/TerrainLodBenchmark.h/.cpp`、`TerrainLodBenchmarkCommandLine.cpp` | 配置与纯解析、输入准备早期分流；旧阶段配对入口独立 |
| `scripts/prepare_cpu_pilot.py` | 输入复制、SHA-256、源码/二进制/环境快照、子进程与准备状态；公共辅助与准备流程目前同文件 |
| `tests/DataOrientedRoamPassPolicyTests.cpp` | 真实状态副本、两评分与网格、线程派发及配置回归；直接访问增量网格字段 |
| `tests/ExperimentManifestTests.cpp`、`ExperimentCameraTests.cpp`、`FormalExperimentCommandLineTests.cpp`、`test_cpu_pilot_preparation.py` | 输入、记录、轨迹、命令行和真实准备进程回归 |
| `CMakeLists.txt`、`tests/CMakeLists.txt` | 公共 DOD 源列表、共享 formal 输入源列表、CPU 测试与进程测试注册 |

## 3. 真实阶段顺序

FACT：`Pipeline.cpp::BuildInternal` 先增加 `BuildSequence`，归一化深度/预算/合并阈值，清空本帧统计与当前细分路径，更新视图、视锥和可绘制尺寸，并调用 `BeginIncrementalMeshUpdate`。根据高度图、预算、深度等条件重建误差树、网格或根拓扑，随后执行：

```text
MergeWithDiamondQueue
  → RefreshPersistentMergeQueuePriorities
  → 可选快照 / 配对证据 / 并行分块提交
  → RunMergeSerialConvergence
RefineWithSplitQueue
  → RefreshPersistentSplitQueuePriorities
  → 可选快照 / 配对证据 / 并行分块提交
  → SynchronizeSerialSplitBudget / RunSplitSerialConvergence
ApplyIncrementalMeshUpdates / FinalizeIncrementalMeshUpdate
验证、活动叶统计、当前/上一帧细分路径交换、阶段证据
```

FACT：因此合并拓扑边界必须位于合并评分刷新之后，细分评分边界必须位于合并收敛之后，细分拓扑边界必须位于细分评分刷新之后，网格边界必须位于全部拓扑修改完成且网格修改记录尚未消费时。

FACT：`PassExperiment.cpp::PrepareNextFrameState` 复制上一帧状态，再自行重置本帧字段；它要求高度图有效且最大深度、预算不变，不处理生产路径的完整重置/容量/误差树准备。当前五阶段配对分别测量后调用评分刷新或 `Advance*TopologySerialForExperiment`，没有与生产路径共用一个逐阶段分发函数。

FACT：`AdvanceMergeTopologySerialForExperiment` 设置串行动作并直接收敛；细分版本还先同步预算。这些接口不能替换为当前完整拓扑入口，否则会重复刷新评分。

## 4. 拓扑规划中的可复用规则与写入

FACT：两个 `SnapshotPersistent*QueueCandidates` 接受 const 状态，只复制满足阈值的当前队列条目。细分候选的 `Sequence` 来自堆数组遍历顺序；合并候选排序在调用方，按分数升序、路径编号破同分。

FACT：`Topology.cpp::BuildInteriorSplitChunks` 复制并排序候选，使用 `SafeInteriorSplitChunkId` 分类。它在读取状态的同时增加 `Stats.InteriorSplitCandidateCount` / `BoundarySplitCandidateCount`，不是当前可直接使用的只读接口。

FACT：安全细分要求叶节点、未达最大深度、已有可复用子节点、不需强制邻居细分，以及所有会触及的节点位于同块。分类为内部但超过 `RemainingSerialSplitBudget` 的候选不进入提交分块；内部数量不等于计划提交数量。

FACT：合并规划按 `SafeInteriorMergeChunkId(..., false)` 分类，生产提交时再用 `true` 重新验证。两个阶段的分类、调度集合和最终实际提交数量不能互换。

FACT：`DataOrientedRoamTopologyChunkGridSize=8` 表示 8×8 个空间分块，最多 64 个非空块；`MaxTopologyCommitWorkerCount=8` 是线程上限。协议的 `nonEmptyChunkCount / 8` 不表示非空块比例一定小于等于 1。

FACT：串行生产策略不构造候选快照或分块，相关统计可能为 0。因此不能读取串行帧结束统计来代替发现时的候选规划。

## 5. 网格规划与几何写入目前耦合

FACT：`DataOrientedRoamIncrementalMesh` 同时持有 `Data` 和 `NodeSlots`、`SlotOwners`、`SlotDirtyGenerations`、`DirtySlots`、`UpdateRanges`、`DebugTransitionLeaves`、`TopologyEdits`、`Generation` 及三个标志。

FACT：`BeginIncrementalMeshUpdate` 清理当帧修改与区间、递增代数，处理代数回绕，并按初始化状态决定是否记录拓扑修改。`RecordMeshSplit/Merge` 只追加有序修改记录。

FACT：`ApplyIncrementalMeshUpdates` 会先刷新上一帧调试过渡叶，依次重放细分/合并修改；槽位不一致则按最终活动叶重新初始化。初始化和 `ResizeMeshForSlotCount` 会清空、预留或调整真实顶点/索引数组，不能作为只读探测调用。

FACT：左子节点继承父槽位，右子节点追加；删除以尾槽填洞；合并会规避保留子节点恰处于尾槽的覆盖问题。最终脏槽位排序、去重并剔除被缩掉的尾部，再按策略写顶点/索引。

FACT：`FinalizeIncrementalMeshUpdate` 将相邻脏槽位合并为区间，全量路径输出全网格区间，同时发布统计并构造下一帧 `DebugTransitionLeaves`。即使本帧没有拓扑修改，调试过渡属性仍可能导致写入。

INFERENCE：准确预计写入与区间需要复用上述元数据转换；只用修改条数或活动三角形数估算不足以覆盖填洞、调试过渡和回退。隔离规划必须去除真实 Data 的清空、扩容与写入，而不能先完成网格再读结果冒充预计值。

## 6. 身份、选择与入口限制

FACT：现有 `HashScoreInput` 将队列节点转换为路径并排序，刻意忽略堆数组顺序；`HashMeshInput` 覆盖更新编号、网格代数、槽位所有者、修改记录和调试过渡叶；拓扑冻结哈希主要编码排序后的候选路径/分数/序号。这些身份各有用途，不是完整的状态未变化证明。

FACT：当前 `TargetStateRef.PrimaryWorkValue` 为 float，`FeatureVector` 为分号分隔有限 float；目标加载器不重算 `SelectionFeatureHash`，也不验证真实 DOD 重放状态。选择器实现时必须明确数值编码及版本，不能仅填非零值。

FACT：`LoadCameraManifest` 要求给定场景完整的 64 点，拒绝额外未知场景；对已经冻结的六场景相机文件再次只传一个场景，不会自动过滤其余相机行。空目标文件目前被拒绝。

FACT：准备脚本的源码与构建摘要描述准备时的版本。PREP-03 重建程序后，应验证并沿用冻结输入字节，同时记录本次执行源码/二进制身份；不能要求执行器与旧输入准备二进制哈希相同。

## 7. 待实现与不确定性

PLANNED：逐阶段执行与观察、无副作用拓扑/网格规划、获准特征探测、真实阶段输入身份、确定性选择器及发现进程，均由 PREP-03 小规划定义；本次只扫描与撰写文档，没有实现这些能力。

UNCERTAIN：尚未通过执行验证“公共阶段拆分后的结果等价”“元数据规划与实际网格逐项相符”及六场景的实际有效目标覆盖。这些必须作为实施验收，不能由文件存在或当前回归成绩推导。

PLANNED：完整策略计时、诊断隔离、冻结拓扑请求线程上限修复和配对分析仍归 PREP-04；图形上传与正式恢复仍受 CPU gate 约束。

## 8. 实施后的职责与调用关系

FACT：`DataOrientedRoamPipeline::Build` 与 `BuildWithPassObserver` 共用 `BuildInternal`。后者调用 `PrepareDataOrientedRoamFrame`、`ExecuteDataOrientedRoamCpuPasses`，随后完成拓扑/网格验证、叶统计、迟滞路径交换及原阶段证据收尾。旧 `PassExperiment::PrepareNextFrameState` 仍先深复制上一帧，但复用同一帧准备；测量后的串行推进也调用公共单阶段入口。

FACT：`DataOrientedRoamPassExecution.h/.cpp` 拥有合并评分、合并拓扑、细分评分、细分拓扑、网格提交的固定顺序。`ExecuteDataOrientedRoamPass` 的拓扑分支调用 `CommitScoredMergeTopology` / `CommitScoredSplitTopology`，不重新整批评分。原 `MergeWithDiamondQueue` / `RefineWithSplitQueue` 保留为评分加提交的兼容包装。同步观察回调借用 const 状态，仅在当前调用有效；观察成本不进入五阶段包络，整帧更新耗时仍可能包含观察成本。

FACT：`DataOrientedRoamTopologyPlan.h/.cpp` 拥有候选复制/排序、安全分类和分块预算截取。`PlanDataOrientedRoamSplitTopology` / `PlanDataOrientedRoamMergeTopology` 的无候选参数版本复用长期队列快照；传入候选版本也供现有冻结回放使用。结果拥有 `Candidates`、`Chunks`、内部/边界/计划调度/非空分块计数。生产把计数显式加入 Stats；串行普通路径不构造计划。`SafeInterior*ChunkId` 是规划和真实提交复核的共同判据；事务、队列更新、线程与串行收敛仍归 Topology。

FACT：`DataOrientedRoamMeshState.h` 将 `DataOrientedRoamMeshMetadata` 与 `TerrainMeshData` 组合为原网格对象。基础节点/位置类型移至 Types.h，MeshState.h 不依赖完整 State.h。State、Pipeline、PassExperiment、Topology、Validation 与既有测试的直接字段访问已适配 `Metadata`。

FACT：`DataOrientedRoamMeshPlanInput` 只含只读节点池、活动叶数组与帧序号，无法取得几何数组。`BeginDataOrientedRoamMeshPlan` 负责代数与本帧记录重置；`ApplyDataOrientedRoamMeshPlan` 原位重放槽位修改并报告是否重新初始化；`FinalizeDataOrientedRoamMeshPlan` 生成范围和下一帧调试过渡叶。MeshEmit 根据计划结果维护几何容量并写入；Probe 只复制 Metadata 后运行同一规则。追加、尾槽填洞、尾槽子节点合并、错误记录回退与调试属性老化继续共用。

## 9. 探测、身份与选择契约

FACT：`ProbeDataOrientedRoamPassWorkload` 从真实队列、活动叶、预算和修改条目读取执行前量；拓扑分类来自只读计划，网格写入与区间来自元数据副本。返回 `DataOrientedRoamPassWorkload` 值，不返回计划或状态引用。最大活动深度扫描当前活动叶，预算余量按叶数量截断到非负；拓扑预算截取仍使用算法自身的串行预算字段。所有比率零分母得到 0，选择值为 binary32。

FACT：`HashDataOrientedRoamPassInput` 的版本为 1。编码字段包括阶段、公共设置、帧序号、视图、两种预算、节点 SoA 有效数组、活动数组、堆顺序、成员反向位置、迟滞集合、误差树、网格元数据和有效几何。逐字段编码避免结构填充；容器长度和有效元素参与，预留容量、指针、Stats、诊断开关、待测策略及线程请求不参与。迟滞散列表只排序成员，其余有序数组不排序。高度图内容身份仍由清单及 Python SHA-256 绑定；该哈希不是跨编译器配置指纹。旧评分、拓扑及网格回放哈希未改编码。

FACT：`FormalExperimentTargetSelector.h/.cpp` 不依赖 DOD、文件系统、时钟或发现记录。`TargetSelectionCandidate` 只含场景/阶段/采样、主要值和原始辅助向量；每次 `SelectTargetStates` 接收一个场景/阶段，空组合法，重复采样或非法数值抛异常。版本 1、种子 20260830，配置仅允许 4 或 8 点。按主要值与采样排序分为三层，UTF-8 FNV-1a 裁决层内顺序；P5/P95 线性插值只用于覆盖距离，补点仍使用最初层散列破同距。

FACT：目标 `FeatureVector` 保留原始向量，9 位有效数字支持 binary32 往返；`HashTargetSelectionFeatures` 编码 uint32 选择版本、uint32 阶段和主要值/辅助值的 binary32 位模式。当前 Windows x64 实现使用小端标量编码。`selectionRank` 按低、中、高、覆盖顺序从 0 连续递增。

## 10. 发现运行与追溯

FACT：新配置 `cpu-workload-discovery` 要求冻结场景、完整冻结相机和不存在的输出目录，允许 `--targets-per-pass 4|8`，拒绝目标输入、再次场景过滤及普通策略/计时覆盖。`FormalExperimentRunner` 拥有目录和文件；`FormalWorkloadDiscovery` 为每场景创建独立 Pipeline，从 k=0 顺序运行 64 帧。编排只消费 DOD 探测值及公开完成统计，不直接读取节点、队列或槽位。

FACT：每帧五行在帧末诊断后发布。初始化行保留且不可选，无工作行标 `no_work`；失败或中断不成为可测目标。发现 CSV 为 v2，场景/相机/目标仍为 v1。覆盖报告每场景/阶段都有一行，候选不足不重复补点。全部组无目标时不写空目标清单，摘要为 `targets_unavailable`；正常冻结协议中细分评分通常始终有工作，因此该分支是输出契约的防御性处理，不是本次真实运行结果。

FACT：发现摘要分别统计有效、无工作和失败行。目标先写 `target-states.pending.csv` 并通过现有加载器重读，随后才改名为标准入口；若后续摘要或脚本身份核验失败，标准目标文件改名为 `rejected-target-states.csv`。C++ 摘要表示算法发现状态，外层 `run-metadata.json` 表示包含文件/源码身份核验的整次尝试状态，下阶段必须确认后者已完成。

FACT：`cpu_pilot_support.py` 从原准备脚本提取文件、源码、Git、环境及 CSV 读取辅助；准备入口保持原状态语义。`discover_cpu_pilot.py` 核对 PREP-02 `inputs_ready`、冻结文件实际字节、资产 SHA-256，再记录本次源码、程序、构建与环境。旧准备版本与当前执行版本可以不同，当前执行的前后身份必须一致。脚本独占新尝试目录，调用真实 CLI，重读唯一记录键集合、目标引用、连续序号和覆盖摘要后才标 `discovery_complete`。

## 11. 已取得的证据与后续验证

FACT：集成试跑 `benchmark-output/prep-03/discovery-integration-01/` 生成 1920 行、113 个目标；三个 Test129 合并拓扑组不足，候选数分别为 3、2、0。其他 27 组各 4 点。该数字只表明候选覆盖，不表示性能交叉。

FACT：`DataOrientedRoamPassExecutionTests` 对照生产观察与上一帧副本逐阶段推进；独立验证模式仅加载清单并从根重新构建目标身份。`DataOrientedRoamWorkloadProbeTests` 使用显式源状态快照覆盖容器内容、顺序、容量、地址、统计、预算和几何，另核对预计写入/区间。`ExperimentTargetSelectorTests` 使用独立 FNV 字节夹具验证选择规则。真实进程测试为 `test_cpu_workload_discovery.py`，最终运行结果记录于小规划和架构审查。

FACT：明确拓扑夹具覆盖无可复用子节点、深度限制、强制邻居、同分序号/路径排序、先分类后截取预算及单个/多个非空块；网格夹具覆盖尾槽填洞。中途相机校验失败夹具在第 10 帧终止并保留先前 50 行，返回失败且无目标。Python 独立实现按原始 binary32 特征重算全部 30 组选择；改变全部发现耗时列不改变其选择结果。

FACT：最终独立产物位于 `benchmark-output/prep-03/cpu-discovery-20260909/`，沿用 PREP-02 原冻结输入并完成全部前后身份核验。1920 行中有效 1596、无工作 324、失败 0；目标 113 个。原冻结输入与重新准备输入的目标字节相同，OpenGL 与 D3D12 的目标字节也相同。源快照和目标摘要见小规划第 8 节及该目录的元数据。

FACT：两后端完整应用构建成功。OpenGL 首轮 31/32 通过，旧输入准备在并行构建期间超时后独立复核通过，全部 32 项均取得通过结果；D3D12 全量 32/32 通过，195.43 秒。最终注释覆盖 `src` 15.6%、DOD 20.2%，新增/迁移注释格式及差异检查通过。架构审查已同步；验收后经用户许可，代码提交为 `4d7ab68`，文档单独归档，未推送。提交前仅额外清理 Python 公共脚本末尾空行，AST 不变；发现产物仍记录验收时的源码快照。

## 12. 核心执行路径索引

```text
生产 / 同步发现观察
Pipeline::BuildInternal → PrepareDataOrientedRoamFrame
  → ExecuteDataOrientedRoamCpuPasses → observer(const state, pass)
    → Probe / PassInput → TopologyPlan 或 MeshPlan 元数据副本
  → ExecuteDataOrientedRoamPass → 评分 / 已评分拓扑 / MeshEmit
  → Pipeline 诊断、统计与迟滞收尾

发现与选择
discover_cpu_pilot.py → 冻结输入摘要核验 → 实际 CLI
  → FormalExperimentRunner → DiscoverCpuScenarioWorkloads
    → 独立场景 Pipeline → 帧末有效性 → SelectTargetStates
  → 发现 / 覆盖 / 目标 / 摘要输出 → Python 前后身份与完整性核验
```

## 13. 未确认事项

UNCERTAIN：本阶段扫描未发现尚未确认的关键所有权或调用关系；实际 CPU 配对可靠性、噪声界限和性能交叉尚未测量，继续归 PREP-04/05。当前输入身份不承诺跨编译器位级一致；图形上传和正式恢复仍未实现。
