# CPU CBT 持续更新代码事实

> 日期：2026-09-12
> 范围：CPU-CBT-01/02 当前实现；第二阶段起点 `9db18d3`，实际采集源码身份见阶段审查
> 规划：[CPU-CBT-02](../../plans/cpu_cbt/cpu_cbt_02_parallel_bisect_plan.md)
> 审查：[并行模板填写阶段审查](../../reviews/cpu_cbt/cpu_cbt_02_parallel_bisect_review.md)；新串行局部性能验收待处理

## 1. 模块边界与文件

FACT：模块位于 `src/algorithms/cpu_cbt/`，命名空间为 `ParallelRoam::Algorithms::CpuCbt`。它包装参考纯 C++ 函数，提供独占持久状态上的同步更新和全量 CPU 网格。细分可选原参考或新模板内核，后者通过同步回调并行填写独占槽；其余阶段串行。上层仅有两个独立驱动，线程池归驱动持有，未接入应用算法注册、DOD 状态、渲染器或质量评价器。

| 文件 | 类型 / 入口及实际职责 |
| --- | --- |
| [CpuCbtState.h](../../../src/algorithms/cpu_cbt/CpuCbtState.h) | `CpuCbtSettings`、`CpuCbtState` 及配置检查、初始化声明；唯一持久状态所有者 |
| [CpuCbtState.cpp](../../../src/algorithms/cpu_cbt/CpuCbtState.cpp) | `ValidateCpuCbtSettings`、`InitializeCpuCbt`；检查参数、构造六基础状态，成功后移动替换 |
| [CpuCbtUpdate.h](../../../src/algorithms/cpu_cbt/CpuCbtUpdate.h) | `CpuCbtBisectImplementation`、`CpuCbtUpdateOptions`、`CpuCbtUpdateReport`、`UpdateCpuCbt`；实现选择、同步调用和计数/耗时契约 |
| [CpuCbtUpdate.cpp](../../../src/algorithms/cpu_cbt/CpuCbtUpdate.cpp) | 私有 `CheckInput`、`PlanningView` 和轮次编排；分类、索引转换、预算、细分路径选择、参考合并和占用发布 |
| [CpuCbtBisect.h](../../../src/algorithms/cpu_cbt/CpuCbtBisect.h) | `CpuCbtRangeTask/Executor`、`CpuCbtBisectTimings/Result`、`CommitCpuCbtBisects`；同步任务与临时下一代边界 |
| [CpuCbtBisect.cpp](../../../src/algorithms/cpu_cbt/CpuCbtBisect.cpp) | `TemplateRecord`、所有权预检、四模板局部填写、归并及顺序传播；只拥有单次调用临时数组，不持有线程池 |
| [CpuCbtMesh.h](../../../src/algorithms/cpu_cbt/CpuCbtMesh.h) | 分类四点与完整网格两个只读入口 |
| [CpuCbtMesh.cpp](../../../src/algorithms/cpu_cbt/CpuCbtMesh.cpp) | 私有 `Geometry` 包装参考几何，检查有效性和有限值；构造独立三角形顶点输出 |
| [CpuCbtTests.cpp](../../../tests/CpuCbtTests.cpp) | 解析模板、联合计划、旧合并重验、持续释放复用、预算；三档字段对照、所有权失败、1/2/4 线程、反向区间及真实并发夹具 |
| [CpuCbtProbe.cpp](../../../tests/CpuCbtProbe.cpp) | 两个固定自然输入的独立三档持续状态、字段诊断、线程证据、摘要、CSV 与分段统计；实验编排留在驱动 |
| [CpuCbtExecutionSupport.h](../../../tests/CpuCbtExecutionSupport.h) | `Tests::CpuCbtRangeEvidence`、`Tests::CpuCbtTestExecutor`；两个驱动共用固定分块与持久池的薄适配器 |
| [ProjectOptions.cmake](../../../cmake/ProjectOptions.cmake) | 默认 OFF 的 `PARALLEL_ROAM_BUILD_CPU_CBT_PROTOTYPE` |
| [tests/CMakeLists.txt](../../../tests/CMakeLists.txt) | 可选纯 C++ 参考库、包装库、两个可执行目标和 `CpuCbt` CTest |

