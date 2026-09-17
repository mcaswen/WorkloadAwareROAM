# QPC-04H：源高度接收提案与旧 Fit 移除消融

2026-09-17。类型：QPC-04 下的算法小规划。基线：`c00ca8a`（QPC-06E）。**已实施并完成有限闭环：结构/证书证据成立，完整降本门禁未通过；不改变默认政策，不进入下一阶段。**

本编号专指本次 E/F/H 提案生成政策。此前未提交的“04H 认证降本”草案已并入 QPC-06A，本次不恢复该旧任务，也不把算法改动记作 06 的同结果优化。

## 1. 问题、目标与当前依据

用户希望评估：新逐点质量政策是否还需要先执行旧 Fit。当前 E/F/H 在旧曲面上生成新点，再由 `TransactionalCertification::Fit` 按旧统一最大误差目标调整高度，成功后才进入新逐点认证。04G 已发现新契约允许、但旧门槛排除的高度；06E 后旧 Fit 也仍是实际热点。

但直接删除调用不足以形成替代算法：未拟合的新点通常仍在旧曲面上，只增加面数、不产生真实几何改善；逐点认证还依赖合法目录与固定接口前提。本轮验证一个最小替代：**保持目录中的参数坐标和连接，把唯一新点设为源高度，经过必要结构检查，再直接接受新逐点政策的完整认证。**

需要回答三个问题：

1. 能否在不依赖旧 Fit 的情况下，构造并发布满足现有逐点损伤/进展条件的 E/F/H？
2. 删除旧 Fit 后，实际减少的工作是否超过新增逐点认证及后续事务工作？
3. 在同 seed 的持续轨迹上，独立质量与完整成本是否形成值得保留的候选？

依据：[04F 事实](../../codebase/cpu_refinement/qpc_04f_target_quality_facts.md)、[04G 事实](../../codebase/cpu_refinement/qpc_04g_proposal_feasibility_facts.md)、[06E 报告](../../research/cpu_refinement/qpc_06e_face_evidence_results.md)及[大规划](transactional_quality_performance_closure_major_plan.md)。已阅读开发/规划规范，并核查 Proposals、Certification、PointwiseQuality、BoundaryRefinement、FlipRecovery、Reservation、Commit、配置和实验映射。

06E 的 Windows P 移动完整均值为 Sierra 55.4581 ms、Canyon 53.7852 ms；Linux 为 34.9432/35.7365 ms。Sierra 诊断中旧 Fit 仍有 28,606 次调用。perf 包含份额和跨线程 Tracy 区间之和不能换算成完整帧节省承诺。

## 2. 范围与政策契约

### 2.1 两个对照分支

| 分支 | 配置与提案路径 | 用途 |
|---|---|---|
| P0 | 原 `pointwise-target + error-first`，E/F/H 继续旧 Fit，再逐点认证 | 当前同版本直接对照 |
| PS | P0 的请求/目录不变；仅 E/F/H 使用源高度生成，再逐点认证 | 本轮算法候选 |

拟新增 `TransactionalReceiverHeightPolicy { LegacyFit, SourceHeight }`，字段 `ReceiverHeightPolicy`，默认 `LegacyFit`。实验 JSON 使用 `receiverHeightPolicy: legacy-fit / source-height`。`SourceHeight` 只允许 `PointwiseTarget + ErrorFirst + PreserveSurvivingHeights=true + HeightGuard=false`；非法组合明确拒绝。

政策变更走现有 Reset，重新从同一冻结 seed 开始；不得在 P0 已演化状态上热切换。配置身份、实验任务身份、追溯绑定和证书绑定包含该字段。旧配置省略字段时仍走原路径，保留旧任务身份兼容性。

### 2.2 精确替换范围

