# CPU CBT 持续更新代码事实

> 日期：2026-09-12
> 范围：CPU-CBT-01 实现；根仓库起点 `b078d59`，完整源码身份见阶段审查
> 规划：[CPU-CBT-01](../../plans/cpu_cbt/cpu_cbt_01_serial_update_plan.md)
> 审查：[串行更新阶段审查](../../reviews/cpu_cbt/cpu_cbt_01_serial_update_review.md)

## 1. 模块边界与文件

FACT：模块位于 `src/algorithms/cpu_cbt/`，命名空间为 `ParallelRoam::Algorithms::CpuCbt`。它包装参考纯 C++ 函数，提供独占持久状态上的同步串行更新和全量 CPU 网格。上层仅有两个独立驱动，未接入应用算法注册、DOD 状态、渲染器、线程池或质量 evaluator。

| 文件 | 类型 / 入口及实际职责 |
| --- | --- |
| [CpuCbtState.h](../../../src/algorithms/cpu_cbt/CpuCbtState.h) | `CpuCbtSettings`、`CpuCbtState` 及配置检查、初始化声明；唯一持久状态所有者 |
| [CpuCbtState.cpp](../../../src/algorithms/cpu_cbt/CpuCbtState.cpp) | `ValidateCpuCbtSettings`、`InitializeCpuCbt`；检查参数、构造六基础状态，成功后移动替换 |
| [CpuCbtUpdate.h](../../../src/algorithms/cpu_cbt/CpuCbtUpdate.h) | `CpuCbtUpdateOptions`、`CpuCbtUpdateReport`、`UpdateCpuCbt`；同步调用和计数/耗时契约 |
| [CpuCbtUpdate.cpp](../../../src/algorithms/cpu_cbt/CpuCbtUpdate.cpp) | 私有 `CheckInput`、`PlanningView` 和轮次编排；分类、索引转换、预算、参考提交、占用发布，无自有模板内核 |
| [CpuCbtMesh.h](../../../src/algorithms/cpu_cbt/CpuCbtMesh.h) | 分类四点与完整网格两个只读入口 |
| [CpuCbtMesh.cpp](../../../src/algorithms/cpu_cbt/CpuCbtMesh.cpp) | 私有 `Geometry` 包装参考几何，检查有效性和有限值；构造独立三角形顶点输出 |
| [CpuCbtTests.cpp](../../../tests/CpuCbtTests.cpp) | 解析模板、联合计划、旧合并重验、持续释放复用、预算、失败不发布及诊断不变性 |
| [CpuCbtProbe.cpp](../../../tests/CpuCbtProbe.cpp) | 两个固定自然输入、视图轨迹、计时、诊断、摘要、CSV 和段末适应统计；实验驱动职责集中于此，不进入算法库 |
| [ProjectOptions.cmake](../../../cmake/ProjectOptions.cmake) | 默认 OFF 的 `PARALLEL_ROAM_BUILD_CPU_CBT_PROTOTYPE` |
| [tests/CMakeLists.txt](../../../tests/CMakeLists.txt) | 可选纯 C++ 参考库、包装库、两个可执行目标和 `CpuCbt` CTest |

FACT：依赖为 `驱动 → CPU CBT 包装 → Cbt2024 纯 C++ / 当前公共地形类型`。直接编译参考七组 `.cpp`：`CbtOccupancyTree`、`CbtBisectorTopology`、`CbtClassification`、`CbtSplitPlanner`、`CbtBisectCommit`、`CbtSimplifyCommit`、`CbtTerrainGeometry`。还包含其七个头和 `CbtGpuAbi.h`、`CbtGpuAbi.shared.h`；后两者提供数值/布局常量，没有 GPU 执行。

参考实际目录为 `third_party/RoamTesting/src/algorithms/cbt_2024/`，独立仓库提交 `d462089d5c2888cc09537a6a3f44a7c33cbf4499`，工作区未修改。目标包含目录先用当前项目 `src/`，再用参考 `src/`，因此公共 `ITerrainLodAlgorithm.h` 来自当前项目。参考依赖未作为根仓库内容导入。

