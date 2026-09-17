# QPC-06B：同代次空批复用代码事实

2026-09-17。范围：Pipeline持续入口、IdlePlanCache及直接State/Samples/Commit/Reservation依赖。

## 文件、所有权与调用

FACT：`src/algorithms/greedy_transactional_lod/TransactionalIdlePlanCache.h/.cpp`持有一个optional CertifiedBatch、非拥有State实例指针、诊断模式及目录尝试量。它只允许空Exchanges且Version等于当前State的记录；没有提案/证书/几何缓存，也没有按根查找。

FACT：Pipeline独占该缓存；Update先Initialize/CheckLimit，查实例＋Version＋Diagnostics。命中复制完整逻辑空批并返回，包含IntentBudgets、IntentIds、PoolIds、IntentResults、Attempts及资源分母；不生成假SampleTouches/PairChecks。未命中仍调用原Reservation::Plan和Apply，正常返回后Remember。

FACT：SetView真正改变矩阵、分辨率或深度约定时，在全部准备完成、状态修改前Clear；Apply非空批同样在准备完成、发布前Clear。Clear的Reasons插入可能分配，放在发布前避免状态变化后引入失败点。真正发布推进Version，故键核对仍是第二道保护。相同相机返回不失效。

FACT：源高度、Q、几何/预算/质量/前缀规则属于此Pipeline固定输入；Samples与State在视图/拓扑阶段共同更新。公开State/Samples getter只返回const，非正常const_cast不在契约内。平台Reset销毁整个Pipeline；跨实例相同Version不命中。线程只在冷Plan内派发，缓存本身由单个控制入口访问。

## 异常与观测

FACT：未完成Plan不会调用Remember；命中前和拷贝后也调用当前CheckLimit。副本和记录账本成功后才替换缓存。过期/篡改非空批的原Apply检查未删除。

`idle_plan_hit/stored/invalidated/reused_attempts/payload_bytes`保存在核心WorkLedger.Reasons；最后一项是记录时容量估计，短字符串会重复计量，不是分配器实测峰值。当前公共平台CSV没有新增这些字段；报告用实际零访问空机会和完整输出对照，不把它们冒称为直接导出的命中计数。

`TransactionalRenderBridge::Account`仍从batch读取逻辑人口/结果，从work读取实际访问/配对/认证费用。因此缓存复用时examined等表示被复用的逻辑求解结果，不是当帧实际重新认证根数。CSV的exchanges只统计配对交换；翻边与边界预算另外核对各自CSV。空机会判断还检查网格写入数，避免漏掉净零翻边。

## 复杂度、验证及限制

INFERENCE：键O(1)，空批复制/释放O(r+m+a)，容量O(r+m+a)，仅一份，不随帧数增长。任何拓扑或视图改变都清空，移动首次求解成本没有降低。

专项将冷Reservation::Plan与热Update全部逻辑字段逐项对照，检查实例/诊断绑定、当前期限、访问配额中断后重试、视图显露后的成功、外部Apply重用面槽和Reset。六条后测正常轨迹各与对应原政策比较曲面、相机、预算、逻辑结果、上传字节、实际写入，以及翻边/边界预算CSV；不运行全量CTest。

UNCERTAIN：没有按局部影响跨代复用能力，没有新的持续质量/恢复保证。有限工程数据不能证明长期频率分布或全部机器上的速度优势；结果见[报告](../../research/cpu_refinement/qpc_06b_idle_plan_reuse_results.md)。
