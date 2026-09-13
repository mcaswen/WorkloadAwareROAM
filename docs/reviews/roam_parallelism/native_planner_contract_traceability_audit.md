# NMP 形式化定理与运行时义务追溯审计

> 日期：2026-09-13；审计基线：`c36286f`；类型：契约、源码和既有证明的定向审计。
> 状态：审计完成；暂停继续扩展 NMP-01P 性能修改。现有严格参考、夹具、轨迹和性能门槛保留；E1 仍是待确认的后续契约，不在本轮改写实现。
> 本文由同一 Agent 核对，不是独立同行评审。没有新增 Lean 定理、运行时代码、测试或性能采样。

## 1. 审计出口

**确认：当前 Lean 证明的是给定端点下的局部队列修复，不要求 Planner 重现 Legacy 的逐步决策。** 但不能进一步推出“现有 Planner 的全部中间维护都不必要”，原因有二：

1. NMP 后来增加了原生目标发现任务，目标明确取 Legacy 动态控制器的终态；该目标不是调用方已经免费提供的输入。
2. Lean 的目标状态采用净差分 `TargetMarks`，原生终态还包含阻塞、失败移除、历史轮次、强制激活和缓存存在性等义务。`J` 相同不自动使这些义务相同，现有 Lean 也没有直接覆盖全部原生队列。

因此，本轮不继续微优化，也不把已通过的测试倒推为算法定理。建议后续先冻结原生端点投影与队列支持集，再讨论 E1 或观察点之间的维护合并。给定目标的物化结果、目标发现方法、当前实现的维护成本分别评价。

### 1.1 分类与用语

| 分类 | 本文含义 | 不代表什么 |
| --- | --- | --- |
| A | 抽象物化规格中的输入前提、定义或已证后置条件；标明机械证明与纸面证明 | 不是所有正确 ROAM 算法都必须采用同一过程或数据结构 |
| B | 原生目标发现的已确认契约，以及当前用于核对它的独立 Legacy oracle | 不是由 Lean 推出的必要步骤，也不是所有同终态算法的下界 |
| C | 开发期加强验证、定位分歧或表示实现的检查 | 不自动成为未来算法的外部语义 |

“数学上必需”须相对于明确规格解释。一个局部修复构造的充分条件，不是所有求解方法都必须履行的操作清单。表示不变量在实际读取/公开边界可能仍是内存安全与正确性要求，不能因属于 C 就任意破坏。

### 1.2 证据索引

