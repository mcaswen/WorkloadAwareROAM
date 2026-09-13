# 全局优先级事务化 CPU LOD：持续原型实现大规划

> 2026-09-14；**Major Plan，供用户 Review，尚未批准实施**。由 GMP-04 输出，本文件不扩大原型前规划的执行授权。
> 依据：[GMP-01 契约](../../research/cpu_refinement/greedy_multipass_contract.md)、[GMP-02 组合推导](../../research/cpu_refinement/transaction_batch_derivation.md)、[GMP-03 自然结果](../../research/cpu_refinement/transaction_backend_gate.md)、[已有工作边界](../../research/cpu_refinement/transactional_lod_prior_art.md)、[能力事实](../../codebase/cpu_refinement/preprototype_capability_baseline.md)。

## 1. 问题、目标与准入限制

当前已有无需 Legacy 产 J 的有限事务发现与自然批次，但实现为离线 Python，全量恢复派生状态，且两个快照暴露视图外高度误差上升。下一步验证的是：**在持续自有网格上直接发现并兑现预算事务，完整计入认证和维护后，是否仍值得采用 CPU 批次执行。**

核心变化是拆分逐事务 greedy 决策与立即拓扑修改的反馈；不把“multi-pass”或局部删点本身当创新。新算法保留全局优先思想、活动预算与共形性，推广连接空间，**不继承原 ROAM 二叉 cut、逐请求 greedy 或质量最优性**。

本规划成功需要同时交付：

1. 不复制整帧、不 replay Legacy、不输入外部目标 J 的持续串行发现/应用路径
2. 完整存活状态、样本/候选局部续接及增量 CPU mesh 消费，而非只输出一个合法网格
3. 一个预注册的质量保护变体，验证 GMP-03 已暴露的视图外风险；原 v1 保留为诊断基线
4. 竞争力合理的动态串行 A、批次串行 B 与相同任务并行 C，对比完整工作和时间

可以按阶段得到“算法可续接但成本过高”或“局部保护令机会消失”的负结果；不得为了完成大规划自动放松质量、扩大 primitive 或继续堆微优化。

## 2. 范围、非目标与复用

范围为独立 `src/experiment/greedy_transactional_lod/`，无图形测试/探针运行，沿用两个场景和现有 E/F/H、三价/一般 one-ring。正常后端使用固定耳切；DP 仍是限额离线 oracle，不进入每轮默认路径。

不做：2-ring、多点任意 remeshing、GPU、NMP 优化、通用 profiler/求解器、生产注册/GUI 开关、渲染上传重构、全实验矩阵。新的算法入口开关在实验 CLI 区分 A/B/C，Classic/DOD 生产入口继续独立；生产切换另行规划。

| 判断 | 对象 | 理由 |
| --- | --- | --- |
| Reuse | 冻结来源/相机、公共 mesh 值类型、计时及已有解析反例 | 避免重建输入平台，保持原结果可追溯 |
| Wrap | GMP-03 中立快照与原始样本；现有同步 `MaterializationExecutor` 仅在探针桥接执行回调 | 初始化不求目标；复用已处理任务异常/排空的执行能力，不复制线程池或让核心依赖 MPR 状态 |
| Extend | tests 构建清单、独立验证及阶段结果文档 | 新实验与生产清单分离 |
| Create | 一般网格持续状态、局部样本索引、认证、事务预留/应用、动态串行策略 | DOD/MPR 的层次身份/队列/旧高度输出不适用于一般拟合网格 |

已扫描 `MaterializationExecutor`、DOD 线程池、公共算法/mesh 头及 tests 构建方式。现有模块没有通用 C++ 精确几何/有理认证依赖；不能声称 Python Fraction 可以直接搬入热路径而免费获得原精度。

## 3. 模块、目录和关键文件

按职责分离，阶段到达才新增，不预建空框架。以下 `.h/.cpp` 表示同一组件接口与实现；简单值集合只要 `.h`。

