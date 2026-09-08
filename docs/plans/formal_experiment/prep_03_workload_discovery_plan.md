# PREP-03：CPU 阶段边界、规划特征与工作负载发现

> 规划类型：小规划，落实已确认大规划中的一个阶段\
> 日期：2026-09-09\
> 状态：已完成实现、双后端验证及架构核查；经用户许可，代码提交为 `4d7ab68`，文档单独归档\
> 起点：`4c78cb6`，PREP-01/02 及注释整改已入库，工作区干净\
> 上位规划：[正式实验准备大规划](formal_experiment_preparation_major_plan.md)第 7.5、8.1/8.2、9.1、11 节\
> 当前事实：[CPU 阶段边界与工作负载基线](../../codebase/formal_experiment/cpu_pass_boundary_and_workload_baseline.md)

## 1. 目标、范围与验收口径

PREP-02 已能冻结六个场景的相机输入，但还没有真实 CPU 发现记录。现有生产入口将评分与拓扑合在一起，配对入口另写推进顺序；候选分块会修改统计，槽位更新会改动真实几何数组，不能直接作为只读探测能力。

本阶段建立五个 CPU 阶段的公共物理边界，复用生产规则取得执行前与规划期间工作量，沿完整轨迹生成发现记录，再按确定规则选择目标。交付供 PREP-04 重建和配对使用的真实目标，仍不回答串行或并行谁更快。

实施前依据：开发规范第 5/6 节、规划及事实指南、PREP-01/02 小规划与审查、上位规划和 [24 号协议](../../parallel-roam/24-formal-experiment-preparation-plan.md)第 3.4、6、7 节。已扫描生产帧准备、配对构造、队列快照、安全分块、网格修改/写入、已有共享清单、记录、脚本和相邻测试。

**范围内**：轨迹 A、NO 相机、两地形各三预算；五阶段边界；只读拓扑规划；隔离的网格元数据规划；无性能标签的获准特征；阶段输入身份；确定性选择；无窗口发现入口、摘要与失败输出。

**范围外**：策略计时、AB/BA/六排列测量、噪声标定、实际线程上限修复、计时诊断隔离、统计胜负、轨迹 B/C、图形上传、正式配置指纹和恢复。不得通过跑旧配对入口再丢弃时间列来替代发现。

**完成标准**：默认六场景产生 `6 × 64 × 5 = 1920` 条发现记录，包含初始化和无工作状态；只从 `k=1..63` 的有效有工作状态选取唯一目标，默认最多 `6 × 5 × 4 = 120` 个目标。全部输入、选择版本、覆盖不足与错误可追溯。源状态不被探测改变，预计网格写入和区间与随后真实执行相符，修改耗时字段不影响目标选择。

## 2. 架构判断与依赖

```text
discover_cpu_pilot.py → 无窗口 cpu-workload-discovery 配置
  → FormalExperimentRunner：请求、清单、目录、输出状态
    → FormalWorkloadDiscovery：顺序运行场景，转换探测值，调用选择器
      → DOD Pipeline / PassExecution：真实五阶段顺序
        → WorkloadProbe → TopologyPlan / MeshPlan / PassInput
      → FormalExperimentTargetSelector：纯值选择与覆盖报告
```

- **Reuse**：PREP-02 清单、冻结相机、CSV 编解码、源配置、Python 摘要能力；既有评分/事务/收敛/网格写入、线程池及结果验证。
- **Split / Extend**：DOD 帧准备与阶段分发、拓扑规划、网格元数据和写入；共享层的发现记录及 CSV；既有无窗口配置和运行器。
- **Create**：只读探测、阶段输入身份、确定性选择器、发现编排与发现脚本。新文件承担独立职责，不在 `Topology.cpp`、`Pipeline.cpp` 或 `TerrainLodBenchmark.cpp` 继续堆放发现逻辑。

DOD 不依赖 Benchmark、正式清单、CSV 或 Python；共享选择器不依赖 DOD、文件系统和计时器。Benchmark 只转换探测返回的值，不直接读取节点池、队列或槽位。生产路径没有观察器时不生成发现记录、不复制规划元数据、不计算发现输入哈希。

采用同步观察回调接入真实边界，采用纯值规划结果分离计算与提交；不引入通用事件总线、策略注册框架或持久计划缓存。

## 3. 关键组件与接口