| 编号 | 已核对文件与位置 | 证据职责 |
| --- | --- | --- |
| L1 | [StateModel.lean](../../research/roam_parallelism/formal/materialization/StateModel.lean)，`Changed/Born/Leaf/LeafSupport/MergeReady/MergeSupport/Marks/TargetMarks/SplitEntry/MergeEntry/Repair` | 实际抽象定义和自由参数 |
| L2 | [Locality.lean](../../research/roam_parallelism/formal/materialization/Locality.lean)，全部 12 项 theorem | 实际定理前提、结论和证明体 |
| P1 | [理论报告](../../research/roam_parallelism/target_materialization_gate.md)，§2–7、§9–10 | 独立顺序规格 R、历史边界、纸面续接与成本 |
| P2 | [MPR 大规划](../../plans/roam_parallelism/target_materialization_prototype_plan.md)，§1、§3；[原型记录](../../research/roam_parallelism/target_materialization_prototype.md)，§1–5 | 给定目标的原型任务与实际适用范围 |
| N1 | [NMP 大规划](../../plans/roam_parallelism/native_materialization_plan.md)，§1、§3、§6–7 | 新增目标发现任务、封闭 Γ、严格轨迹要求的来源 |
| N2 | [NMP-01 小规划](../../plans/roam_parallelism/native_materialization_01_plan.md)，§4–6、§11；[NMP-01P](../../plans/roam_parallelism/native_planner_performance_plan.md)，§1、§6、§9.4、§9.13 | 已确认的实现验证口径、当前优化及计费 |
| F1 | [接入事实](../../codebase/roam_parallelism/native_materialization_integration_baseline.md)，§2–4、§9、§11–12 | 原生续接差异、夹具范围和当前实现 |
| C1 | [NativeTargetPlanner.cpp](../../../src/algorithms/data_oriented_roam/materialization/NativeTargetPlanner.cpp)，`Build/Extract/DefaultHistory` | 独立动态控制、端点提取、实际 Γ |
| C2 | [NativeRefinementSimulation.cpp](../../../src/algorithms/data_oriented_roam/materialization/NativeRefinementSimulation.cpp)，`Score/SplitScore/MergeRepresentative/CanMerge/Refresh/Split/Merge/Block` | 评分分层、动态前置、候选维护和历史变化 |
| C3 | [DataOrientedRoamTopology.cpp](../../../src/algorithms/data_oriented_roam/DataOrientedRoamTopology.cpp)，`InitializeSplitIteration/RunStrictSplitStep/RunSplitSerialConvergence` | Legacy 目标发现、预算和停止 |
| C4 | [DataOrientedRoamQueues.cpp](../../../src/algorithms/data_oriented_roam/DataOrientedRoamQueues.cpp)，`SplitQueueScore/IsMergeableTopology/CanonicalMergeQueueNode/MergeQueueScore` | 生产逻辑条目及动态依赖 |
| C5 | [DataOrientedRoamScoring.cpp](../../../src/algorithms/data_oriented_roam/DataOrientedRoamScoring.cpp)，`ComputeScreenErrorScore/ShouldSplitWithScore/ClassifyLeafDebug`；[RoamScreenError.h](../../../src/algorithms/RoamScreenError.h) | 纯几何分数、迟滞与调试读取 |
| T1 | [NativePlannerTests](../../../tests/DataOrientedRoamNativePlannerTests.cpp)，`StepAudit/Check/BoundaryCases`；[TestSupport](../../../tests/DataOrientedRoamNativePlannerTestSupport.h)，`Project/Compare/VerifyPlan` | 实际检查时刻、覆盖的额外状态及独立 oracle |
| T2 | [MaterializationDodBridge.cpp](../../../src/experiment/roam_materialization/MaterializationDodBridge.cpp)，`Capture` | 原型目标确实由来源副本运行 Legacy 取得，发现费用另计 |

## 2. Critical：不得跨越的证明适用边界

### 2.1 端点队列等式不证明目标发现

**FACT（L1/L2）**：`i/j : Region α` 都是传入的谓词。两个 `repair_*_queue_exact` 的参数中没有 Legacy、根请求、堆、迭代、预算、停止或时间。它们甚至不要求 `Closed h i` 或 `Closed h j`，因为条目函数等式对任意这两个谓词成立；几何合法输入及预算来自纸面规格，不能由函数等式反推出。

实际结论是：

\[
\operatorname{Repair}(F_{i,j},\operatorname{SplitEntry}(i,m,q),
\operatorname{SplitEntry}(j,\operatorname{TargetMarks}(m,i,j),q))
=\operatorname{SplitEntry}(j,\operatorname{TargetMarks}(m,i,j),q),
\]

合并条目对应使用 `MergeSupport`。`Repair` 在支持集内直接使用传入的 desired 函数，在外部复用 old；证明通过内外分类及外部一致引理完成。它没有构造 `j`，也没有给出可执行堆修复或计时程序。

**影响**：不能把严格发现的 134 ms 称作“未达到这个定理的复杂度”；同样不能因为该定理不要求轨迹，就直接删除发现 `j` 所需的信息更新。

### 2.2 原生目标标记不等于 `TargetMarks`

**FACT（L1/P1）**：`TargetMarks` 保持 history/blocked，split/merged 分别取旧集合与净新增/净删除的并。`target_preserves_history_and_blocked` 的证明为两项 `rfl`，验证定义投影，不是“任意 ROAM 更新都必须保持 blocked”的定理。

P1 §2.2 的参考 R 先删后增，每个差分事件一次；§2.3 已明确排除任意历史下的往返标记恢复。这里的 history 主要对应上一帧迟滞集合，不是原生所有历史字段的总称。