```text
src/experiment/greedy_transactional_lod/
  TransactionalTypes.h
  TransactionalState.h/.cpp
  TransactionalSamples.h/.cpp
  TransactionalPredicates.h/.cpp
  TransactionalCertification.h/.cpp
  TransactionalProposals.h/.cpp
  TransactionalReservation.h/.cpp
  TransactionalCommit.h/.cpp
  TransactionalMesh.h/.cpp
  TransactionalPipeline.h/.cpp
  TransactionalDynamicReference.h/.cpp
  TransactionalInput.h/.cpp
  TransactionalValidation.h/.cpp
tests/
  GreedyTransactionalLodTests.cpp
  GreedyTransactionalLodProbe.cpp
  CMakeLists.txt
docs/{plans,reviews,codebase,research}/cpu_refinement/
```

| 文件 | 主要接口/数据与独立存在理由 |
| --- | --- |
| `TransactionalTypes` | StableId、Version、View/RuleConfig、Intent、Proposal、Certificate、CertifiedBatch、工作/阶段结果；值不持有 DOD 引用 |
| `TransactionalState` | 顶点/面槽、邻接、身份映射、空闲槽与版本，独占拓扑存活期；不给规划器可写数组 |
| `TransactionalSamples` | 固定 reference/Q、owner/闭面关联、每面样本块、局部重分配与当前 P；独立管理 O(Q) 状态 |
| `TransactionalPredicates` | 定向/包含/角度及精确回退；不定义 urgency 或挑事务 |
| `TransactionalCertification` | 屏幕/高度区间、拟合、舍入后接受、数值未知；几何和质量成本独立于全局策略 |
| `TransactionalProposals` | 冻结 E/F/H 和 one-ring 耳切的只读构造；输出完整支持与证书所需值，不修改活网格 |
| `TransactionalReservation` | P 全序、共同空额度、有限池、读写冲突、不可回退预留；不重拟合几何、不提交 |
| `TransactionalCommit` | 根据已认证批次计算局部记录/槽写入、发布与派生修复；不能调用候选发现 |
| `TransactionalMesh` | 当前拟合高度的持久 CPU 输出、槽变动、脏区间、消费代际；不负责拓扑决策 |
| `TransactionalPipeline` | B/C 同一阶段编排、只读借用和 barrier、整体计时；不吸收各组件算法 |
| `TransactionalDynamicReference` | A 的逐事务选择/重试/停止，使用同一原语与局部状态方法；与 B/C 的高层反馈独立 |
| `TransactionalInput` | 中立快照/原始样本输入及摘要检查，仅初始化；诊断 JSON 等解析不进入核心库 |
| `TransactionalValidation` | 全量独立几何/派生/输出核对，计时遍关闭；不作为正常 repair 实现 |
| 两个 tests 文件 | 解析风险验证与有限自然编排/记录分别归属；probe 包装外部同步执行回调 |

不创建通用事务框架、插件系统或数值抽象继承树。核心采用值型提案、显式只读借用和同步执行回调；A 与 B/C 是策略差异，`Serial/Parallel` 是同一批准批次的执行选项，避免交叉 flag 改变任务。

## 4. 依赖、接口、状态和事务生命周期

依赖为 `Probe → Input/Validation/Pipeline/Reference`；`Pipeline → Samples/Proposals/Reservation/Commit/Mesh`；几何模块只依赖核心值/数值能力。探针可链接 MPR 执行适配器的 `Execution()`，将 Workers/Dispatch 桥接为本核心回调；核心头不得包含 MPR/DOD。生产库不依赖实验模块，旧实现不经过新 StateView。

主要接口规格：

- `Initialize(CurrentMesh, RawSource, Config)` 创建自有状态；输入导入、全域认证、Q 初建费用单列
- `Refresh(View)` 更新 reference 可见集与 P；视图不变只更新事务支持，相机改变允许全 Q 投影，不能假装 O(k)
- `Plan(ReadOnlyState, RuleConfig)` 直接产生完整已认证批次；批次持有 snapshot version、局部新几何/依赖和预算证书
- `Apply(CertifiedBatch, Execution)` 先验版本/资源验证，再私有准备，最终发布；不查找新 donor、不更新本批需求
- `ConsumeMesh()` 返回到下次发布/reset 前有效的借用与脏区间；消费是独立动作，不能每轮清空 Pending 逃避生命周期