FACT：依赖为 `驱动 → CpuCbtUpdate → CpuCbtBisect → Cbt2024 值类型`，包装同时复用公共地形类型和参考函数。驱动适配器另依赖既有 `DataOrientedRoamThreadPool`；核心不包含 DOD 头、不链接 DOD 更新管线。直接编译参考七组 `.cpp`：`CbtOccupancyTree`、`CbtBisectorTopology`、`CbtClassification`、`CbtSplitPlanner`、`CbtBisectCommit`、`CbtSimplifyCommit`、`CbtTerrainGeometry`。还包含其七个头和 `CbtGpuAbi.h`、`CbtGpuAbi.shared.h`；后两者提供数值/布局常量，没有 GPU 执行。

参考实际目录为 `third_party/RoamTesting/src/algorithms/cbt_2024/`，独立仓库提交 `d462089d5c2888cc09537a6a3f44a7c33cbf4499`，工作区未修改。目标包含目录先用当前项目 `src/`，再用参考 `src/`，因此公共 `ITerrainLodAlgorithm.h` 来自当前项目。参考依赖未作为根仓库内容导入。

## 2. 数据、所有权与生命周期

FACT：核心状态与报告为普通值类型，无继承。驱动拥有高度图、`CpuCbtState` 和可选线程适配器，函数同步借用；调用期间状态不允许外部并发读写。池内线程跨轮存在，但没有越过调用边界的后台更新。容器由 RAII 释放，无图形资源；驱动池析构时 `Shutdown` 并等待所有任务。

| 状态字段 | 含义、初始化及写入边界 |
| --- | --- |
| `Settings` | 默认尺度 30/4、面积阈值 50 像素平方、最大逻辑编号位长 20、预算 4096；初始化复制入状态，更新只读 |
| `DynamicCapacity` | 常量 131072；动态槽范围 `[0,131072)`，永久槽范围 `[131072,131078)` |
| `Occupancy` | `Capacity128K` 占用树，仅编码动态槽；每轮复制、置位/清位、`Reduce` 后发布 |
| `ControlPoints` | `BuildSquareCbtBaseTopology` 产生的基础控制点；初始化后供几何只读解码 |
| `HeapIds` | 完整物理数组，逻辑二叉编号；空闲槽为 0，六基础初始化为 8～13；不是物理槽编号 |
| `Neighbors` | 完整物理数组，`Previous/Next/Twin` 引用物理槽；开放边界为无效哨兵 |
| `Data` | 完整物理节点数据；活动项每轮分类前清除上轮模板、分配、传播上下文，参考提交形成下一代 |
| `ActiveIndices` | 所有活动物理槽的升序列表；由归约后动态索引加六永久槽重建，不跨轮保留候选队列 |
| `Generation` | 初始化为 0，每次成功更新加 1；即使没有拓扑变化也增加，不等同拓扑版本散列 |

`ValidateCpuCbtSettings(settings,error)` 要求尺度有限且正、高度比例有限且非负、面积阈值有限且正、位长在 `[4,20]`、预算在 `[6,131078]`。失败返回 false 并填写错误，成功清空错误。

`InitializeCpuCbt(state,settings,error)` 在临时 `next` 中创建空池和六基础状态，所有空槽分配/传播编号初始化为哨兵，归约后一次移动替换。配置或构造失败保留传入状态；重复调用相当于重新初始化。调用者应先成功初始化，再调用更新和网格入口。

FACT：当前没有几何缓存、稀疏复制或脏区更新。分类和网格各自重算全部活动叶几何，临时向量及映射每轮重建。参考及新细分提交都复制完整物理数组；即使请求为空也调用所选提交入口，新内核还创建完整物理长度的所有权标记。

## 3. 更新入口与控制流

`UpdateCpuCbt(CpuCbtState&, const HeightMap&, const TerrainLodViewInput&, const CpuCbtUpdateOptions&, const CpuCbtRangeExecutor&)` 返回值持有计数、耗时和错误，不持有临时数组引用。选项默认 `EvaluatePoolOnlyDemand=false`、`BisectImplementation=ReferenceSerial`；末尾执行器默认空，旧调用兼容。`LocalTemplates` 加空执行器使用调用线程；非空执行器同步完成局部填写。未知枚举返回失败，参考档不调用执行器。

