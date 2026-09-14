# GWR-06：成本闭环与阶段出口

> 2026-09-14。GWR-01～04 已完成；GWR-05 单一候选因没有有效排除而撤回。不再扩算法、线程或平台。

## 范围与产物

核查撤回后代码等同保留的 GWR-04 运行机制，补齐新增接口注释与必要包含。无其他算法改动。最后报告给出各阶段完整时间、被删除工作、局部退化、初始化/内存与剩余复杂度；分别标记正确性、工作削减、5×工程距离和论文证据。

最终源码及编译配置、输入、关闭观测二进制归档。两场景 C4 最终各一个进程八轮；A 复用 GWR-04 有效对照，当前 DOD 家族各一个进程用于同环境工程距离，线程与语义差异显式披露。不能将默认 DOD 8 线程与 C4 之比写成同任务 speedup。

复用 FPR：最终两场景各一次 Tracy 捕获，perf 各一次有限采样（初定 17 次同种子八轮重放，目的是样本量，不作为性能重复统计；若仍不足则标注不足，不自动补更多）。只采 C4 主要路径，不扩 Classic/A/线程矩阵。所有构建、性能与采集依次执行；原始采集先在 Linux 原生缓存。

分析 self/inclusive 与同线程/异步边界，不相加父子占比。正式收益使用关闭观测计时；profiler 只判断最后的函数与时序归因，不能把 CPU 采样数换算成独立 wall-clock 开销。

验证复用每阶段 24 帧原二进制对照和 GWR-04 正常/诊断一致证据；最终仅必要相关 CTest、源文件差异/注释/依赖核查。不做冗余全集，也不拿失败候选测试数量充当保留实现证据。

## 退出

全部记录写入 research/cpu_refinement/gwr_final_results.md、codebase/cpu_refinement/gwr_final_facts.md 与 reviews/cpu_refinement/gwr_final_review.md。若当前完整时间仍远于门槛，明确该场景未通过；不预设未测 scaling 或更高质量补偿。GWR 在此关闭，不自动接平台或开始新优化规划。

## 实施结果

已完成。见[最终报告](../../research/cpu_refinement/gwr_final_results.md)、[代码事实](../../codebase/cpu_refinement/gwr_final_facts.md)与[审查](../../reviews/cpu_refinement/gwr_final_review.md)。C4 完整时间 test129 18.915→15.037ms、Peking 28.789→11.226ms；DOD 工程距离约 24.6×/3.9×。初始化和局部维护成本问题保留，GWR-05 运行候选撤回。两场景 perf/Tracy 完整捕获；最终相关测试及原二进制语义核查完成。此有限包关闭，不自动接平台或继续扩大优化。
