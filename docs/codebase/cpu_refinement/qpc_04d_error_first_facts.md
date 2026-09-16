# QPC-04D 接收排序代码事实

日期：2026-09-16。范围：公共政策输入、Samples接收键、局部续接和实验观察。对应[规划](../../plans/cpu_refinement/qpc_04d_error_first_ordering_plan.md)。本文描述实现，实验效果另见[结果](../../research/cpu_refinement/qpc_04d_error_first_results.md)。

## 1. 设置与依赖

**FACT**：`src/algorithms/TransactionalLodSettings.h`唯一声明`TransactionalReceiverOrder { Composite, ErrorFirst }`。公共设置与核心`Configuration`均默认Composite；核心Types只依赖无行为的设置头，不依赖实验模块或渲染器。

`ExperimentCase::LoadResolved`读取可选`receiverOrder`，只接受`composite/error-first`；非Transactional不得启用后者。`ExperimentReplay`映射公共枚举，`TransactionalSeedBuilder::ConfigurationFor`传给核心。公共ValidateInput和核心State构造均拒绝非法枚举。

**FACT**：`TransactionalTerrainLodAlgorithm`原SeedKey包含完整`TransactionalLodSettings`；切换策略沿原Reset/重建流程生效。本轮未修改该控制器。`TransactionalPipeline::SetView`拒绝半途改政策，只允许换视图。`TransactionalDynamicReference`显式拒绝ErrorFirst，没有扩写动态串行参考的语义。

**FACT**：Python catalog与schema校验同一两值政策。runner仅将非默认值加入taskId，默认缺省/显式Composite保持旧taskId；workloadId不含排序。suite复制Transactional的显式选择，为Classic/DOD/CBT恢复Composite。

## 2. 候选资格与排序的分离

**FACT**：`TransactionalSamples::ReceiverKey`是私有纯函数，输入Config、P²、E²、稳定身份和槽位。先判断有限P²严格超过SplitPixels²，然后返回：

```text
Composite: (-P², stableFaceId, slot)
ErrorFirst: (-E², stableFaceId, slot)
```

无额外P次级键、开方或量化。E²来自原闭面贡献归约，可见集合为空时为0。`_priority`始终保存复合P²；`PrioritySquared`含义未改。捐赠方继续用相邻面最大P²与中心身份排序。

**FACT**：初始化Refresh、换视图PrepareView、局部Samples::Prepare都在原有maximum归约完成处构造唯一接收键，没有为Prefix重新扫描Q，没有持久E数组或第二份全序。

1. Refresh生成临时optional槽记录，再交BuildOrders建立原块索引。
2. PrepareView的面任务写唯一槽；投影、评分和索引完整准备后沿原PublishView移动发布。BuildOrders不再重算接收资格，仅接收已生成记录并计算原捐赠记录。
3. 局部Prepare对Removed写null，再对新面及保留接口面归约，使用目标稳定身份覆盖槽记录。即使P保持不变，也重新生成E键；原InvalidatedRoots/Donors继续沿旧完整支持集修复。
4. Publish继续消费原ReceiverIndex::Repair；全部分配仍在prepare，发布不增加失败点。放弃私有prepare不改变live索引。

**FACT**：`TransactionalPriorityIndex`、Reservation、Commit、Mesh和局部几何目录均未修改。256槽块的有序记录、局部脏块修复、块间前缀合并仍是原实现。

**INFERENCE**：相同已发布状态、视图和其他设置下，候选人口、P数组及捐赠池相同；只改变接收顺序。持续执行后两政策可以生成不同状态，随后人口与捐赠池不再要求相同。这是不同贪心政策，不能视为同任务性能优化。

## 3. 诊断与证据归属

**FACT**：`TransactionalQualityProvenance::Rank`在Composite下保持原计算，在ErrorFirst下只读实际Raw全序求名次。诊断的priority字段继续是P，不偷换成纯误差。全根CSV仍包含独立重算的E²、密度项、P²和inPrefix。

`TransactionalRecoveryTrace`新增`--priority-frames I,J`，限制最多两个有效且不重复的观察机会；旧`--priority-audit`仍观察见证来源帧。`policy.json`声明实际排序和priority字段语义。观察发生在SetView之后、Plan之前，费用独立于生产账本。