1. **检查和准备。** `CheckInput` 验证配置、三个物理数组长度、活动数量与占用关系、活动列表唯一/排序、有效编号前缀和深度、活动位、六永久槽、视图尺寸和有限可逆 VP。复制 `Data`，记录 `N_before`、旧空闲槽和 `B-N_before`。
2. **几何和分类。** `BuildCpuCbtClassificationGeometry` 返回与活动列表同序的四点。每个活动项重置瞬态数据后调用参考 `EvaluateCbtClassification`；正细分进入根请求列表，非基础负分类标为可合并，仅偶数逻辑编号发出合并根。可见性写入标志，不决定预算计数。
3. **紧凑视图。** `PlanningView` 建立物理到活动局部编号的逆映射，生成仅含活动节点的 `CbtSplitPlanningNode` 数组。非边界邻接若指向非活动物理槽则失败。`PlanCbtSplits` 要求每个输入节点非零，不能直接传完整稀疏物理池。
4. **可选需求诊断。** 在相同紧凑视图、顺序上以旧物理空闲量调用一次 `PlanCbtSplits`，只记录需求与拒绝，不分配、提交或参与真实选择。物理容量也不足时，这不是无约束需求。
5. **真实计划。** 额度为 `min(oldFreeDynamicSlots,B-N_before)`；联合计划复用参考闭包和模板合并，检查 `RequiredSlotCount≤额度`。不重新排序根请求，不先合并腾预算。
6. **分配和回填。** `AllocateCbtSplitSlots` 对旧占用树做分配。`plan.AllocationNodes` 和模板所属节点仍是局部编号，回填到活动物理槽；`allocation.NodeIndices` 的新槽已是物理编号，不能再次映射。验证模板、所需深度、唯一空槽和分配数量。
7. **所选细分。** 原参考档调用 `CommitCbtBisects`；新内核调用 `CommitCpuCbtBisects` 并记录内部耗时，两者都返回完整下一代数组及新增槽。检查实际新增数与计划相等、细分后即不超预算。新内核失败不改调参考函数。
8. **参考合并重验。** 将第 2 步冻结的物理合并根列表交给细分输出上的 `CommitCbtSimplifications`。该函数内部准备、重验、提交、传播，可能跳过被同轮细分覆盖的旧请求；包装不重新分类或扩充合并列表。
9. **归约发布。** 在临时占用树上对新增槽置位、释放槽清位，归约、重建活动索引，检查永久槽与数量。移动所有数组/树/索引并增加 `Generation`。任何前置异常由外层捕获为 `Success=false`，未发布半成品；没有失败重试或静默串行替代路径。

FACT：只有新内核的模板填写能调用执行器，填写前准备和之后归并、传播仍由调用线程完成。成功 `Success=true` 仅表示一轮执行完成，不能解释为质量合格或没有剩余请求。

硬不变量：

```text
N_before = 6 + oldOccupancy
requiredSlots = allocatedSlots = acceptedSlots
N_split = N_before + acceptedSlots <= B
releasedSlots = pairMergeCount + 2 * quadMergeCount
N_after = N_split - releasedSlots = 6 + newOccupancy <= B
```

本轮合并释放的槽不用于本轮已完成的细分分配，只能在下一轮复用。参考 planner 的保守预留余额与实际 `B-N` 分开，预留拒绝不证明实际闭包必然放不下。

### 3.1 新细分内核的临时所有权

`CommitCpuCbtBisects` 接收只读编号、邻接、节点数据、最终物理模板列表、动态容量和执行器。输入必须已经过原计划/分配和更新层检查，不是任意损坏节点的修复接口。

