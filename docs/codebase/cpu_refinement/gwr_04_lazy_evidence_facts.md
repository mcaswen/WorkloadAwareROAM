# GWR-04 当前代码事实

ReceiverCursor::Next 按 E/F/H 构造一项；Receivers 是耗尽游标的完整诊断入口。Reservation 与 DynamicReference 成功后停止请求后续项。

TransactionalProposalEvidence 在 Fit 中缓存 sample→覆盖面索引/double 拟合权重，Measure 读同一缓存并对当前高度执行原区间公式。公共 Measure 不建立跨遍缓存，ExactErrorSquared 保持独立定位。

Reservation 仍一次并行认证共同 donor pool；不同 donor 的足迹按需缓存。Diagnostics=false 时成功交换立即结束该需求的 pair 循环，D_feasible 仍是精确存在性计数，PairAuditComplete 只描述完整 pair 拒绝审计。A 始终为 false。

没有新惰性 donor scheduler、全局 Q×candidate cache、数值过滤器或几何原语。账本分离 ReceiverConstructed、Evidence*、FootprintBuilds、DonorTouched/Certified。
