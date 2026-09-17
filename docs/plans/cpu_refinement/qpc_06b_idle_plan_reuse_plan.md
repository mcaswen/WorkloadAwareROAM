# QPC-06B：不变状态空批结果复用小规划

2026-09-17。基线`89773d6`。用户授权整个06自主闭环、分阶段提交。本阶段承接[06A结果](../../research/cpu_refinement/qpc_06a_quality_cost_reduction_results.md)，主要政策L=`legacy+error-first`，P=`pointwise-target+error-first`仅受限对照；所有输入显式immutable，默认设置不改。不是新质量算法。

## 1. 问题与范围

06A同环境L轨迹确认：Sierra/Canyon末端没有事务、相机/公开网格不变，仍重复完整目录，约2ms/5～6ms。P的同类重复更多，但06A没有让P整体成本/持续恢复通过。因此选一个对两政策都成立的最保守复用边界，不追加P专属拟合救援。

目标：同一Pipeline实例、相同State Version下，完整Plan已返回空Exchanges时，下一次Update直接复制其有限逻辑结果，省掉重复发现/Fit/质量/预留。保留原IntentIds/Results/Attempts、预算分母和停止结果。实际工作账本不伪造重新执行过的Touch/PairCheck；显式记录命中。

非目标：缓存非空批次、跨视图/跨拓扑局部失败复用、增量依赖图、按根revision、近似相机相等、改前缀/目录/质量目标、恢复限额超时、预留索引、并行规划、05。质量停滞不因省去重复计算而成为已恢复。

## 2. 代码事实与选择理由

已核查Pipeline、State/Commit、Samples、Reservation、Execution、平台适配和既有测试。State只有初始化、SetView与Commit发布可写；Samples输入源持有不变，派生项同步于状态。真正视图变化在数值准备成功后推进Version，拓扑发布同样推进Version。几何/预算/政策/前缀等Config不能在SetView改变；完全相同矩阵/分辨率/深度约定返回不改Version。

Plan在冻结输入上确定；真实新身份、候选键、支持、质量和资源均由该状态/Samples生成。空批没有Apply变化，不会生成新候选或释放资源。因此无需证明“每根局部footprint足够”，用整个状态代次作失效边界即可排除上一轮审计的依赖遗漏。观察计时/线程分配不属于逻辑Plan输出，Diagnostics属于构造时固定执行配置，缓存仍显式绑定它。

选择完整空批结果而非单个失败提案：所需证明更小，不持有Proposal/证书/几何，无成功后资源变化风险；只维护一项缓存。代价是任何成功事务或相机变化都放弃复用，不能减少移动段首次认证费用。

## 3. 文件与职责

| 选择 | 文件 | 责任 |
|---|---|---|
| Create | `src/algorithms/greedy_transactional_lod/TransactionalIdlePlanCache.h/.cpp` | 持有至多一个完成的空CertifiedBatch，绑定State实例/Version/诊断模式；只查找/记录/清除，不调用Plan，不修局部状态 |
| Extend | `TransactionalPipeline.h/.cpp` | 拥有缓存；Update在完成Plan/Apply后记录，真正视图或拓扑发布前清除；完整费用保留在update中 |
| Reuse | `TransactionalReservation`及所有数值/质量/事务组件 | 原完整求解器作为冷路径和独立对照；没有缓存侵入其控制器 |
| Extend | `tests/TransactionalPointwiseQualityTests.cpp` | 复用解析输入，新增旧/新政策空批冷/热一致、视图失效、外部Apply/槽位复用、Reset及超限不缓存；验证比较完整逻辑空批而非只看面数 |
| Extend | `scripts/run_transactional_quality_cost_reduction.py`、`scripts/analyze_transactional_quality_cost.py` | 显式政策/允许实际工作减少的版本对照；归档命中、移动无命中、静止成本和内存；保留06A默认比较行为 |
| Create/Extend | 06B推导/事实/结果/审查、本规划、大规划、推导索引 | 定义真实缓存量词、失效证明、复杂度与负边界 |

依赖：Pipeline→IdlePlanCache→State/Types。没有全局单例、算法层不依赖平台/实验；不加用户开关或共享跨线程缓存。当前Pipeline控制入口单线程，内部并行工作只发生在未命中Plan内。

## 4. 推导与实施步骤