**FACT（C1/C2）**：NMP 的 `Block` 会改变 `SplitBlockedBuild`；`RemoveFailedMerge` 会留下最终候选缺席；split/merge 会写激活、强制来源和轮次；创建后休眠的缓存仍可能必须保留。`Extract` 正是用 Γ 保存相对净转移默认值的例外。

**INFERENCE：原生端点队列不能无条件只用 `LeafSupport(i,j)` / `MergeSupport(i,j)` 修复。** 需要证明原生额外变化在这些集合中，或扩展支持集与条目定义。当前已有自然输入没有覆盖所有可能义务，不足以消除这个证明缺口。

这不是发现 Lean 定理错误，也不是判定当前 Strict Planner 错误；问题在于把两种不同目标规格直接视为同一规格。禁止以 Lean 的净事务标记替换原生 Γ 后，再宣称仍严格兼容 Legacy。

## 3. 运行时要求追溯矩阵

| 当前运行时要求 | Lean 定理/前提或纸面位置 | 数学上的地位 | 当前来源与分类 |
| --- | --- | --- | --- |
| 得到指定最终 Region j | L1/L2 将 j 作为参数；不定义其来源 | 物化必须应用给定目标；与 Legacy 相同不是 Lean 结论 | 给定目标属 A；`j=jLegacy` 属 N1 §1 的 B |
| 最终细分逻辑条目精确 | `repair_split_queue_exact`，L2:123 | 在固定 h/q、指定 `TargetMarks` 下为后置条件 | A；原生对应另需证明 |
| 最终合并逻辑条目精确 | `repair_merge_queue_exact`，L2:134 | 同上，条目按固定组身份解释 | A；代表/伙伴和失败处置映射未机械证明 |
| 支持集外条目复用 | 两项 `*_entry_agrees_outside_support` 与 repair 定理 | 本局部构造安全复用的充分依据；其他正确算法可以重算 | A 的构造/成本选择，不是普遍必要操作 |
| history/blocked 保持 | `target_preserves_history_and_blocked`，L2:89 | 抽象事务的定义等式；history 不等于所有原生历史 | A；原生 blocked 可变化，不能照抄 |
| 净 split/merged 标记 | `TargetMarks`，L1:54；P1 §2.2–2.3 | 无往返参考 R 的目标定义 | A；不证明任意 Legacy 历史相同 |
| ready merge 后闭合 | `ready_merge_preserves_closed`，L2:76 | 前提为旧闭合、整个固定组 ready；证明删除组仍闭合 | A；不是原生任意半组/失败操作的证明 |
| 3k/2k 支持集大小 | P1 §4–5；L2 只证明包含关系 | 依赖二叉孩子和组大小的纸面计数 | A（纸面），不是 Lean 复杂度定理 |
| 硬预算与无在途预留 | P1 §2.2、§6；Lean 无预算变量 | 目标合法和事务发布前提；原生还需正确余额 | A（纸面）与 N1 的 B |
| 每一步 root、分数、forced 尝试一致 | Lean 无对应；N1 §6.4、N2 §5–6 | 当前严格模拟的充分验证方式，不是同终态的必要条件 | B；不应全部改称 C 来掩盖已确认契约 |
| 每次预算交换及失败处置轨迹一致 | Lean 无对应；C3 的分支会影响目标和历史 | 当前发现策略的语义；可替代算法须另证同端点 | B；具体预留实现并非 A |
| stop trajectory、迭代位置一致 | Lean 无对应；N2 §4.4、§6，T1 `VerifyPlan` | 当前严格摘要契约；迭代上限实际可影响目标 | B；若改 E1，哪些摘要保留需单列 |
| 每轮完整逻辑堆/节点投影一致 | Lean 只要求终态条目；T1 `StepAudit` | 比“控制器读到正确值”更强，便于定位错误 | 完整投影属 C；其被决策读取的子投影支撑 B |
| 每个 primitive 后立即与 Legacy 全量堆相同 | 无 Lean 对应，也不是 T1 实际回调频率 | 当前没有逐 primitive 全量比较，不能把它描述成已存在门禁 | T1 在每个严格循环后比较；自然输入只做最终全量投影 |
| Invalidate/Refresh 时刻完全一致 | Lean/决策事件格式均无此要求 | 当前控制流安排；是否可延期取决于下一读取及失败恢复 | 实现选择，额外检查属 C；正确观察属于 B |
| 每次失效立即物理删除、恢复立即重插 | 无；NMP-01P §9.4 已允许封闭延期 | 非必需，现版本已经不如此执行 | 实现选择；并非尚待解除的 A/B 禁令 |
| 内部所有时刻保持堆形状/反向位置与 Legacy 相同 | 无；逻辑投影按身份排序 | 从未要求相同形状；洞修复允许内部暂缺位置 | C/表示不变量：实际读取或公开时自洽即可 |
| 中间候选失效/恢复次数必须相同 | 无；成本计数按实现记录 | 非后置条件；不能等同 root/失败轨迹 | 实现工作量，不是 A；实际派生次数可优化 |
| 中间成功事件历史必须完整 replay | P1 明确物化不 replay；N1 区分发现与物化 | 物化不需要；发现能否省略需证明信息足够 | 当前 Simulation 是 B 的实现，不是 A 下界 |
| Γ、完整原生历史/缓存对应 | Lean 没有 Γ；N1 §6.2–6.3，C1/T1 | NMP 额外输出与兼容性契约 | B；部分属于调试/原生行为要求，不能全部冒称 Lean 已证 |
| 纯缓存的求值集合和次数相同 | Lean 只有固定函数，没有求值缓存 | 每条缓存值正确即可解释函数；覆盖集合非定理要求 | 当前配对/缓存消融的 B/C 强检查，E1 可另定；不计入 Γ |
| 旧状态完全只读、私有工作区不逃逸 | Lean 无地址或所有权模型；N1 §5–7 | 当前解耦架构与实现安全前提 | NMP 已确认边界，不因非 A 而取消 |