以下签名表达经用户确认的边界；普通局部 helper 可在实现时细化，不得改变所有权与依赖方向。

### 3.1 公共阶段执行与观察

`DataOrientedRoamPassExecution.h/.cpp` 提供三个职责一致的入口：

- `PrepareDataOrientedRoamFrame(state, heightMap, terrainSize, heightScale, view, settings)`：复用生产准备规则，负责本帧输入、序号、统计初始化、容量及必要重置。流水线与上一帧状态副本的构造共用；实验仍先检查不允许改变的深度/预算。
- `ExecuteDataOrientedRoamPass(state, passId)`：执行一个满足前置条件的阶段，动作来自当前策略。拓扑阶段消费已评分队列，不再次刷新评分；网格阶段包含规划、写入与区间收尾。
- `ExecuteDataOrientedRoamCpuPasses(state, observer)`：固定五阶段顺序，在每阶段之前调用可选的只读观察回调，再执行该阶段。阶段包络统计保持生产语义，帧末诊断、细分路径交换和报告收尾仍由 Pipeline 负责。

`DataOrientedRoamPipeline` 保留原 `Build`，增加 DOD 内部公共入口 `BuildWithPassObserver(..., observer)`，二者共用 `BuildInternal` 与上述分发。不改 `ITerrainLodAlgorithm`、GUI 或渲染接口。观察器仅在本次同步调用有效，不持有状态引用到下一帧，不重入同一流水线；异常向上报告，本次场景终止。

| 观察边界 | 已完成 | 尚未开始 |
| --- | --- | --- |
| 合并评分 | 帧准备及必要根初始化 | 合并队列评分刷新/建堆 |
| 合并拓扑 | 合并评分刷新/建堆 | 候选规划、合并事务与串行收敛 |
| 细分评分 | 合并拓扑收敛 | 细分队列评分刷新/建堆 |
| 细分拓扑 | 细分评分刷新/建堆 | 候选规划、细分及预算交换/收敛 |
| 网格提交 | 本帧全部拓扑事务和修改记录 | 消费网格修改、槽位规划、几何写入及区间收尾 |

`MergeWithDiamondQueue`、`RefineWithSplitQueue` 中“已评分之后”的部分形成独立拓扑入口，旧组合调用如仍有调用方则保留薄包装，否则移除。`PassExperiment` 的准备与串行推进改为复用上述入口，保留原测量块、策略数量和旧 CSV 语义。冻结拓扑中的强制诊断/线程覆盖留到 PREP-04，不在本阶段宣称旧配对计时已可靠。

### 3.2 拓扑计划

`BuildSplitTopologyPlan(const state)` / `BuildMergeTopologyPlan(const state)` 返回拥有候选快照、稳定顺序、分块和分类计数的临时值。复用队列快照、已有安全判据及排序，不提交事务、不修改队列/预算/统计。生产并行入口从同一计划读取分块并显式合入统计，实际提交前继续调用同一安全判据复核。

细分保留“分数降序、Sequence 破同分”的语义；合并保留“分数升序、路径编号破同分”。分别记录：原始候选、内部/边界候选、预算内计划分块候选、非空分块数。内部分类先于预算截取，禁止用计划调度数或最终提交数替代内部数量。

现有空间网格是 8×8，共 64 块，线程上限是 8，两者分开命名。协议特征 `nonEmptyChunkCount / 8` 保持原定义，可以大于 1；不擅自改成除以 64。实际提前提交数和串行尾部不进入选择输入。

串行生产路径仍直接收敛，不因为发现能力而无条件建立快照或分块。探测时另建临时计划并销毁；PREP-04 的并行计时必须重新承担自己的规划成本，不能复用发现计划来缩短计时。

### 3.3 网格元数据与写入

创建 `DataOrientedRoamMeshState.h`，从 State.h 移出网格状态类型，以组合方式分开：

- `DataOrientedRoamMeshMetadata`：槽位映射、脏代数、脏槽位、区间、调试过渡叶、修改记录、代数和标志。
- `DataOrientedRoamIncrementalMesh`：持有 `Metadata` 与真实 `TerrainMeshData Data`，仍归 `DataOrientedRoamState` 所有。

网格类型所需的节点下标、位置及无效值声明移到已有 `DataOrientedRoamTypes.h`；State.h 包含 MeshState.h，后者只依赖基础类型，不反向包含完整 State，避免循环依赖。