先落纸IDLE-01～04：固定输入确定性、空结果的归纳复用、代次失效完整性、工作/空间界。关键反例是：只以mesh哈希或相机位置作键不能识别Config、候选、相机矩阵/分辨率或稳定身份变化；禁止该方案。

1. 以06A的P1程序为本阶段基线；同环境L0三场景记录直接复用，P1三场景作受限对照。输入从原04F A/B文件取，禁止热切换演化状态。
2. 写缓存组件，只有完整Plan且Exchanges为空才记录；异常、配额终止不记录。命中前/复制后仍调用当前CheckLimit；原逻辑访问不伪装为实际重做。
3. Update、SetView、Apply接入；成功发布时清除，失败前未变状态不必清除但不得记半成品。新Pipeline/Reset自然为空；其他实例和被移动对象不共享旧Owner。
4. 专项夹具比对原Plan与缓存返回全部逻辑字段/目录；覆盖同相机命中、相机变更空→非空、外部事务、失效、无命中/超限/Reset。公开几何不变不能替代这些检查。
5. 有限同轨迹后测：L三场景、P三场景；Peking旧策略无命中是固定税对照，Sierra/Canyon静止是命中输入，移动和返回单列。仅明确退化时至多一次复测；结果合格则提交，整个06收口，不自动05。

## 5. 验证、成本与停止

针对性Windows D3D12核心专项，不跑全部CTest。正常轨迹仍24/96/96、50k、160前缀、8线程、前三暖机，输入、种子与资产不变。保存逐帧实际曲面/面数、raw/examined/receivers/need/feasible/exchanges/free及相机身份；工作账本允许命中时真实下降，命中不算新增事务成功。完整Intent/Attempts等由解析独立Plan比较覆盖。

测量完整CPU、移动、返回、静止、接收及其余阶段；缓存存储/复制/清除实际费用含在update，记录命中/记入/失效与有限负载字节。预算最多六后测进程，各180s/8GiB，预计5～10分钟内含归约；需要复测仅定向一组，不新开规模/线程矩阵。基线来自同一会话的06A末端，编译/诊断不并行采样。

目标：静止重复空批认证工作确实消失，Sierra或Canyon末端完整成本有明显下降（期望≥50%）；移动/无命中成本不出现超过`max(0.05ms,5%基线,已有噪声)`的持续退化。未达则撤回一个机制，不扩按根缓存。静止收益不冒充移动提速或持续质量恢复；没有命中的原帧保留。

## 6. 实施结果与核查

已完成，见[结果](../../research/cpu_refinement/qpc_06b_idle_plan_reuse_results.md)、[代码事实](../../codebase/cpu_refinement/qpc_06b_idle_plan_facts.md)和[架构审查](../../reviews/cpu_refinement/qpc_06b_idle_plan_reuse_review.md)。06A政策检查点之后，L作为主要成本政策、P作为受限对照；默认配置不改，不用P的逐点保证为L签收。

保留唯一的同实例同代次空结果缓存，没有增加按根revision、跨视图复用或一般缓存系统。真正发布前清除，以免清理账本的分配失败发生在状态改变后。原Plan保持冷路径；成功事务、视图变化、Reset、同版本其他实例、诊断模式不同均不能误命中。超时或访问配额中断的半成品不记录，热命中仍检查当前期限。

专项核对完整逻辑空批、失效、槽位复用、失败转成功和中断重试；受影响目标构建及专项完成，未跑全量CTest。六个正常后测进程与对应基线保持曲面、逻辑选择、预算、翻边/边界结果和输出写入一致。L的Sierra/Canyon末端区间（含首个冷刷新）CPU均值分别从2.843降到0.566ms、6.134降到0.814ms；各32机会的样本访问总量分别从4,480降到140、33,920降到1,060。超过冻结的50%静止降本目标，移动/无命中未出现超过工程门槛的持续退化。

实际观测边界：核心账本已有缓存计数，但公共平台CSV未扩展其字段。自然报告明确区分真实零访问空机会与直接缓存命中，不把前者冒称后者；容量仅为记录时估计，内存边界另用原生进程峰值验证。有限逻辑结果复制与销毁仍完整计费，热路径不是O(1)返回。

本轮06闭环。静止失败成本降低不代表持续质量恢复，也不降低移动首次认证的主要费用；未启动05或修改Fit/质量规则。