- 仅 E/F/H 的唯一新点可改高，旧点的参数坐标、高度和身份不变。新点取 `SourceHeightAt(u,v)`，按真实 binary64 存储并接受实际 float 输出域核查。
- 保持同一根、游标目录顺序、八项身份上限、参数坐标、连接方式、支持和形状阈值。H 与其他项可能重复，也不在本轮去重。
- B 单侧边界、R 翻边及 donor 保持既有构造与认证路径。本轮不是“删除全部旧认证”，其 `SetProgressTarget/Measure/Accepts` 成本继续单列。
- 新分支不执行旧 Fit、不执行 E/F/H 的旧统一最大值接受门槛，也不因源高失败回退旧 Fit、投影到可行区间或搜索其他高度。
- 双域、完整闭支持、离屏高度损伤、正进展、精确回退及发布前绑定均保持。源高度只是一个候选，不是质量证书。
- 无源高合格项时按原目录继续；全部失败则如实拒绝。保留原形状失败触发翻边的定义，不能把新的质量失败改算成形状失败。

不改变请求资格、优先级/稳定排序、前缀、捐赠池、预算命名与不回流、冲突策略、线程数、空批缓存语义和提交/续接机制。不设计新求解器、原语、通用验证器或 profiler；不扩大地形/规模矩阵，不恢复 ATT。

## 3. 架构、文件归属与接口

算法源码仍位于 `src/algorithms/greedy_transactional_lod/`。源高度准备独立成组件，避免把高度政策的数值与结构逻辑塞入 Reservation；用枚举显式选择，不引入策略类继承体系。

| 判断 | 文件 | 职责与边界 |
|---|---|---|
| Create | `TransactionalSourceHeightReceiver.h/.cpp` | 从只读快照和已有 E/F/H 提案准备源高度候选；检查目录结构前提与唯一新点，不作质量接受、预算或预留 |
| Extend | `TransactionalReservation.cpp` | 在原接收根任务中选择旧认证入口或新准备入口；新准备成功后必须调用原逐点认证，仍按根序收集首个合格项 |
| Reuse | `TransactionalProposals.*`、`TransactionalSamples.*`、`TransactionalPredicates.*` | 原游标、源高插值、形状判据及局部取证；不复制整个目录生成器 |
| Reuse | `TransactionalCertification.*` | P0、B/R/donor 的原行为保留；PS 的 E/F/H 不再调用 Fit，不删除旧实现 |
| Extend | `TransactionalLodSettings.h`、`TransactionalTypes.h`、`TransactionalSeedBuilder.cpp`、`TransactionalPipeline.cpp` | 公共/核心政策、映射、非法组合与 Reset/SetView 边界；不由数值组件读取 JSON |
| Extend | `TransactionalPointwiseQuality.cpp` | 只补配置/证书身份的绑定核查；损伤、进展、样本支持和数值接受式不改 |
| Extend | `src/experiment/infrastructure/ExperimentCase.h/.cpp`、`configs/experiments/schema/case.schema.json` | 实验字段解析与校验 |
| Extend | `src/benchmark/experiment/ExperimentReplay.cpp`、`TransactionalRecoveryTrace.cpp`、`src/experiment/greedy_transactional_lod/TransactionalQualityProvenance.cpp` | 沿现有入口映射同一个设置，正常平台与只读追溯不得产生隐含不同政策 |
| Extend | `scripts/experiment_infrastructure/catalog.py`、`runner.py`、`suite.py` | 解析/冻结/任务身份及非事务算法字段过滤；不增加新的运行框架 |
| Extend，必要最小范围 | `TransactionalTypes.h` 中 WorkLedger、`TransactionalExecution.cpp` 的账本归并，以及实验层 `TransactionalExchangeQualityAudit.cpp`、`TransactionalProposalFeasibilityAudit.*` | 新分支调用/失败计数、政策绑定与无旧 Target 的明确标识；复用既有局部输入/质量审计，不另建账本组件 |
| Create / Extend | `tests/TransactionalSourceHeightReceiverTests.cpp`、相关配置专项、`cmake/TransactionalLod.cmake`、`tests/CMakeLists.txt` | 定向验证结构前提、认证不可跳过、旧分支兼容及组件登记 |
| Create | `scripts/analyze_transactional_source_height.py` | 只读归约政策差异、质量、工作和时间；复用共同 runner/evaluator，不复制采集器 |