`DataOrientedRoamMeshPlan.h/.cpp` 接收只读节点/活动叶输入与可写网格元数据，复用同一套修改重放、尾槽填洞、脏槽位去重、区间生成和调试过渡规则。该入口不能访问或调整真实顶点/索引数组。

- 生产：原地更新自己持有的元数据，然后由 MeshEmit 调整数据数组、写入顶点/索引并发布统计；不增加每帧完整元数据复制。保持已有预留容量语义，避免缩小性能基线。
- 探测：只复制元数据，在副本上运行相同规划，返回预计写入数量、区间、完整初始化/回退原因。不得复制完整 State 或网格几何后运行真实提交来冒充规划。
- 初始化、代数回绕、上一帧调试属性消退、同帧多次修改及槽位不一致回退都进入共享规则。原地规划后必须把下一帧所需的调试过渡叶保留下来。

计划既保留规范化脏槽位，也明确全量初始化/回退标志。发现以固定串行增量来源策略的预计写入为准；`SerialFull` 的完整重写请求由实际策略执行解释，不能把该对照策略的全量范围混入所有目标的共同脏比例。

### 3.4 特征与阶段输入身份

`ProbeDataOrientedRoamPassWorkload(const state, passId)` 返回 DOD 值类型，不返回可写状态、节点指针或长期计划句柄。计数从边界处的真实集合读取，不从刚清零或帧末的 Stats 反推。

| 阶段 | 主要工作量 | 允许的辅助选择字段 |
| --- | --- | --- |
| 合并评分 | `pre_mergeQueueEntryCount` | 条目数量、活动三角形/预算、最大活动深度/最大深度 |
| 细分评分 | `pre_splitQueueEntryCount` | 条目数量、活动三角形/预算、预算余量/预算、最大活动深度/最大深度 |
| 合并拓扑 | `planning_interiorCandidateCount + planning_boundaryCandidateCount` | 内部比例、边界比例、非空分块数/8、预算余量比例 |
| 细分拓扑 | 同上 | 同上；额外记录预算内计划数量用于解释，但不自行加入选择向量 |
| 网格提交 | `planning_dirtyTriangleCount / pre_activeTriangleCount` | 活动三角形/预算、拓扑修改条数/活动三角形、预计区间数/预计待写三角形 |

最大活动深度从当前活动叶计算，预算余量按当前活动叶数量截断到非负；拓扑调度仍遵守原算法的预算字段。所有比值分母为零时记 0。计数使用整数保存，投射到目标选择数值时统一为 binary32；选择器内部百分位与距离使用 double。计时与诊断成本另列，不加入获准特征。

创建 `DataOrientedRoamPassInput.h/.cpp`，提供版本化的 `HashDataOrientedRoamPassInput(state, passId)`，供探测和后续配对在同一边界重算。复用公共 FNV 编码能力，但不把旧的候选哈希直接当作完整阶段输入身份。

身份编码包含版本、阶段、当前视图/公共数值输入、更新序号，以及会影响该阶段的节点/邻接/活动索引、持久队列顺序与成员、迟滞/预算状态；网格阶段还包含元数据、调试标记及有效已有网格内容。逐字段编码，不哈希地址、padding、容量未使用尾部、耗时或策略测量结果。用于比较的动作和线程请求不进入共同冻结身份，源配置与资产 SHA-256 由清单和运行摘要绑定。旧回归哈希保持原语义，新身份使用独立版本。

`featureCollectionMs` 仅计特征计算与必要规划成本；身份编码和只读诊断耗时分别记录。源不变验收另用逐字段快照覆盖队列实际顺序、节点字段、原子预算值、网格元数据/有效内容与版本，不能只比较忽略顺序的规范化哈希。当前输入身份不宣称是 PREP-09 的跨入口文件配置指纹。

### 3.5 确定性目标选择

`FormalExperimentTargetSelector.h/.cpp` 定义独立 `TargetSelectionCandidate`、配置和返回值；输入只含场景/阶段/采样身份、主要工作量及固定顺序辅助向量，不含整个发现记录、时长或实际策略行为。

`SelectTargetStates(candidates, config)` 无文件 I/O，不创建随机设备，不调用 DOD。固定种子 `20260830`，支持明确版本的两种配置：pilot 默认每组 4 点，即低/中/高各 1 点加 1 个覆盖补点；8 点配置各层 2 点加 2 个覆盖补点。输出记录选择器版本和目标数，不能把 4 点覆盖标为完成 8 点协议。

