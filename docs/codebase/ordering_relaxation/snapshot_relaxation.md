# 快照排序松弛执行事实（已归档）

> 更新：2026-09-12
> 范围：已关闭的 GATE-03 单线程实验历史源码；不是当前活动入口或正式多核算法
> 依据：[小规划](../../plans/ordering_relaxation/gate_03_snapshot_relaxation_plan.md)、[原操作观察事实](topology_operation_observation.md)

用户决定关闭本轮，六份实现/配套文件保存在[过时目录](../../../obsolete/ordering_relaxation/gate_03/README.md)。当前共享文件恢复到 `a0f3551`，不再提供指定根、快照选批或 `--relax-natural`；GATE-01 观察/严格步进与 GATE-02 质量能力保留。以下各节的 FACT 均限定为归档版本，不表示当前生产或测试能力。

## 1. 代码归属与调用关系

FACT：归档代码位于 [OrderingExperiment.h](../../../obsolete/ordering_relaxation/gate_03/src/algorithms/data_oriented_roam/ordering_relaxation/OrderingExperiment.h) 和 [OrderingExperiment.cpp](../../../obsolete/ordering_relaxation/gate_03/src/algorithms/data_oriented_roam/ordering_relaxation/OrderingExperiment.cpp)，命名空间为 `ParallelRoam::Algorithms::DataOrientedRoam::OrderingRelaxation`。归档前只有 `OrderingGate` 测试目标编译该实现；当前无构建目标引用它。

历史调用链为 `tests/OrderingGateTests.cpp → RunOrderingExperiment → BuildBatch/ApplyBatch → TopologyExperiment`。只有归档的 [DataOrientedRoamTopology.cpp](../../../obsolete/ordering_relaxation/gate_03/src/algorithms/data_oriented_roam/DataOrientedRoamTopology.cpp) 调用私有 `SplitNodeImpl` 和 serial policy。评分、队列、状态 accessor、渲染、原 formal 配对协议未改；没有增加 runner 项目或 selector 注册体系。

## 2. 控制入口

FACT：归档的 [TopologyExperiment](../../../obsolete/ordering_relaxation/gate_03/src/algorithms/data_oriented_roam/DataOrientedRoamTopologyExperiment.h) 曾增加以下入口，当前活动头文件已撤去：

| 接口 | 实际行为 |
| --- | --- |
| `InspectSplitIteration` | 只读预览停止、优先合并或严格 split 头，不消费迭代；共用内部 `InspectSplitControl` |
| `AdvancePlannedSplitIteration` | 以逻辑 rootPath 指定根；零表示严格头。返回步骤，并在 `TopologyConvergenceObservation` 中保存本循环体真实工作 |
| `RunStrictSplitStep<Observe, Planned>` | 生产、原严格入口和指定根入口共用循环；指定根只替换已获控制器允许的 split 选择 |

原迭代后增、merge 优先、预算交换、失败删除/屏蔽和部分成功语义保留。计划根仍必须在当前 split 队列中且合格；根失败后的强制修改与预算合并不会回滚。生产默认模板不收集观察、不构造计划或投影。

`MergeQueueScore` 会把本帧刚 split 的菱形评分设为 `float::max`，禁止立即合并还原。实现验证因此使用已有可合并候选检查优先合并，不能假设任意新 split 都能制造下一步优先 merge。

## 3. 计划构造与应用

FACT：`OrderingConfiguration` 只有 Strict、StrictPrefix 和 PriorityBand 三个模式。正松弛参数在 `(0,1]`，其余模式要求 Alpha 为零。固定最多检查 64 根、接收 32 根；double 包含等号比较作用于已有 float 队列评分，不重算生产优先级。

`BuildBatch` 从同一 `S_j` 逐候选复制状态，通过指定根单步执行与 `AnalyzeSplitOperation` 相同的观察 policy。这样可在实验文件保存读资源前态及写资源后态，而无需让旧试算接口返回完整副本。每个副本使用同一迭代位置，后一个候选不读取前一候选的结果。

必选严格头；头无法成功时整批交回严格单步。StrictPrefix 在严格队列中遇到首个不合格/冲突项便停止，正松弛可在带内跳过。计划保存源哈希、位置、根描述、逻辑前后态、资源和原因；`ApplyBatch` 通过 const 计划消费，不能追加成员。

依赖按闭包交叠、写写、读写判断，原因优先级跨全部已选成员统一裁决。联合 `PeakBudgetUse` 不得超过开始预算，新节点总量不能超过下标空间；新资源以创建路径区分，不用试算物理下标制造冲突。物理分配仍顺序执行，尚无并发分配或延迟协调维护。

实际应用逐根核对前态读资源、队列评分、闭包/尝试顺序、写资源效果、相对预算预留轨迹及净消耗。应用遇到硬控制边界即撤销剩余成员，整段已应用前缀记 `control_interrupted`。正松弛允许新高分 split 等待下批，StrictPrefix 必须继续匹配当前严格头。完整成功且原正确性检查成立才记 `validated_batch`。

## 4. 协调维护与独立性边界

以下为本次静态调用链核查及定向验证共同支持的边界。局部足迹不包含全部协调器读写，因此独立性表述仅针对局部拓扑内核。

