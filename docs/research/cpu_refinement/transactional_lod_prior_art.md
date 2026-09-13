# 事务化 CPU LOD：原型准入的已有工作边界

> 2026-09-14；GMP-04 定向核查。目的为筛掉不成立的创新表述、约束下一原型消融，**不是穷尽性新颖性证明**。本页区分论文实际内容、当前方案差异和仍待证实的贡献。

## 1. 最接近的已知方法

| 原始来源与本次可读范围 | 已有内容 | 本项目必须承认的重合 / 尚可检验的差异 |
| --- | --- | --- |
| Garland、Heckbert，1995，*Fast Polygonal Approximation of Terrains and Height Fields*；[作者摘要](https://www.cs.cmu.edu/~garland/scape/abstract.scape)、[作者项目页](https://www.cs.cmu.edu/~garland/scape/)；PDF 本轮超时 | 逐步选择当前最大误差输入点插入地形网格，支持 Delaunay 或数据相关剖分；优化误差和面数 | 贪心误差驱动地形细分不是新思想。本项目要证明的不是再次采用 greedy，而是冻结需求后如何生成、认证、预留和并行兑现完整事务 |
| Michael D. Adams，作者稿 `sp2012`，*A Highly-Effective Incremental/Decremental Delaunay Mesh-Generation Strategy for Image Representation*；[作者全文 §2–4、§5.2](https://www.ece.uvic.ca/~frodo/publications/publications/sp2012.pdf) | 指定顶点数、MSE 目标、插删点贪心；已有面/点优先队列、样本唯一归属和延迟删除优先级更新。BPR 删除不增误差的点，再补相同数量 | 插删点预算重分配、sample ownership、延迟维护都不是本项目独创。需比较固定视图屏幕证书、成对净零预算和快照事务执行，而不能只宣称“把维护推迟” |
| Franc、Skala，2000，*Triangular Mesh Decimation in Parallel Environment*；[大学仓库摘要](https://dspace.zcu.cz/items/28e7e6d9-1d44-4d08-a762-2f237226f849)；PDF 跳到登录页，本轮未读正文 | 使用 super independent set 避免临界区的多线程三角网格简化 | 独立操作选择及 CPU 并行简化已有直接先例。摘要不能支持“它未维护完整状态”或“不支持某种冲突”的否定判断；本项目须用具体 R/W、预留和持续状态实验立论 |
| Hu 等，2016 arXiv v1，*Error-Bounded and Feature Preserving Surface Remeshing with Minimal Angle Improvement*；[作者摘要](https://arxiv.org/abs/1611.02147) | 优先化局部操作和顶点重定位；近似误差为硬约束，最小角与复杂度为优化目标 | 误差认证、形状目标和局部重网格不是新概念。本项目是固定活动面预算及角度接纳条件下的有限 CPU 执行规格，不能因此宣称质量更强或普遍可达 |
| Zhang 等，2022，*Constrained Remeshing Using Evolutionary Vertex Optimization*；[出版社摘要](https://doi.org/10.1111/cgf.14471)、[Eurographics 条目](https://diglib.eg.org/items/bc828aa4-fb3b-4637-97a3-6a63f0af3c50)；本轮未读完整正文 | 连接/几何联合变量，复用边分裂、折叠、翻边与点移动；有指定顶点数的误差受限角度改善应用 | 固定数量加质量约束的局部几何优化已有工作。当前候选差异应落实为执行和维护协议；不能以本轮未见其并发章节为“首次”的证据 |

上述年份按列明版本；不将投稿预印本与最终发表年份混为一谈。Franc/Skala 此条准确标题与其他 *Parallel Triangular Mesh Reduction* 条目分开，不交叉移用正文结论。DP、多边形耳切和独立集等局部原语仍按[先前回收推导](cavity_budget_recovery_derivation.md)的 reused primitives 定位。

## 2. 本轮排除的主张

不能将“全局 greedy、多个 pass、插删点平衡预算、局部误差认证、样本归属、延迟队列维护、独立集并行、SoA”任何一项单独命名为新算法贡献。它们的简单罗列也不足以建立组合创新。

尤其不能借 CPU-CBT/NMP 的历史失败，反推本方案必然成功；也不能将不同质量/停止语义的 A→B 差值当作并行加速。GMP-03 的三价池为空，使这项消融只能说明当前原语能力，不足以当成传统方法弱基线。

## 3. 值得原型检验的组合假设

以下是**本项目的待验证主张**，不是从上述文献“未写到”推导出的新颖性事实：

1. 完整同代需求按固定全序产生；接收证据不受回收后端过滤，P/G/C 分离，使局部回收能力和全局执行兑现率可分别归因。
2. 先认证完整读写支持、成对净预算和样本接受，再作不可回退的全局预留；获批事务的目标已确定，提交不再反馈发现目标。
3. 同一持续网格上的样本/资格/输出局部修复有明确账本；把实际 CPU 工作与固定快照质量、未来视图质量并列报告，证明批处理没有把发现或维护成本藏起来。
4. 在同一实现里以动态串行 A、批次串行 B、同批并行 C 分开评估语义损失、维护收益与真正的线程收益，再与 Classic/DOD 作家族层对照。

现有 GMP-01/02 给出规格与条件式证明，GMP-03 提供有限自然可执行性。第 3、4 项仍缺持久实现与性能/轨迹数据，不能写为已有成果。即使原型通过，论文仍需更完整的近邻正文比较；本次检索没有排除其他动态并行重网格研究的本质重合。

## 4. 准入判断

**可以把这组执行假设做成受限原型，但不能以“新颖性已确认”准入。** 当前有可复核的具体算法契约和自然事务，足以开展有限工程验证；并没有文献证据证明上述完整组合已被这五项工作逐项等价实现，也没有足够覆盖证明它首次出现。

下一轮首要投入为直接发现、局部续接、质量保护及成本，避免继续扩充 primitive 或 theory 来拉大与文献的表面差异。若完整工作量、持续质量或多核收益不支持候选叙事，就按相应阶段出口停止，保留 CPU ROAM 执行/特征分析主线。
