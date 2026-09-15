# CPU ROAM 研究探索状态

2026-09-16：[FER-01首轮正式实验](experiment_infrastructure/fer_01_results.md)已完成。复用EIP冻结4种地形、6组输入和20配置，采集60个独立计时进程、18组实际视觉回放和72个质量点；[数表及19组图像](experiment_infrastructure/fer_01_appendix.md)保留全部反例。同任务1→8线程约3.22×/4.44×，但Peking转向与峡谷质量、200k预留成本仍未满足竞争目标。实验完成不改变下列历史阶段各自的证明边界。

> 更新日期：2026-09-14；依据：保留此前探索状态，MPR-03 已提交为 `602ed48`，原生接入规划为 `c2cd2d8`。NMP-01 实施与验收已提交 `2f04f48`，见[阶段审查](../reviews/roam_parallelism/native_materialization_01_review.md)。NMP-01P 压力性能未达门槛，契约审计后暂停扩展局部性能修改；候选出生及后续推导已提交为 `2d8809f`，未进入 NMP-02。当前另探索独立 greedy 候选，不修改主线定义。

现有 [CPU ROAM 执行与特征分析研究定义](../plans/formal_experiment/cpu_roam_research_definition.md)继续有效。本目录记录独立探索，不自动修改研究问题、启动实现或重开历史实验。

2026-09-14 的[全局优先级事务化 CPU LOD 原型前大规划](../plans/cpu_refinement/greedy_multipass_preprototype_plan.md)已按用户认可的范围闭环，当前状态见下文 GMP-01～04。研究具体聚焦逐修改 greedy 反馈与拓扑写入的解耦，保留批次之间的反馈；multi-pass 是执行形式，一般 one-ring 是提高预算需求兑现能力的原语。立项时尚无自然数据，四阶段依次冻结快照顺序预留及 P/G/C、完整事务读写/续接义务、小规模自然兑现与工作量审计、原型准入；实际冻结四个快照。不直接实施持久算法，也不把局部重剖分转为独立论文主线。

该大规划及相关记录已按用户要求先提交为 `1b93ca8`。[GMP-01 小规划](../plans/cpu_refinement/gmp_01_execution_contract_plan.md)完成[执行契约](cpu_refinement/greedy_multipass_contract.md)、七类手算与[实施审查](../reviews/cpu_refinement/gmp_01_execution_contract_review.md)，冻结有限接收目录、P/G/C/E、样本归属、共同预算需求与预留规则。用户授权按“完成一个小阶段、验证审查、单独提交、再开始下一阶段”自主闭环，不再逐次请求确认；原型与生产接入边界保持。

GMP-01 已提交 `64b00aa`，GMP-02 已提交 `e01cd06`，GMP-03 已提交 `ed277cf`。[事务组合与续接推导](cpu_refinement/transaction_batch_derivation.md)保留共享接口边和外部 owner 反例；四个冻结自然快照的前缀审计得到[一般回收交换数 21/23/0/1](cpu_refinement/transaction_backend_gate.md)，能有限应用和生成下轮需求。固定视域屏幕误差不增，但部分全域高度误差明显上升；总体兑现率、长期质量、持久局部维护与 CPU 加速均未成立。

[GMP-04](../plans/cpu_refinement/gmp_04_prototype_admission_plan.md)随后完成[近邻方法对照](cpu_refinement/transactional_lod_prior_art.md)与准入矩阵，**原型前 Gate 为 PASS，大规划闭环**。[下一持续原型 Major Plan](../plans/cpu_refinement/greedy_transactional_cpu_prototype_plan.md)已获自主实施授权：直接串行内核 → 局部续接/高度策略 → 动态参考/短轨迹 → 有限多核，每小阶段完成审查并提交后才进入下一阶段。评审后已明确 P/receiver evidence、高度自由度与跨批资源生命周期；全 Q 高度保护只作保守对照，持续视域恢复尚未验证。21/23 是自然批次存在性证据，不是持续批宽或多核收益，生产开关不在本规划范围。

[GTP-01](../plans/cpu_refinement/gtp_01_serial_kernel_plan.md)已完成单批 C++ 内核；[结果](cpu_refinement/gtp_01_serial_kernel_results.md)为 19/21/0/1 个交换及 Peking14 两次空额度细分，独立局部精确认证和反序通过。重心舍入的最小角边界差异已逐提案解释；尚无正常局部样本/mesh 续接或真实多线程，本阶段提交后进入 GTP-02。