状态方案：稳定身份与物理槽分开，顶点/面使用连续槽及空闲表；每面最多三个顶点/邻面，顶点 incident-face 集在参数域最小角不变量下有界。这里的角度沿用 GMP 的二维参数域定义，不宣称拟合高度后的三维面角同时受界。删除槽不得导致全域 identity 重编号；发生 output 尾部搬移时只修对应记录与输出范围。容量增长允许 O(N) 偶发成本，记录次数/字节，不宣称每轮严格 O(k)。

样本位置由原始栅格的固定六组公共点隐式编码，reference 值和视图值可缓存。面关联采用可局部增删的块/小段，不用每次重建全局 CSR。局部修复枚举被删除/新增/高度变化的完整闭补丁及外接口样本；保留面上的旧 owner 不意味着样本不受影响。归属由稳定 face ID 同分，边界贡献覆盖所有相关面；这些更新也会使外部面的 P 失效。

提交不能在 live 状态上一边发现一边试错。阶段顺序为：冻结批次 → 核对版本/预算 → 为所有新槽和局部记录预留容量 → 并行或串行准备 → 验证准备成功 → 一次性发布核心 → 按规定归约/重算派生状态 → 推进代际。可能分配的步骤在发布前完成；提交后的写入不得再抛出可恢复分配异常。任务失败先排空，旧状态仍可用；如果不能实现局部强失败保证，应在小规划暴露，不以整帧复制替代。

第一版共享派生写使用收集贡献后串行归约，普通边关联仍按完整 R/W 排斥；不增加共享写交换律或宽松冲突优化。活动发布 N≤B；准备内存峰值与活动预算是两项账本。回收与接收作为净零事务发布，不向消费者暴露中间洞或超预算网格。

输出使用当前顶点高度；首版三个输出顶点/面并使用当前面的几何法线，避免用 source 法线掩盖拟合。共享平滑法线作为未来可选输出变体，不是本轮必做。颜色/UV/代际/索引范围仍满足公共 CPU mesh 值契约；未注册到生产 renderer。

## 5. 数值与质量：先保留基线，再单独保护

### 5.1 C++ 表示与认证边界

推荐正常状态采用实际发布的 binary64 参数坐标/高度；新点先舍入，再对**实际存储值**重做几何和样本接受。不得认证有理理想点、却提交另一舍入点。GMP-03 保留有理几何，C++ 版本因此使用独立数值标签，不要求跨版本未来轨迹逐字节一致；同一 C++ B/C 必须一致。

几何采用快速过滤和精确回退，质量采用保守区间与必要精确比较；无法认证则 `numeric_unknown` 拒绝。精确投影只需有理平方比较，不必先求精确平方根。拟合用双精度候选不承担证明，验收阶段覆盖 sample-on-edge、近退化角度、共享坐标、近面和深度分母。

