# GATE-03：快照选批与单线程反事实执行小规划

> 类型：Minor Plan，落实既有离线 Gate 的第三阶段
> 状态：已关闭（用户决定，2026-09-12 完成归档）；保留有界筛查证据，完整质量—并行性 Gate 未判通过
> 日期：2026-09-11
> 前置版本：`a0f3551`；GATE-02 最小参考校准通过，双线性主参考已锁定，完整质量评价按需暂停

## 1. 目标与范围

用户最新决定直接启动 **GATE-03，不另拆 03A/03B**。本阶段回答：在当前 head-preserving、disjoint-footprint 模型下，放宽 split ordering 是否能找到比严格前缀更多的独立拓扑工作，还是主要受闭包和协调依赖限制。

实现同一冻结 `SplitTopology` 输入上的五个完整单阶段变体：严格 `α=0`、`strict-prefix`、`α=0.1/0.25/1`。每个变体从自己的 `S0` 副本真实演化至原控制器停止，生成自己的候选、闭包、预算状态及最终网格。只分析第一张快照、重排串行 trace 或只执行最初几个根，都不能称为完成反事实执行。

保持拓扑合法性和预算上限为硬约束；merge ordering、阶段顺序、预算交换和停止规则沿用原实现。只交换 split 候选的执行次序。负结果限于本次输入、必选头请求、保守足迹、固定选择器及检查覆盖；不能否定一般 ordering relaxation。大量共享闭包或协调依赖拒绝优先解释为当前模型边界。

本阶段先取得独立工作证据。质量评价仍按用户决定暂停，不补 RMS、配对 `D_max`、Peking 收敛或完整 Pareto 工具；没有配对质量证据时只能建议继续质量验证，不能宣布质量—并行性 Gate 已通过。不实现真实多核算法、可交换共享写、闭包去重、跳头策略、GPU 路线或通用实验框架。

## 2. 依据、现状与文件归属