选择规则沿用 24 号协议，补齐未明确的编码细节：

1. 按主要工作量、采样编号升序，层编号为 `min(2, floor(3r/n))`。
2. 对 UTF-8 文本 `20260830|scenarioId|passId|sampleIndex|stratum` 计算 FNV-1a 64；`passId` 用公共名称，`stratum` 固定为 `low/middle/high`。各层按散列、采样编号取配额。
3. 在该场景/阶段的全部有效候选上计算辅助维度 P5/P95，位置 `p(n-1)` 线性插值；差值为零时该维为 0，否则归一化并截断到 0..1。
4. 未满名额由最大化“到已选集合的最小欧氏距离”补齐；实现可比较距离平方，避免无意义开方。同距按候选最初所属层的上述散列、采样编号裁决，输出层标为 `coverage`。
5. 无候选时返回空选择及覆盖不足；少于目标数时选完所有不同采样点，不复制补齐。相同主要值、全部辅助维相同也必须确定性完成。

`selectionRank` 是同场景/阶段整个结果的连续序号，从 0 开始；顺序固定为低、中、高、覆盖补点。`featureVector` 保留未做 P5/P95 归一化的获准辅助向量，以便重算；`SelectionFeatureHash` 编码选择器版本、阶段、主要值与该向量的 binary32 位模式。归一化仅服务于选点，不能写成唯一原始证据。

### 3.6 发现运行与记录

`FormalWorkloadDiscovery.h/.cpp` 接收已校验场景与冻结相机行。每场景独立加载资产、创建流水线，从根连续运行 `k=0..63`，五阶段观察器只调用 DOD 探测接口；每帧结束后再汇总诊断结果并判定当帧记录有效性。来源配置复用 PREP-02 固定串行增量设置，开启正确性证据，不执行任何策略计时。

每帧恰有五条记录；无工作标 `no_work`，初始化点仍保存并注明不可选择。实际输入/预算/队列/拓扑检查失败时保留失败原因并使场景失败，禁止在后面换点补齐。未检查的证据明确标记未检查，不能用零违规数量冒充检查完成。失败或不完整场景不发布可供配对消费的目标。

扩展 `CpuDiscoveryRecord`，保留原身份和通用工作量列，增加上述 `pre_`/`planning_` 字段、帧后验证状态、不可选择原因、规划回退原因及独立采集成本。发现 CSV 升至 v2；场景/相机/目标 v1 和旧配对格式保持兼容，不整体增加所有 schema 的版本。

每场景/阶段输出覆盖记录：请求目标数、有效候选数、实际目标数、各层数量、缺少数量、选择器版本/种子与不足原因。`TargetStateRef` 沿用 v1 写入器；版本及选择配置在发现摘要、覆盖记录和运行元数据中绑定。目标的相机、重放和特征身份均来自同一条已校验发现记录，不填合成值。

若全部组都无目标，发现可完成并报告覆盖不足，但不生成一个无法被现有加载器接受的空目标清单；摘要明确 `targets_unavailable`。有目标时重新用 `LoadTargetManifest` 校验实际落盘文件。每组不足是研究输入覆盖事实，不等于输入解析失败，也不代表存在性能交叉。

## 4. 目录与文件归属

下表为相对仓库根目录的实际计划；标为 Create 的文件目前不存在。网格字段组合调整仅在直接调用方做机械适配，不借机重构其他算法。