## 2. 数据、所有权与生命周期

FACT：所有类型均为普通值类型，无继承、回调或后台任务。驱动拥有高度图和 `CpuCbtState`，函数同步借用；调用期间状态不允许并发读写。容器由 RAII 释放，没有图形资源或独立 shutdown。

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

FACT：当前没有几何缓存、稀疏复制或脏区更新。分类和网格各自重算全部活动叶几何，临时向量及映射每轮重建。参考提交也复制完整物理数组；即使请求为空，包装仍调用这些参考入口。

## 3. 更新入口与控制流

`UpdateCpuCbt(CpuCbtState&, const HeightMap&, const TerrainLodViewInput&, const CpuCbtUpdateOptions&)` 返回值持有计数、耗时和错误，不持有临时数组引用。默认 `EvaluatePoolOnlyDemand=false`。

1. **检查和准备。** `CheckInput` 验证配置、三个物理数组长度、活动数量与占用关系、活动列表唯一/排序、有效编号前缀和深度、活动位、六永久槽、视图尺寸和有限可逆 VP。复制 `Data`，记录 `N_before`、旧空闲槽和 `B-N_before`。
2. **几何和分类。** `BuildCpuCbtClassificationGeometry` 返回与活动列表同序的四点。每个活动项重置瞬态数据后调用参考 `EvaluateCbtClassification`；正细分进入根请求列表，非基础负分类标为可合并，仅偶数逻辑编号发出合并根。可见性写入标志，不决定预算计数。
3. **紧凑视图。** `PlanningView` 建立物理到活动局部编号的逆映射，生成仅含活动节点的 `CbtSplitPlanningNode` 数组。非边界邻接若指向非活动物理槽则失败。`PlanCbtSplits` 要求每个输入节点非零，不能直接传完整稀疏物理池。
4. **可选需求诊断。** 在相同紧凑视图、顺序上以旧物理空闲量调用一次 `PlanCbtSplits`，只记录需求与拒绝，不分配、提交或参与真实选择。物理容量也不足时，这不是无约束需求。
5. **真实计划。** 额度为 `min(oldFreeDynamicSlots,B-N_before)`；联合计划复用参考闭包和模板合并，检查 `RequiredSlotCount≤额度`。不重新排序根请求，不先合并腾预算。
6. **分配和回填。** `AllocateCbtSplitSlots` 对旧占用树做分配。`plan.AllocationNodes` 和模板所属节点仍是局部编号，回填到活动物理槽；`allocation.NodeIndices` 的新槽已是物理编号，不能再次映射。验证模板、所需深度、唯一空槽和分配数量。
7. **参考细分。** `CommitCbtBisects` 顺序完成局部填写和外部传播，返回完整下一代数组及新增槽。检查实际新增数与计划相等、细分后即不超预算。
8. **参考合并重验。** 将第 2 步冻结的物理合并根列表交给细分输出上的 `CommitCbtSimplifications`。该函数内部准备、重验、提交、传播，可能跳过被同轮细分覆盖的旧请求；包装不重新分类或扩充合并列表。
9. **归约发布。** 在临时占用树上对新增槽置位、释放槽清位，归约、重建活动索引，检查永久槽与数量。移动所有数组/树/索引并增加 `Generation`。任何前置异常由外层捕获为 `Success=false`，未发布半成品；没有失败重试或静默串行替代路径。

FACT：当前本来就是全串行路径。成功 `Success=true` 仅表示一轮执行完成，不能解释为质量合格或没有剩余请求。

硬不变量：

```text
N_before = 6 + oldOccupancy
requiredSlots = allocatedSlots = acceptedSlots
N_split = N_before + acceptedSlots <= B
releasedSlots = pairMergeCount + 2 * quadMergeCount
N_after = N_split - releasedSlots = 6 + newOccupancy <= B
```