1. 验证三数组长度、动态容量；分配 `TemplateRecord`，由 `PrepareOwnership` 用普通字节数组认领所有父槽和新槽。父槽活动且唯一，新槽原先空闲且处于动态域，各模板所需新槽不能重复或与父槽重叠；验证四种模式及 64 位编号增长范围。
2. 复制三个输出数组，预留新增槽、传播列表。记录按模板索引独占，包含 `Slots[3]`、`SlotCount`、`Kind`、`Propagation`、`Executed` 和 `Valid`；共享容器大小不交工作线程修改。
3. `FillTemplate` 按 center / right-double / left-double / triple 原公式写保留父槽和已分配新槽。`EvaluateNeighbors` 只读冻结输入的邻居最终计划；`WriteData` 只读取自己刚写入的输出编号。任务不分配、不抛异常、不改压缩占用或全局计数。
4. 同步返回后检查每项已执行且有效，按原模板及模板内槽次序归并列表和计数。`Propagate` 才读取完整新代，按原分支和原顺序修补其他槽的邻接/事件；无细分、center、right-double、left-double 分支与参考一致。

非法邻居模式由本模板记录失败；遗漏执行区间会在归并时失败。非法或越界区间直接不执行；接口仍要求执行器恰好覆盖一次，内核不会通过并发原子计数防御恶意重叠/重复调用。失败结果始终是临时值，由更新层停止且保留原状态。

### 3.2 驱动执行器与同步

`CpuCbtTestExecutor` 只接受 1/2/4 配置，构造时预创建所需池线程。`Executor(evidence)` 返回借用适配器的同步闭包，适配器寿命必须覆盖所有调用；同一适配器不允许并发调用或嵌套派发。

给定模板数 K 和配置 W，`m=min(W,K)`，区间 j 为 `[K*j/m,K*(j+1)/m)`。K=0 不派发；m=1 由调用线程执行；其余在池中提交 m 项并等待。调用线程不作为额外填写线程，没有根据性能调出的粒度阈值。

仅诊断时预分配 `CpuCbtRangeEvidence`，每区间写自己的起止与 `std::thread::id`；配置数量和实际线程数量不同。派发中途异常时，适配器设置 `_failed`，调用 `Shutdown` 处理完已接受任务，再重抛，避免内核临时数组先被销毁。池之后不复用。线程池原实现无任务异常捕获，故非抛出任务是前置契约；调用线程分配失败才进入上述路径。

## 4. 几何输出

`Geometry(state,heightMap,slot)` 调用 `EvaluateCbtTerrainGeometry`，检查高度图、槽位范围、解码有效性及顶点位置/法线有限值。参考函数依逻辑编号解码 UV，通过现有高度图采样生成世界坐标和父级分类辅助位置。

`BuildCpuCbtClassificationGeometry` 每活动叶输出当前三个世界顶点及参考父级辅助点，第四点不是三角形重心。异常交给更新边界处理。

`BuildCpuCbtMesh(state,heightMap,mesh,error)` 在临时 `TerrainMeshData` 中生成 3N 个独立顶点及顺序索引，复制参考位置/法线/UV，填写实际高度图尺寸与尺度。成功移动替换网格，失败保留原网格并返回错误。它不继承 `ITerrainLodAlgorithm`，也不输出渲染资源；当前完整网格生成与拓扑更新是两个独立同步调用。

## 5. 报告和计时契约

FACT：`CpuCbtUpdateReport` 的数量字段以本轮为单位，累计统计由驱动完成。

| 字段组 | 解释 |
| --- | --- |
| `TriangleCountBefore/AfterSplit/After` | 更新前、细分后、完整发布后数量 |
| `SplitProposalCount/MergeProposalCount` | 轮开始分类产生的根请求数；不等于实际基础操作数 |
| `RequiredSlots/AcceptedSlots/ReleasedSlots` | 带预算计划所需、新提交动态槽、实际释放动态槽 |
| `ReservationRejectedCount/DuplicateCandidateCount/PlannerRemainingSlots` | 原 planner 结果；拒绝包含保守预留机制的影响 |
| `OldFreeDynamicSlots`、三项 `BudgetRemaining*` | 物理池空闲量及真实 `B-N`，各自记录 |
| `TemplateCounts[4]` | center、right-double、left-double、triple 模板数；总和为模板任务数 |
| `PairMergeCount/QuadMergeCount` | 两类合并任务数；四节点合并释放两个动态槽 |
| `BudgetLimitedRound/JointlyLimitedRound` | 拒绝非零且预算额度严格小于 / 等于旧物理空闲额度 |
| `PoolOnlyRequiredSlots/PoolOnlyReservationRejections` | 只读诊断结果；关闭时为 `std::nullopt`，CSV 留空 |
| `PreparationMs`、`ClassificationGeometryMs`、`ClassificationMs`、`MappingMs`、`PlanningMs`、`AllocationMs` | 检查/初始副本、分类几何、分类、紧凑映射、计划、分配回填的墙钟成本 |
| `BisectCommitMs/SimplifyCommitMs/PublishMs` | 完整所选细分调用、完整参考合并调用、占用归约和发布 |
| `LocalBisectTimings` | 仅新内核有值；`PrepareMs` 为预检/副本/预分配，`TemplateFillWallMs` 含派发等待，`CollectMs` 为顺序归并，`PropagationMs` 为外部传播；参考档留空 |
| `DemandDiagnosticMs/UpdateMs` | 额外需求计划 / 完整更新墙钟；后者包含临时对象释放，诊断打开时也包含额外计划，因此 validate 数据不作性能结果 |

