# GWR-01 契约审计与大规划修订审查

2026-09-14；依据 [GWR 大规划](../../plans/cpu_refinement/greedy_transactional_work_reduction_plan.md)、[小规划](../../plans/cpu_refinement/gwr_01_contract_audit_plan.md)、[审计表](../../research/cpu_refinement/gwr_contract_lifecycle_audit.md)和 FPR 代码成本事实。

## Critical

无阻断 GWR-02 的未解决契约问题。原始 P/质量/预算/ID 保持，Samples 消费者和失败原子性已列为必迁义务，不通过读取旧视图值维持接口。

## Major

1. 不能删除构造必需的动态几何谓词；已在大规划三分类明确。独立验证原本在 ROI 外，不预支其成本。
2. donor 惰性可能把认证移入串行等待；改为条件候选，保留全池并发基线和触达量，不将已有缓存重新包装成收益。
3. top-r 还需尾部提升/回收池/人口和 A 局部更新；已在索引职责和验收中封闭。
4. 按需 Q 没有完整规范数值/域界，条件阶段可以不准入；不强制完成全部设施才准停止。

以上风险已进入大规划及下一小规划约束，不属于已被性能证明解决的问题。

## Minor

保留类型化出口和逐场景 5× 工程线。未改代码，复用 FPR 性能证据；GWR-02 独立采前后基线。文件只承担研究契约、阶段安排与审查，无新运行时依赖。

## 结论

GWR-01 的契约/生命周期审计完成，可进入 GWR-02；性能可行性仍开放。本轮不提交 Git。
