# GWR-01：工作削减契约与生命周期审计

> 2026-09-14；小规划，依据已获实施授权的 [GWR 大规划](greedy_transactional_work_reduction_plan.md)。先完成本审计再改运行时。

## 目标与文件归属

将 FPR 热点映射到可删的重复工作、可替换的表示和必须保留的动态决策，冻结视图/拓扑/证书的所有权与观察点。Reuse FPR 数据和原代码事实；Create 研究审计 `docs/research/cpu_refinement/gwr_contract_lifecycle_audit.md`；Create 对应 `docs/reviews/cpu_refinement/gwr_01_contract_audit_review.md`。不创建新 profiler、算法或一般证明框架。

## 步骤与验收

1. 读开发规范、规划/审查规范、GWR、GTP-04 事实和 FPR；核对 Samples、State/Commit、Proposals/Certification、Reservation、A 与探针的真实消费者。
2. 列清 boundary、owner、视图值、优先索引、证书、预算和 Pending 的产生/失效/观察；特别核查 SetView 失败、共享边样本及 H 改高。
3. 三分认证；确认普通浮点不能替代区间/有理前提，配对尾部审计和 donor 并发准备是两笔费用。
4. 冻结 GWR-02 的结构事实/视图缓冲接口以及 GWR-03 的尾部提升义务。按需视图证明留到削减后有条件评估，不在本阶段扩建理论。

纯文档审计复用 FPR，无运行时修改，不重跑性能矩阵。为下一代码阶段保存观测关闭 Release 的旧可执行文件及身份；相关自然前测归 GWR-02。出口为“契约审计完成，可开始已明确的实现”，不是性能 PASS。

## 实施结果

已逐项核查并形成[审计表](../../research/cpu_refinement/gwr_contract_lifecycle_audit.md)。当前没有无法定义的同任务观察点；GWR-02 可实施。精确按需 Q 的域界和舍入界未证明，保持条件准入；没有以旧 Lean 或有限验证替代这些义务。