MPR-02 已提交为 `d635a9b`。[串行成本分析](roam_parallelism/target_materialization_serial_costs.md)与[MPR-03 小规划](../plans/roam_parallelism/target_materialization_prototype_03_plan.md)记录局部向量/记录句柄优化，完整状态与续接核查保持。新增压力样本 k=27,783，两组快验中串行物化约 332→240 ms、四线程约 226 ms；正式重复矩阵已按用户要求停止。小输入仍只有单次诊断，性能风险与统计限制见[结果 §14](roam_parallelism/target_materialization_prototype.md#14-mpr-03-开发快验结果与停止记录)，不宣称稳定多核加速或全部输入性能验收。

用户随后要求直接替换原 DOD 细分拓扑阶段，并保留原／新实现选择。[原生接入大规划](../plans/roam_parallelism/native_materialization_plan.md)已获认可；[NMP-01 小规划](../plans/roam_parallelism/native_materialization_01_plan.md)现已实现独立严格逻辑规划、封闭 Δ/Γ、完整决策轨迹和三点现实核查。有限输入内，第一箭头不需要生产 mutation、全状态复制或旧 pass 产 J；完整目标/队列/历史/预算与 Legacy 一致，正常出口没有目标关系或 heap 布局。MPR 继续保留为独立物化参考，关系恢复协议的实际生产充分性仍留 NMP-02。

NMP-01 不能合并成一个无保留的成功结论：原压力点旧节点覆盖8.393%，合并堆写/读覆盖57.594%/76.035%；原Tplan约1088.565ms，同轮Legacy约61.630ms。有序覆盖成本高不反证语义解耦。NMP-01P已从覆盖/堆/缓存优化继续扩展到连续决策堆、四字段成员投影和局部访问复用；当前三点完整轨迹一致，压力Tplan约133.892ms、同轮Legacy61.801ms，仍未达到80%门槛。节点页初始化与成员/堆的O(N+Q)准备完整计费，七字段拓扑投影因无可靠总收益撤回；详细成本和空间交换见小规划§9.13。用户已授权继续按热点调整方案，固定优化包数限制已撤销；50%目标保持，未开启NMP-02。此前test129微秒波动不重开调查。

| 方向 | 当前状态 | 范围与记录 |
| --- | --- | --- |
| activation-spectrum 增量维护 | **No-Go，关闭** | 保留[理论 Gate](roam_threshold/incremental_gate.md)及固定依赖证明；不继续实现或扩展推导。该结论不是对所有隐式增量算法的不可能性证明 |
| recourse / minimum-change | **有独立研究价值，暂存** | [暂存记录](roam_recourse/minimum_change_note.md)保留问题定义、等成本公式和质量—修改量交换；优化工作量，不作为并行结构问题的答案 |
| parallel-first CPU refinement | **继续开放，独立 greedy 候选继续推导** | [局部预算交换候选](cpu_refinement/greedy_local_exchange_candidate.md)、[自由高度](cpu_refinement/local_height_minimax_derivation.md)及[有界补丁](cpu_refinement/bounded_patch_refit_derivation.md)之后，[一般当前回收](cpu_refinement/cavity_budget_recovery_derivation.md)给出局部质量递推、完整 16 面预算交换正例和类别内形状/质量否证；过程与脚本输出留存，一般可达性、新颖性与完整收益未成立 |
| 必要依赖与并行上界分析 | **首轮审计完成，暂不建设分析器** | [审计报告](roam_parallelism/necessary_dependency_audit.md)区分逐菱形发布链与联合发布反例；尚无覆盖允许变换的非平凡长链证书，不否定所有后续分析可能 |
| Work–Inflation Gate | **保留草案，暂缓新增采样** | [测量契约](roam_parallelism/work_inflation_measurement_contract.md)与[小规划](../plans/roam_parallelism/work_inflation_gate_plan.md)保留已有能力和缺口；根据后续讨论，优先规划参数化成本语义 Gate，不按本草案启动插桩或采集 |
| 参数化成本语义 Gate | **本轮 No-Go，PCSG-01 提前结束** | [报告](roam_parallelism/parametric_cost_semantics_gate.md)保留评分/脏写分区等价和固定目标叶集合对应；拓扑的连接、索引及完整状态成本未封闭，按[小规划](../plans/roam_parallelism/parametric_cost_semantics_gate_plan.md)停止，不创建 Lean 框架或新算法 |
| ROAM 目标差分物化 | **获得强验证，可以继续** | [理论报告](roam_parallelism/target_materialization_gate.md)保留构造及 Lean 引理；[原型实施记录 §10–12](roam_parallelism/target_materialization_prototype.md#10-mpr-02-实际执行边界)提供完整直接物化、续接、差分局部性与真实并行证据。用户认可算法可行性并授权继续规划性能兑现；当前有限实现未取得稳定整体加速，原始性能及目标发现/外围成本继续保留 |
| CPU ROAM 执行与特征分析 | **现有主线保持有效** | 复用既有多阶段拆分、CPU 并行实现和实验能力；新算法候选若未形成有实质区别的结构，研究投入回到此主线 |

此前表示层探索的母问题是：能否设计天然适合共享内存 CPU 并行的地形细分模型，同时尽量保持 ROAM 的硬三角形预算和几何质量特性？其记录保留，尚无新机制准入。本轮必要依赖审计已完成并按证据不足的停止条件结束取证；用户关于停止新算法探索的表述曾明确为讨论，不追认为关闭全部候选的决定。

后续状态以[契约审计](../reviews/roam_parallelism/native_planner_contract_traceability_audit.md)、[候选出生证明](roam_parallelism/native_candidate_birth_proof.md)及[两项工作削减推导](roam_parallelism/native_target_discovery_reduction_derivation.md)为准：严格请求的条件队列替换与在线输出归约有明确结论，但主要原生维护删减和 2～5 倍发现收益未成立。上文 NMP-01P 的优化授权与数值保留为历史记录，不代表当前继续实施微优化。

用户现明确要求探索自己的 greedy CPU 拓扑算法。[新候选](cpu_refinement/greedy_local_exchange_candidate.md)自己决定局部修改，不需要 Legacy 给出 J。后续 §8～11 给出 bilinear 停滞反例、见证插点的针对性修正、先配对再选独立交换的低 span 构造，以及连续证书的显式成本边界。选择可并行不代表质量会持续改善或有限核 CPU 更快；[自审](../reviews/cpu_refinement/greedy_local_exchange_candidate_review.md)没有批准实现或宣称原创算法成立。

最新[自由高度附页](cpu_refinement/local_height_minimax_derivation.md)补齐指定误差的区间判定、严格进展/不能改善证据，以及有效深度域内准确透视误差的线性可行性。它同时暴露“自由拟合后冻结旧顶点”的误差下限；3→5 有界星形共同拟合仅为待审修正，不自动继承旧回收与冲突结论。没有新增生产求解器、并行设施或性能承诺。

后续[完整补丁审计](cpu_refinement/bounded_patch_refit_derivation.md)进一步排除三价中心限制和历史高度回滚，给出受控完整邻域双高度拟合、当前三价回收及重新计费的条件选择界。固定采样、冻结视图和正 η 下可证明接受交换有限终止，但 8 面形状反例说明终止可能是不良停滞。推导假设、被排除的规则、证明步骤、有限几何与组合检查均留存；没有新增 C++、运行性能矩阵或提交 Git。

最新[一般当前邻域回收](cpu_refinement/cavity_budget_recovery_derivation.md)把候选扩展到 d→d−2 的局部重剖分。已有构造能在 16 面、预算不变、同一角度约束下将连续最大高度误差 1→1/4，但另有六面邻域证明全部固定环重剖分都违反形状限制。局部 DP 可用于区分启发式漏解与类别内不可行，三角形证书成本必须单列；删点、独立集并行和多边形 DP 的历史先例已经明确，尚无新颖性准入。

此前理论轮次不扩展 recourse、加权修改量、滞回或动态一致性算法，也不恢复旧 closure/batching、CPU-CBT 或 GPU 路线；后续获批的 MPR-01 只实现独立实验原型，不增加生产反向依赖。必要依赖审计与工作膨胀测量草案已提交为 `24c6391`；参数化成本语义 Gate 的规划、报告、自审及索引已按用户许可提交为 `69ab803`。

用户随后确认实施目标差分物化理论小规划。在固定视图/轮次和已知合法目标下形成完整抽象状态构造：合并候选复核范围至多 2k，工作界包含差分、深度、有序索引和几何查询费用。支持集及队列等式完成 Lean 检查，其他部分的纸面范围见[自审](../reviews/roam_parallelism/target_materialization_gate_review.md)。该结果不追认为 PCSG 已实施内容，也不等于严格 greedy 目标发现或原生 C++ 已并行化。理论与原型规划已提交为 `1e05b24`；工作膨胀测量继续暂停。

用户进一步确认原型大规划与 MPR-01，并要求先提交再继续；已按此顺序完成。原型采用独立状态、同目标逐菱形参考、直接物化与 DOD 来源桥接，不写回生产 DOD。Peking 的旧非空 Pending 得到保留和消费；自然任务混合增删、预算和全部队列对应成立。标准容器维护、导入／认证／恢复／换轮费用保留，不能只引用事务时间宣称端到端收益。用户随后明确授权提交实现及[阶段自审](../reviews/roam_parallelism/target_materialization_prototype_01_review.md)，并进入 MPR-02 小规划；具体并行边界仍须先 Review，不直接开始多线程修改。

MPR-01 已按许可提交为 `d876cea`。用户随后确认 [MPR-02 小规划](../plans/roam_parallelism/target_materialization_prototype_02_plan.md)，已完成独立顺序参考、共同串行/并行准备路径与薄同步适配器。三段并行边界保留唯一所有权，标准容器与发布仍串行；完整状态与五进程成本对照齐全，尚无稳定的多核事务净收益。生产 P95 仅作一次触发复测，未确认版本相关退化；[阶段自审](../reviews/roam_parallelism/target_materialization_prototype_02_review.md)保留证据边界。用户进一步明确“获得强验证，可以继续”，授权提交现有实现，再规划串行成本、工作量和预期收益；不直接扩展新表示或目标发现。
