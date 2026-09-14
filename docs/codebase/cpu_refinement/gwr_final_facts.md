# GWR 保留实现与边界

> 2026-09-14；未提交工作区实现，来源哈希见 benchmark-output/cpu-refinement/gwr-06/final-identity.json。各阶段事实在 gwr_02/03/04 对应页。

## 所有权和依赖

- State/Commit 保存并局部保持 Boundary，顶点 degree 继续来自 Incident；没有新 View 层拓扑所有权。
- Samples 拥有 SampleGeometry、ViewState 与两个 PriorityIndex。ViewState 只管理当前/备用投影数组，不依赖 Samples/State/Pipeline。所有 Values 消费已迁移为 Value/Geometry/Projection。
- PriorityIndex 模板保存连续槽与 K=256 有序块；局部 PrepareRepair 预留容量并生成私有块，Publish 只移动；Prefix 合并块首，保留尾部和精确 Count。A 与 B/C 共用查询，A 高层反馈独立。
- Proposals::ReceiverCursor 按冻结 E/F/H 生成单项，成功后不再构造尾部。ProposalEvidence 仅供一次 Fit 内参数域覆盖/权重复用，质量证书仍在原区间/有理域计算实际高度。
- Reservation 仍并行准备完整共同 donor pool，按 proposal 缓存足迹。普通成功后停止额外 pair，诊断继续；D_feasible 精确，PairAuditComplete 只描述拒绝分布遍历。
- 独立 Validation、原 C++ 数值规则、线程池、DOD、渲染/上传未改变职责。GWR-05 ViewEvidence 源码与接线已撤回，没有条件候选留在默认路径。

## 正确性与成本事实

最终两场景共 16 帧与原二进制 mesh/实际决策一致；A 的各阶段 8 帧对照和 GWR-04 B 诊断/C4 正常 16 帧对照保存。两项相关最终测试通过，未跑全项目全集。

Peking 完整均值由 28.789→11.226ms，test129 18.915→15.037ms；当前 DOD 为 2.858/0.612ms（8 线程，非同任务）。不能称端到端优于 DOD。

局部块重排有工作放大，test129 初始化另确认增加约 30ms；原因只定位到 Samples::Refresh 边界，精确子函数因果仍未知。双缓冲增加持久空间，未测 RSS。没有关闭这两项成本风险。

完整视图仍为全 Q；精确认证仍为热点；没有算法 span 定理、次线性 view 结果或新的质量优势。完整报告见 research/cpu_refinement/gwr_final_results.md。