| 操作 | 文件 | 职责及独立理由 |
| --- | --- | --- |
| Create / Split | `src/algorithms/data_oriented_roam/DataOrientedRoamPassExecution.h/.cpp` | 共享帧准备、单阶段执行和固定顺序观察；消除生产/实验两套边界 |
| Create / Split | 同目录 `DataOrientedRoamTopologyPlan.h/.cpp` | 候选快照、排序、只读安全分类与分块；事务提交继续归 Topology |
| Create / Split | 同目录 `DataOrientedRoamMeshState.h` | 组合元数据与几何存储的类型；规划副本无需携带顶点/索引 |
| Create / Split | 同目录 `DataOrientedRoamMeshPlan.h/.cpp` | 槽位重放、脏集合、区间与调试过渡规划；与几何采样和写入分离 |
| Create | 同目录 `DataOrientedRoamWorkloadProbe.h/.cpp` | 将 DOD 内部状态/计划转换为窄特征值并记录采集成本 |
| Create | 同目录 `DataOrientedRoamPassInput.h/.cpp` | 版本化阶段输入身份编码；供发现与 PREP-04 共同重建，不参与选点 |
| Extend | 同目录 `DataOrientedRoamPipeline.h/.cpp` | 观察入口及共同执行接入；继续拥有状态、线程池和帧末收尾 |
| Split / Extend | 同目录 `DataOrientedRoamTopology.h/.cpp`、`DataOrientedRoamMeshEmit.h/.cpp` | 迁出规划职责，保留事务、策略执行、顶点/索引写入与统计发布 |
| Split / Extend | 同目录 `DataOrientedRoamTypes.h`、`DataOrientedRoamState.h/.cpp` | 基础下标类型归属、组合网格状态和复制/重置适配，保持借用语义 |
| Extend | 同目录 `DataOrientedRoamPassExperiment.cpp`、`DataOrientedRoamValidation.cpp` | 复用阶段准备/推进、适配网格字段；保留旧回归模式与诊断职责 |
| Reuse | 同目录 `DataOrientedRoamQueues.h/.cpp`、Scoring、ThreadPool | 直接使用现有刷新、候选快照、安全评分和同步任务能力，不复制算法 |
| Create | `src/experiment/formal/FormalExperimentTargetSelector.h/.cpp` | 获准特征值类型、纯选择算法与覆盖结果，避免输出记录泄漏耗时 |
| Extend | 同目录 `FormalExperimentRecords.h`、`FormalExperimentCsv.h/.cpp` | 发现 v2、覆盖与发现完成摘要；既有输入/配对格式保持各自版本 |
| Reuse | 同目录 `FormalExperimentManifest.h/.cpp`、`FormalExperimentCamera.h/.cpp` | 读取已冻结输入、严格引用检查、目标序列化；不复制轨迹公式 |
| Create | `src/benchmark/formal/FormalWorkloadDiscovery.h/.cpp` | 场景连续推进、只读探测转换、有效性与选择编排，独立于计时配对 |
| Extend | 同目录 `FormalExperimentRunner.h/.cpp` | 发现请求路由、独占输出目录、逐场景记录及最终摘要/错误返回 |
| Extend | `src/benchmark/TerrainLodBenchmark.h/.cpp`、`TerrainLodBenchmarkCommandLine.cpp` | 新配置和选择数量参数的纯解析/分流，不读取 DOD 内部字段 |
| Create / Split | `scripts/cpu_pilot_support.py` | 从准备脚本提取文件/源码/环境摘要等公共辅助，供两个入口复用 |
| Extend | `scripts/prepare_cpu_pilot.py` | 改为导入公共辅助，保留原参数、输出目录与准备状态语义 |
| Create | `scripts/discover_cpu_pilot.py` | 验证冻结输入摘要、启动发现进程、核对输出与记录本轮来源；不做选点或统计推断 |
| Extend | 根与 `tests/CMakeLists.txt` | 注册共享 DOD/formal 源和语义测试；两个后端使用同一 CPU 实现 |
| Create | `tests/DataOrientedRoamPassExecutionTests.cpp` | 真实边界、准备复用和串行/最大安全并行轨迹等价 |
| Create | `tests/DataOrientedRoamWorkloadProbeTests.cpp` | 源状态不变、拓扑分类/预算和网格预计值对实际输出 |
| Create | `tests/ExperimentTargetSelectorTests.cpp` | 分层、并列、百分位、补点、4/8 点、不足与时间污染隔离 |
| Create | `tests/test_cpu_workload_discovery.py` | 实际进程的记录完整性、目标重放、摘要篡改、失败及目录保护 |
| Extend | `tests/DataOrientedRoamPassPolicyTests.cpp`、`ExperimentManifestTests.cpp`、`FormalExperimentCommandLineTests.cpp`、`test_cpu_pilot_preparation.py` | 字段迁移、版本与入口回归，不因提取降低原断言 |

测试若共用完整状态快照和固定轨迹夹具，抽到 `tests/DataOrientedRoamExperimentTestSupport.h`，仅供测试使用；不将验证 helper 加入生产公开接口。新增源文件的公共类型和非显然接口使用三行 `/// <summary>`，关键流程用短注释解释约束，迁移的历史长注释同步整理。

## 5. 命令、文件生命周期与追溯