依赖为 `Reservation → SourceHeightReceiver → Samples/Predicates`；质量认证仍由 `PointwiseQuality` 负责，生产算法不依赖实验层。源高组件只修改调用者拥有的私有 Proposal；不持有跨提案、跨线程或跨帧可变缓存。

新接口表达“结构准备成功/失败”，不能在完成逐点认证前把提案标成可发布。建议 `Prepare(state, samples, proposal, work)` 返回准备结果；Reservation 只做分支编排。既有 P0 调用、计数和数值顺序保持；PS 最终 `Reason=certified` 只能来自完整认证成功。

准备必须检查：类型为 E/F/H、目录身份与支持有效、唯一自由度确为新点、旧点不变、新点坐标/高度有限且在现有范围内、原形状条件和净 +2 面关系成立。利用合法游标的构造保证与有限检查，不重新运行任意网格全验证器；Commit 和 SameInterface 的现有检查继续保留。

实施前列全 `TargetMicropixels/ErrorLower/ErrorUpper` 消费点。PS 不产生旧门槛；禁止用默认零值伪装旧证书，禁止下游偷偷读取它决定配对。当前 P 的配对已经不读旧 Target，但诊断、追溯与输出也须明确“不适用”。如果发现生产路径确实依赖旧目标，先补齐本节契约，不直接绕过检查。

## 4. 形式推导任务与源码义务

推导写入 `docs/research/cpu_refinement/qpc_04h_source_height_derivation.md`，保留定义、逐步推理、失败尝试和反例；实施后更新推导索引与数学总览。本轮复用既有逐点损伤理论，不为了计数再建 Lean 项目。

### SH-01：候选构造与局部合法性

固定快照 M、合法目录项 T、唯一新点 v 和源高度场 H。定义源高候选只做 `h(v) ← fl64(H(u_v,v_v))`，其他点及连接保持。证明旧接口不变、参数域覆盖/形状不因新高度改变、面数成本仍为 +2；明确这些结论依赖原目录合法，不能从“高度来自源”独立推出。

连接、邻接、样本、网格及 Pending 的续接仍由现有提交链负责。纸面局部保证、真实存值和输出量化的检查分别列出；不把 H 的数学值与存储值混同。

### SH-02：新政策安全性不依赖旧 Fit

把既有损伤条件和正进展条件的前提逐项映射到新路径：结构准备、完整闭支持、两表示域、现有阈值、数值证据、共享样本、批次验证及代次绑定。只有全部齐备，才能沿用认证推出的结论。

关键证明不是“源高一定安全”，而是“任何由源高生成且通过现有认证的提案，满足同一声明契约”。应保留源高改变邻域插值、导致其他样本变坏而被拒绝的反例。不扩大到连续地形、未来视图或有限时间恢复。

### SH-03：可行域和进展边界

在同一目录项上比较三个候选：旧曲面插值高度、旧 Fit 输出、源高度。理想算术下前者通常仅分割同一曲面，不能当恢复；数值极小正进展仍单列，不升级为有效改善。

旧 Fit 有解不保证源高度合格，源高度合格也不保证通过旧统一门槛。04G 的 28 个见证是“存在某个高度”，不等于源高度这一个试值可用。失败只能否定当前单点生成器；本轮不把有限试值失败写成高度可行域为空。

### SH-04：工作变化与完整成本