**审计纠偏**：数学前提、用户确认的目标发现语义、开发检查和容器内部动作不能串成“Lean 要求逐步维护”这一因果链。反过来，用户确认的 B 也不能仅因未在 Lean 中出现，就无记录地降级或删除。

## 4. 目标 j 的来源与两种事务

### 4.1 理论与 MPR：外部提供目标

P1/P2 的任务是 `(M₀,J,Δ) → S_R`。R 是已知目标的先合并后细分应用规格，不按评分重新选择 J。MPR 自然来源由 T2 `Capture` 在 DOD 副本上运行 Legacy 取得最终活动事件集合，再生成完整差分；发现、复制和认证分别计费。

MPR 物化恢复的是这个 J 对应的原型事务状态，不是把 Legacy 副本的最终 Γ、堆或邻接作为答案导入。原型结果也明确不覆盖原生全部部分失败、调试和历史统计。因此它支持直接目标应用，但没有以运行时免费 oracle 解决生产替换。

### 4.2 NMP：在生产入口独立发现 Legacy 终态

N1 §1、§3 明确新增了第一箭头：

\[
S_L=\operatorname{LegacyUntilStop}(M_0),\quad
J_L=\operatorname{ActiveEvents}(S_L),\quad
\Delta_L=I_0\triangle J_L.
\]

有方向的 Δ 和规范化 Γ 还需从同一 Legacy 终态规格定义。NMP-01 采用独立严格逻辑模拟得到它们；最终邻接、生产索引、网格和堆布局不进入正常结果。源代码没有调用旧控制器取得目标。

**结论**：目标发现是 NMP 明确加入、经 Review 确认的任务，不是 Lean 暗含的要求，也不是无意多造的物化步骤。现在仍不能仅凭终态修复定理删掉整个 Planner。若改为外部提供 J/Γ，就回到给定目标的应用任务，外部发现费用仍须计入完整性能。

### 4.3 当前观察点与可替换的维护方式

对现有控制器保留 B 的一种充分做法，是让下列读取获得相同逻辑值，而不是维持每次中间物理写入：