建议入口：

```text
python scripts/discover_cpu_pilot.py
  --prepared-input-dir <已有 PREP-02 尝试目录>
  --executable <本轮应用> --build-preset <预设>
  --output-dir <不存在的新尝试目录>
  [--targets-per-pass 4|8]
```

脚本使用准备目录中实际冻结的场景和相机，不重新生成相机。先验证准备状态与已记录的冻结文件/资产摘要，再记录本轮源码、二进制、配置和环境；旧准备二进制不同不构成输入失效。运行前后重新核对依赖文件，任一变化使本轮失败。

内部应用入口为 `--benchmark --profile cpu-workload-discovery --scenario-manifest ... --camera-manifest ... --output-dir ... --targets-per-pass 4|8`。复用现有 `FormalInputRequest` 的文件路径，发现配置要求相机清单存在，拒绝目标输入、普通策略覆盖和再次过滤场景；发现读取准备目录已经确定的完整场景集合，单场景运行先准备对应单场景输入。避免把六场景相机文件与单场景参数拼接成无效引用。

发现状态与 `inputs_ready` 分开：`preparing → discovering → discovery_complete`，错误为 `failed`，中断保留未完成状态。既有目录含空目录一律拒绝，失败文件保留，不恢复、不覆盖输入目录，不把 CSV 存在当作成功。

输出包括 `discovery.csv`、`target-coverage.csv`、有目标时的 `target-states.csv`、`discovery-summary.csv`、本轮 `run-metadata.json` 和日志。摘要记录预期/实际场景、采样和阶段记录数、有效/无工作/失败数、选择器版本及覆盖不足。脚本重新核对键集合唯一性与目标身份、输出摘要后才写完成状态；不发布正式实验完成标记。

发现 CSV 中的耗时属于探测或诊断成本，受观察器影响的整帧统计不得解释为策略性能。PREP-04 必须在新进程、新尝试目录重新重建目标状态，不能直接使用发现留下的热计划或可变状态。

## 6. 分步实施与验证

本阶段内部按下表顺序推进，保持每步能构建和回归；不是另设 PREP 大阶段。

| 步骤 | 工作 | 必须取得的证据 |
| --- | --- | --- |
| 03-1 | 保存当前基线；提取帧准备与阶段执行、观察器、旧配对推进适配 | 同场景串行/最大安全并行固定轨迹的规范化拓扑、网格、阶段动作与队列不变量一致；五边界顺序准确且评分不重复 |
| 03-2 | 提取只读拓扑计划、组合网格元数据及共享网格计划 | 生产分类/排序/预算截取保留；无副作用计划与实际槽位、脏集合、区间、版本及回退原因一致 |
| 03-3 | 阶段输入身份和获准特征探测 | 在每个真实边界多次探测前后源节点、队列顺序、预算、网格内容/版本不变；有效输入变化可影响相应身份，计时/诊断字段变化不影响身份 |
| 03-4 | 选择器、发现 v2、覆盖和目标绑定 | 已知排序/散列/百分位夹具、输入重排、全相等/零分母/不足、4/8 点与无工作排除；时间字段改为任意合法耗时值不影响目标字节 |
| 03-5 | 发现编排、CLI、Python 辅助提取和追溯接入 | 六场景 1920 行与单场景 320 行、目标重新加载及重建、两个独立进程选择相同、错误/中断/已有目录行为正确 |
| 03-6 | 双后端完整应用构建与 CTest、逐项规范和架构复核 | 所有新增与既有测试实际注册；源事实、规划实现结果和架构审查同步；不以此替代 PREP-04/05 验收 |

关键语义用例：

- 初始化 `k=0` 与非初始化帧、无工作队列、阈值附近和最大预算；边界观察与从上一帧副本按公共准备/执行到达的状态对应，帧准备只执行一次。
- 拓扑内部/边界全覆盖、内部候选超过预算、无可复用子节点、强制邻居、单个与多个非空分块；分类总和等于快照数，计划提交集合是内部集合的预算内子集，实际提交不被当成规划事实。
- 网格追加、尾槽填洞、保留子节点恰在尾槽、连续细分/合并、删除后过期脏槽、调试属性消退、初始化/错误记录回退、代数回绕、零脏项；计划阶段不采样高度、不调整顶点/索引容器。
- 输入身份对队列顺序、节点/邻接、迟滞、预算、相机和网格元数据的相关变化敏感；对象地址与计时变化不进入身份。源不变检查与结果等价检查分开。
- 选择器在 0/1/2/3/4/7/8/更多候选、相同主要值、重复采样键、非法数值、散列/距离并列及低层不足时均有确定结果；相同特征不能少选或重复选点。
- 发现完成后，测试只使用相同源码与输入从根重建所选点，重算阶段输入和特征身份，逐条核对目标；不通过执行串并行计时来完成这项检查。
- 准备脚本提取前后参数和状态不变；篡改相机/资产/运行中源码、缺阶段行、重复记录、半途失败和全组无目标分别验证，失败产物不得伪装为可测目标。