全部耗时使用 `steady_clock`，单位 ms；空模板的填写项为零。驱动另计 `meshMs` 和外层 `updateAndMeshMs`，在两项计时结束后才求摘要、直接比较、审计和写 CSV。网格每轮构建到空目标，前轮保留网格在算法计时后移动替换并释放，与归档旧程序边界一致。加载、初始化和并行档池创建成本在 metadata 单列；外部进程时长还包含进程启动、诊断、摘要、输出和清理。

## 6. 驱动、诊断与产物

FACT：`CpuCbtProbe::main` 接受成对选项，拒绝未知/重复参数；必需参数缺失时报错退出：

```text
--scenario cpu-cbt-test129-b4096-v1|cpu-cbt-peking547-b20000-v1
--mode validate|measure --output 不存在的新目录
[--implementation reference|serial|parallel|all] [--threads 1|4]
```

`Run` 固定 test129 为 129×129、30/4、B4096，Peking 为真实 547×547、80/12、B20000。共同 128K 池、位长 20、面积 50；相机 h20→h80→h20，各 32 轮，状态连续。两种视图用 `lookAtRH`、`perspectiveRH_NO` 生成一次，分辨率 1280×720，60°、near0.05/far1000、`usesZeroToOneDepth=false`。完整冻结参数见小规划第 3 节。

输入身份分工：C++ 加载后检查真实尺寸；本阶段采集层按冻结值核查两个资产 SHA-256，并保存程序、源码、环境及命令。metadata 的 `sourceReference` 是标识文本，不能代替外部实际源码散列。独立运行 CLI 时不会自动检查资产 SHA-256。

新增选项省略时仍为 reference/1；reference/serial 要求 1，parallel/all 要求 4，all 仅用于 validate。`Trajectory` 独立拥有状态、网格、当前结果、96 轮报告和输出流。all 从相同六基础初始状态建立 R/S/P 三条轨迹，每轮各自真实更新，`ComparePublished` 直接比较完整编号、活动列表、代次、占用位/树、活动节点全部命名字段、邻接以及网格全部命名字段，再比较 `WorkFields`。一致后只对 P 运行一次 `Audit`。

当前 validate 和 measure 均关闭 `EvaluatePoolOnlyDemand`，复用第一阶段需求证据；必要预算/索引/所有权检查相同。validate 才采集线程身份、直接比较和全局审计；自然轨迹没有强制并发屏障，测试屏障只在指定夹具中。

`TopologyDigest` 编码活动物理槽、逻辑编号及三邻接，`MeshDigest` 编码位置、法线、UV 和索引，保留第一阶段语义。`StateDigest` 补充代次、活动物理槽、全部活动节点数据、占用位和归约树；它需与旧拓扑摘要一起使用。`CompleteMeshDigest` 在旧网格摘要上补网格尺寸、尺度、顶点高度及调试字段。均为命名字段 FNV64，不编码容量、padding 或地址，不是抗碰撞身份或一般同构证明。计量逐轮摘要关联到直接字段诊断，不用摘要替代诊断证明。

`Audit` 独立检查全物理槽非零状态与占用一致、活动数量、编号唯一/六前缀/深度、无祖先重叠、反向邻接、3N 网格大小、UV 合法非退化、总面积和完整边。`VertexMatcher` 用格桶缩小范围，检查相邻九桶及实际欧氏距离≤1e-6；多重匹配拒绝。总面积容差 1e-5，内部完整边须恰好成对且绕序相反，单边仅允许在单位域边界。

