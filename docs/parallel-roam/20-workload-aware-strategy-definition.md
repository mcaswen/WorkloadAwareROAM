# Workload-Aware ROAM：策略定义

本文档定义各 pass 的合法执行策略、策略接口、基线、oracle 和消融对照。策略必须服从[具体问题定义](19-workload-aware-problem-definition.md)中的 pass 边界和语义等价约束。

## 策略接口

第一阶段先建立实验控制面，而不是直接训练复杂模型。策略接口直接以 pass 名称为主键：

~~~text
ExecutionMode = Serial | Parallel | Auto
UpdateMode    = Incremental | Full | Auto

PassPolicy(passId):
  validExecutionModes
  validUpdateModes
  requestedStrategy
  effectiveStrategy
~~~

ExecutionMode 和 UpdateMode 不是全局笛卡尔积；不适用某一维度的 pass 不暴露该开关。每个 pass 独立配置 requested strategy，并记录 effective strategy、worker count 和 fallback reason。Serial 与 Parallel 必须读取同一 immutable snapshot，遵守同一 score、eligibility、排序、闭包和提交语义；可以使用不同调度 kernel，但不能改变有效工作集或结果。

初始策略模型按以下顺序演进：

1. 离线二维 lookup table；
2. 可解释的分段线性或浅层决策树；
3. 加入 hysteresis 和最小驻留帧数的在线策略；
4. 只有在低维模型明显不足时，才考虑更复杂的分类器或回归器。

策略目标是带 deadline 约束的 frame/update wall time 最小化：

~~~text
minimize predicted update time
subject to topology_valid
           triangle_budget_valid
           predicted_frame_time <= frame_budget
~~~

当所有策略都不可行时，策略必须显式返回 none feasible 或触发质量/工作量 fallback，不能把违反 deadline 的最低耗时策略报告为“最优”。本文不预设必须使用机器学习。若查表或分段模型已经接近 oracle，应优先保留可解释、开销低和易于跨平台标定的方案。

## 方法与基线

| 方法 | 角色 |
|---|---|
| Classic CPU ROAM | 对象式、主要串行的经典路径参考；不作为 DOD 内部 pass crossover 的配对样本 |
| DOD fixed serial | 核心 Decision、Topology Mutation 和 Mesh emit pass 固定使用合法 serial 策略 |
| DOD maximum-safe-parallel | 核心 pass 固定使用安全的 parallel-assisted 策略；forced closure 和 convergence 保留必要串行 |
| DOD Decision variants | Merge mark、Split scan/mark 的 serial/parallel；deferred/full 只作条件性扩展 |
| DOD Topology variants | Merge topology、Split topology 的 serial/parallel-assisted，不设置 full topology mode |
| DOD Output variants | Mesh emit 的 serial/parallel 和 dirty/full；CPU upload 记录联动的 dirty/full 成本 |
| Best static configuration | 在训练 workload 上选择的最佳固定策略组合，用于区分自适应收益与简单调参收益 |
| Greedy pass oracle | 从同一冻结 pass 输入执行所有合法策略，每个 pass 独立选择局部最快策略，再组合成一帧 |
| Frame-level oracle | 从同一帧初始状态重放合法策略组合，直接选择最低 end-to-end frame/update time |
| Workload-aware adaptive DOD | 本文方法；根据低成本特征在线选择每个 pass 的 effective strategy |

Greedy oracle 与 frame oracle 的比较是主实验，而不是附带消融：

- 若二者差距很小，可得到重要结果：execution passes are sufficiently decoupled，低成本 per-pass policy 足够；
- 若二者差距显著，则量化 pass coupling，并把 coordinated frame policy 作为必要方法，而不是假定局部最优可以直接相加。

必要消融包括：

- 仅 candidate count 的单阈值；
- candidate count + worker count；
- 加入 during-planning 的 non-empty chunk 和 estimated imbalance；
- 加入 estimated boundary ratio 和 estimated closure demand；
- greedy pass oracle 与 frame-level oracle；
- 对具有 update mode 的具体 pass，分别包含与移除 incremental/full 选择。