1. 循环入口迭代保护；低分合并队首的身份和键。
2. 细分队首、分数、深度和迟滞谓词；不能跳过一个本应导致停止的头部。
3. 根内有效性、孩子/底边关系、forced 来源、深度/guard、预算预留；成功前置后的关系重读。
4. 根失败后的预算拒绝判断和新的合并队首；失败移除、阻塞及停止效果。
5. 当前刷新内部的代表/伙伴存在性查询，以及最终预算、队列、历史/缓存提取。

第 5 项中一部分查询来自当前维护实现，可随实现重构消除；其语义结果不能在仍有调用者读取时变成过期值。旧条目的延迟物理删除、精确失效的资格复用，在保持这些观察值时不需要 E1。

允许 E1 后，可以采用不同的观察过程，但必须独立建立同端点证明。仅清理 stale 队首仍不足：新合法高分候选若未发布，当前未过期的低分队首也可能被错误选中。预算竞争和停止会让错误选择影响最终 J；没有一般的顺序无关结论。

## 5. 实际评分、资格和状态依赖审计

### 5.1 固定的纯分数与动态逻辑条目

**FACT（C5）**：`ComputeScreenErrorScore(state, domain, geometricError)` 只使用三角形域、几何误差、原始高度采样、地形尺度/高度比例、视图投影、视锥平面及分辨率。共享函数合并屏幕几何界与投影长边密度。它不读取活动数组、邻接、预算、轮次或队列。

在 C1 的同步只读来源和冻结环境契约下，同一身份的域/误差不变，纯分数 q 可视为固定函数。新身份可能首次求值，但不意味着旧身份的 q 随拓扑变化。C2 已在一次调用内缓存这个纯值，不缓存抑制哨兵。

| 生产/规划函数 | 实际依赖 | 与 Lean 的关系及剩余义务 |
| --- | --- | --- |
| `Score` / `ComputeScreenErrorScore` | 固定域、误差、地形及视图环境 | 固定 q 的假设在声明调用内有源码依据；浮点到 Nat 不是已证数值映射 |
| `SplitQueueScore` / `SplitScore` | 活动叶资格、深度、最大深度、本轮 blocked、本轮 MergeBuild，再用 q | 对应 `Leaf/allowed/blocked/merged` 分层；动态不是纯 q 的变化。若 blocked 或 merged 超出净差分变化，须扩支持集 |
| `ShouldSplitWithScore` / `WantsSplit` | 有效身份、深度、队列分数、split/merge 阈值、`PreviousSplitPaths` | 属目标发现的迟滞/停止规则；不在 Lean 队列修复函数中 |
| `IsMergeableTopology` / `MergeRepresentative` | 活动内部资格、直接孩子叶资格、Base 对侧状态、双向 Base、对侧孩子、Path 平分 | 与固定菱形 `MergeReady` 的对应需几何/规范关系证明；原生动态 Base 不能仅按函数名当作固定组 |
| `MergeQueueScore` / `Refresh` | 代表及 partner、本轮任一成员 SplitBuild；未抑制时取成员 q 的最大值 | 固定组映射成立时可定义固定 `q_m(d)`；partner 改组或额外 SplitBuild 变化需支持集覆盖 |
| `CanMerge` | 缓存 IsSplit、子节点状态、Base 互指、对侧孩子、纯分数上限 | 是实际执行前复核，不等同活动成员资格；不属于 Lean 的完整生产失败证明 |
| `Block/RemoveFailedMerge` | 动态失败结果与后续是否重新接纳 | 改变条目键或成员；Lean 没有一般的失败移除掩码 |
| 激活/强制来源与缓存创建 | 调试分类、缓存生命周期；缓存规模影响下一调用迭代上限 | 不改变本调用纯 q，但属于部分原生输出/续接契约；不应塞进 score 或声称 Lean 已覆盖 |

