# 算法推导与形式化验证总目录

[06H认证准备](../cpu_refinement/qpc_06h_certification_preparation_results.md)：CP-01集合成员等价、CP-02成功后绑定及根内复用负结果；纸面与有限检查，无新Lean。

2026-09-18补充：[06G坐标证据解耦](../cpu_refinement/qpc_06g_coordinate_evidence_results.md)，CE-01坐标不依赖高度、CE-02覆盖与认证等价及实际成本；纸面推导与有限C++核对，无新增Lean。

更新：2026-09-18。范围：项目自有推导、证明过程、反例及 Lean 源文件；不含第三方、工具下载与构建缓存。原始材料保留在所属模块，不复制或移动证明工程。

## 1. 阅读入口

- **连续阅读公式与推导链：**[数学推导总览](mathematical_derivations.md)。包含前提、推导、边界和原文链接，不仅罗列公式。
- **一般模型：**[资源约束三角化事务](../adaptive_triangulation/README.md)、[推导与反例](../adaptive_triangulation/model_derivation.md)和[最终报告](../adaptive_triangulation/generalization_results.md)。ATT-01～03完成：几何纸面、资源/质量/预算3个Lean源、有限实例与反例；条件安全模型可一般化，非通用greedy算法或生产修复。用户已暂停此支线，不继续扩展。
- **当前算法优化：**[QPC-04E 逐点损伤与质量组合推导](../cpu_refinement/quality_contract_derivation.md)的抽象Lean核心与只读自然审计已完成，见[结果](../cpu_refinement/qpc_04e_quality_contract_results.md)。[04F持续政策](../../plans/cpu_refinement/qpc_04f_target_quality_policy_plan.md)已实施；QC-10～13为纸面推导/数值夹具/有限独立审计，见[结果](../cpu_refinement/qpc_04f_target_quality_results.md)。损伤控制有有限验证，恢复/成本受限，默认未改。
- **最新诊断：**[QPC-04G推导](../cpu_refinement/proposal_feasibility_derivation.md)完成PF-01～06及[有限审计](../cpu_refinement/qpc_04g_proposal_feasibility_results.md)：43项中28个新契约正例均被旧统一最大值门槛排除，8项安全域仅能保持旧曲面；双域/原生核查完成，生产拟合未改。12项正例进展仅存储级微量，所有正例局部Emax不变，不能当作持续恢复完成。
- **实现与费用：**[算法及阶段复杂度](../cpu_refinement/transactional_algorithm_overview.md)、[QPC-04D 原因分析](../cpu_refinement/qpc_04d_error_first_results.md)、[质量与成本重审](../cpu_refinement/transactional_quality_cost_reassessment.md)。
- **执行调度推导：**[QPC-06D §3](../cpu_refinement/qpc_06d_receiver_task_balance_results.md#3-从热点到实现什么变了什么成本没有消失)给出独立根唯一领取、确定前缀收集的同结果论证及理想列表调度界；纸面推导、专项测试与有限运行支持，不新增Lean或语义必需span定理。
- **面证据推导：**[QPC-06E §3](../cpu_refinement/qpc_06e_face_evidence_results.md#3-从函数热点到等价变换推导过程)记录原表达式子式复用、覆盖/认证等价和`2e+6h→8b`的工作计数；保留区间舍入、空间和早拒绝边界，没有新增Lean。
- **高度生成消融：**[QPC-04H推导](../cpu_refinement/qpc_04h_source_height_derivation.md)区分源高候选的结构准备和双域认证；旧 Fit 不是质量定理前提，但删除它也会删除前置筛选。[有限结果](../cpu_refinement/qpc_04h_source_height_results.md)单列可行性、质量分布和成本，不将无 Fit 等同于性能收益。
- **高度拒绝推导：**[QPC-06F HR-01～05](../cpu_refinement/qpc_06f_height_rejection_derivation.md)完成保守损伤、提示当前重证、正常接受集合/实际决策等价和收支条件。纸面与34,705次原认证重放支持有限降本，没有新增Lean或质量修复结论。
- **历史状态：**[研究入口](../README.md)。早期状态按记录日期解释，不替代最新阶段报告。

## 2. 证据层级与模型分界

| 层级 | 含义 | 不能据此声称 |
|---|---|---|
| 纸面证明 | 在明确前提下给出一般论证 | 全部生产字段、数值和并发已验证 |
| Lean 核心检查 | 指定抽象声明曾经内核检查，记录可追溯 | 几何对应、实现和性能也已证明 |
| 有限核查 | 手算、脚本或有限真实输入 | 全输入定理或连续曲面保证 |
| 源码/实验事实 | 对特定版本与配置成立 | 其他政策或同质量竞争性结论 |
| 待验证 | 候选条件或运行时对应未完成 | 已经准入生产 |

两条模型不能直接拼成一个定理：

1. **固定 ROAM 层次：**固定事件身份/依赖、已知目标或单调优先级；闭包与端点修复 Lean 属于这里。
2. **事务化一般连接：**从当前网格生成需求、局部回收与重连、快照顺序预留；结果不要求是 ROAM 二叉层次的 cut。

旧模型提供局部性和状态契约的思路，不证明当前算法的目标选择或持续质量。局部删点、区间求解、DP 和冲突集合本身也不自动构成原创贡献。

另立的**一般局部三角化事务模型**已完成条件拼接、资源和质量机械层及有限实例。它抽取不依赖ROAM的安全组合命题，不代表上述两条模型已经统一，也不反向证明当前生产满足新增质量条件。

## 3. 当前事务算法的推导索引

| 编号 | 原文 | 核心问题与过程 | 状态/边界 |
|---|---|---|---|
| TX-00 | [兼容性表示](../cpu_refinement/compatibility_representation.md) | 无裂缝是否必须传播细分 | 历史问题分析 |
| TX-01 | [Greedy 局部预算交换](../cpu_refinement/greedy_local_exchange_candidate.md) | 需求、交换、停滞反例、见证插点、证书成本 | 条件证明与有限核查 |
| TX-02 | [高度 minimax](../cpu_refinement/local_height_minimax_derivation.md) | 区间、活动极值、少量自由度、透视约束 | 固定几何/视图；非持续恢复证明 |
| TX-03 | [有界完整补丁](../cpu_refinement/bounded_patch_refit_derivation.md) | 改高支持、双高度拟合、三价回收及停滞 | 历史高度自由度；当前政策已收紧 |
| TX-04 | [一般 one-ring 回收](../cpu_refinement/cavity_budget_recovery_derivation.md) | CR-01～08、面数、DP、正例及类别内反例 | DP 是审计工具，不是生产默认 |
| TX-05 | [GMP 执行契约](../cpu_refinement/greedy_multipass_contract.md) | P/G/C、目录、快照贪心预留 | 历史 v1；后续有边界 +1 等修订 |
| TX-06 | [批次组合与续接](../cpu_refinement/transaction_batch_derivation.md) | 读写集、共享接口、交换应用、局部修复 | 条件证明/有限核查，非完整 C++ 验证 |
| TX-07 | [算法及复杂度](../cpu_refinement/transactional_algorithm_overview.md) | 阶段费用、精确分支、串行成本 | GWR 主体，页首有后续修订 |
| TX-08 | [质量/成本重审](../cpu_refinement/transactional_quality_cost_reassessment.md) | 04B/04D 反例→需求、损伤与进展分离 | 候选设计，未改生产 |
| TX-09 | [QPC-04E 形式推导](../cpu_refinement/quality_contract_derivation.md) | QC-01～09、逐点条件、归纳、组合及计费 | QC-01～03抽象Lean、71交换有限审计；三操作点保留机会，非持续修复 |
| TX-10 | [QPC-04F 接入规划](../../plans/cpu_refinement/qpc_04f_target_quality_policy_plan.md) | QC-10～13：请求、独立证据组合、持续义务和数值对应 | 已实施；纸面推导及47交换双域独立核查，未扩Lean证明范围；恢复/成本受限 |
| TX-11 | [QPC-04G 固定提案可行域](../cpu_refinement/proposal_feasibility_derivation.md) | PF-01～06：透视误差仿射条件、内/外包方向、单点无进展、存储对应 | 纸面推导+解析反例+43项有限有理审计；没有新Lean，不是持续恢复证明 |
| TX-12 | [QPC-06A认证降本](../cpu_refinement/quality_cost_reduction_derivation.md) | QCOST-01～04：局部证据复用、binary64定向累计、决策等价与费用 | 纸面推导与独立有理oracle；不修改质量政策或扩展Lean范围 |
| TX-13 | [QPC-06B空批复用](../cpu_refinement/idle_plan_reuse_derivation.md) | IDLE-01～04：完整空结果、实例/代次失效、资源中断与复杂度 | 纸面推导、专项及六条同政策有限对照；只降静止重复成本，无新Lean或恢复保证 |
| TX-14 | [QPC-06D任务均衡](../cpu_refinement/qpc_06d_receiver_task_balance_results.md) | 独立根唯一领取、确定前缀消费与理想调度界 | 纸面推导及有限执行证据；几何工作不变 |
| TX-15 | [QPC-06E面证据复用](../cpu_refinement/qpc_06e_face_evidence_results.md) | FE-01～03：原子式缓存、双域闭覆盖与认证等价、实际工作计费 | 数值oracle、有限轨迹与纸面推导；完整覆盖仍O(ud)，非质量修复 |
| TX-16 | [QPC-04H源高度生成](../cpu_refinement/qpc_04h_source_height_derivation.md) | SH-01～04：合法目录、生成器与证书分离、单点可行性和筛选人口计费 | 纸面推导、43项离线检查、原生见证及有限持续验证；无新Lean，不保证恢复或降本 |
| TX-17 | [QPC-06F高度拒绝/提示](../cpu_refinement/qpc_06f_height_rejection_derivation.md) | HR-01～05：区间严格损伤、旧样本身份当前重证、接受集合与工作收支 | 纸面+数值专项+34,705次原认证重放；有限等价降本，无新Lean，非质量修复 |
| ATT-00 | [一般事务模型推导](../adaptive_triangulation/model_derivation.md)、[操作嵌入](../adaptive_triangulation/operation_embeddings.md)、[范围表](../adaptive_triangulation/generalization_results.md) | 接口、面数、资源语义、证书迁移与加权质量 | ATT-01～03完成；条件安全模型，非生产/性能证明 |

质量演进证据：[PQ-01](../cpu_refinement/pq_01_quality_provenance_results.md)、[PQ-02](../cpu_refinement/pq_02_residual_provenance_results.md)、[PQ-03](../cpu_refinement/pq_03_fit_counterfactual_results.md)、[PQ-04](../cpu_refinement/pq_04_local_recovery_results.md)、[PQ-05](../cpu_refinement/pq_05_flip_recovery_results.md)、[QPC-01](../cpu_refinement/qpc_01_quality_recovery_results.md)、[04A](../cpu_refinement/qpc_04a_boundary_feasibility_results.md)、[04B](../cpu_refinement/qpc_04b_boundary_integration_results.md)、[04C](../cpu_refinement/qpc_04c_quality_target_results.md)、[04D](../cpu_refinement/qpc_04d_error_first_results.md)。它们是事实与反例来源，不升级为 Lean 定理。

## 4. 固定层次、目标物化与其他理论支线

| 编号 | 原文/入口 | 内容 | 保留状态 |
|---|---|---|---|
| RT-01 | [固定依赖与阈值证明](../roam_threshold/model_and_proofs.md) | 闭包并集、必选集、最小闭合集、硬预算 | 抽象核心 Lean；几何纸面 |
| RT-02 | [激活谱与完整成本](../roam_threshold/algorithm_and_limits.md) | 饱和最大值、阈值搜索、全层次费用 | 集合对应 Lean；成本纸面 |
| RT-03 | [增量激活谱 Gate](../roam_threshold/incremental_gate.md) | 增减传播、阈值维护、相机变化下界 | No-Go 已关闭，非一般不可能性 |
| RP-01 | [必要依赖审计](../roam_parallelism/necessary_dependency_audit.md) | 私有准备与联合发布反例 | 递归链不等于必要 CPU 关键路径 |
| RP-02 | [参数化成本语义](../roam_parallelism/parametric_cost_semantics_gate.md) | 同任务、工作/跨度、盈亏条件 | PCSG 提前结束，无 Lean 工程 |
| RP-03 | [目标差分物化](../roam_parallelism/target_materialization_gate.md) | 叶/合并支持、邻接、队列与续接 | 核心 Lean；目标已知 |
| RP-04 | [物化原型](../roam_parallelism/target_materialization_prototype.md)、[串行成本](../roam_parallelism/target_materialization_serial_costs.md) | 构造到实现的费用边界 | 有限原型，非目标发现证明 |
| RP-05 | [Planner 契约审计](../../reviews/roam_parallelism/native_planner_contract_traceability_audit.md) | 端点定理与严格轨迹 oracle 区分 | 原生 Δ/Γ/评分缺口保留 |
| RP-06 | [候选出生证明](../roam_parallelism/native_candidate_birth_proof.md) | 出生、失败再接纳、版本覆盖、严格队首 | 纸面/有界核查，无生产新队列 |
| RP-07 | [目标发现工作削减](../roam_parallelism/native_target_discovery_reduction_derivation.md) | 方向性维护、在线 Δ/Γ、输出成本 | 未得到更快发现保证 |
| RR-01 | [最小修改量](../roam_recourse/minimum_change_note.md) | 等成本修改量公式 | 暂存，不扩展 |
| RW-01 | [工作膨胀测量契约](../roam_parallelism/work_inflation_measurement_contract.md) | 逻辑工作、编排、CPU work、墙钟分离 | 草案，非必要跨度证明 |

来源另见[阈值模型来源](../roam_threshold/sources.md)与[事务算法近邻工作](../cpu_refinement/transactional_lod_prior_art.md)。本次整理未重新检索文献或更新新颖性判断。

## 5. 全部项目 Lean 源文件

截至本页日期，共 **10 个源文件、4 个证明目录**。前7个状态引用历史记录；本轮检查新增ATT模块，原QC仅为导入重建缓存，工具链为Lean 4.8.0。

| 文件 | 核心声明/职责 | 验证依据 |
|---|---|---|
| [Closure.lean](../roam_threshold/formal/Closure.lean) | `closure_union`、`closure_least`、相对闭包等 14 项 | [阈值验证记录](../roam_threshold/verification.md) |
| [Threshold.lean](../roam_threshold/formal/Threshold.lean) | `fits_iff_required`、`target_least`、`closure_cost_submodular`、`hard_budget_iff` 等 11 项 | 同上 |
| [Spectrum.lean](../roam_threshold/formal/Spectrum.lean) | `support_maximum_spec`、`support_equals_closure`、`target_activation` | 同上 |
| [Counterexamples.lean](../roam_threshold/formal/Counterexamples.lean) | 一般闭包、非单调优先级、收益、价格化等 8 项 | 同上 |
| [StateModel.lean](../roam_parallelism/formal/materialization/StateModel.lean) | `Region/LeafSupport/MergeSupport/TargetMarks/Repair` 定义 | [物化验证 §10](../roam_parallelism/target_materialization_gate.md#101-实际机械检查) |
| [Locality.lean](../roam_parallelism/formal/materialization/Locality.lean) | 12 项引理，包括 `repair_split_queue_exact`、`repair_merge_queue_exact` | 同上 |
| [QualityContract.lean](../cpu_refinement/formal/quality_contract/QualityContract.lean) | 15项标量/有限和引理，含`pointwise_iff_excess`、`sequence_envelope`、`potential_nonincrease`、`potential_zero_iff`、`common_maximum_bound` | [本轮机械检查](../cpu_refinement/data/qpc_04e_quality_contract/lean-verification.json) |
| [StateTransactions.lean](../adaptive_triangulation/formal/StateTransactions.lean) | 具体资源更新、读取/使能稳定、`apply_commute`、`count_delta`、`delta_stable` | [ATT-02账本](../adaptive_triangulation/data/att_02.json) |
| [BatchSafety.lean](../adaptive_triangulation/formal/BatchSafety.lean) | `run_permutation`、`count_run_frozen`、`endpoint_budget`、`batch_quality`、`budget_and_quality` | [ATT-03账本](../adaptive_triangulation/data/att_03_lean.json)；ATT-02记录保留旧版本 |
| [QualityComposition.lean](../adaptive_triangulation/formal/QualityComposition.lean) | `weighted_nonincrease`、`observation_frame`、`certificate_migrates` | 同上 |

[阈值工具链](../roam_threshold/formal/lean-toolchain)、[物化工具链](../roam_parallelism/formal/materialization/lean-toolchain)、[质量工具链](../cpu_refinement/formal/quality_contract/lean-toolchain)和[ATT工具链](../adaptive_triangulation/formal/lean-toolchain)均冻结Lean 4.8.0。声明、公理依赖、复现命令及输入指纹读相应验证记录；本轮通过Ubuntu编排调用Windows Lean。

QC与ATT质量Lean使用显式有序代数法律，未机械建立一般Rat/Real实例或生产几何对应；ATT的有限数值实例另用有理数检查。旧Lean的`score`是固定函数，`TargetMarks`是净端点定义；各类模型互不替代，不能据此省略生产失败生命周期、动态依赖或目标发现成本。

## 6. 文件职责与维护规则

~~~text
docs/research/
├── derivations/
│   ├── README.md                    推导/反例/Lean 导航与状态
│   └── mathematical_derivations.md  统一符号的数学推导总览
├── cpu_refinement/
│   ├── quality_contract_derivation.md  QC-01～09 详细过程
│   └── formal/quality_contract/        新质量抽象模型、1个Lean源
├── adaptive_triangulation/
│   ├── README.md                    一般模型的范围、来源与规划
│   ├── model_derivation.md          ATT推导过程、反例与条件总定理
│   ├── generalization_results.md    最终边界/生产对应/成本
│   └── formal/                      资源、质量、预算3个Lean源
├── roam_threshold/formal/           原 4 个 Lean 源文件
└── roam_parallelism/formal/materialization/  原 2 个 Lean 源文件
~~~

1. 详细原文为事实源；总览压缩讲解，不反向改写历史假设或实验结果。
2. 新小阶段修改算法契约时，交付包含：符号/前提→命题→推导过程→反例→源码义务→成本→验证状态。
3. 用稳定编号对应规划、推导、检查与结果；Lean 另记文件和声明名，不把纸面证明统称为内核验证。
4. 每轮保留失败思路、前提变更，再更新索引和总览；公式标明适用版本及未证明项。
5. 总目录维护只核对链接、公式定界、源清单与映射；各阶段实际验证单独记录，不因索引更新重跑历史矩阵。
