# QPC-06F 高度拒绝与失败样本提示：代码事实

2026-09-18。范围仅为06F新增拒绝与提示模块及其直接调用链；源高生成器见[04H事实](qpc_04h_source_height_facts.md)。对应[规划](../../plans/cpu_refinement/qpc_06f_height_rejection_plan.md)、[结果](../../research/cpu_refinement/qpc_06f_height_rejection_results.md)。以下未另标记的实现描述均为FACT。

## 1. 文件、类型和职责

源码目录统一为 `src/algorithms/greedy_transactional_lod/`，namespace为`ParallelRoam::Algorithms::GreedyTransactionalLod`。

| 文件/类型 | 当前职责 | 状态与调用者 |
|---|---|---|
| `TransactionalHeightRejection.h/.cpp` / `HeightRejectionContext` | 单次认证输入模式、HintSample、Audit；输出FailureSample、IntervalFailure、OriginalReason | 栈上上下文，由Reservation创建，PointwiseQuality填写 |
| 同文件 / `TransactionalHeightRejection` | `ProvesDamage`保守区间判定；`Recheck`当前提示样本闭覆盖、可见人口及高度重证 | 无持久状态，复用QualitySampleEvidence/QualityFaceEvidence |
| `TransactionalRejectionHints.h/.cpp` / `RejectionHint` | Root、Kind、Ordinal、Sample四项记录 | 不保存高度、投影、失败标志或成功证书 |
| 同文件 / `TransactionalRejectionHints` | `Find`只读索引；`Remember`按输入顺序确定性FIFO归并；`Size`观测 | Pipeline独占，跨帧，只有屏障后的主线程写 |
| `TransactionalExecution.h` | 内部Reference/HeightFirst/SampleHint消融、诊断审计开关和失败回调 | 默认SampleHint；不新增GUI/JSON质量政策 |
| `TransactionalPointwiseQuality.*` | 可选拒绝上下文；完整认证与成功证书仍归此层 | 没有上下文的旧调用仍为原认证oracle |
| `TransactionalReservation.*` | 查提示、每根独占失败输出、按原根/目录序收集 | 不拥有缓存，不实现数值高度判据 |
| `TransactionalPipeline.*` | 初始化所有权、Plan后的归并、原Apply/续接 | 不将缓存加入生产mesh/state哈希 |
| `src/benchmark/experiment/TransactionalRecoveryTrace.cpp` | 手动Plan/Apply诊断中的独立提示表、原认证影子重放、CSV | 不属于正常计时路径 |
| `tests/TransactionalHeightRejectionTests.cpp` | 有理oracle、提示失效、FIFO、配额、1/8线程有限续接 | 专项测试；不穷举真实几何输入 |
| `scripts/analyze_transactional_height_rejection.py` | 只读归约普通计时、轨迹、工作、影子结果、既有perf/Tracy | 没有运行时依赖 |

依赖为`Pipeline → Hints / Reservation → PointwiseQuality → HeightRejection → Quality*Evidence`。提示表仅依赖Types和profiling；没有下层反向依赖实验代码。

## 2. 缓存身份、所有权与生命周期

`Key=tuple<Identity,char,size_t>`为根稳定身份、目录类型和ordinal；有序map逐字段比较完整键。`_positions`把键映射到固定`_entries[2048]`槽，`_next`指FIFO最旧槽。已有键更新样本但不更新年龄。满表时先插入新索引，再删除旧索引和覆盖记录；持久条目上限2048，替换期间map短暂2049项。空间O(C)，不是Q或完整候选人口的缓存。

`Pipeline::Initialize`仅在SourceHeight且SampleHint时分配表；默认LegacyFit不分配。普通SetView及局部拓扑变化不清表，每次使用仍重证。新的Pipeline/Reset实例重新分配，SetView禁止重绑源、预算和质量政策。析构释放unique_ptr和map。无跨实例共享、无后台清理、无并发写。

`Reservation::Plan`仅借用const表；根任务只写自己索引下的失败vector。RunIndependent同步返回后，主线程按根、目录序填`learned`并执行诊断回调。Pipeline在Plan完整返回后、Apply之前调用Remember；不存在发布区内新分配。

**异常边界：**Remember和后续Apply准备仍可能分配失败。生产拓扑/样本/mesh不会因此半发布；已经完成归并的提示可能保留，而不是保证回滚整个提示表。提示是无语义权威的检查顺序，下一次使用仍核对当前支持并重证，所以保留旧/新提示不会认证错误事务。这是规划初稿“异常丢弃新提示”的精确化，不是新增业务状态回滚能力。

## 3. 认证控制流

`Certify`清空此前QualityProof，然后按原顺序准备核心binary64和公开float两个域。`Faces`、`SameInterface`、完整支持收集、排序去重及面证据准备均在捷径前。只有SourceHeight的接收E/F/H核心域启用上下文；B、R、donor、float域及LegacyFit不启用。