`Option (Option Nat)` 的外层表示成员、内层表示抑制。生产使用 ±最大有限浮点数作为抑制键并以 Path 打破同分；Lean 没有证明这些具体比较或堆极值。支持集等式本身只用同一 q，不用 Nat 的大小关系，因此可讨论推广为其他值类型；本轮没有把这种推广冒称为已经机械验证。

旧键有效性也必须明确。Lean 的 old 是准确的 `Entry(i,m,q)`，不是任意已有堆数组。自然入口有评分阶段；微型夹具中的人工键和故障注入用于 B/C 验证，不能自动归入 A 的有效旧队列前提。生产异常失败后候选缺席的状态，需在条目定义中表达处置，或明确排除该输入域，不能直接重算补回。

### 5.2 对现有支持集不足的最小边界说明

取 `i=j`，有一个未抑制活动叶 t。若原生结果仅把 t 置为本轮 blocked，则净 Δ 为空、`LeafSupport` 为空，但其细分队列键必须改变。直接套用旧 repair 构造会复用旧键。这里违反的是 `TargetMarks` 保持 blocked 的适用条件，不是反驳 Lean 定理。

现有 T1 的非叶前置故障有 `k=0,h=1`，证明当前防御性输出域确实需要容纳这种义务；这是故障注入，不是自然工作负载发生率证据。失败合并候选仍具结构资格却被移除时，原 Lean `MergeEntry` 也不能表达这种缺席。

### 5.3 原生扩展需要补的充分条件

以下为**纸面待补契约，不是新增已证 Lean 定理或已授权实现**。在共同身份、固定组映射成立时，令 K/G/S 的差表示有效 blocked/merged/split 位的旧新差异，F 表示失败合并缺席掩码。可先取保守支持集：

\[
U_s=F_\Delta\cup(K_0\triangle K_1)\cup(G_0\triangle G_1)
\cup D_s^{q}\cup D_s^{\mathrm{other}},
\]

\[
U_m=C_\Delta\cup d[S_0\triangle S_1]\cup(F_0\triangle F_1)
\cup D_m^{q}\cup D_m^{\mathrm{relation}}.
\]

`D^q` 为评分值变化支持，在本调用固定环境下为空；`D_s^other` / `D_m^relation` 只能逐项解释为尚未被基础模型覆盖的资格、深度/身份或代表关系依赖，不能成为任意状态的垃圾桶。若证明原生固定组、合法活动关系完全对应模型，应消掉不需要的额外项。

待证明的是：支持集外每项成员资格、抑制、组身份和有效键均相同；初始队列符合旧条目规格；支持集内按完整目标标记和处置重算。由逐身份内外分类可得修复精确性，但从 C++ 得到这些前提、如何局部枚举各项、总代价是否受 `k+h` 控制仍需证明。

Γ 的 ForcedActivation 不必扩展队列支持集，因为它当前不被评分/资格读取；它仍需恢复调试结果。CacheBirth、内部规范关系、Pending 也各有自身支持集，不能把一个队列引理冒充完整原生状态定理。P1 §7 的 `Γ_W/Γ_D` 是几何/评分计算费用，和 NMP 的续接义务 Γ 不是同一对象。

## 6. Major：134 ms 中能够和不能够归因的工作

### 6.1 既有数字的正确含义

以下复用 NMP-01P §9.13，不重新采样。普通压力均值 133.891678 ms；粗遍 134.839 ms，其中循环 114.357 ms，约 84.8%。详细自耗时来自受插桩影响的 224.547 ms 遍，不能按比例折算为普通时间。

| 已有数量 | 本轮能够确认的含义 | 不能推出的结论 |
| --- | --- | --- |
| 42977 逻辑失效、25115 恢复、17862 最终删堆 | 当前已通过封闭维护复用 25115 个条目，恢复时避免物理删后重插 | 25115 不是还未兑现的同等物理删除节省，也不是同一事件 split 后 merge 的次数 |
| 428207 候选检查、1795522 邻域访问 | 当前发现实现反复维护局部候选；次数不是 Lean 指定的 | 不能认定全部检查可删除，或直接算成可节省毫秒 |
| 两堆读 1019304、写 529823 | 当前容器访问账本，含准备和修复 | 不是理论必需次数，也不是硬件 load/store 或数学下界 |
| primitive split 16208、merge 11575；k=27783 | 两种基础事件修改总数恰与净差分大小相等 | 当前压力样本没有显示大量成功事件往返；不能据代表恢复宣称有大量拓扑 churn |
| h=16610，自然 Γ 全为 forced 例外 | 自然结果确实超出不记录强制来源的抽象投影 | h 不等于额外队列修复数，也不等于拓扑事件往返数 |

