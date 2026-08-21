# Workload-Aware ROAM：策略定义

本文档定义各 pass 的合法执行策略、策略接口、基线、oracle 和消融对照。策略必须服从[具体问题定义](19-workload-aware-problem-definition.md)中的 pass 边界和语义等价约束。

## 策略接口

第一阶段只建立可重复的实验控制面，不训练模型。论文和 CSV 使用 pass-specific action id，不用一个抽象的 `ExecutionMode × UpdateMode` 暗示所有 pass 共享相同策略：

~~~text
PassDecision:
  passId
  requestedAction
  effectiveAction
  requestedWorkerCount
  effectiveWorkerCount
  fallbackReason
~~~

主实验的 action 集合如下：

| Pass | 合法 action |
|---|---|
| Merge 评分 | `SerialRefresh`、`ParallelRefresh` |
| Split 扫描/评分 | `SerialRefresh`、`ParallelRefresh` |
| Merge 拓扑 | `SerialImmediate`、`ParallelAssisted` |
| Split 拓扑 | `SerialImmediate`、`ParallelAssisted` |
| 网格提交 | `SerialDirty`、`ParallelDirty`、`SerialFull`；`ParallelFull` 通过前置验证后加入 |
| CPU 上传 | `DirtyRange`、`FullBuffer` |

`Auto` 只表示由 policy 选择上述 action 之一，不是额外执行模式。requested action 因候选不足、线程不足、buffer 扩容或后端限制而无法执行时，必须记录 effective action 和 fallback reason。

配对 action 必须读取同一 immutable snapshot，使用相同 score、eligibility、排序、预算和 closure 规则。不同 kernel 可以有不同调度方式，但不能静默改变有效工作集；允许合法顺序差异时必须使用预先定义的规范化结果契约。

只有 Experiment 1 确认存在稳定 crossover 后，策略模型才按以下顺序演进：

1. 离线二维 lookup table；
2. 可解释的分段线性或浅层决策树；
3. 加入 hysteresis 和最小驻留帧数的在线策略；
4. 只有在低维模型明显不足时，才考虑更复杂的分类器或回归器。

策略目标是在正确性约束下最小化整帧 CPU update，并满足当前 pass 的剩余时间预算：

~~~text
minimize predicted update time
subject to topology_valid
           triangle_budget_valid
           predicted_frame_time <= frame_budget
~~~

当所有 action 都不可行时，策略返回 `NoneFeasible`；质量或三角形预算调整属于另一层控制，本研究不把它伪装成 execution action。本文不预设必须使用机器学习：若查表或分段模型已接近 oracle，就不引入更复杂模型。

## 方法与基线

| 方法 | 角色 |
|---|---|
| Classic CPU ROAM | 对象式、主要串行的经典路径参考；不作为 DOD 内部 pass crossover 的配对样本 |
| DOD fixed serial | 评分=`SerialRefresh`，拓扑=`SerialImmediate`，mesh=`SerialDirty`，上传=`DirtyRange` |
| DOD maximum-safe-parallel | 评分=`ParallelRefresh`，拓扑=`ParallelAssisted`，mesh=`ParallelDirty`，上传=`DirtyRange`；closure/convergence 仍串行 |
| DOD pass variants | 分别运行上表中的合法 action；不设置 incremental score 或全量拓扑 |
| Best static configuration | 在训练 workload 上选择的最佳固定策略组合，用于区分自适应收益与简单调参收益 |
| Greedy pass oracle | 从同一冻结 pass 输入执行合法 action，每个 pass 独立选择局部最快结果 |
| Frame-level oracle | 从同一帧初始状态重放合法 action 组合，选择最低 end-to-end CPU update time |
| Workload-aware adaptive DOD | 本文方法；根据低成本特征在线选择每个 pass 的 effective action |

Greedy oracle 与 frame oracle 的比较是主实验，而不是附带消融：

- 若二者差距很小，可得到重要结果：execution passes are sufficiently decoupled，低成本 per-pass policy 足够；
- 若二者差距显著，则量化 pass coupling，并把 coordinated frame policy 作为必要方法，而不是假定局部最优可以直接相加。

Frame oracle 只在选定目标帧上离线计算。按首轮 action 集合，理论组合上限为 `2 × 2 × 2 × 2 × 3 × 2 = 96`；加入 `ParallelFull` 后为 128。实际执行前删除不适用 action 和确定性 fallback，避免在完整 trajectory 的每一帧穷举。

必要消融包括：

- 仅候选数量的单阈值；
- 候选数量 + 线程数量；
- 加入 during-planning 的 non-empty chunk 和 estimated imbalance；
- 加入预估边界比例和 estimated closure demand；
- greedy pass oracle 与 frame-level oracle；
- 分别包含与移除网格提交 dirty/full 和 CPU 上传 dirty/full action。