设本轮访问目录项数为 v，项 i 的面数为 d_i、完整认证样本数为 s_i。源高赋值使用常数个原始采样值；局部检查访问 O(d_i) 条几何记录，当前 State 的身份查询和局部有序容器使实现成本为 O(d_i log N + d_i log d_i)，不能把访问次数直接当成时间复杂度。后续覆盖/认证仍可能 O(s_i d_i)，精确算术另受位长影响。

对同一冻结输入，分别记账：

\[
W_{P0}=\sum_{i\in V}W_{Fit,i}+\sum_{i\in A_{Fit}}W_{Q,i}+W_{downstream,0},
\]
\[
W_{PS}=\sum_{i\in V'}W_{source,i}+\sum_{i\in A_{structure}}W_{Q,i}+W_{downstream,S}.
\]

首个成功项可能变化，因此 V 与 V' 未必相同；结构通过集也可能远大于旧 Fit 成功集。必须分析删除旧筛选后增加的认证、实际事务和续接成本，不能只令 `W_Fit=0` 计算收益。持续轨迹状态不同后的总时间属于政策比较，不称同任务并行加速。

## 5. 实施顺序与有限实验

### 5.1 先证明入口完整，再接入

1. 保存 `c00ca8a` 源码/二进制/配置身份与 06E 原始数据，登记所有旧认证输出消费点，完成 SH-01/02 的前提表。
2. 实现源高组件、显式政策及最小实验映射；先跑定向结构/双域/配置测试，确认没有“准备成功但未认证就发布”的入口。
3. 复用 04G 已保存的 43 项局部输入，固定各自快照和目录，只增加源高一个试值。按形状失败、结构成功但逐点失败、双域成功、仅极小进展分类；不使用未来视图或离线可行区间选择生产高度。旧捕获缺失时先报告缺口，不能把 PS 新轨迹冒充同快照对照。
4. 对有代表性的源高通过/拒绝项运行原生私有 Proposal 复核。若没有可发布项，仍保留分类结果；不立刻增加第二个试值或新求解器。
5. 结构与证书前提成立后，运行下述两条持续轨迹，独立评价与计时分开。完成推导、工作/质量/成本报告及架构审查，交本阶段验收。

### 5.2 冻结输入、基线与性能

- 场景沿 06E 的 Sierra/Canyon；预算 50k、r=m=160、8 线程、96 个机会、暖段从 frame3 开始。固定原相机、seed、源、Q、阈值、翻边及边界设置；Sierra 实际仅约 9千～1万面，不称为饱和 50k。
- P0 为本阶段相同构建上的旧 Fit 路径，PS 只更换上述 E/F/H 生成器。历史 L 仅作背景，不新增 C/L/DOD/CBT 全矩阵。
- Windows D3D12 普通运行是完整性能主对照，每场景 P0/PS 各一个独立进程；06E 历史基线另列，不覆盖。Linux 做必要构建/专项和一个有限诊断，不复制平台性能矩阵。
- 正常计时关闭质量捕获、完整审计、perf 和 Tracy；保持 CPU-ready/阶段计时边界，报告移动 frame3～31、返回/静止及完整轨迹的均值、中位、P95、实际 N 和工作量。
- 复用 06E 的旧热点证据；只为 Sierra PS 补一次 perf、一次 Tracy 及同构建未连接对照，核对成本是否转移至逐点认证、预留或续接。不新增逐样本 zone。
- 记录 E/F/H 的 Fit 调用、源高准备、结构拒绝、逐点调用/样本访问/精确回退、首次成功目录项、实际交换、配对与续接；B/R/donor 的旧工作单列。仅必要汇总计数，关闭诊断不做额外几何求值。

### 5.3 独立质量、续接与视觉

P0/PS 各自从同 seed 真实持续运行。复用共同捕获/evaluator，在每条轨迹的 **frame2、16、48、95** 比较独立 sampled Emax、参数域 RMS、全域 Hmax、逐点 Dmax 和误差分布；参考仍为原始 samples + bilinear。PS−P0 与已有 DOD 诊断口径分开，不能把不同网格的逐点差混成绝对误差。

源高改变实际曲面，不能沿用 06E 的“不变曲面免重评”。逐帧保留请求、失败理由、事务数、面数、状态/网格身份及停滞长度；四个独立评价帧不等于整段或连续曲面质量证明。

复用交换审计，在每场景首个含 E/F/H 的非空批及 frame48 的非空批核对双域损伤/进展与组合；重合只查一次，空批如实记缺失。该取样只用于操作覆盖，不选择最好结果。使用现有有限记录上限，截断记未知，不用部分记录判通过。

复用平台图像捕获，在 frame16/48 检查裂缝、折叠、尖峰和边界连续性，Agent 实际看图；用户视觉签收单列。不重建视觉工具、不扩大后端。若几何/样本/缓存续接失败，停止自然采样并保留首个反例。

### 5.4 验证配额

定向测试覆盖：非法政策组合、旧点不变、真实源高存值、退化/形状失败、源高局部退化被拒、核心通过但 float 失败、无正进展、证书缺失/过期/字段篡改、混合 B/R/捐赠预算、后续 Plan/Apply/Reset 与空批缓存。复用现有逐点、核心持续、配置和空批专项，不跑全 CTest。

旧分支测试与自然轨迹必须保持原逻辑输出；新分支允许改变轨迹，但所有安全条件不减。未触及渲染接口，不重跑两个后端完整矩阵。

先冻结可执行命令、程序身份和 caps 再采集；正常单进程沿 180 秒/8 GiB，独立质量进程沿共同 evaluator 的既有配额。预计正常/诊断采集 15 分钟内，独立评价与局部有理审计 20 分钟内；达配额保留未知，不追加轨迹/规模/线程。只有具体回退才按规范 `max(0.05ms,5%,已知波动)` 对受影响旧/新配置至多复测一次。

## 6. 门禁、停止条件与交付

| 门禁 | 判定与范围 |
|---|---|
| 结构/契约门禁 | PS 的 E/F/H 确实不调用旧 Fit；没有质量旁路/隐式回退；双域、预算、固定旧点、续接与证书绑定成立。失败即停，不以性能抵消 |
| 旧路径兼容 | 默认、P0 及未更换的 B/R/donor 规则不变；P0 对 06E 的逻辑结果保持。若仅共享框架引入性能退化，独立分析后处理 |
| 有限独立质量筛查 | 预注册工程警戒线：任一所选帧 `Emax_PS > Emax_P0 + 0.1px` 则不判联合收益通过；0.1px 不是视觉阈值或数学保证。RMS/Hmax/分布仍须完整审查，最大值未触线不能掩盖新增局部尖峰 |
| 成本候选价值 | 目标为至少一个场景移动完整均值下降 10%，另一场景无超过工程门槛的持续回退，并保留全部质量/实际N/事务分母。不是收益承诺，也不是同任务加速门槛 |
| 持续恢复/生产推荐 | 本轮有限结果不能签发；既有 seed 残差、目录阻塞和长停滞单列，默认不晋升 PS |

零事务、仅舍入级进展或较少有效更新得到的快时间，不单独算成功。若安全成立但源高度频繁不可行、逐点认证人口放大或独立质量变差，本轮可完成并结论为“不保留当前源高候选”。若只改善质量而不降本，单列质量候选价值，不宣称性能修复通过。

不在本阶段追加可行域投影、一维搜索、回退 Fit、放宽进展/损伤/形状、扩大前缀或更多恢复机会。它们需要本轮证据支持的新决策；源高单点失败不能反推“去旧 Fit 不可能”。

交付路径：

- 本规划实施记录及大规划状态。
- `docs/research/cpu_refinement/qpc_04h_source_height_derivation.md` 与 `qpc_04h_source_height_results.md`：证明过程、反例、逐层工作变化和质量—成本结论。
- `docs/codebase/cpu_refinement/qpc_04h_source_height_facts.md`、`docs/reviews/cpu_refinement/qpc_04h_source_height_review.md`：实际依赖/调用、Critical/Major/Minor 审查及性能限制。
- 原始数据留忽略目录 `benchmark-output/cpu-refinement/qpc-04h/`；提交用精简归约留 `docs/research/cpu_refinement/data/qpc_04h_source_height/`，保留源码/程序/命令身份。
- 推导索引及数学总览登记纸面、有限检查和未证明边界。

## 7. 实施结果与对照核查

已新增源高组件、显式配置与结构准备入口，默认旧路径保留。生产 P 配对不读取旧 Target，诊断输出明确标记源高项没有旧证据。旧04G可行域捕获只接受LegacyFit配置，源高试值直接复用其冻结数据，避免更改历史绑定。

已完成源高专项、原逐点专项及核心持续测试；43项冻结输入得到27项双域通过、11项质量拒绝、5项形状拒绝。27项均非低于1e−12的极小进展，但局部最大值均不变；E/F/H三个源高见证经原生复核通过新条件、拒绝旧门槛。两场景首非空批及frame48共127项非空事务的双域损伤/进展和组合核查通过，Canyon48为空如实保留。

首测PS移动成本高于P0，同时本轮P0也比06E历史绝对时间高约30%。按§5.4/开发规范7.3，对两场景各追加一次PS/P0定向复测，并在同一窗口各运行一次已冻结06E原程序，区分政策工作变化与机器/构建差异；首测全部保留，不择优替换。不扩展其他场景、线程或政策。

独立质量四帧未触发Emax的0.1px警戒线，RMS有所改善，但Sierra/Canyon的PS−P0局部Dmax最大分别0.3674/2.0174px。绝对质量、误差分布、实际N和费用分别报告，不提升为持续恢复通过。

Windows首测移动均值P0→PS：Sierra73.219→171.609ms、Canyon70.000→103.719ms；复测72.261→178.368ms、73.339→107.108ms。未达到降本目标。Sierra的旧Fit调用归零，但逐点调用从06E诊断1,357增至11,234，样本修复也随更多实际细分增加。原因、时序和复杂度见[报告](../../research/cpu_refinement/qpc_04h_source_height_results.md)。

最终审查修正了面数检查遮蔽形状失败的问题，保持原翻边资格。修正后定向重跑源高专项及两条正常PS轨迹，全部96帧导出逻辑列与修正前一致；最终PS移动173.604/108.797ms。已有独立质量/视觉对应相同网格，按规范复用，不重跑完整矩阵；此前perf/Tracy注明采集版本，不冒充最终重新采样。

P0与历史06E的逻辑输出一致；旧程序同窗口重跑也变慢，不能把所有历史差值归版本。但Sierra当前P0仍比同时段旧程序高7.12%，原因未定，不能签发默认路径无性能影响。作为阶段限制和独立性能问题留在审查中，不在04H无限追加优化。

文件职责与§3一致：准备、数值证书、编排、诊断分开，未改渲染接口或新建缓存系统。源码/程序/数据身份、精简归约与图表、纸面SH-01～04、代码事实和[阶段审查](../../reviews/cpu_refinement/qpc_04h_source_height_review.md)均已补齐。

观测范围限制：已保存成功事务类型、冻结输入的目录ordinal、每帧失败/工作账本；没有扩展全96帧每个未获批根的首成功ordinal明细，不以汇总理由代替完整调用数。调用增长由一次Sierra Tracy直接验证。Agent已查看P0/PS两场景frame16/48，未见新增可见裂缝或大尖峰，用户视觉签收未代办。

阶段结论为“源高单点不保留为性能替代候选”；显式分支仅留作可复现实验，默认LegacyFit不改。没有追加可行域求解、回退Fit、放宽质量、扩大前缀或新场景；本阶段交付后停止。