基线和验证输出使用新的 `benchmark-output/prep-03/` 尝试子目录，按本地排除规则保存，不覆盖 PREP-01/02 原始输出。既有功能测试保留，不为纯文件移动编写镜像式测试。

## 7. 风险、取舍与待确认决策

| 风险或歧义 | 本规划采用的处理 |
| --- | --- |
| 阶段拆分改变评分次数、预算交换或已有耗时归属 | 先固定前后轨迹与边界证据；共享物理执行，不为探测重写拓扑算法；发现不作性能比较 |
| 网格提取引入每帧大复制或遗漏回退/调试写入 | 类型分离，生产原地计划、探测复制元数据；逐项比较输出并限制写入能力 |
| 当前分类函数暗中写 Stats | 计划返回计数，生产显式合入；探测只消费返回值，实际提交的动态再检查保留 |
| 4 点 pilot 与原协议 8 点混淆 | 两种明确选择配置，默认 4 点；覆盖摘要写请求数量和版本，8 点只验证通用规则 |
| 选择字符串、补点散列和数值精度存在协议空白 | 按第 3.5 节明确字面量、原始层散列和 binary32/double 边界，作为版本契约供本次 Review |
| 既有哈希不能证明源完全未改 | 新阶段输入身份与逐字段无副作用测试分开；保留旧回归身份，不冒充完整文件配置指纹 |
| 暂存所有几何/状态导致发现内存扩大 | 五阶段仅保留值记录；拓扑计划与网格元数据副本在当前观察调用结束释放，整场景不保存状态快照 |
| PREP-02 冻结文件、过滤与空目标契约被破坏 | 使用完整已准备集合；零目标显式报告不生成空目标文件；准备脚本回归保持 |

本次已确认的架构决策集中为三项：DOD 内增加同步边界观察并共用帧准备/执行；网格元数据与几何存储组合分离、生产和探测复用计划规则；纯值选择器默认 4 点并显式支持 8 点规则。新增 PassInput、MeshState 和脚本公共辅助是上述职责拆分所需文件，未越过既定模块层级。

备选方案及取舍：只在现有 PassExperiment 内追加发现逻辑，改动较少，但会继续维护独立推进顺序且与计时职责耦合；本规划选择共用生产阶段入口。复制完整 State 后执行真实网格更新，可以快速得到数量，但会执行几何生成并扩大探测成本，不满足只读规划的定义；选择隔离元数据规划。直接把整个发现记录传给选择器更省转换代码，但会暴露耗时和执行后字段；选择显式构造获准特征值。

若实施发现必须改变公共 LOD/渲染接口、生产拓扑策略、图形资源生命周期或上述编码契约，应先说明并重新确认；不能通过静默放宽断言或输出字段掩盖问题。

## 8. 实现情况

已按用户确认的方案完成代码实现、双后端验证、独立发现和架构核查。实施与验收期间未提交；验收后经用户许可，代码提交为 `4d7ab68`，文档单独归档，未推送。

提交前的暂存检查补查了新增文件，并清理 `scripts/cpu_pilot_support.py` 末尾两行空行，Python AST 对比一致。以下发现产物及源码摘要保留验收时的真实快照，不改写为提交后的来源。

已完成公共五阶段入口、只读拓扑规划、独立网格元数据规划、阶段输入身份 v1、工作量探测、选择器 v1、发现 CSV v2、发现运行器及脚本。拓扑规划实际接口为 `PlanDataOrientedRoamSplitTopology` / `PlanDataOrientedRoamMergeTopology`；网格规划以 `DataOrientedRoamMeshPlanInput` 限制输入为只读节点池、活动叶集合和帧序号，不暴露几何数组。