**新依赖的推荐决策**：仅实验目标引入锁定版本的 Boost.Multiprecision 头文件能力，提供二进制浮点精确有理回退；[官方 cpp_rational 文档](https://www.boost.org/doc/libs/latest/libs/multiprecision/doc/html/boost_multiprecision/tut/rational/cpp_rational.html)确认该有理类型与 Boost Software License 1.0。具体版本与可用性在 GTP-01 小规划冻结，不自动下载或复制未知来源。现有 CMake 未提供该依赖，此项是需随本 Major Plan 一起 Review 的新增依赖，不是已安装事实。优先让快速过滤处理多数样本，精确回退次数/成本单列；不建设通用几何库。若依赖/数值实现费用超出小阶段，应停在该门禁，不改为 epsilon 认证或无限精度的默认全热路径。

### 5.2 v1 诊断基线

保留 β、θ、η、P/G/C、有限目录/池和空额度规则，原视域 V 接受逻辑不改；用 GMP-03 快照做初始单批对照。双精度发布的差异必须有具体数值/证书解释，不能把差异全部归于“浮点正常”。原始理论/有理 Python 仍是离线局部 oracle，不进入正常计时或给 C++ 目标。

### 5.3 推荐的受限质量保护变体，尚未实施

在同一提案目录内增加 **全 Q 局部高度上界不增** 条件：对事务完整修改域 K，要求 `max(q∈Q∩K)|h_new−h*| ≤ max(q∈Q∩K)|h_old−h*|`，接收/回收组成完整交换后认证。重叠边界和旧高度变化的完整邻域均包括在 K。几何独立批次加此局部条件，可保证该批全 Q 的 sampled Hmax 不增；不是逐点不增，更不是所有视点屏幕误差不增。空样本或数值未知不当作安全。

拟合解选择可同时固定为“满足所有证书的解中，尽量接近旧高度/新点 source 高度”，作为确定性 tie-break，不能把它当作单独质量保证。GTP-02 小规划先写出一/二维可执行规则与费用，再运行。v1 和保护版本分别标记，分别报告认证率/批宽/成本；**不能拿 v1 的 21/23 个交换作为保护版本证据**。

这个变体不新增几何原语，但确实收紧接受集，必须在结果出现前冻结。若有限面板上的非平凡机会消失，停止扩大并行实现并 Review 质量契约；不自动放宽 H 界、改 θ、扩大 donor pool 或开启 2-ring。持续相机质量仍要直接测量：每轮绝对 screen/H、最大点、预算、交换量和不适应原因，不能以 H 不增替代未来视图质量比较。

## 6. 四类参考与直接目标发现成本

| 路径 | 解决的问题 | 对照解释 |
| --- | --- | --- |
| A 动态串行 | 每次应用后在新状态生成下一请求；同原语、P/G/C 和同质量变体，允许正常局部缓存/优先队列 | A→B 是反馈语义、批次选择和组织的联合变化；报告不同网格与质量/实际工作 |
| B 批次串行 | 同代全域需求排序，固定认证/预留，串行应用 | 与 C 同配置时必须产生同一批次与完整续接结果 |
| C 批次并行 | 同 B 的规划语义，独立认证/准备可并行，实际唯一槽写入及 barrier 后续接 | B→C 才是同任务线程收益；不能只比较拿到外部 batch 的填写时间 |
| D Classic/DOD | 现有正式算法的整体 CPU 成本/质量/预算/适用范围 | 不是 A 的替身；输入阈值数值相同也不等于任务或质量相同 |

A 的候选集合应由局部失效/重算维护，不能为了简单在每次操作后扫全 Q。相机换帧刷新全域一次合理；局部事务只刷新真实支持。某候选失败后的重试仅在相关状态版本变化时发生；动态停止、相同 priority tie-break 和有限搜索规则在 GTP-03 小规划冻结。有限的全量重算 A 只可作正确性 oracle，不能作为计时 reference。

B/C 的前缀限额默认仍为 64，donor pool64，目录≤8；完整原始 P 生成和全序不省略，尾部为本批未处理。CPU cost 包含排序或等价确定性前缀选择的全输入费用。后续扩大限额属于另标记工作点，不能拿它替换冻结结果。

完整计时必须为 `T_refresh + T_order + T_proposal/cert + T_reserve + T_prepare + T_publish + T_state + T_mesh`。同时保留给定批次应用边界；加载/初始化、独立诊断和输出文件另列。正常快路径不能读预先计算好的交易、局部高度或目标 J，不能把认证移到计时区外。

## 7. 工作、span 与有限并行边界

每轮保存 N、|Q|、样本关联量、原始/检查/认证需求、真实 proposal/pair 数、精确回退、失败/冲突、受影响面/边/样本、容量/分配、mesh 写及各阶段时间。

成本目标是 `O(N_candidate + sampleTouches)` 的刷新，加需求排序/选择、局部证书与事务维护；不能写成每面扫描所有 Q 的 `O(N|Q|)`。几何 degree≤19 只限制局部拓扑记录，不限制一个面/环里含多少 reference 样本。全 Q 刷新与动态数值回退可能是主成本，必须独立说明。

首版只并行：各面/样本块的独立评分、互不修改状态的提案/认证、已批准事务的私有记录填写及预分配唯一槽写入。全局顺序预留、容量管理、映射/空闲表修改及共享派生归约先串行。读快照期间不重分配 live 数组；并行阶段不 `push_back` 共享容器，不写同一压缩字，不让线程调度改变 ID 或选择。

全部费用记录后才估计剩余 headroom。没有工作量/吞吐证据时不根据批宽直接报 W/D 或理论 CPU 上限；不再把串行 trace 顺序当必需依赖。完整 C 没有收益时，若大部分成本在未并行认证，应称当前边界受限；若即使被选并行段免费也无足够总收益，则停止扩建这套并行边界。

## 8. 实施阶段与每段停止条件

每阶段先写小规划，完成必要验证/自审后按用户许可提交，再进入下一阶段。**本 Major Plan 尚待 Review，以下不是当前实施授权。**

| 阶段 | 范围与产物 | 验收、成本及停止 |
| --- | --- | --- |
| **GTP-01 表示/数值与单批串行内核** | 确定数值依赖与真实发布值；自有状态/Q；v1 直接提案/认证/预留/一次应用；解析与四个既有快照 | 无 Legacy 目标、前后状态合法，局部接受独立核查；初建/发现/认证/应用全计费。不能可靠认证或发现仍需旧执行器则停；不先造并行设施 |
| **GTP-02 局部续接与质量保护** | 同一活状态连续更新，局部 ownership/P/网格/Pending；预注册 §5.3 保护变体 | 局部结果对全量 oracle；消费/不消费交错及槽复用；视图外高度反例；保护前后认证/批宽单列。若需要未登记全扫/reimport 或保护令机会消失则 Review，尚不并行 |
| **GTP-03 动态参考与有限轨迹成本** | A 的自然局部维护参考，B 的持续轨迹；同一 seed 各自独立演化 | 两场景 sample14 初始化，固定视图3轮，再用既有 sample15..18 各1轮，再返回sample14一轮；共8次更新，不从Legacy再导入。对 A/B 报质量、利用率、适应与总工作，不能把不等价时间差称并行收益。成本/质量明显失去研究价值则停 |
| **GTP-04 有限多核与出口** | B/C 相同决定；并行独立认证/准备及唯一槽应用，完整发布/维护计费；D 作少量家族对照 | 先1/4线程（可用核心不足则预冻结较小数），真实工作线程证据、同批/最终/下一轮状态相等、并发错误负例。报告完整update和update+mesh；无收益保留成本分解，不为一条加速曲线扩展原语/平台。生产集成另写规划 |

GTP-01 单批没有局部续接性能保证；GTP-02 完成后才能称持续原型。GTP-03 轨迹只是现实核查，不是泛化数据；没有质量 operating point 覆盖时不宣称“质量可接受”。GTP-04 可以得出功能成立但性能不支持进一步集成。

## 9. 验证与性能契约

复用已追溯 GMP 输入/解析例，独立验证新增风险而非复跑全部旧证明。只构建新核心/测试/探针；生产和两个图形后端未改，不运行全 CTest。影响共享组件时才加该组件相关测试。

正确性最小集合：共享边样本外 owner、非 dyadic 新点、完整旧点高度支持、边写/高度读冲突、重复预算预留、空额度闲置、顺序/反序、容量不足/任务异常、版本失效、连续槽复用、Pending 多轮消费、相机变化后从旧状态继续。含局部/全量 oracle 对照；不通过“重新导入干净网格”恢复。全量检查仅诊断遍，正常路径只作必要安全检查。

每个实现阶段沿用[开发规范 §7.3](../../standards/development_guidelines.md#73-工程性能回归的实际影响门槛)，先保存当前相关路径一组独立进程的短基线，修改后同环境同输入复测；新增能力独立计费。普通开发先一组、发现实质变化才定向补一组，不默认 5×30、全线程/全场景矩阵。预热至多1、内部短重复至多3，具体小规划按运行成本收紧；首次自然耗时超出预冻结配额保存部分证据并停止。

诊断遍记录逻辑工作/线程参与/全量正确性，计时遍关闭细粒度探针；两者输入/结果身份一致。CPU总时间若直接可得可辅助记录，不新增大 profiler。源/配置/程序/数据 SHA、原始逐进程结果、正常及压力限制存 `benchmark-output/cpu-refinement/gtp-xx/`，Git 忽略；报告不能只保留最快样本。

归因要求：微小差值按实际影响门槛记录，不扣几微秒；明显退化形成独立原因报告，按届时用户授权确定修复范围。不能以最快局部 prepare 隐藏串行认证、预留或状态维护。没有预先承诺 2～5 倍，也没有把“快于 Python”当 CPU 算法成功；真正对照是竞争力合理的 A/B/C 和独立质量。

## 10. 与原 DOD 五阶段及未来生产接入

本原型不是把新代码插到旧 `SplitTopology` 然后复用旧节点。新一般网格在首次初始化后由自身维护：

| 旧职责 | 新原型对应 | 边界 |
| --- | --- | --- |
| MergeScore | 回收代理/证书刷新 | 不复用旧菱形 eligibility、heap 或 hierarchy error |
| MergeTopology | 已配对交易中的回收部分 | 与接收统一预留/发布，不独立运行旧阶段 |
| SplitScore | 完整当前网格 P 与接收认证 | P、G 分离；不能对拟合面沿用 source variance |
| SplitTopology | 全局 intent → 事务预留 → 批量应用 | 改变执行/连接语义，不借名原 `ParallelAssisted` |
| MeshEmit | 当前一般网格增量 CPU mesh | 自有身份/槽/Pending，不能调用旧 `WriteDomainTriangle` 重新采 source |

未来若值得接入，新增独立算法 ID，经 `ITerrainLodAlgorithm`/`TerrainLodRenderPacket` 返回借用数据和更新区间，用户可切换 Classic/DOD/新算法；切换明确 reset/初始化，不共享两套活状态。OpenGL/D3D12 继续各自资源/上传模型，只有公共 CPU 输出契约一致。本规划不实施该适配或改旧五阶段计时语义。

## 11. 关键风险、备选与目标架构

| 风险 | 决策及备选 |
| --- | --- |
| 保护规则下无非平凡批次 | 保留 v1 与保护结果；暂停并行扩建，Review 接受契约，不加新原语救数据 |
| 精确回退/样本工作太大 | 先分账和过滤命中率，允许有限性能改进；不把不确定接受为真，不以免费 oracle 掩盖 |
| A 维护困难 | 先写局部正确性/失效表；全量慢 A 只作 oracle，公平性能比较不能省略 |
| 顺序预留/缓存维护抵消并行 | 报工作比例与当前边界；可结束原型，不承诺 universal replacement |
| 与近邻研究重合 | 以执行契约和完整实证补差异；本轮有限检索不等于论文 novelty 通过 |
| 旧原型工具复用形成跨域依赖 | 仅探针包装 MPR 执行适配器；核心为自己的同步回调，无层次/历史数据耦合 |

待本 Major Plan Review 的关键决策是：独立实验状态与上述文件边界、binary64 发布后认证及新增精确回退依赖、v1/高度保护两种明确标签、先续接/质量再有限多核的四阶段顺序。推荐按本方案推进，不需要在开始前继续泛化理论；实施仍须获得新规划授权。

目标架构为 `自有当前网格 → 同代P/需求 → 局部提案与认证 → 全局预留 → 批量准备/发布 → 局部状态与mesh → 下一轮`。目录只落实验模块/测试及相应文档；来源、数值、状态、策略、应用和输出各自拥有职责，生产无反向依赖。

## 实现情况

尚未实施。本文件为 GMP-04 的设计产物，已具备阶段/文件/依赖/质量风险和验证边界，供用户 Review。