**FACT**：`run_transactional_receiver_ordering.py`仅冻结三输入并调用既有平台runner、追溯及独立quality工具；`analyze_transactional_receiver_ordering.py`从全根CSV独立排序并核对真实前缀，汇总质量分布、工作、阶段成本和实际截图。没有把生产评分/认证算法复制进脚本。

**FACT**：C++专项测试从闭面样本独立生成预期全序，检查资格、捐赠池、P不变而E变化、阈值/空支持/非有限P、接口/槽复用、Pending、失败与过期批次、同政策线程一致。原适配测试补充种子不变和设置切换重建；输入测试补充默认/非法值与算法隔离。

## 4. 成本与尚未解决项

**INFERENCE**：额外键选择是O(1)/被评分面；准备期O(S)槽记录替代原BuildOrders同形记录。持久索引规模未变，未增加O(Q)遍历。局部修复仍由实际样本贡献与触及块数决定，不因本轮变为常数。

**FACT**：接收键生成从view_order移到原view_scores内部；完整view_refresh仍包含二者。不能将某一子计时的减少单独解释为算法提速。

**UNCERTAIN**：ErrorFirst对持续质量、饥饿与完整CPU成本没有一般保证。现有运行时/独立采样不一致、低于触发值不发请求、目录形状拒绝、局部最大值接受允许误差转移，以及不可回收边界的生命周期限制均未修复。相应证据必须按独立实验结果表述。

## 5. 原因分析补充：接受、恢复与成本字段

**FACT**：`TransactionalProposals::Donor`把`TransactionalSamples::VisibleSupport`写入提案的`Samples`；该集合是闭补丁支持经当前参考可见性过滤后的去重集合。`TransactionalCertification::Measure`先把`ErrorLower/ErrorUpper`置0，空集合返回成功。`Accepts`先检查目标非负，再以区间上下界比较；误差上界0可直接通过正目标。donor没有receiver的`SetProgressTarget`空样本拒绝。因此“闭补丁有参考样本”并不意味着donor必须有当前可见认证证据。

**FACT**：固定存活点高度不禁止删除内部中心。中心删去后，新连接的插值曲面可以在原中心位置改变高度；这个变化不需要修改任何保留顶点，也不需要执行新点拟合。后续是否请求恢复仍由`ReceiverKey`的复合P资格判断决定，ErrorFirst只改变资格集合内部的顺序。

**FACT**：`TransactionalReservation::Plan`在`Need>0`时对整个共同池派发donor认证；之后顺序处理receiver。每个配对依次检查认证状态、接受目标、内部冲突、中心复用及已预留资源；`Unite`构造联合有序集合，`blocked`逐项比较既有预留事务。`PairChecks`在分支之前增加，因而不是等成本操作单位。`Samples::Prepare`在`Pipeline::Apply`中串行准备局部样本、接口评分和索引修复；它不只修改新面。

**FACT**：`TransactionalRenderBridge::Account`把`SampleTouches`导出为`touches`，把`SampleEvaluations`导出为`evaluations`，把`MeshVertices/MeshIndices`导出为`vertexWrites/indexWrites`。当前`Touch()`调用归属认证模块；它不是样本续接数量。`RepairSamples`没有通过这一公共CSV导出，`indexWrites`也不是候选索引维护次数。

**INFERENCE**：当前`Project`只在全Q换视图与局部`Evaluate`中计入`SampleEvaluations`，暖窗口没有初始化。因此在已核对的本次轨迹上，`Σevaluations−Σ换视图机会q`可恢复局部重评价次数。这个量不等于唯一写入样本数，也不包含全部包围框定位或闭面贡献扫描。

**FACT**：分析脚本新增`explain`模式只读取既有暖帧、源码身份和独立追溯记录；生产模块没有反向依赖。Canyon追加记录匹配原B的96机会输出哈希及公共工作，直接显示frame24删除5928、空可见donor证据及后续P未达4的链路。具体数值见[结果§5/7](../../research/cpu_refinement/qpc_04d_error_first_results.md)和[归约数据](../../research/cpu_refinement/data/qpc_04d_error_first/cause-analysis.json)，不把这一单点来源扩张成全部误差分布的原因。