| 产物 | 内容与归属 |
| --- | --- |
| `metadata.csv` | 协议 2、固定参数、输入路径/尺寸、视图摘要、加载/初始化和池创建耗时、实现与请求线程数 |
| `views.csv` | 两高度的 View/Projection/VP 实际矩阵，按列输出；驱动生成 |
| `rounds.csv` | 原 45 列加实现、请求线程数、两个新增摘要和四项新内核计时，共 53 列；参考内部计时留空 |
| `segments.csv` | 三段首次稳定、稳定尾部起点、适应成本、预算竞争和最终利用率；驱动生成 |
| `threads.csv`、`comparison.csv` | validate 才生成；非空区间真实线程证据，以及当轮比较档数/结果/独立审计档位 |
| `failure.txt`、`failure-state.csv` | 轮内更新、网格或审计异常时，每条轨迹保存轮号/原因及当时已发布节点的编号、邻接和全部元数据；初始化等轮外异常由 main 输出并退出 |
| `process.json`、采集汇总 JSON、源码包和散列 | 外部采集产生，非 C++ 驱动内部文件协议 |

`Stable` 只判断模板数和合并任务数均为 0，驱动另核对拓扑摘要未变。`WriteSegments` 的 `firstStableRound` 为段内首次无修改轮，局部编号 1～32；适应延迟为直到段末至少连续两轮无修改的后缀起点。累计 CPU 成本包含该起点轮。没有这样的后缀则留空并标记未在 32 轮内稳定，不追加轮数。稳定开始时的待细分数和拒绝数保留，以识别预算停滞。

all 的 metadata/rounds/segments 分 R/S/P 子目录，视图、线程和对照表位于公共输出根；单档文件直接位于输出根。measure 每个进程仅一个实现。原始数据由 `benchmark-output/cpu-cbt/` 精确忽略规则保留本地；采集脚本同样是阶段证据，没有进入项目级运行框架。

## 7. 构建、验证与修改影响

可选目标要求 `PARALLEL_ROAM_BUILD_TESTS=ON`。原型块独立调用现有 GLM/STB 准备函数，缺少依赖或任一参考源则显式失败。两个静态库为 `parallel_roam_cpu_cbt_reference`、`parallel_roam_cpu_cbt`，后者包含新 `CpuCbtBisect.cpp`。两个程序为 `parallel_roam_cpu_cbt_tests`、`parallel_roam_cpu_cbt_probe`，各自编译原 DOD 线程池 `.cpp` 并链接 `Threads::Threads`，不把具体池放入核心库。实际 `APP=OFF` 构建成功，无 D3D12、OpenGL 或 SDL 执行依赖。

`CpuCbt` 测试工作目录为项目根，超时 120 秒。测试符号：`CheckTemplatesAndPlanning` 覆盖四模板、长兼容链的保守预留返还与整条拒绝、共享闭包、满槽池；`CheckOldMergeRevalidation` 覆盖旧请求被细分覆盖和另一个仍能合并、唯一四节点合并；`CheckContinuousRound` 用临时平坦 PGM 跑 24 轮，核对释放复用、预算和诊断不变性，并测试失败不发布。自然两个 96 轮审计和独立计量见阶段审查，未运行全项目测试矩阵。

`CompareCommits` 比较 R、新内核调用线程、1/2/4 执行器和反向逐模板区间的提交结果，包括完整活动字段及新增/传播列表次序。`CheckOwnershipAndThreads` 覆盖重复父/新槽、父子冲突、越界、模式/长度/整数溢出、遗漏回调、空批次，并用两区间测试专属屏障验证不同非调用线程同时进入填写。`CheckGeneratedBatches` 固定种子 `0x43425402`，从六基础合法状态经原计划/分配生成 8 轮批次，全部保留对照；未重选种子。持续夹具另比较新 S/P 和诊断档，注入执行器完成已接受任务后抛错，核对失败不发布。