已保存 `4c78cb6` 程序及固定轨迹基线。拆分后 24 帧策略回放的非计时字段与原始 CSV 一致，包括拓扑、网格、阶段动作和队列不变量。新增边界测试核对上一帧副本与生产观察边界；探测测试逐项快照节点、队列、预算、元数据和实际几何，验证探测只读及预计写入/区间与真实执行相符。选择器已验证独立散列夹具、4/8 点、同值同距、覆盖补点和输入重排。

完整追溯的独立发现沿 PREP-02 原冻结相机生成 1920 行，其中有效 1596 行、无工作 324 行、失败 0 行；最终选择 113 个目标。`test129-a-b20000` / `test129-a-b4096` / `test129-a-b512` 的合并拓扑分别只有 3 / 2 / 0 个有效候选，合计缺少 7 个目标；其他 27 组各 4 个。已生成逐组覆盖报告，没有复制补点。这些结果不证明性能交叉存在。

| 步骤 | 真实交付与验证 |
| --- | --- |
| 03-1 | 公共帧准备和五阶段执行/观察；24 帧策略基线非计时字段一致；上一帧副本与观察边界逐阶段对应 |
| 03-2 | 无副作用拓扑计划与共享网格元数据计划；明确夹具覆盖安全分类、同分排序、预算截取、强制邻居、多个非空块、填洞与回退 |
| 03-3 | 阶段输入 v1 和窄探测值；真实边界显式快照不变，原始工作量和预计写入/区间与随后执行一致 |
| 03-4 | 选择器 v1、固定种子、4/8 点、发现 v2、目标 v1；C++ 散列夹具及 Python 独立重算 30 组一致，时间字段变化不影响选点 |
| 03-5 | 真实 CLI/脚本、六场景和单场景、根重建、独立进程重复、缺行/重复/目标错误/源码变化；中途失败保留 50 行且无目标，全组无目标用合成输出契约夹具验证 |
| 03-6 | 两后端完整应用已构建；OpenGL 32 项均取得通过结果；D3D12 全量 32/32 通过；规范、事实与架构审查已同步 |

OpenGL 首轮全量回归中旧 `cpu_pilot_preparation` 在并行构建期间触发 180 秒超时，其余 31 项通过；构建结束后该项独立复核以 16.98 秒通过，未改变测试门槛。最终发现进程测试包含本轮收尾用例，用时 260.11 秒。日志为 `build/relwithdebinfo-fetch/prep03-build.log`、`prep03-ctest.log` 和 `prep03-preparation-recheck.log`；D3D12 日志采用同名文件并位于对应构建目录。

D3D12 最终全量回归 32/32 通过，总计 195.43 秒，其中真实发现测试 160.15 秒。两后端的目标文件逐字节相同，对照结果位于 `benchmark-output/prep-03/cross-backend-comparison.json`。这些时长仅描述工程验收，不用于策略性能比较。

本次实际执行入口：

```powershell
python scripts/discover_cpu_pilot.py `
  --prepared-input-dir benchmark-output/prep-02/input-freeze-20260909 `
  --executable build/relwithdebinfo-fetch/bin/ParallelROAM.exe `
  --build-preset relwithdebinfo-fetch `
  --output-dir benchmark-output/prep-03/cpu-discovery-20260909
```

目录已存在后不能重复使用上述输出路径。完整产物为 `discovery.csv`、`target-coverage.csv`、`target-states.csv`、`discovery-summary.csv`、`run-metadata.json` 与 `discover.log`，状态为 `discovery_complete`。源快照 SHA-256 为 `ca142339b0a35afa3988efeb771cd98fb75b172a6fb31286303802b6f6c8d7a5`；目标文件 SHA-256 为 `0c18fd1f8e6635a3382a67606f9bbd47889d7f35dc98a3f364b3a8839815bd76`。原冻结输入与测试中新准备输入产生完全相同的目标文件字节。

最终核查按开发规范逐项比对职责、依赖、只读能力、生产额外复制限制、错误与版本契约，并同步[当前事实](../../codebase/formal_experiment/cpu_pass_boundary_and_workload_baseline.md)和[架构审查](../../reviews/formal_experiment/prep_03_workload_discovery_architecture_review.md)。新增/迁移注释使用三行 `/// <summary>`，格式和覆盖检查通过。PREP-04 的可靠配对与 PREP-05 的 CPU pilot gate 尚未执行。