在每个成功 primitive 对应一个活动事件加入/移除的有效自然输入解释下，修改次数等于对称差大小，意味着没有被相反修改抵消的额外事件翻转。它不排除失败尝试、临时叶、邻接写入或队列反复维护；也不是其他输入都无 churn 的证明。

### 6.2 从信息义务到实现工作量

| 工作类别 | 给定目标物化是否要求当前做法 | 原生目标发现为什么仍可能需要信息 | 审计结论 |
| --- | --- | --- | --- |
| 根选择、动态前置、预算失败/交换 | Lean 不包含 | 决定 J/Γ；现有控制器在这些读取点继续演化 | 当前 B 的实现；尚无必须逐步执行的下界，也无可全部省略的证明 |
| 中间三邻接、活动资格更新 | 物化只需最终局部构造 | 递归会重新读取 Base/孩子和失败后的状态 | 可换表示或查询方式；不能只到最后恢复才供前面的读取使用 |
| 邻域枚举、Invalidate/Refresh、资格复核 | 只需端点支持集内重算 | 发现过程中需要正确的下一候选与处置 | 主要可研究的重复工作；次数不受定理强制，需观察/失效证明 |
| 物理堆修复、反向位置、页索引 | 没有这些抽象对象 | 当前数据结构实现准确极值与成员查询 | 表示成本，可替换；完整计费并保留读取时自洽 |
| 纯评分重复计算 | 固定函数，不要求重复求值 | 新候选可能需要尚未知的键 | 已缓存部分；其余计算必要性取决于实际查询/输出，不由 Lean 给出次数 |
| Γ 提取、必要缓存/历史恢复 | 原模型只覆盖限定净事务 | 原生同终态及续接要求保留额外效果 | 义务不能删除；遍历 touched 的方式并非已证最优 |
| 构造、全 N/Q 投影、排序、指标和销毁 | Lean 不给运行时成本 | 当前实现为表示与可观测性付费 | 是现实费用，不是算法理论要求；不能挪出完整计时 |

**能够回答的是工作归属，不能回答“134 ms 中恰有 X ms 数学必需、Y ms 完全冗余”。** 现有计时将信息计算、维护和访问交织在一起；Lean 不给最小时间，删除某类动作还可能要求新增证书/dirty 维护。当前数据支持“维护放大值得验证”，不证明它已经足以解释或消除 63% 的差距。

诊断轨迹与全量逐步投影在普通计时中关闭，移除测试本身不会直接回收 114 ms。真正可能产生收益的是改变为满足强过程契约而选用的执行方式。

## 7. 后续契约与实现边界建议

### 7.1 本轮立即生效的范围

1. 暂停 NMP-01P 新增性能实现；当前代码基线、数据和 80%/50% 门槛保留，不判通过或 No-Go。
2. 不删除或修改现有严格夹具、轨迹对照和独立 Legacy；Strict Planner 继续作为已测的过程参考。
3. MPR 继续提供给定目标的物化证据；不把其理论边界扩大为原生端到端更新，不恢复旧研究支线。
4. 本审计只新增此文档并回填规划状态/入口；没有新的容器、队列算法、profiler 或测试矩阵。

### 7.2 建议的下一决策，而非本轮实施授权

**建议讨论 E1：完整原生端点等价，允许不同中间求解轨迹。** 保持 `J_L/Δ_L/Γ_L`、预算和明确的原生可续接投影；物理布局、重复求值、候选维护次数可不同。停止摘要、实际操作统计、纯缓存覆盖集合哪些属于外部兼容输出，必须逐项列清，不能默认为全部沿用，也不能默认为全部可删。

