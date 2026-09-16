# 一般三角化事务：验证账本

日期：2026-09-17。阶段按完成顺序追加，未运行内容不标通过。

## ATT-01：局部替换与原语

- 纸面证据：[局部拼接和操作嵌入](operation_embeddings.md)，包含相对接口同胚的拼接/逆映射、欧拉计数、前缀上界和不适用反例。
- 有限检查：`python3 docs/research/adaptive_triangulation/checks/check_local_replacements.py --output docs/research/adaptive_triangulation/data/att_01.json`。
- [记录](data/att_01.json)：8个正例、4个反例；完整坐标/面列表和检查器及复用几何文件SHA-256。
- 费用：检查体0.00261633秒、峰值RSS21,848,064字节；启动/导入未计入检查体时间，旧同任务基线N/A。没有生产变更，因此不重跑生产性能。
- 范围：平面有理几何与明确的竖直单射嵌入实例；全局无自交需额外条件。没有新Lean证明，原有7个文件不变。

出口：**条件几何模型与有限实例成立**，继续资源/预算层；不宣称所有局部操作都可接受。

## ATT-02：资源与预算

**完成：具体资源更新的组合与终点预算。** [纸面过程与边界](model_derivation.md#9-att-02由具体更新导出交换与资源定理)、[机器记录](data/att_02.json)。

- `StateTransactions.lean`：框架、读取/使能稳定、逐键交换、实际存在计数及冻结净额稳定。
- `BatchSafety.lean`：一般有限排列、独立批次真实面数等式、终点预算、确定续接投影；正/负/零净额和预算反例。
- State首编译0.84秒；Batch首编译0.80秒，补充声明后一次失败0.71秒，修正后0.68秒。全部通过Ubuntu调用Windows Lean4.8.0，RSS仅是WSL启动器，**不是Lean进程峰值**，故不作为内存数据。
- 最终所列声明仅依赖Lean标准逻辑公理`propext / Classical.choice / Quot.sound`，预算数值反例无公理依赖；源码无`sorry/admit`。资源代数无ROAM导入。
- 完整几何面键映射、生产完整R/W、分配/归约协议仍是实例义务；没有机械证明任意前缀预算或C++并发安全。

出口：**资源/预算抽象核心成立**，继续固定环境下的质量证书组合。新证明无旧同任务性能基线，不运行生产性能矩阵。

## ATT-03：质量与总定理

**完成：一般损失、观察依赖与有条件总模型。** [最终结果及范围表](generalization_results.md)、[推导第10～12节](model_derivation.md#10-att-03加权质量及证书迁移的实际推导)。

- [Lean账本](data/att_03_lean.json)：原QC源码未改，仅生成导入缓存0.729秒；新增QualityComposition0.648秒、修改BatchSafety0.826秒，首次均完成。保留完整命令/输出/源码SHA-256、标准逻辑公理；无`sorry/admit`，没有新增全局公理。StateTransactions复用ATT-02已编译且源指纹一致的缓存。
- `weighted_nonincrease`：显式标量及非负乘法保序下的逐点目标、加权有限和。`observation_frame/certificate_migrates`：具体投影与读域覆盖推出单证书迁移。`batch_quality/budget_and_quality`：归纳及终点预算组合。没有机械建立Real实例、三角复形、双重和可加等式或生产资源映射。
- 有限命令：`python3 docs/research/adaptive_triangulation/checks/check_local_replacements.py --stage att-03 --output docs/research/adaptive_triangulation/data/att_03.json`。
- [有理实例](data/att_03.json)：3个合法局部替换、6个排列、5个质量边界；逻辑键状态与确定边邻接相同，9面→9面，Ψ25/4→3/8。3个排列中间11面不能按9面预算逐步可见发布。
- 新检查体0.008663235秒，RSS22,159,360字节；只计当前夹具体，启动/导入不计。它扩展了检查内容，不能与ATT-01的0.0026秒当成同任务性能回归。旧ATT-01结果保留其历史源码指纹，本次未重跑/覆写。
- 质量损失是固定参数点的非几何属性平方残差。屏幕/高度实例为条件映射；未知视图、连续曲面和非局部遮挡未证明。

出口：**有条件的一般资源约束局部三角化事务模型成立；Major闭环。** 核心安全不依赖ROAM；任意几何/损失的局部性、通用greedy求解器、生产符合性、性能与新颖性未通过或未研究，不能合并成无类型PASS。
