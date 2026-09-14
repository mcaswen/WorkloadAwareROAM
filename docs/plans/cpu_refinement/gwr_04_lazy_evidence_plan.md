# GWR-04：惰性目录、局部证据与必要配对

> 2026-09-14。GWR-03 已闭环；复用其 after 作紧邻前测，另存二进制。按大规划授权实施。

## 设计与职责

1. Proposals 增加 ReceiverCursor，按原 E/F/H 和 ordinal 惰性构造。边/顶点稳定排序不变，F witness 到达该段才求值，成功后不构造尾部。原 Receivers 完整入口作为诊断兼容能力，用同一游标耗尽；A 用游标但保持自己的逐事务控制。
2. Create TransactionalProposalEvidence.h/.cpp：只在一次 Fit/Measure 内保存 sample 对应的首个覆盖面索引与普通 double 权重。按已排序 Samples 对应连续记录，首次实际触达才寻找覆盖面；拟合后高度改变，索引与参数权重仍有效，但每次从当前 proposal 读实际高度。不跨 topology/view 缓存，不保存点指针；不将 double 权重用于区间/有理质量证明。
3. Certification 复用上述覆盖事实，删除 Fit 中三次相同定位/权重恢复。Measure 与精确回退共享同一次调用的面选择；Reference、区间插值、投影域、实际 binary64 高度和精确接受保留原运算次序。公开 ExactErrorSquared 不用该缓存，保留独立复核。
4. Reservation 每个已认证 donor 足迹最多构建一次；正常模式命中后停止当前需求额外 pair 审计，Diagnostics 保留完整遍历。D_feasible 仍精确：成功已证明至少存在一个 feasible；未成功则必须检查完整池。拒绝分布标注完整性，不能把未触达 pair 填为失败。分离账本，不改变事务序列。
5. 先计不同 donor 必要触达数。若大量未触达，评估一次固定小波段的惰性认证；若几乎全池触达或新增等待抵消收益，不引入新调度/缓存策略。当前已存在每快照一次 donor 认证，不能再将它称为新增收益。

Extend Types/Execution/Probe：真正构造数、覆盖检索/复用/字节、足迹、不同 donor 触达、pair 与审计完整性。Evidence 不依赖 Reservation/Pipeline，游标不控制接受；不拓展全局证据缓存、精确数值内核或 ClipPolygon。

## 验证与测量

已有相关夹具覆盖 Shapes、真实舍入接受、A 缓存/新算一致和持续状态；新增 eager 目录与游标逐项几何/命名对照、lazy/diagnostic 同批与同下一状态。自然两场景 C4 与 Peking A 各一进程八轮，与 GWR-03 对照 mesh、intent、首成功 proposal、budget、实际 exchange 与 A decisions/stop。账本和未执行审计允许不同，必须显式记录。

使用关闭 profiler 的完整帧、receiver/donor、reservation、局部 continuation；保留函数热点采样供最后剩余成本归因。不得仅以 Fit 单函数更快判断。语义失败修正或撤回；性能退化按规范至多一次定向复测。完成后审查并进入 GWR-05 条件审计，不能顺势增加任意数值优化包。

## 实施结果

已完成，见[结果](../../research/cpu_refinement/gwr_04_lazy_evidence_results.md)与[审查](../../reviews/cpu_refinement/gwr_04_lazy_evidence_review.md)。单次 donor 无复用缓存撤回；实际全部 donor 触达，不实施惰性调度。正常/诊断同结果，进入 GWR-05 有限条件审计。
