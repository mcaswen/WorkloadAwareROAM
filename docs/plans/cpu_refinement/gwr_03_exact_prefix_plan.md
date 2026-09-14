# GWR-03：持续精确前缀

> 2026-09-14；GWR-02 已闭环，按大规划授权实施。紧邻前测复用 GWR-02 after，保留此版本独立二进制。

## 设计与范围

替换 Samples 的完整 set/map 顺序，不改变优先值、资格、同分规则或 A 的查询。新 TransactionalPriorityIndex.h 为两种键共享的模板，独立拥有连续槽记录、固定 256 槽的有序块、精确计数。模板实现留头文件，不强造空 cpp。

冻结一种较简单的实现：每块保存全部有效尾部；Prefix 对所有块首做一次 heapify，再归并最多 r 项。无需每层维护 64 项摘要，局部更改只重排脏块。代价是完整重建 O(n* log K)、查询 O(b+r log b)、h 脏块更新 O(h K log K)，b=ceil(n*/K)、K=256。查询仍访问 b 个块首，并非 O(r) 或与总槽数完全无关。此取舍降低摘要复制/事务维护，若 A 完整时间退化则撤回或在本阶段修正。

PrepareRepair 接收按物理槽去重后的可选 key 写集，预留容量、构造脏块目标、计算目标计数；Publish 只写 records 和移动脏块，无分配。尾部提升、删除、同槽新身份由完整块记录保证。容量增长仍可能移动外层记录/块索引，计入 Reserve；空余槽不影响资格计数。全排序仅为测试 oracle。

Samples 负责资格、priority 和 donor incident maximum。State 只新增 VertexSlot 只读查询，目标新增点槽来自 PreparedTopology::AddedIndex。局部更新不再维护 donorCosts map，按物理槽替换记录；稳定逻辑 ID 仍在 key 中。View 准备持有新完整索引，发布移动。

## 文件职责

Create TransactionalPriorityIndex.h：记录/块归并/准备发布；Extend Samples.h/.cpp：构建与修复输入；Extend State.h：窄槽查询；Extend Types/Execution/Probe：块、槽、比较及查询计数。Create tests/TransactionalPriorityIndexTests.cpp：独立排序 oracle，CMake 注册单一测试。无新全局调度。

## 验证与出口

独立测试包含跨块同分、r=0/1/64/all、尾部提升、失效槽复用、增长、丢弃准备后旧查询不变；对完整排序比较 count 与 prefix。现有 transactional_lod 覆盖续接与 A cached/fresh。自然沿用两场景 C4 与 Peking A，各一个进程八轮，对照 mesh、事务、A 决策/停止；工作计数允许变化。

记录完整帧/建序/局部 next_order 和索引工作。回归超过规范阈值才一次定向复测。正确性、性能和审查闭环后进入 GWR-04。

## 实施结果

已完成，见[结果](../../research/cpu_refinement/gwr_03_exact_prefix_results.md)与[审查](../../reviews/cpu_refinement/gwr_03_exact_prefix_review.md)。24 帧等价；完整路径下降，局部修复退化明确保留。进入 GWR-04。
