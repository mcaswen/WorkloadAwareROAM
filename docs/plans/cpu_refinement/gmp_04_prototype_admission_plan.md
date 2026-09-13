# GMP-04：原型准入与实施设计小规划

> 2026-09-14；GMP-03 已提交 `ed277cf` 后启动。用户授权本大规划内自主闭环与阶段提交。本阶段纯文档，止于下一份 Major Plan 供 Review，不实施持续 C++ 原型。

## 1. 问题、依据与出口

依据[原型前大规划](greedy_multipass_preprototype_plan.md)、[GMP-03 结果](../../research/cpu_refinement/transaction_backend_gate.md)、能力基线及开发/规划/审查规范。自然交换批次已非空，下一步判断其是否值得进入一个有限原型；必须同时处理有限前缀、昂贵认证、视图外高度恶化及持久状态未实现，不能只看 21/23 的批宽。

准入条件逐项映射：直接需求来源、条件式事务正确性、自然批次、费用边界、续接义务和已有工作差异。原型前 PASS 与“生产/长期质量未验证”可以同时成立，不能把未实现或未测量写成已失败。若已有工作覆盖全部候选执行差异，或质量问题使可声明的实验契约本身不一致，则不进入实现，记录具体缺口。

## 2. 范围、文件与职责

| 文件 | 动作与理由 |
| --- | --- |
| 本小规划 | Create：冻结文档阶段范围及退出条件 |
| `docs/research/cpu_refinement/transaction_backend_gate.md` | Extend：在数据之后加入独立的准入矩阵，不改原始结果 |
| `docs/research/cpu_refinement/transactional_lod_prior_art.md` | Create：有来源的近邻方法对照；不把文献论断混进代码事实 |
| `docs/plans/cpu_refinement/greedy_transactional_cpu_prototype_plan.md` | Create：下一独立实验模块的 Major Plan，文件/状态/四参考/数值/性能与停止门禁 |
| `docs/codebase/cpu_refinement/preprototype_capability_baseline.md` | Extend：仅按必要头文件和构建清单补充可复用边界 |
| `docs/reviews/cpu_refinement/gmp_04_prototype_admission_review.md` | Create：同 Agent 自审，区分已通过证据和实现前提 |
| 当前 Major Plan、研究索引 | Extend：闭环阶段状态及下一规划链接 |
| `.gitignore` | Extend：收尾检查补本模块原始产物的定向忽略；不全局忽略已有受管理 benchmark 数据 |

复用 GMP-01/02 契约和证明、GMP-03 数据，不重新采样。定向读 Garland 贪心地形、Adams 插删点/BPR、Franc/Skala 并行简化、Hu/Zhang 约束重网格的原始来源；只比较需求、质量/预算、事务选择/并行及持久维护。访问不到正文时标注摘要证据，不据缺失信息宣称首次。

新规划先扫描邻近实验模块、线程池和公共 mesh 边界，再决定 Reuse/Wrap/Create。新内核不得反向依赖 DOD 或 Python；来源适配、数值认证、持续状态、候选策略、预留和应用分别归属。保留诊断参考，避免新求解器热路径先堆通用平台。

## 3. 步骤、验证与限制

1. 小规划/职责自审；按现有授权执行，不另外等确认。
2. 定向文献核查及必要的源码接口扫描，形成事实与候选差异表。
3. 写准入矩阵，明确当前视图质量的窄契约与未来持续视图门禁；不新增本轮算法或改 GMP-03 评价。
4. 完成下一原型 Major Plan：阶段可以提前停止；自然源只是初始化，不能每轮导入 Legacy；直接计费完整发现、认证、预留、应用、状态/mesh 维护；A/B/C/D 分开。
5. 文档链接/状态/数值追溯与架构自审，单独提交 GMP-04。本次 Major Plan 至此结束，新原型实施不在本轮授权范围内。

纯文档不改变运行性能，不复跑 GMP-03、全 CTest、绘图后端或压力矩阵。只核对既有报告数值和必要引用；可审查但未证实的 novelty 仍写待验证。原型规划不得因论文叙事将局部原语包装为原创，也不得预支 2～5 倍性能。

## 实施结果

完成[近邻方法对照](../../research/cpu_refinement/transactional_lod_prior_art.md)、[准入矩阵](../../research/cpu_refinement/transaction_backend_gate.md#8-gmp-04-准入决策gmp-03-提交后)与[下一原型 Major Plan](greedy_transactional_cpu_prototype_plan.md)。源码扫描确认可包装的同步执行回调、公共 mesh 值/寿命和实验构建分层；未发现现有精确有理 C++ 依赖，新依赖作为待 Review 决策显式列出，没有安装。

决策为原型前 PASS，可进入持续原型设计 Review；生产/持续质量、实际多核收益及论文新颖性尚未验证。下一规划先实现直接串行单批，再局部续接/明确高度策略，再动态参考和短轨迹，最后有限多核；v1 与保守保护对照分开，保护结果未测且不是统一准入 Gate。Adams 等先例的关键重合已经记录，文献正文不可读的部分明确降级为摘要范围。

[同 Agent 自审](../../reviews/cpu_refinement/gmp_04_prototype_admission_review.md)与文档追溯完成后单独提交。本阶段没有改动运行代码、采集新快照或复跑性能；GMP-01～04 在本次提交闭环，后续原型尚待用户 Review。

收尾全仓库状态检查发现旧规则只覆盖 CPU-CBT/MPR，新 `cpu-refinement` 原始目录尚未被忽略。补充单目录 `.gitignore` 规则，原始数据留本地、未进入提交；此前报告的“由 Git 忽略”从本阶段修正后实际成立。对未共享的本阶段提交纳入此收尾项，保持一阶段一个提交，不修改前面阶段历史。

2026-09-14 用户认可后续评审并授权调整文档：本规划、结果解释、下一 Major Plan、自审、原 Major Plan 收尾和研究索引同步修订；不改变 GMP-03 冻结契约或数据。修订重点是准入 PASS、批宽存在性/耗时归因边界、P/evidence、高度自由度、批次账本及持续恢复。文件职责沿用 §2，无新模块或架构依赖；纯文档核查，不运行实验或实施 GTP。