本轮合并释放的槽不用于本轮已完成的细分分配，只能在下一轮复用。参考 planner 的保守预留余额与实际 `B-N` 分开，预留拒绝不证明实际闭包必然放不下。

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
| `BisectCommitMs/SimplifyCommitMs/PublishMs` | 完整参考细分调用、完整参考合并调用、占用归约和发布；未拆出模板填写或传播内部成本 |
| `DemandDiagnosticMs/UpdateMs` | 额外需求计划 / 完整更新墙钟；后者包含临时对象释放，诊断打开时也包含额外计划，因此 validate 数据不作性能结果 |

全部耗时使用 `steady_clock`，单位 ms。驱动另计 `meshMs` 和外层 `updateAndMeshMs`，在两项计时结束后才求摘要、审计和写 CSV。加载、初始化在 metadata 单列；输出 I/O 未单独分段计时，位于上述算法计时外，外部进程时长包含进程启动、诊断与输出等全部成本。

## 6. 驱动、诊断与产物

FACT：`CpuCbtProbe::main` 仅接受以下固定位置参数：

```text
--scenario cpu-cbt-test129-b4096-v1|cpu-cbt-peking547-b20000-v1
--mode validate|measure --output 不存在的新目录
```

`Run` 固定 test129 为 129×129、30/4、B4096，Peking 为真实 547×547、80/12、B20000。共同 128K 池、位长 20、面积 50；相机 h20→h80→h20，各 32 轮，状态连续。两种视图用 `lookAtRH`、`perspectiveRH_NO` 生成一次，分辨率 1280×720，60°、near0.05/far1000、`usesZeroToOneDepth=false`。完整冻结参数见小规划第 3 节。

输入身份分工：C++ 加载后检查真实尺寸；本阶段采集层按冻结值核查两个资产 SHA-256，并保存程序、源码、环境及命令。metadata 的 `sourceReference` 是标识文本，不能代替外部实际源码散列。独立运行 CLI 时不会自动检查资产 SHA-256。

`validate` 开启每轮 pool-only 计划和 `Audit`；`measure` 均关闭，其余算法检查、计数和计时相同。`TopologyDigest` 编码活动物理槽、逻辑编号及三邻接，`MeshDigest` 编码位置、法线、UV 和索引；使用命名字段的 FNV64，不编码容量、padding 或地址。它们用于本阶段同平台逐轮确定性核对，不是一般逻辑同构证明或抗碰撞身份。解析 `SameState` 另直接比较节点有效字段、数组和占用树。

`Audit` 独立检查全物理槽非零状态与占用一致、活动数量、编号唯一/六前缀/深度、无祖先重叠、反向邻接、3N 网格大小、UV 合法非退化、总面积和完整边。`VertexMatcher` 用格桶缩小范围，检查相邻九桶及实际欧氏距离≤1e-6；多重匹配拒绝。总面积容差 1e-5，内部完整边须恰好成对且绕序相反，单边仅允许在单位域边界。

| 产物 | 内容与归属 |
| --- | --- |
| `metadata.csv` | 固定参数、输入路径/尺寸、视图摘要、加载/初始化耗时；驱动生成 |
| `views.csv` | 两高度的 View/Projection/VP 实际矩阵，按列输出；驱动生成 |
| `rounds.csv` | 45 列逐轮计数、诊断、摘要及计时；驱动生成 |
| `segments.csv` | 三段首次稳定、稳定尾部起点、适应成本、预算竞争和最终利用率；驱动生成 |
| `failure.txt`、`failure-state.csv` | 更新失败时保存轮号/原因及此前已发布的活动节点；审计失败仅写原因；加载/网格等其他异常由 main 输出错误并退出 |
| `process.json`、采集汇总 JSON、源码包和散列 | 外部采集产生，非 C++ 驱动内部文件协议 |