提示路径：binary_search当前支持 → 当前QualitySampleEvidence → 原稳定面序Cover(old/new) → VisibilityAgrees → 当前参考/旧新高度界 → `after.Low > max(before.High,cap.High)`。非有限、无覆盖、域外、可见人口不一致或界相交都不能提前拒绝。提示未拒绝时，从原第一个样本完整重跑，不将提示检查当成功证书的一部分。

S1路径位于原CheckSample内：保留人口检查和高度求值，将确定高度损伤提前到两个ScreenPrepared调用之前；不能拒绝则沿原屏幕、损伤、精确回退和进展顺序。成功项仍完成两域所有样本及原正进展判断后才设置QualityProof。

核心高度失败记录FailureSample；区间证明时IntervalFailure=true，精确回退发现的高度失败也可以训练身份，但未来只允许当前区间严格证明早拒绝。首个失败理由可能改变，但形状类别不改变；当前NeedsFlipRecovery要求E/F/H全部shape_infeasible，两种质量失败都不满足此条件。

## 4. 诊断、计费与错误

`HeightRejectionContext::Audit`使每次实际提示拒绝复制该提案并以空上下文调用原Certify。原路径接受立即抛错；原理由写入OriginalReason及`height_hint_shadow_*`。独立audit账本不并入生产工作计数，审计耗时明确为`height_hint_shadow`。这项不在普通runner/perf/Tracy打开。

Recheck每次新增样本访问执行`ledger.Touch()`；所有提示miss后的重复取证计入实际工作。理由计数包括queries/hits、hint_checks/outside/rejected、early_height_rejects、full_domain_loops、screen_evaluations、remember/evictions/entries。entries为当前容量观测，不能跨帧求和当作空间。Time包括query/check/merge；任务内时间经Execution标为task_wall_sum，不能直接当完整CPU时间。

RecoveryTrace自身按历史诊断方式手动Plan/Apply，所以另持有一个提示表；其Pipeline中的表不会被Update消费。诊断会多持有一个表，该成本不冒充生产内存。普通生产只有一个表。影子重放遵守原访问限制及相同deadline，触限不算完成的等价实验。

## 5. 算法和复杂度

固定容量C下查/写O(log C)。提示成员查询O(log s)，闭覆盖O(d)，数值位长另计。提案仍有支持整理O(s log s)、面准备O(d)和原最坏O(sd)数值遍历。命中只缩短昂贵遍历；不减少入口数，也不消灭支持收集/排序、源高准备、View或Samples::Prepare。证明和收支公式见[HR-01～05](../../research/cpu_refinement/qpc_06f_height_rejection_derivation.md)。

## 6. 验证及变更风险

新增专项：17×17有理损伤比较、等号/重叠/非有限；原/S1/S2结果；域外及所有合法样本的过期提示；失败不得有证书；访问限制安全退出；FIFO容量/年龄/键；有限视图序列1/8线程结果和收集顺序。复用逐点与源高专项覆盖既有双域证书规则，持续Pipeline专项覆盖发布/续接边界。

自然同政策S0/S2两场景各96帧逻辑列及公开网格哈希一致；Sierra诊断所有34,705次实际提示拒绝被原认证确认。有限证据不等于全状态形式化；本轮没有新增Lean。错误更改完整键、删除当前支持检查、使用旧面系数、对提示通过直接签发证书、在worker修改表，都会破坏本轮边界。

## 7. 执行路径与符号索引

正常：`Pipeline::Initialize → Update → Reservation::Plan → Hints::Find → PointwiseQuality::Certify → HeightRejection::Recheck/ProvesDamage → 原完整认证或失败 → 根序归并 → Hints::Remember → Pipeline::Apply`。

回退：域外/不确定提示 → 原样本起点 → 完整核心与float证书；空批缓存命中沿原返回，不重训提示。

诊断：`TransactionalRecoveryTrace → Plan(ObserveHeightFailure,AuditHeightRejections) → Certify(带提示) → Certify(无上下文影子) → height-failures.csv/pointwise-work.csv`。

核心状态索引：`HeightRejectionContext`（单次认证），`RejectionHint`（身份记录），`TransactionalRejectionHints::_positions/_entries/_next`（唯一跨帧新状态），`TransactionalPipeline::_heightHints`（所有者），`HeightRejectionMode`（内部消融）。未引入配置schema迁移。

## Unresolved / Uncertain

尚未证明任意输入都能命中或提速；真实提示效率依赖跨帧失败位置的复现。完整支持整理、成功项认证和样本续接仍可能主导。内存上界来自数据结构容量，不含allocator实现常数；没有测全进程RSS归因。持续质量、目录完备和DOD竞争力不属于本模块已证结论。