修改影响：改变活动顺序会改变分类候选和计划次序；改变局部/物理编号转换会影响所有邻接和新槽分配；改变 transient reset 或合并列表形成时机将改变组合语义；改变提交内部复制/发布会改变下一阶段参考串行成本。应按相应风险选择验证，不能仅凭最终三角形数相等接受修改。

FACT：本轮两个自然轨迹均逐轮 R/S/P 一致，test129 的 15 个有细分轮全部观测到多线程参与，Peking 的 33 轮中为 30 轮。S 的模板填写累计成本只占完整更新约 0.066% / 0.209%；五进程 S/P 完整累计更新比值范围均跨 1，没有明确整体收益证据。新串行 test129 细分提交局部成本复测越线，完整指标未越线，见[独立分析](../../reviews/cpu_cbt/cpu_cbt_02_performance_analysis.md)。

INFERENCE：完整物理池准备和全量分类几何限制本实现仅并行填写的收益空间；该结论不否定其他 CPU 批量存储或更新边界。改变共享读取来源、输出槽所有权、传播顺序或执行器等待行为会直接破坏并发安全；改变计时内外清理边界会破坏前后性能可比性。

## 8. 符号与执行路径索引

```text
CpuCbtState.h/.cpp
  CpuCbtSettings / CpuCbtState / ValidateCpuCbtSettings / InitializeCpuCbt
CpuCbtUpdate.h/.cpp
  CpuCbtBisectImplementation / CpuCbtUpdateOptions / CpuCbtUpdateReport
  CheckInput / PlanningView / UpdateCpuCbt
CpuCbtBisect.h/.cpp
  CpuCbtRangeTask / CpuCbtRangeExecutor / CpuCbtBisectTimings / CpuCbtBisectResult
  TemplateRecord / PrepareOwnership / EvaluateNeighbors / WriteData
  FillTemplate / Propagate / CommitCpuCbtBisects
CpuCbtMesh.h/.cpp
  Geometry / BuildCpuCbtClassificationGeometry / BuildCpuCbtMesh
CpuCbtProbe.cpp
  Digest / TopologyDigest / MeshDigest / StateDigest / CompleteMeshDigest
  ComparePublished / VertexMatcher / Audit / Round / Trajectory
  Templates / Stable / WriteSegments / WorkFields / WriteRound / SaveFailure / Step / Run / main
CpuCbtExecutionSupport.h
  CpuCbtRangeEvidence / CpuCbtTestExecutor / Executor
CpuCbtTests.cpp
  View / SameState / CheckTopology / CheckTemplatesAndPlanning
  SameData / SameCommit / CompareCommits / CheckOldMergeRevalidation
  CheckContinuousRound / CheckOwnershipAndThreads / CheckGeneratedBatches / main

初始化：Run → LoadFromFile → InitializeCpuCbt → 构造冻结视图
真实轮次：Run → Step → UpdateCpuCbt → 分类 → PlanningView → Plan → Allocate
        → 原参考或新细分提交 → CommitCbtSimplifications → Reduce → 发布
新细分：CommitCpuCbtBisects → PrepareOwnership → 数组准备
       → [空执行器：填写全区间 | 驱动执行器：固定区间 → 池 ParallelFor → 等待]
       → 顺序归并 → Propagate → 临时值返回
只读诊断：UpdateCpuCbt → pool-only Plan → 仅记录，再执行相同真实轮次
输出：Step → BuildCpuCbtMesh → 摘要 → [validate: 直接三档比较 → P 独立 Audit] → WriteRound
段末：96 轮结束 → WriteSegments → 进程退出
```

## Unresolved / Uncertain

- 当前扫描未发现 ownership 或核心调用路径无法定位的问题；正确性证据限于定向夹具和两个冻结轨迹，不是任意输入的形式证明。
- UNCERTAIN：新串行准备成本中各分配/清零/副本及缓存状态对局部退化的独立贡献尚未隔离，不将组合计时当成唯一根因证明；处理方式待用户决定。
- 真实池入队分配失败未通过内存压力强行复现；已审查 `Shutdown` 排空和引用生命周期，定向夹具覆盖同步执行器报错后不发布。
- 独立几何质量与 ROAM 的比较未开展，主参考已锁定但评价能力仍暂停；当前面积阈值、预算利用率和稳定轮数不构成质量保证。