| 共享维护 | 当前读取/作用 | 本阶段处理 |
| --- | --- | --- |
| split 队列、屏蔽、评分 | 资格依赖活动标记、深度、build 标记和上一帧路径；堆槽影响存储，不定义优先级 | 保持立即串行维护；应用重查根资格/分数，控制器重查真实堆顶 |
| merge 邻域及反向关联 | 从邻接、父子和活动状态确定可合并菱形，影响后续优先 merge/预算交换 | 不忽略控制影响；逐根检查，排列比较逻辑候选/代表/伙伴 |
| 活动索引及尾项填洞 | 位置可改变无关节点反向表，但不改该节点的逻辑活动身份 | 串行执行，比较活动集合和成员有效性，不要求数组排列相同 |
| 当前/上一帧 split 集合 | 本根更新当前集合，迟滞资格只读取冻结的上一帧集合 | 集合由协调器写，投影保留两套集合；不混用为动态前提 |
| mesh 修改日志 | 追加父节点操作，拓扑递归不读取日志内容 | 保留原顺序执行并验证 replay；排列比较逻辑修改及最终规范化几何 |
| 新节点与统计 | SoA 追加分配、初始化、计数写入 | 实际仍串行；分配失败标资源限制，统计单列，不计作已证明可并行维护 |

INFERENCE：在已覆盖的局部资源、固定只读来源和显式控制检查下，选中的集合存在独立内核工作。它不证明 `SplitNodeImpl + 所有共享维护` 可直接放入线程池，也不证明先并行内核、后延迟维护等价；这些仍属于未来算法设计问题。

## 5. 逻辑投影、审计与成本

FACT：`ProjectNodes/NodeWords` 按 PathId 编码域、父子/邻接、深度、评分、方差、chunk 及创建/激活/split/merge 历史，包括非活动历史节点。新节点身份不依赖下标，数值位模式精确比较。

`LogicalState` 另比较活动集合、两套路径集合、屏蔽和队列候选、代表/伙伴、预算与后继控制。mesh 元数据按逻辑所有者比较槽位、脏标记、范围和修改，原 replay 验证有效顺序；独立副本上的 full emit 提供规范化几何证据。计时与物理布局不属于逻辑等价，粗工作量差异仍保留。

`AuditBatch` 对最多四根枚举全部排列，较大集合固定原序、逆序、左循环和交换首两根。`RunOrderingExperiment` 只保存首个实际成功多成员批次的审计副本；自然驱动在一个输入首次完成审计后不审计后续配置。通过只覆盖选中集合，不暗示每批穷举。

每个变体默认 180 秒，试算逐根检查 deadline。超时仍完成已知队列的轻量分母统计，但不再试算；`resource_limit_not_inspected` 与依赖拒绝分开。审计超时属于 `audit_incomplete/resource_limited`，不能当作依赖反例。真正排列差异才使用 `dependency_model_mismatch` 并暂停正向资格。

工作计数复用 `TopologyOperationWork`：primitive 尝试/成功/forced、创建/复用、邻接、活动索引、队列 API/成员变动、mesh 和路径插入。实际、试算与审计分开；原控制器查询另计。它不是逐字段 `W_touch`，不覆盖所有堆比较、复制或计划开销。

## 6. 驱动、结果和未实现能力

FACT：手动模式为：

```text
parallel_roam_OrderingGate_tests --relax-natural scenarios cameras new-output scenario sampleIndex
```

仅接受原四个冻结输入。每个变体独立复制同一个真实阶段来源；普通 CTest 只运行解析用例。CSV 保存配置、批次、检查候选足迹、主原因、实际根和审计；来源清单、程序及源码身份在产物目录归档。

本轮 `screen-1/` 完成 17 个变体：前三个输入各五配置，高工作量完成两个严格配置。高工作量 `α=0.1` 在第 70 次迭代附近达到 180 秒上限，另两个参数未运行。三个选中自然批次共完成 30 次排列应用；详细数字和解释见[阶段审查](../../reviews/ordering_relaxation/gate_03_snapshot_relaxation_review.md)。

筛查源码身份为 `dc3a95b28fcadca44767dc017da1ca2e74e4c914dea8be9382a4901c7456a560`；最终验证程序对应源码为 `97713ba1da48a71b842fe263c273bc22e0e1b8c24951fa0bf2403962b70f8650`。后续仅修正资源终止说明、截断分母/审计分类并补齐解析边界用例；完整变体的选批、应用和正常审计逻辑未变，因此保留原筛查版本，不重采确定性矩阵。原高输入最后一条超时批次有一项未分类暴露，已从完整比较中排除，不能用最终修正追认旧记录完整。

交付前另补局部等价、只读来源及控制边界注释，源码身份为 `7d6527cb07e33d9d75931b604242bf550eda2b41b3644c696df2457e4a0a7c23`。`final/delivery-identity.json` 保留与已验证源码的逐行对照：只有上述 `.h/.cpp` 的整行注释变化，去除注释后内容一致，因此未重复构建或采集。

LEGACY：小子集搜索未触发，完整质量比较及配对 `D_max` 未完成；未实现真实线程提交、可交换闭包、GPU 算法或完整质量—并行性前沿。关闭后不继续本轮待办，没有把离线耗时或粗工作跨度报告成实测 Speedup。

## 未确认事项

归档未改变原证据限制：高工作量正松弛不完整，配对质量和实际多核收益未知。它们作为历史缺口保留，不构成当前必须完成的任务。