已阅读[开发规范](../../standards/development_guidelines.md)、[规划指南](../plan_guideline.md)、[大规划](ordering_relaxation_offline_gate_plan.md)、[讨论记录](ordering_relaxation_discussion_record.md)、[GATE-01 小规划](gate_01_topology_operation_observation_plan.md)、[操作观察事实](../../codebase/ordering_relaxation/topology_operation_observation.md)、[GATE-01 审查](../../reviews/ordering_relaxation/gate_01_topology_operation_observation_review.md)及 [GATE-02 移交](gate_02_mesh_quality_evaluation_plan.md#10-主参考锁定与阶段移交)。同时扫描了拓扑实验入口、共用严格循环、队列/资格判断、状态副本、现有实验测试支持及 CMake 目标。

当前 `AnalyzeSplitOperation` 已能在输入副本上观察一个真实根，但它不判断控制器是否允许该根，也不应用到调用方状态。`AdvanceStrictSplitIteration` 只能执行当前严格选择。两者之间缺少受控制器约束的指定根应用入口；不能在新文件复制严格循环，或绕过它直接拼接闭包操作。

现有足迹使用逻辑 `PathId`，包括递归资格和邻接等局部状态；共享队列、索引、统计及 mesh 日志仍由 serial policy 立即维护。`RequiresSerialMaintenance` 不是独立性证明。现有完整输入哈希也包含物理布局信息，不能直接用来比较不同排列产生的新节点编号。

| 处理 | 文件 | 本阶段职责及独立存在理由 |
| --- | --- | --- |
| Extend | `src/algorithms/data_oriented_roam/DataOrientedRoamTopologyExperiment.h` | 声明控制决策只读预览和指定根单步的窄实验接口；复用现有迭代、步骤和观察类型 |
| Extend | `src/algorithms/data_oriented_roam/DataOrientedRoamTopology.cpp` | 从共用循环复用控制判断及真实根操作；严格入口和指定根入口共享合并、失败及预算处理，实验选批不放入此文件 |
| Create | `src/algorithms/data_oriented_roam/ordering_relaxation/OrderingExperiment.h` | 本次固定实验的配置、封闭批次、应用状态和汇总结果；这些是排序实验数据，不属于生产通用状态 |
| Create | `src/algorithms/data_oriented_roam/ordering_relaxation/OrderingExperiment.cpp` | 快照选批、受约束应用、完整变体编排及小型逻辑投影/审计；不建设 selector 注册、事务系统或独立 cursor 类 |
| Extend | `tests/OrderingGateTests.cpp` | 解析用例、四个旧输入的来源重建、手动筛查和简单记录输出；复用已有入口，不新增 CLI 项目或 Python runner |
| Extend | `tests/CMakeLists.txt` | 仅给 `OrderingGate` 目标加入新的实验实现；不将实验驱动加入生产源码清单 |

Reuse：`DataOrientedRoamState` 副本、`TopologySplitObservation/TopologyOperationWork`、队列比较器和资格函数、`StateSnapshot`、既有结果证据及 full mesh emit。Wrap：在实验文件包装上述操作为固定选批/应用流程；没有新的策略继承体系。评分、队列、SoA accessor、渲染和 formal 配对协议不在修改范围；需要侵入这些边界时先停止并说明。

依赖方向为 `OrderingGateTests → OrderingExperiment → TopologyExperiment/既有 DOD 能力`。只有原拓扑文件接触私有递归操作；生产不依赖 `OrderingExperiment`。测试输出负责文件与清单，算法层只返回领域数据。源码只新增上述 `.h/.cpp`，不提前细拆为 planner、simulator、auditor 等组件。

## 3. 关键接口与执行契约

### 3.1 保留严格控制器

拟补充两个窄入口，具体类型复用现有结构：

- `InspectSplitIteration(const state&, const iteration&)`：只读报告下一控制动作、严格头路径/评分或停止原因；不消费迭代、不清队列、不初始化预算。判断逻辑与共用循环同源。
- `AdvancePlannedSplitIteration(state&, iteration&, rootPath, observation&)`：仅在控制器允许 split 时选择已计划根，调用原观察 policy；优先合并、停止、失败后的预算交换和屏蔽仍由共用循环处理。结果明确是否执行了计划根。

原生产和严格步进保持默认编译期路径，不建立实验向量、不逐根哈希，也不付选批和观察成本。实验分支只替换 split 根的选择，不复制 `SplitNodeImpl` 或失败处理。预览不能代替真实单步：例如迭代上限仍保留原后增语义，合并失败仍真实删除候选。

`BeginStrictSplitIteration` 在每个独立变体入口只调用一次。每次原循环体照旧消费迭代，根内 forced primitive 不额外消费；不在新 batch 重算上限。若当前严格头的试算失败，直接执行原严格单步，保留部分成功、预算和后续控制结果，不能将失败试算变成回滚。

### 3.2 从同一快照构造封闭计划

`BuildBatch(const S_j&, config, iteration) → BatchPlan` 不修改来源。控制器优先合并或停止时不构造 batch；由严格单步处理后再考虑下一快照。

1. 读取 `S_j` 中已有队列评分及资格，按 `SplitPriorityPrecedes` 排序；不重算生产 score。非有限参数/评分明确失败，最高分非正时退回严格单步。
2. 正松弛以提升到 `double` 的既有评分比较 `P >= (1-alpha)*Pmax`，包含等号。`α=0` 是严格控制器，不通过窗口模拟。同分正松弛仍按 PathId 决定顺序。
3. 必选严格头；其余带内候选按确定顺序尝试。每根在 `S_j` 的独立副本上复用 `AnalyzeSplitOperation` 所用的真实观察操作，副本逐个释放。实现通过新指定根单步保留试算后的局部效果，不增加返回整份状态的第三个接口。后一个根不能看到前一个试算结果。
4. 同一检查顺序使用完整足迹、闭包冲突和联合资源谓词；最多检查 64 根、接收 32 根。冲突项可跳过，但不能越过优先级带。不得针对结果修改参数或限制。
5. 保存输入哈希、迭代位置、根路径/原评分、每根观察、联合预留及拒绝原因；返回后计划不可扩充。新节点以逻辑子路径表示，不以两个试算副本相同的数组下标制造假冲突。

`strict-prefix` 使用同一模型和限制，但遍历严格队列顺序时遇到第一个不合格或不可接收项便停止；不能先过滤再把不连续成员称为前缀。`α=0` 和 `strict-prefix` 的完整结果及逐步决策必须复现严格 `S`，正松弛不要求复现原拓扑。

### 3.3 依赖、资源和协调维护

保持大规划的 disjoint-footprint 谓词和闭包重叠拒绝，去重足迹不充当访问次数。联合预算要求各根 `PeakBudgetUse` 之和不超过 `S_j` 的可用预算；另核对实际净消耗及每根过程峰值，不借用未来 merge 释放的预算。

新节点需求按互不重叠逻辑路径与统一容量预留建模，实际应用仍顺序调用原分配；不实现并发分配器。分配失败或无法确认资源归属不算有效 batch。该模型下的可行工作不包含尚未实现的分配收益。

实施时对既有协调维护形成一张简短的读依赖核对表：队列/屏蔽如何影响下一根资格，活动索引是否只影响位置，路径集合是否参与当前资格，mesh 日志是否反馈拓扑。已证明与根内核独立的维护仍立即串行执行，并单独计量；任何会改变另一成员资格、闭包或局部结果的作用必须进依赖判断。无法覆盖时标 `coordinator_dependency` 或 `dependency_unknown`，不简单忽略所有共享写，也不因一个维护标志就拒绝所有成员。

首版不证明维护可以延迟，不把完整根函数算成已可并行。若必须重写大量队列或 accessor 才能解释依赖，触发模型/投入检查点，不继续扩建。

### 3.4 应用与真实状态演化

`ApplyBatch(state&, const plan&, iteration&)` 首先核验输入身份与迭代位置；按计划的 `P/PathId` 顺序应用。每根开始前重新检查优先合并、停止与迭代边界，但不能从新状态追加成员。正松弛允许新出现的更高优先级 split 等到下一批；`strict-prefix` 则要求当前严格头仍正好是下一计划成员。

实际根观察按逻辑路径与试算对齐：成功/失败、强制闭包、读写/创建资源、局部效果、净预算和峰值要求吻合。后续成员的完整输入哈希必然可变化，不能拿它与 `S_j` 哈希强行相等；核验对象是计划约束的资源和根效果。

若控制插入 merge/停止或严格前缀被新头打断，撤销剩余成员，已经执行的整段按串行工作记为 `control_interrupted`，不得追认为一个较短独立批次。非预期根失败、闭包/资源/局部效果变化标 `plan_mismatch` 并停止变体，保留真实状态与证据，不修补计划继续跑。

只有封闭成员全部应用吻合、联合资源成立且正确性检查通过，才记 `validated_batch`。之后以真实新状态构造 `B_(j+1)`。变体结束执行已有完整 mesh emit 和拓扑/队列/预算证据，来源 `S0` 始终不变。普通预算停止是原算法终止，工具时间/内存截断则是 `resource_limited`，两者不能混用。

## 4. 逻辑等价投影和有限审计

零松弛/严格前缀与原执行使用已有结果、完整后继状态和逐步决策对照；它们保持严格分配次序。**批内排列另用 PathId 规范化比较**，不能复用原完整输入哈希作为等价判据。

| 投影内容 | 保留的语义 |
| --- | --- |
| 所有已存在节点，包括非活动历史子节点 | 域、父子/邻接的逻辑路径、深度、split/创建/激活/merge 历史、forced 标记、评分及方差索引；忽略物理下标和地址 |
| 活动、路径与队列状态 | 活动叶/内部节点逻辑集合、上一帧/当前 split 集合、按路径映射的屏蔽状态、有效 split/merge 候选和精确评分/排序；保留真实资格，不要求堆槽和活动数组位置一致 |
| 预算和所有权 | 实际三角形数、最终余量、无泄漏预留、创建路径归属；每个排列的过程峰值都不越过联合预留，不要求瞬时曲线相同 |
| 根闭包与 mesh 效果 | 按根路径对齐真实操作与必要依赖、部分失败效果；mesh 修改映射回逻辑节点/几何，比较最终规范化几何，不要求跨根日志顺序和物理 mesh 槽相同 |
| 后继控制 | 每个成员均未越过硬控制边界，最终控制动作/停止原因、迭代消耗和下一严格候选相同 |

只忽略已确认没有语义的存储位置、计时及日志排列，不加评分容差。如果某项后继状态不能可靠规范化或协调效果不能解释，就记录未决，不能随意删除该字段后宣称相等。工作计数允许因串行维护顺序不同而变化，但差异必须报告，并从可并行工作中排除。

审计在同一 `S_j` 的副本上置换同一封闭成员；不得重建闭包或添加成员。审计仅交换 split 顺序，仍检查 merge、停止和资源约束；`strict-prefix` 的严格顺序复现另行检查。故意漏掉一个已知冲突的负例必须能触发差异或非法控制证据。

自然审计最多选择每输入一个批次：按配置顺序 `strict-prefix → 0.1 → 0.25 → 1`，取首个实际完整通过的多成员 batch。`|B|≤4` 枚举全部排列；较大批次用原序、逆序、左循环一位、交换首两根四种去重排列。合计最多 96 次完整应用、5 分钟，原序已有证据可复用；无多成员 batch 则无需排列。

一旦出现 `dependency_model_mismatch`，撤销相关独立资格并暂停同模型所有正向结论，保留最小差异；未审计项明确标未审计，不能暗示每批都已穷举。只有负结果无法由必选头冲突或覆盖解释时，才进行最多两个状态的小子集审计：包含头的前 8 个已检查带内候选，最多 `2^7` 个子集；宽度与 primitive 工作分别比较，找到更好集合仍须应用验证。它不回写主选择器，不追加主矩阵，审计成本计入上述总限额。

## 5. 冻结输入、输出和停止条件

复用 GATE-01 按旧执行前特征冻结的三层代表及反例，不使用 GATE-02 的最细校准网格。输入哈希在改变执行设置之前校验；所有历史相机仍真实重建。

| 运行顺序 | 场景 | sampleIndex | SplitTopology 输入哈希 | 原严格根尝试数 |
| --- | --- | --- | --- | --- |
| 1，反例锚点 | `peking547-a-b20000` | 14 | `4677929227313376363` | 40 |
| 2，低队列代表 | `test129-a-b4096` | 5 | `2878997366482998522` | 9 |
| 3，中队列代表 | `peking547-a-b20000` | 9 | `10820725050463347417` | 37 |
| 4，高队列代表 | `peking547-a-b80000` | 60 | `2459220430983787707` | 144 |

清单继续使用 `benchmark-output/prep-02/input-freeze-20260909/inputs/scenarios.csv` 与 `camera-samples.csv`，SHA-256 分别为 `7cd5e4f3c0fd0961a7cf3566f32603a8b8ee46e9365a8c8d0f729c289e2a2721`、`b6e9015fc597204f9a9162e8ded618ab12f961d39afb6386bc58b802905b0899`。沿用 GATE-01 的资产和来源校验，不根据新 closure/质量结果换样。

每输入按 `α=0 → strict-prefix → 0.1 → 0.25 → 1` 独立执行，共最多 20 个完整变体，确定性记录各采一次。自然执行总墙钟上限 20 分钟，单变体 180 秒，进程峰值 4 GiB；完整状态试算逐个释放，批次/审计记录随批写出，不能积累全部状态副本。达到上限停止并保留覆盖，不为跑完矩阵自动提高上限。

测试驱动仅需一个手动模式：`--relax-natural <scenarios> <cameras> <new-output> <scenario> <sampleIndex>`；接受上述冻结输入，普通 CTest 不运行自然矩阵。输出复用简单 CSV 编码，分别记录变体汇总、批次/候选原因和必要审计差异；不新增通用 schema 框架。原始产物归档于 `benchmark-output/ordering-relaxation/gate-03-<attempt>/`，目录必须新建。

最少记录项：

- 来源/程序/协议身份、配置、初末状态、停止原因、实际 `N/B/U`、正确性和严格对照。
- 计划宽度与 `validatedBatchWidth` 分布、完整批次数、控制中断及串行根数；每批各根 primitive 数、最大根负载和总负载，用于判断宽批是否被一个大 closure 主导。
- `W_root`、primitive 尝试/成功/失败与 split/merge 数、已有粗计数各分量；实际应用与试算/审计分开。相对严格 `S` 报绝对及相对额外工作，分母为零保留绝对值，不编造比例。
- 候选总数、带内/带外、检查/未检查、接收/拒绝分母和窗口排名跨度；主拒绝原因沿大规划固定顺序：窗口、检查/容量限制、未知依赖、共享闭包、写写、读写、协调、预算、分配、其他明确失败。严格前缀主动终止后的项另列未检查，不伪称已发生依赖拒绝。
- 选批/复制试算、实际应用、协调维护与审计的可测成本及进程峰值内存。没有独立测量的部分写清包络；不把粗计数直接换算成 Speedup，也不将整个 root 的开销计为可并行内核。

阶段内检查点：

1. 先完成解析用例及窄接口。若需要大范围改造数据结构才能解释依赖，停止为 `model_limited`，不继续搭建自然驱动。
2. 先跑锚点及低、中代表；每项都是真实完整变体。若发现模型漏边、非法结果或超限，立即停止对应采集并审查；不能带着无效证据继续比较。
3. 三项所有正松弛均无经验证多成员，且已检查成员都被必选头阻挡、没有未检查带内候选足以改变判断时，可直接 `stopped_current_model`，不强制跑高代表或建更多工具。若检查限制/未知依赖主导，写 `evidence_incomplete/model_limited`，不能以批宽 1 宣称问题本身无并行性。
4. 否则完成高代表及必要有限审计，报告实际预算利用率、可行 primitive 工作、额外工作与批内不均衡。出现独立工作只说明值得进一步测量质量；不自动启动 GATE-04/05 或正式算法，也不为寻找正结果扩大松弛范围。

开发投入以本节三个实现职责为界：窄入口、单一实验实现、现有驱动扩展。任一检查点显示必须新增事务层、通用记录/调度平台或改写协调维护，就提交已获得证据和缺口再决定；大规划是最大范围，不是必须造完的基础设施清单。

## 6. 实施顺序与必要验证

1. 保存修改前源码/程序/输入身份、四项严格结果和第 7 节性能基线；随后接入共用控制预览与指定根步进。定向比较默认严格入口，确认预算交换、部分失败、优先合并和迭代上限没有漂移。
2. 实现快照计划及真实应用。解析夹具覆盖同分/窗口边界、不可变来源、同源试算、新候选只能下批、读写/共享闭包、联合峰值预留、独立逻辑分配、控制中断、严格前缀与故意漏边；复用旧部分失败夹具，不复制完整测试矩阵。
3. 增加规范化投影和有界审计，按第 5 节顺序筛查。若需要小子集审计，先用一例已知 greedy 次优集合核验它，再运行选中的自然子集；未使用时不建设搜索组件。
4. 运行受影响路径性能复测，核查与大规划、开发规范逐项一致，记录结果和继续/停止边界。

构建使用已有 `relwithdebinfo-fetch`，仅构建 `parallel_roam_OrderingGate_tests`、`parallel_roam_FormalSplitTopologyEquivalence_tests`、`parallel_roam_DataOrientedRoamSplitPrefix_tests`、runtime probe 和性能需要的 `parallel_roam`。选择：

```powershell
ctest --test-dir build/relwithdebinfo-fetch -C RelWithDebInfo -R "^(OrderingGate|FormalSplitTopologyEquivalence|DataOrientedRoamSplitPrefix)$" --output-on-failure
```

三个入口分别覆盖本次实验边界、原 `sampleIndex=14` 等价反例、默认并行前缀/回退。自然零松弛和 strict-prefix 在本次筛查内完成严格对照，不再单独重复整套自然观察。复用 GATE-02 主参考校准，不跑质量测试、全量 P5、全部 CTest 或双后端矩阵；新失败或实际越界修改才追加相关验证。

验收允许“有效负结果后停止”，不要求出现多成员或正向信号。必须同时具备：来源冻结、真实独立演化、严格对照成立、硬约束成立、计划信息边界可核查、拒绝/覆盖/额外工作可解释、审计状态准确、必要工程验证闭环。资源截断和模型未决允许结束投入，但不能冒充实验验收通过。

## 7. 性能对照与文档闭环

修改共用拓扑循环后不能仅复用 GATE-01 的无回归结论。修改前保存 `a0f3551` 代码对应程序、源码归档和运行依赖；原代码构建身份不符时先补构建。本阶段只选两组互补路径，不重跑原四组全部矩阵：

| 组 | 命令及覆盖 |
| --- | --- |
| 自然严格、诊断关闭 | `<probe> <repo> <scenarios> <cameras> test129-a-b4096 serial-incremental diagnostics-off <run>/frames.csv`；覆盖无观察/无诊断的生产严格循环 |
| 默认预算压力 | `<app> --benchmark --algorithm dod --profile budget-saturation --pass-policy default --csv <run>/frames.csv`；覆盖默认策略及预算交换/回退 |

程序路径分别为 `build/relwithdebinfo-fetch/tests/RelWithDebInfo/parallel_roam_runtime_performance_probe.exe` 与 `build/relwithdebinfo-fetch/bin/ParallelROAM.exe` 的前后归档副本；`repo` 是项目绝对目录，清单路径固定为第 5 节。Probe 的 `EnablePassEvidence/EnableTopologyValidation/EnableTopologyPairEvidence=false/false/false`；app 是 `true/false/false`。两者操作观察均关闭，不能混称同一诊断配置。

复用 `scripts/compare_runtime_performance.py` 和既有局部配置格式，分别建立 app/probe 配置，固定 `comparison=AB`、`layout=pair`、`affinity=all`，使用相同 OpenGL 构建及环境。实际展开的命令、版本路径和配置在首次运行前写入 `<attempt>/protocol.md`：

```text
python scripts/compare_runtime_performance.py --config <attempt>/app-config.json --output <attempt>/comparison/app --cohort gate03-app
python scripts/compare_runtime_performance.py --config <attempt>/probe-config.json --output <attempt>/comparison/probe --cohort gate03-off
```

每版本各预热一次、五次独立计量，前后版本交替，性能任务与构建串行。以独立进程的整体及受影响收敛包络中位数/P95 比较，同时核对工作量和结果。工程调查门槛仍为 `max(0.05 ms, 基线的5%, 原五次统计值极差)`，首次跨门槛才至多完整复测一次；不重开已关闭的微秒问题。两组工程测试总投入上限 45 分钟，超限标未验证。

已有 `--observe-natural` 路径在源码修改前后各采一次作相同工作量的秒级工具成本对照，复用其中的四项严格结果；不把这两次当统计显著性证据。新增选批、实际应用与审计只有新增成本，没有虚构修改前耗时。此项与正式筛查分目录，目的和范围分列；GATE-02 求值器未变，不重复校准计时。

达到门槛且复测稳定的退化单独记录现象、分析和原因，由用户决定新增修复小规划或修改后续规划；不自行修复、延期或扩大测试。阶段实现后更新本节及大规划状态，并按各目录指南写 `docs/codebase/ordering_relaxation/snapshot_relaxation.md` 和 `docs/reviews/ordering_relaxation/gate_03_snapshot_relaxation_review.md`，只记录实际能力和证据。

### 当前核查与实现情况

小规划已对照现有接口与大规划核查：沿用原文件归属、必选头、64/32 限制、最多四输入五配置；明确完整反事实演化、逻辑投影和停止条件。未修改研究问题，未把 GATE-02 暂停能力列为启动前置，未新增 03A/03B。

2026-09-11 用户确认继续实施。修改前两组各一次预热、五次独立计量，以及四项严格观察结果已存入 `benchmark-output/ordering-relaxation/gate-03-20260911/before/`。控制预览和指定根单步继续共用原控制循环；试算经新窄入口调用同一观察 policy，以便实验文件保留局部前后态。这是第 3.1 节既定接口内的复用，没有复制递归操作或扩展文件边界。

### 实现结果与停止决定

已完成第 6 节的窄接口、封闭快照计划、单线程真实状态演化、逻辑投影、有限排列及手动筛查。代码仅新增计划内的实验 `.h/.cpp`，原递归操作、控制器及测试输入复用；没有新建通用平台。输入依旧为 sample14 锚点、test129/sample5、Peking/sample9 和 sample60。

| 项目 | 本轮结果 |
| --- | --- |
| 严格对照 | 四个输入的严格结果及有限根观察与修改前归档相同；strict-prefix 逐步决策及完整结果复现 S |
| 完整筛查 | 前三个输入各五配置，高输入两个严格配置，共 17 个变体；正松弛每个已完成输入都只有一个多成员批次，最大宽度依次为 5、2、4 |
| 排列与记录核查 | 三个预选自然批次共 30 次排列应用；核对 697 条完整批次记录、7955 个检查候选的来源、足迹、预算和分母 |
| 高输入停止 | α=0.1 约 180.095 秒达到单变体上限，停止在 70 次迭代；α=0.25/1 未运行，不上调限制、不冒充低质量完成点 |
| 投入 | 四个自然进程累计约 501.50 秒，最高峰值约 370.83 MiB；没有触发小子集搜索，没有恢复质量评价或扩建 runner |
| 定向验证 | 三个计划内 CTest 最终均有适用通过证据；新增 merge 夹具假设修正后仅重跑 OrderingGate，保留首次失败日志 |
| 生产性能 | 两组、六个包络的中位数/P95 共 24 项未触发冻结工程门槛；不追加复测或修复规划 |
| 原观察工具成本 | 单次总墙钟 11.70 → 13.57 秒，峰值约 337 MiB；只报告差异，不从一对运行推断版本相关性 |

资源截断后的收尾修正仅涉及候选分母、原因文字和审计资源分类，使用过期 deadline 解析用例核查，不重跑完整自然矩阵。旧高输入最后一个超时批次的未分类暴露原样保留并排除；没有用修正后的规则回填旧数据。最终纯注释补充不改可执行语句，源码/程序和差异证据分别归档，见[代码事实](../../codebase/ordering_relaxation/snapshot_relaxation.md)与[阶段审查](../../reviews/ordering_relaxation/gate_03_snapshot_relaxation_review.md)。

本轮没有拓扑或预算违规，但只有局部独立工作证据。sample14 的完整执行未偏离 S；sample9 改变请求顺序而规范化几何相同；test129 改变几何并多使用一个三角形，尚待独立质量评价。简单单位 primitive 模型的工作/跨度约为 1.045～1.091，不能称 CPU Speedup；大量后续候选受联合预算限制，未检查窗口仍然很大。

筛查结束时仅保留有限机会及高输入缺口，未宣布完整 Gate 通过。用户随后决定关闭本轮并归档实现，最终处置以下节为准；上述数据和限制保持不变。

## 8. 用户决定关闭与实现归档

2026-09-11 用户明确要求“这轮 gate03 先关闭，之前的实现移入过时文件夹”。该决定授权停止并撤出本阶段实验实现；不等同于一般排序松弛被否证，也不授权开始 CBT 路线或提交 Git。

### 文件边界与处置步骤

本次为既有阶段的关闭整理，不引入新算法、公共抽象或运行开关。

1. Create：`obsolete/ordering_relaxation/gate_03/README.md` 和 `manifest.json`，仅记录归档范围、基准提交、文件校验和及恢复方式，不建立独立构建系统。
2. Move：将本阶段独有的 `OrderingExperiment.h/.cpp` 按原相对路径移入该目录；归档文件不进入当前生产或测试构建。
3. Preserve / Restore：先完整保存本阶段修改后的 `DataOrientedRoamTopology.cpp`、`DataOrientedRoamTopologyExperiment.h`、`tests/OrderingGateTests.cpp` 和 `tests/CMakeLists.txt`，再将工作树中的这四个共享文件恢复到本阶段前的 `a0f3551` 内容。逐项核对差异仅属于本阶段，避免损失其他修改。
4. Reuse：保留 GATE-01 操作观察、严格步进及其测试，保留 GATE-02 双线性主参考与既有校准能力。规划、事实和审查按文档目录规范留在原处，明确历史状态并把实现链接指向归档。
5. 更新大规划、讨论记录和阶段移交状态：本轮已关闭，GATE-04/05 不继续；旧实验输出留在 `benchmark-output/ordering-relaxation/gate-03-20260911/`，不改写、不删除。

依赖恢复为 `OrderingGateTests → GATE-01 TopologyExperiment → 既有 DOD`；`obsolete/` 只有历史证据，不成为活动模块的依赖。

### 验证与性能对照

先核对六份归档文件与关闭前原件的 SHA-256，再核对四份恢复文件与 `a0f3551` 的 Git 内容一致。重新构建 `OrderingGate`、`FormalSplitTopologyEquivalence` 和 runtime probe；只运行前两个定向 CTest，覆盖保留的观察/严格步进及原拓扑反例。检查活动源码、构建清单与生成项目不再引用归档或指定根入口，核对相关文档链接。

性能只选现有 `test129-a-b4096 / serial-incremental / diagnostics-off` Probe，覆盖撤回共用控制改动后的细分和预算交换；诊断三开关固定为 `false/false/false`，操作观察关闭。关闭前版本复用本阶段 `after/` 已归档程序、源码和报告，关闭后使用恢复源码的新构建；采用现有比较脚本，在同一环境按 AB/BA 各一次预热、五次独立计量。记录整体及受影响细分/合并包络的进程级中位数/P95、工作量和结果，门槛沿用开发规范第 7.3 节；不重跑自然松弛矩阵、质量校准、图形后端矩阵或已关闭的微秒调查。

本次产物独立保存到 `benchmark-output/ordering-relaxation/gate-03-close-20260912/`；关闭整理跨过本地午夜，原筛查日期及产物目录仍为 2026-09-11。构建与计量串行执行。审查结果追加到既有 GATE-03 审查，避免为一次关闭另建实验平台。

### 归档实现结果

2026-09-12 完成归档。六份文件与关闭前原件 SHA-256 一致；用归档覆盖层重建的源码身份精确复现关闭前交付身份 `7d6527cb07e33d9d75931b604242bf550eda2b41b3644c696df2457e4a0a7c23`。四份活动共享文件的 Git 内容与 `a0f3551` 一致，GATE-03 独有实现已移出活动目录；归档中保留原 CMake 文件仅用于历史重建。

三个选定目标构建完成；`FormalSplitTopologyEquivalence` 和保留 GATE-01 夹具的 `OrderingGate` 分别约 29.96 秒、0.04 秒完成。Probe 前后每版本一次预热、五次独立计量，语义摘要与历史记录相同，12 项相关工程统计未触发冻结门槛，不追加复测。整体耗时中位数同期为 0.9147 → 0.8905 ms；P95 为 1.122775 → 1.297045 ms，配对变化 +0.06882 ms，未超过原基线极差形成的 0.47319 ms 门槛，不把波动写成严格性能等同。完整报告见本次产物 `report.md`、`engineering-comparison.json` 及 `comparison/probe/`。

关闭结论已同步大规划、讨论记录、GATE-02 移交、GATE-01 当前接口事实及本阶段历史事实/审查。原始筛查数据、已知缺口及双线性参考决定保持不变；当前无继续 GATE-04/05 的任务。关闭整理结束时未提交 Git，用户于 2026-09-12 随后授权提交本轮归档，再编写独立的最小 CPU 类 CBT 实现规划。
