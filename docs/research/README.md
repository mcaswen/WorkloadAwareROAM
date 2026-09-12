# CPU ROAM 研究探索状态

> 更新日期：2026-09-12；依据：保留此前探索状态，并记录用户认可继续规划的必要依赖审计候选。

现有 [CPU ROAM 执行与特征分析研究定义](../plans/formal_experiment/cpu_roam_research_definition.md)继续有效。本目录记录独立探索，不自动修改研究问题、启动实现或重开历史实验。

| 方向 | 当前状态 | 范围与记录 |
| --- | --- | --- |
| activation-spectrum 增量维护 | **No-Go，关闭** | 保留[理论 Gate](roam_threshold/incremental_gate.md)及固定依赖证明；不继续实现或扩展推导。该结论不是对所有隐式增量算法的不可能性证明 |
| recourse / minimum-change | **有独立研究价值，暂存** | [暂存记录](roam_recourse/minimum_change_note.md)保留问题定义、等成本公式和质量—修改量交换；优化工作量，不作为并行结构问题的答案 |
| parallel-first CPU refinement | **继续开放，仅表示层探索** | [无裂缝兼容性探索](cpu_refinement/compatibility_representation.md)研究如何改变局部依赖；尚无新算法或实现准入结论 |
| 必要依赖与并行上界分析 | **方向已记录，小规划待 Review** | [方向记录](roam_parallelism/direction_note.md)研究可审计的必要依赖与有条件的并行上界，尝试解释多阶段执行选择；尚未完成依赖证明或分析器 |
| CPU ROAM 执行与特征分析 | **现有主线保持有效** | 复用既有多阶段拆分、CPU 并行实现和实验能力；新算法候选若未形成有实质区别的结构，研究投入回到此主线 |

此前表示层探索的母问题是：能否设计天然适合共享内存 CPU 并行的地形细分模型，同时尽量保持 ROAM 的硬三角形预算和几何质量特性？其记录保留，尚无新机制准入。本轮优先形成必要依赖审计的小规划；用户关于停止新算法探索的表述曾明确为讨论，不追认为关闭全部候选的决定。

本轮不扩展 recourse、加权修改量、滞回或动态一致性算法，不实现 C++，不追加 Lean，也不恢复 closure/batching、CPU-CBT 或 GPU 路线。研究笔记与已有证明之间只建立引用，不增加生产依赖。用户已授权记录新方向后提交现有研究材料，再编写小规划；该授权不包含后续分析器实现或远程推送。
