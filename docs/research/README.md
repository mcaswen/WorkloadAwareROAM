# CPU ROAM 研究探索状态

> 更新日期：2026-09-13；依据：保留此前探索状态，MPR-03 已提交为 `602ed48`，原生接入规划为 `c2cd2d8`。NMP-01 实施与验收完成，见[阶段审查](../reviews/roam_parallelism/native_materialization_01_review.md)：固定范围严格语义解耦通过，压力 heap 覆盖及规划成本需 Review；未进入 NMP-02。

现有 [CPU ROAM 执行与特征分析研究定义](../plans/formal_experiment/cpu_roam_research_definition.md)继续有效。本目录记录独立探索，不自动修改研究问题、启动实现或重开历史实验。

MPR-02 已提交为 `d635a9b`。[串行成本分析](roam_parallelism/target_materialization_serial_costs.md)与[MPR-03 小规划](../plans/roam_parallelism/target_materialization_prototype_03_plan.md)记录局部向量/记录句柄优化，完整状态与续接核查保持。新增压力样本 k=27,783，两组快验中串行物化约 332→240 ms、四线程约 226 ms；正式重复矩阵已按用户要求停止。小输入仍只有单次诊断，性能风险与统计限制见[结果 §14](roam_parallelism/target_materialization_prototype.md#14-mpr-03-开发快验结果与停止记录)，不宣称稳定多核加速或全部输入性能验收。

用户随后要求直接替换原 DOD 细分拓扑阶段，并保留原／新实现选择。[原生接入大规划](../plans/roam_parallelism/native_materialization_plan.md)已获认可；[NMP-01 小规划](../plans/roam_parallelism/native_materialization_01_plan.md)现已实现独立严格逻辑规划、封闭 Δ/Γ、完整决策轨迹和三点现实核查。有限输入内，第一箭头不需要生产 mutation、全状态复制或旧 pass 产 J；完整目标/队列/历史/预算与 Legacy 一致，正常出口没有目标关系或 heap 布局。MPR 继续保留为独立物化参考，关系恢复协议的实际生产充分性仍留 NMP-02。

NMP-01 不能合并成一个无保留的成功结论：压力点旧节点覆盖 8.393%，但合并堆写/读覆盖为 57.594%/76.035%；T_plan 约 1088.565 ms，同轮 Legacy 完整细分阶段约 61.630 ms。当前有序覆盖版本的规划成本高，未体现完整更新收益。阶段结论是语义解耦成立、压力 heap 与容器费用需要 Review；未自动转向新优化或开启 NMP-02。此前 test129 计时波动的原因继续保留，不追加微秒追测。

| 方向 | 当前状态 | 范围与记录 |
| --- | --- | --- |
| activation-spectrum 增量维护 | **No-Go，关闭** | 保留[理论 Gate](roam_threshold/incremental_gate.md)及固定依赖证明；不继续实现或扩展推导。该结论不是对所有隐式增量算法的不可能性证明 |
| recourse / minimum-change | **有独立研究价值，暂存** | [暂存记录](roam_recourse/minimum_change_note.md)保留问题定义、等成本公式和质量—修改量交换；优化工作量，不作为并行结构问题的答案 |
| parallel-first CPU refinement | **继续开放，仅表示层探索** | [无裂缝兼容性探索](cpu_refinement/compatibility_representation.md)研究如何改变局部依赖；尚无新算法或实现准入结论 |
| 必要依赖与并行上界分析 | **首轮审计完成，暂不建设分析器** | [审计报告](roam_parallelism/necessary_dependency_audit.md)区分逐菱形发布链与联合发布反例；尚无覆盖允许变换的非平凡长链证书，不否定所有后续分析可能 |
| Work–Inflation Gate | **保留草案，暂缓新增采样** | [测量契约](roam_parallelism/work_inflation_measurement_contract.md)与[小规划](../plans/roam_parallelism/work_inflation_gate_plan.md)保留已有能力和缺口；根据后续讨论，优先规划参数化成本语义 Gate，不按本草案启动插桩或采集 |
| 参数化成本语义 Gate | **本轮 No-Go，PCSG-01 提前结束** | [报告](roam_parallelism/parametric_cost_semantics_gate.md)保留评分/脏写分区等价和固定目标叶集合对应；拓扑的连接、索引及完整状态成本未封闭，按[小规划](../plans/roam_parallelism/parametric_cost_semantics_gate_plan.md)停止，不创建 Lean 框架或新算法 |
| ROAM 目标差分物化 | **获得强验证，可以继续** | [理论报告](roam_parallelism/target_materialization_gate.md)保留构造及 Lean 引理；[原型实施记录 §10–12](roam_parallelism/target_materialization_prototype.md#10-mpr-02-实际执行边界)提供完整直接物化、续接、差分局部性与真实并行证据。用户认可算法可行性并授权继续规划性能兑现；当前有限实现未取得稳定整体加速，原始性能及目标发现/外围成本继续保留 |
| CPU ROAM 执行与特征分析 | **现有主线保持有效** | 复用既有多阶段拆分、CPU 并行实现和实验能力；新算法候选若未形成有实质区别的结构，研究投入回到此主线 |

此前表示层探索的母问题是：能否设计天然适合共享内存 CPU 并行的地形细分模型，同时尽量保持 ROAM 的硬三角形预算和几何质量特性？其记录保留，尚无新机制准入。本轮必要依赖审计已完成并按证据不足的停止条件结束取证；用户关于停止新算法探索的表述曾明确为讨论，不追认为关闭全部候选的决定。

此前理论轮次不扩展 recourse、加权修改量、滞回或动态一致性算法，也不恢复旧 closure/batching、CPU-CBT 或 GPU 路线；后续获批的 MPR-01 只实现独立实验原型，不增加生产反向依赖。必要依赖审计与工作膨胀测量草案已提交为 `24c6391`；参数化成本语义 Gate 的规划、报告、自审及索引已按用户许可提交为 `69ab803`。

用户随后确认实施目标差分物化理论小规划。在固定视图/轮次和已知合法目标下形成完整抽象状态构造：合并候选复核范围至多 2k，工作界包含差分、深度、有序索引和几何查询费用。支持集及队列等式完成 Lean 检查，其他部分的纸面范围见[自审](../reviews/roam_parallelism/target_materialization_gate_review.md)。该结果不追认为 PCSG 已实施内容，也不等于严格 greedy 目标发现或原生 C++ 已并行化。理论与原型规划已提交为 `1e05b24`；工作膨胀测量继续暂停。

用户进一步确认原型大规划与 MPR-01，并要求先提交再继续；已按此顺序完成。原型采用独立状态、同目标逐菱形参考、直接物化与 DOD 来源桥接，不写回生产 DOD。Peking 的旧非空 Pending 得到保留和消费；自然任务混合增删、预算和全部队列对应成立。标准容器维护、导入／认证／恢复／换轮费用保留，不能只引用事务时间宣称端到端收益。用户随后明确授权提交实现及[阶段自审](../reviews/roam_parallelism/target_materialization_prototype_01_review.md)，并进入 MPR-02 小规划；具体并行边界仍须先 Review，不直接开始多线程修改。

MPR-01 已按许可提交为 `d876cea`。用户随后确认 [MPR-02 小规划](../plans/roam_parallelism/target_materialization_prototype_02_plan.md)，已完成独立顺序参考、共同串行/并行准备路径与薄同步适配器。三段并行边界保留唯一所有权，标准容器与发布仍串行；完整状态与五进程成本对照齐全，尚无稳定的多核事务净收益。生产 P95 仅作一次触发复测，未确认版本相关退化；[阶段自审](../reviews/roam_parallelism/target_materialization_prototype_02_review.md)保留证据边界。用户进一步明确“获得强验证，可以继续”，授权提交现有实现，再规划串行成本、工作量和预期收益；不直接扩展新表示或目标发现。