`Stable` 只判断模板数和合并任务数均为 0，驱动另核对拓扑摘要未变。`WriteSegments` 的 `firstStableRound` 为段内首次无修改轮，局部编号 1～32；适应延迟为直到段末至少连续两轮无修改的后缀起点。累计 CPU 成本包含该起点轮。没有这样的后缀则留空并标记未在 32 轮内稳定，不追加轮数。稳定开始时的待细分数和拒绝数保留，以识别预算停滞。

## 7. 构建、验证与修改影响

可选目标要求 `PARALLEL_ROAM_BUILD_TESTS=ON`。原型块独立调用现有 GLM/STB 准备函数，缺少依赖或任一参考源则显式失败。两个静态库为 `parallel_roam_cpu_cbt_reference`、`parallel_roam_cpu_cbt`，两个程序为 `parallel_roam_cpu_cbt_tests`、`parallel_roam_cpu_cbt_probe`。实际 `APP=OFF` 构建成功，无 D3D12、OpenGL 或 SDL 执行依赖。

`CpuCbt` 测试工作目录为项目根，超时 120 秒。测试符号：`CheckTemplatesAndPlanning` 覆盖四模板、长兼容链的保守预留返还与整条拒绝、共享闭包、满槽池；`CheckOldMergeRevalidation` 覆盖旧请求被细分覆盖和另一个仍能合并、唯一四节点合并；`CheckContinuousRound` 用临时平坦 PGM 跑 24 轮，核对释放复用、预算和诊断不变性，并测试失败不发布。自然两个 96 轮审计和独立计量见阶段审查，未运行全项目测试矩阵。

修改影响：改变活动顺序会改变分类候选和计划次序；改变局部/物理编号转换会影响所有邻接和新槽分配；改变 transient reset 或合并列表形成时机将改变组合语义；改变提交内部复制/发布会改变下一阶段参考串行成本。应按相应风险选择验证，不能仅凭最终三角形数相等接受修改。

INFERENCE：当前准备与参考提交的完整池复制构成与活跃模板数不成比例的固定成本；分类几何按活动叶全量执行。阶段计时可以指出组合成本，但不能据此估计模板本地填写的多核收益。

## 8. 符号与执行路径索引

```text
CpuCbtState.h/.cpp
  CpuCbtSettings / CpuCbtState / ValidateCpuCbtSettings / InitializeCpuCbt
CpuCbtUpdate.h/.cpp
  CpuCbtUpdateOptions / CpuCbtUpdateReport / CheckInput / PlanningView / UpdateCpuCbt
CpuCbtMesh.h/.cpp
  Geometry / BuildCpuCbtClassificationGeometry / BuildCpuCbtMesh
CpuCbtProbe.cpp
  Digest / TopologyDigest / MeshDigest / VertexMatcher / Audit
  Round / Templates / Stable / WriteSegments / WriteRound / Run / main
CpuCbtTests.cpp
  View / SameState / CheckTopology / CheckTemplatesAndPlanning
  CheckOldMergeRevalidation / CheckContinuousRound / main

初始化：Run → LoadFromFile → InitializeCpuCbt → 构造冻结视图
真实轮次：Run → UpdateCpuCbt → 分类 → PlanningView → Plan → Allocate
        → CommitCbtBisects → CommitCbtSimplifications → Reduce → 发布
只读诊断：UpdateCpuCbt → pool-only Plan → 仅记录，再执行相同真实轮次
输出：Run → BuildCpuCbtMesh → 摘要 → [validate: Audit] → WriteRound
段末：96 轮结束 → WriteSegments → 进程退出
```

## Unresolved / Uncertain

- 当前扫描未发现 ownership 或核心调用路径无法定位的问题；正确性证据限于定向夹具和两个冻结轨迹，不是任意输入的形式证明。
- UNCERTAIN：模板本地填写可并行比例和完整更新时间加速尚无实现或测量；组合调用计时不能填补该证据。
- PLANNED：CPU-CBT-02 的新串行/新并行内核、同步执行器和三档对照尚未实现。
- 独立几何质量与 ROAM 的比较未开展，主参考已锁定但评价能力仍暂停；当前面积阈值、预算利用率和稳定轮数不构成质量保证。