现有 B 轨迹可以为新候选保留成对诊断，不必把每个中间差异都当作 E1 失败；这种变更只作用于另行确认的新契约，不能改旧测试让当前版本“通过”。最终比较仍由独立 Legacy 终态生成，不能从新算法输出反向定义参考。

下一份机制规划应先交付三件具体东西：

- **端点规格**：固定 J 的来源、规范 Γ、队列/历史/缓存投影和有效输入域；补原生 `Entry` 与支持集的纸面对应，保留异常输入与防御性检查的独立分类。
- **发现信息边界**：选定一种维护省略，列出哪些读者仍需什么信息、何时取得，证明或定向核查未改变指定端点；不先开发一套新系统。
- **成本账本**：分别列减少的资格/邻域/堆维护和新增的版本、dirty、证明查询与准备工作。给出完整 `Tdiscovery + Tmaterialize`，不把仅物化界或 Planner 单项当作更新收益。

若只做准确队首可见、封闭维护合并或失效缓存，仍可在原 B 契约下优化，无需 E1。若改变根顺序、合并决策步骤或跳过历史过程，就必须给出同端点理由，现有 Lean 不提供这条证明。

**建议保留发现与物化这两个已明确的模块边界，重审二者的证明责任；不因本次审计把它们重新合并，也不把物化器变成缺失目标的求解器。** 当前没有充分证据决定 Dense、Lazy 或另一目标求解器的性能胜负，后续机制需要 Review；本轮不继续追加微优化。

## 8. Minor、验证与未决事项

### 8.1 文件职责与核查方式

本文位于既有 `docs/reviews/roam_parallelism/`，职责是追溯契约来源和证明适用边界，不重复创建完整模块事实或新大规划。已有事实引用保留其历史基线；NMP 两份规划只回填暂停状态与本次审计入口，不改旧验收数据。

本轮逐项阅读两份 Lean 文件、纸面参考和限制、原型目标来源、原生控制/评分/队列/Γ 提取及相关测试。用 Ubuntu 计算的源码 SHA-256 与 P1 §10.1 已记录的机械检查版本完全相同：

| 文件 | SHA-256 |
| --- | --- |
| `StateModel.lean` | `2013b7e2312f951755758fcdd87a2cfaf2b2e31b8da2e2ed2ec8605242378cfb` |
| `Locality.lean` | `e27b55d12ce9aacd4f63ba2f9959f556a56779c1ecab7848d381375a22c3e59a` |
| `lean-toolchain` | `61561b06f5587e027815fac8f59bf6ca160dfbf3f7372fc6297664acfd85b8c0` |

复用已有 12 项定理编译/公理检查记录；本轮不声称重新编译 Lean，也不新增一般性定理。运行时代码未变，C++ 构建、CTest、性能和后端复测不适用。交付检查限定于文档引用、文本格式、变更范围及与原规划边界比对。

最终核查：三份文档的 41 个本地文件引用均可解析，UTF-8、代码/公式定界及冲突标记检查无异常；新增审计无行尾空白。Git 差异仅为本审计与两份规划，运行时代码、Lean 和严格夹具未变；未提交 Git。现有 `diff --check` 无空白错误，未以更新文档为由重复运行原性能或正确性矩阵。

### 8.2 未决事项

- 原生固定菱形、动态 Base/活动状态和抽象 `MergeReady` 的一般对应，以及 Γ 扩展支持集的完整性，未被现有 Lean 证明。
- E1 的最终外部投影及摘要兼容范围尚待确认，本轮没有替用户放宽旧契约。
- 同终态目标发现能否省略更多中间维护、实际可节省多少 work/时间，仍未知；现有工作量不能单独推出 49 ms 可达。
- NMP-02 原生物化与真实后续评分、细分/合并、网格消费和实现切换仍未完成，终态字段审计不能冒充这些集成已经验证。
- 稀疏/稠密/Legacy 是否存在稳定完整工作负载交叉仍未证明，不能用本次契约审计替代性能证据。
