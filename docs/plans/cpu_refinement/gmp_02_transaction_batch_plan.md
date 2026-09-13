# GMP-02：事务组合与续接义务小规划

> 2026-09-14；用户授权自主闭环；实施前规划与自审。依赖 [GMP-01 契约](../../research/cpu_refinement/greedy_multipass_contract.md)，不新增几何原语或持久运行时。

## 目标与文件边界

证明同代局部证书、完整 R/W、资源预留和明确归约足以支持批次排列等价，并解释全部下一轮状态怎样局部恢复。有限脚本只核查容易漏掉的共享边界、混合交换和样本重新归属。

| 文件 | 归属与动作 |
| --- | --- |
| `docs/research/cpu_refinement/transaction_batch_derivation.md` | Create：一般推导、失败尝试和完整字段支持表 |
| `docs/research/cpu_refinement/checks/verify_transaction_batch.py` | Create：有理数小网格事务应用与独立重建对照，复用旧几何函数；不是自然审计内核 |
| `docs/reviews/cpu_refinement/gmp_02_transaction_batch_review.md` | Create：结果、成本、未覆盖及阶段出口 |
| 本规划、Major Plan、研究索引 | Extend：状态与产物入口 |

脚本单向引用 `verify_bounded_patch_refit.py` 和 `verify_cavity_retriangulation.py` 的纯几何函数，不改旧脚本，不让生产代码依赖研究脚本。新增职责为有限组合验证，不新增通用事务框架。

## 实施步骤与契约

1. 明确核心面/边/高度记录与屏障后派生记录。普通共享写保守拒绝；脏集合并集、唯一增量集合可归约，最终属性从新网格局部重算，禁止读取部分归约值。
2. 接收/回收并集作为事务支持，外接口继承；读集合包含高度与边关联不存在性。建立预算和私有存储账本；发布整批原子可见。
3. 列举续接字段与失效支持，证明 Q owner 可能在改动边界上落在保留外面，不能只扫描已删除面的 owned samples。收集完整 closed support 的样本/关联，重定位时同时看边界外邻面。
4. 有限验证：两个远离交换正/反序；两个仅共享保留顶点的局部修改经派生重算后等价；旧高度修改与相邻证书读取的冲突；删除面不交但边记录/高度冲突的拒绝；随机重排面 ID 后的公共边 owner；同相机下局部修复 vs 全量重建的下一轮 P/资格/样本/输出一致；预算及重复贡献负例。

验收不要求所有几何不交事务都被接纳；只证明接纳条件足够。高度修改需完整邻域，不对纯面并集加一个未经计费的全域维护步骤。

## 最小验证与性能

修改前只计时既有 `check_budget_exchange()` 一次，复用 CR-04 正例作为相关基线；不跑全 CR/Lean/CTest。修改后原函数一次同环境复测，新增脚本一次完整计费。新增能力没有旧版本，不计算虚假加速比。

输出 `benchmark-output/cpu-refinement/gmp-02/run-01/{before,after,checks}.json`，记录输入身份、脚本摘要与墙钟。预计每项少于 10 秒，新增有限检查总计 30 秒内；超过配额先分析，不扩大夹具。遵循开发规范 §7.3，不追微秒波动，只有具体回归再复测一次。

自审：文件职责独立，复用仍有效几何验证；通用正确性来自纸面支持证明，不将有限排列等价外推为所有网格定理。若必须全域重建才能恢复某项状态，记录并停止原型准入，不转为新平台实现。

## 实施结果

已完成[组合推导](../../research/cpu_refinement/transaction_batch_derivation.md)、有限脚本和[实施审查](../../reviews/cpu_refinement/gmp_02_transaction_batch_review.md)。四种排列一致，局部续接对照一致；13 个外部 owner 样本证实闭支持不可省略。首次相邻正例触发边写冲突，保留为负例；几何正例改用确实不冲突的输入，未放松断言。

原 CR-04 检查 99.909→91.767 ms，新能力 91.525 ms；单进程快验不推断显著性。结果位于 `benchmark-output/cpu-refinement/gmp-02/run-01/`。阶段已闭环，单独提交后才冻结 GMP-03 面板与探针。
