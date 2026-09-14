# FPR-03 实现审查

2026-09-14；依据[小规划](../../plans/profiling/fpr_03_thread_timeline_plan.md)、[代码事实](../../codebase/profiling/fpr_03_thread_timeline_facts.md)、[实测报告](../../research/profiling/fpr_03_thread_timeline_report.md)。

## Critical

无。关闭宏未求值、独占线程区间、实际断连失败、原非计时结果一致均已验证。没有变更评分、事务选择、锁或任务粒度。

## Major

无未解决的架构问题。FPR-02 关闭路径性能疑点在保留旧程序的相邻核查中没有持续重现，按无版本相关证据关闭，不解释为已证明零成本。

## Minor

- Peking 捕获 P95 比未连接高 10.8%，该单轮时序仅用作结构诊断；总体均值差约 1.9%，不再扩张事件或重复测量追求更好数字
- 真实官方 CSV 暴露了多行元数据与零时长省略，已在发出端选择单行标签和非空完成区间，原失败证据保留
- 能力和自然采集复用一个 Tracy 后端；格式特殊的时间线聚合独立于 perf 报告，未增加通用 profiler 数据库
- 函数 self/inclusive、线程任务和等待具有不同语义，报告明确不可相加；未用父线程栈推导所有异步 CPU 工作

## 结论

FPR-03 完成，可进入 FPR-04 的有限家族接入、使用契约和归因汇总。GWR 仍未开始，不因热点已知顺手实施优化。
