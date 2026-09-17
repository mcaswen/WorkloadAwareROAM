# QPC-04F：显式质量目标与逐点损伤约束的持续接入

日期：2026-09-17。类型：QPC-04下的Minor Plan。规划基线：`c2cc278`。状态：**实施及有限验证完成；损伤控制/自然可执行获有限验证，恢复与成本受限，待用户验收。** 用户暂停ATT支线并要求继续QPC小规划；本轮不扩展一般事务模型。上承[大规划](transactional_quality_performance_closure_major_plan.md)、[04E结果](../../research/cpu_refinement/qpc_04e_quality_contract_results.md)和[QC推导](../../research/cpu_refinement/quality_contract_derivation.md)。完成本阶段后交用户验收，不自动进入QPC-05。

## 1. 问题、目标和判断边界

04E证明了候选条件的抽象安全性质，并在71个**原政策已批准交换**上进行了只读审计；它没有生成新政策的请求、选择或持续状态。Sierra95的大量目标超标根不满足旧复合P资格，Canyon24的14个原donor又全部违反候选高度约束。继续过滤旧批次不足以回答新政策能否运行。

本阶段只接入一个默认关闭的实验政策：**显式误差目标产生请求，receiver/donor分别认证逐点损伤，随后沿原顺序完成预算预留和冲突处理。** 验证它能否阻断已知坏交换，同时形成真实持续进展，并记录代价和剩余阻塞。

两项出口分开：

1. 安全性：已接受事务是否满足声明的运行时Q约束，跨轮是否避免新增高度超标损伤。
2. 恢复能力：新的请求是否真正产生有效提案并降低已有残差；安全拒绝全部事务不算恢复成功。

不承诺所有场景达到目标，不要求本轮快于DOD，不将初始种子误差判为本轮新增退化。它是质量政策变更，不是保持事务选择不变的性能优化。

## 2. 范围和既有事实

依据[04E事实](../../codebase/cpu_refinement/qpc_04e_quality_contract_facts.md)、[04D原因](../../research/cpu_refinement/qpc_04d_error_first_results.md)及本次定向源码核对：

- `TransactionalSamples::ReceiverKey`仍先检查`P² > SplitPixels²`，然后才使用误差优先键。只改排序不能解决未出生请求。
- `Certification::Fit`保留旧局部最大值减0.01px的进展门槛；选择近零高度改变量。它是现有提案生成器，不等于新目标的可行域求解器。
- `Reservation::Plan`在receiver首个旧认证成功时停止；paired分支仍以`receiver.TargetMicropixels`接受donor。新条件必须进入目录迭代和配对判断，不能只在最终批次上再过滤。
- 净零R、空额Free、边界B和paired均可改变插值曲面；不能只给donor增加约束。
- `Proposal.Samples`和旧`Measure`面向可见支持；高度约束需要全部闭补丁样本。公开`ExactErrorSquared`返回double，不能当无舍入证书使用。
- `SetView`、局部样本修复和初始建表共同维护候选键。新资格必须同时覆盖这些入口，不能只改一次建表。

本阶段不改E/B/F/H/R几何目录、Fit选值、最小角、存活点高度、donor池顺序、预算、前缀、线程或相机。不给目录增加“质量失败后的新翻边”入口。不做资源索引、层次Q、平台GUI、渲染上传或新profiler；ATT保持暂停。

## 3. 冻结政策

### 3.1 配置、种子与身份

增加`qualityPolicy = legacy | pointwise-target`，默认`legacy`。新模式要求`PreserveSurvivingHeights=true`、旧`HeightGuard=false`、接收排序为`error-first`；不把旧联合最大高度保护改名复用。

唯一自然实验操作点为：

\[
E_*=0.5\text{ px},\qquad H_*=\mathrm{HeightScale}/256.
\]

它来自04E已冻结六组中的一个有保留机会的点：Peking15保留28/34，Sierra48保留2/5，Peking23及Canyon相应旧批没有正进展保留。选择这一点是为了同时观察可执行、已达标和受限情形，**不是生产推荐参数或人眼阈值**。本轮不追加参数扫描或改用更宽H追求事务数量。

E*/H*与`SplitPixels/MergePixels`分开。初始化DOD种子继续使用原场景阈值；两臂必须导出相同种子身份和几何。新设置进入持续配置、任务身份及Reset判定；运行中更改政策/目标需要Reset，不能改变跨轮归纳的常量。输入校验拒绝非有限、非正目标及不支持的组合。

新政策字段只在启用时追加版本化身份，旧配置和旧任务身份保持兼容。`workloadId`仍表示共同输入，`taskId/policyId`区分算法政策；不顺手重做整个历史身份体系。

### 3.2 请求与全局顺序

记根t上运行时可见闭面样本的缓存最大平方误差为`A(t)`。新模式：

\[
\operatorname{Request}(t)\iff A(t)>E_*^2,\qquad
\operatorname{Key}(t)=(-A(t),\operatorname{stableID}(t),\operatorname{slot}(t)).
\]

使用已有块索引获取全局前r项；不新增完整排序或全Q索引。原复合P数组和donor排序保留，密度需求仅诊断，不进入新质量请求分母。无可见样本不产生可见误差请求；无效投影/非有限误差单列为invalid，不能填零或误称达标。

这里的确定性全序针对**实现缓存的数值**。近E*舍入边界必须有定向夹具和诊断，不把浮点资格声称为精确实数请求完备性。损伤/进展接受则采用下述保守数值认证。若未来要求数学意义精确的请求出生，需要另补数值对应，不能在本阶段暗示已经解决。

### 3.3 每个提案的损伤证书

固定batch-start状态、view、Q、参考和完成拟合后的实际输出几何。对完整闭修改支持收集去重样本，当前可见子集检查屏幕条件，全部样本检查高度条件：

\[
a'(q)\le\max(a(q),E_*^2),\qquad
u'(q)\le\max(u(q),H_*^2),
\]

其中`a=e_screen²`，`u=|h−h*|²`，reference仍是raw samples＋bilinear。可见性依据冻结reference/view契约，不能由修改后面片把困难样本变成“不可见”。真实共享边贡献、补丁外不变性及覆盖有效性是证书前提。

证书作用于将实际发布/输出的坐标及高度：若原提交过程有float转换，认证端须复用同一确定转换后再求值。不能认证高精度私有提案、却发布另一个几何。先核查已有转换事实；无法保持对应时暂停该路径，不提高容差。

定义固定可见集合上的进展：

\[
g(T)=\sum_{q\in Q_V}[a(q)-E_*^2]_+
       -\sum_{q\in Q_V}[a_T'(q)-E_*^2]_+.
\]

receiver要求损伤条件成立且`g_R>0`；donor要求损伤条件成立，其`g_D≥0`由逐点条件推出。共享接口贡献不变并去重时，完整交换`g=g_R+g_D>0`。Free及R也要求自己的正进展。

**这比04E“只要求完整交换g>0”更保守**：不允许receiver没有进展、仅靠donor改善补贴它。这样请求的兑现确实需要接收侧产生质量进展，同时避免把认证退回逐pair求解。receiver总进展仍不保证根最大误差严格下降；报告二者，不声称单根恢复时限。

新模式的donor接受以其独立逐点证书代替旧`Accepts(donor, receiver.TargetMicropixels)`，不叠加未声明的旧阈值。旧模式完整保留。Fit原0.01px门槛仍是提案生成限制，可能阻挡小于该幅度但有益的修复，必须单独统计，不能写成目标已满足。

### 3.4 目录、预留与批次

receiver按原目录顺序生成、旧结构/拟合认证、新逐点认证，**第一个同时成功的提案**才结束；新质量失败继续尝试原目录余项。R仍仅由原E/F/H全形状失败条件触发；在已有R候选循环内也检查新条件，避免首个旧成功R阻断后续原有R。

命名空额在receiver选择完成后按原顺序分配；维持`+1/+2/0`与donor`−2`、失败不回流、donor唯一、相同读写冲突规则。donor独立证书每snapshot最多计算一次；不能在每个配对重复扫样本。

批次共享样本不能重复计算进展。优先利用完整R/W与不变接口的构造证明，保留一次批次证书合并核查；遇到共享样本发生不兼容改变、版本失效或发布几何不符，整批停止发布并报告invariant failure，不能静默删掉事务后当作相同greedy结果。未知质量是普通保守拒绝；结构不变量破坏是错误，两者分开。

## 4. 架构与文件归属

依赖保持`配置/实验入口 → Samples/Reservation → Proposals/Quality → 几何与数值事实`；实验观察器只读生产状态，算法不得依赖实验目录或Python。无需通用策略框架；一个显式政策枚举和局部证书组件足够。

| 选择 | 文件/组件 | 职责及修改理由 |
|---|---|---|
| Extend | `src/algorithms/TransactionalLodSettings.h`、`greedy_transactional_lod/TransactionalTypes.h` | 政策、目标和小型证书/账本类型；不放计算实现 |
| Extend | `TransactionalSeedBuilder.cpp`、`TransactionalState.cpp`、`TransactionalPipeline.cpp`、`TransactionalTerrainLodAlgorithm.cpp`、`ITerrainLodAlgorithm.h` | 参数合法性、相同种子、代次/Reset及身份；Pipeline仅验证证书所属代次和原子发布前提 |
| Extend | `TransactionalSamples.h/.cpp` | 共用ReceiverKey入口切换资格；原P、donor及局部索引维护复用 |
| Create/Extract | `greedy_transactional_lod/TransactionalQualityEvaluation.h/.cpp` | 仅模块内部的覆盖、插值、reference、区间及精确误差求值；从Certification定向提取真实共用数值代码，避免两套数值语义 |
| Create | `greedy_transactional_lod/TransactionalPointwiseQuality.h/.cpp` | 目标损伤、正进展证据、未知分类与共享支持组合；不选root、donor或预算 |
| Extend | `TransactionalCertification.cpp`、`TransactionalProposalEvidence.h/.cpp` | 旧认证复用提取内核；evidence允许显式不可变闭样本列表，原可见列表语义不变；Fit/ClipPolygon留在原职责 |
| Extend | `TransactionalProposals.cpp`、`TransactionalFlipRecovery.cpp`、`TransactionalReservation.cpp` | 在原候选迭代中调用政策证书；替换新模式配对接受，不增加几何目录或资源索引 |
| Extend | `src/experiment/infrastructure/ExperimentCase.h/.cpp`、`src/benchmark/experiment/ExperimentReplay.cpp` | 实验配置到算法设置的映射与校验，不增加GUI菜单 |
| Extend | `scripts/experiment_infrastructure/{catalog,suite,runner}.py` | 配置透传、任务身份及共同输入匹配，默认配置保持兼容 |
| Extend | `configs/experiments/schema/case.schema.json` | 登记同一可选政策及正目标字段，原配置继续有效 |
| Extend | `TransactionalExchangeQualityAudit.*`、`TransactionalQualityProvenance.cpp`及`TransactionalRecoveryTrace.cpp`（现有experiment/benchmark目录） | 只读导出新政策身份、实际资格及已接受交换；修正原捕获对旧P资格的假设，不加入全量历史数据库 |
| Create | `tests/TransactionalPointwiseQualityTests.cpp` | 新条件、数值边界、所有事务路径及续接的专项夹具 |
| Reuse/Extend | 现有ReceiverOrdering、ExchangeQualityAudit、HeightPolicy及算法专项测试，现有CMake源/测试注册处 | 覆盖提取、旧模式兼容和新入口，不重跑全项目矩阵 |
| Create | `scripts/run_transactional_target_quality.py`、`scripts/analyze_transactional_target_quality.py` | 前者冻结/运行，后者归约/制图；复用04D、04E与公共实验工具，不复制evaluator |

新增数值文件只抽取共同计算；私有模板确需共享时放该组件内部头，不做通用几何库或扩大公共API。若提取要求改变旧舍入/求值顺序，先停止并缩小提取范围，不为新证书重写旧Fit。

`ProposalQualityCertificate`绑定snapshot、view、policy、目标和最终提案几何；持有自己的闭样本/evidence，不借用会变动的cursor临时对象。拟合结束后几何不可再变。并行生成只写独占记录，屏障后Reservation只读；新缓存随batch释放，不能跨view复用。旧模式不分配新证书数组或执行额外闭支持扫描。

## 5. 形式推导、对应与反例留痕

实施先扩展原[quality_contract_derivation.md](../../research/cpu_refinement/quality_contract_derivation.md)，不另起ATT或重做闭包推导。按“定义→推导步骤→所需前提→代码落点→夹具→未证明项”记录：

1. **QC-10 请求与目标。** 理想样本域的`max a>E*² ⇔ 存在超标样本`，与数值缓存资格、密度项的区别；新资格不推出目录可行或请求一定进入前缀。
2. **QC-11 提案到完整交换。** receiver正进展、donor不增、共享不变贡献去重，推出Free/R/paired的完整Ψ性质；列出比04E更保守之处。覆盖共享边Q和重复所有权反例。
3. **QC-12 持续状态。** 复用QC-02归纳，说明固定Q/H、完整闭修改域、实际发布几何三个运行时义务；列出相机变化、Q_eval不同、种子超标不能推出恢复的反例。
4. **QC-13 数值与成本。** 区间快速判定，歧义进入原有理数内核；有限进展和沿04E用128bit定点外包区间累积，正下界才接受，跨零为unknown。不用double和epsilon冒充证书；证明整数定向取整的包含关系并用独立Fraction交叉检查。

现有Lean的QC-01～03是引用基础，不新增几何公理或为形式化数量扩一般定理。若QC-11需要一个尚无的有限和辅助引理，可加到原模块并仅重建该模块；论文总公式页和推导索引同步记录机械/纸面/实测边界。所有失败推导和政策收紧保留，不只写最终公式。

## 6. 成本与最小统计

记p为实际尝试提案数，s_i为完整闭支持样本数，d_i为提案面数，a_i为收集关联数，b为批准批宽。共用evidence后，保守工作界为：

\[
W_{quality}=O\!\left(\sum_{i=1}^{p}(a_i\log a_i+s_i d_i)\right)
 +W_{exact},
\]

另列批次去重/组合成本、证据内存峰值及构造/析构。d有界不使s或精确数值位长成为常数；已拒proposal与未用donor也计费。配对对已认证独立证据只读摘要，原footprint冲突费用完整保留，不承诺Reservation变快。

只加必要计数/已有阶段计时：目标超标根、前缀内根、目录尝试、旧Fit/形状失败、屏幕/高度失败、无正进展、数值未知、donor不可用、预算/冲突拒绝、实际交换和实际面数；质量计算记录闭/可见样本访问、区间/精确次数、最大有理位长、证据字节和耗时。按主要失败原因记分母，同时可保留重叠约束计数，不能相加当互斥总量。

新模式完整CPU仍包括view、发现、认证、预留、发布和续接。独立精确审计、图像和全Q质量评价单列诊断成本。新增政策更慢时先用这些数据解释，不转入容器、预留或数值微优化。

## 7. 冻结实验与验收

### 7.1 最小输入

延用04D同一三场景、50k上限、`r=m=160`、8线程、D3D12、1280×720、neutral材质及3次暖机机会。保留原深度、Q、轨迹、种子规则和所有输入指纹：

| 场景 | TerrainSize / HeightScale | 旧Split / Merge | 机会/质量帧 | 边界B |
|---|---|---|---|---|
| Peking | 80 / 12 | 0.25 / 0.1 | 24；2/15/16/23 | 开 |
| Sierra | 80 / 10.38941992187497 | 4 / 2 | 96；2/48/80/95 | 关 |
| Canyon | 80 / 11.973810546875 | 4 / 2 | 96；2/48/80/95 | 开 |

两臂均固定旧点、开启既有flip recovery，接收排序为error-first。A为04D的旧接受/资格，B为本政策。旧composite、DOD及04E六参数结果保留为历史参照，不增加新的自然分叉。新E*不改变表中初始化阈值；不能从旧后期状态重置并冒充持续B轨迹。

数据复用前核对资产、resolved、相机、Q/evaluator和种子身份。A的质量/几何可在相同输出核查后复用；涉及旧新CPU成本的比较必须同环境匹配采集，不直接使用跨二进制历史毫秒数。QPC-02/03未闭环，不能据此宣布跨算法同质量竞争胜出。

### 7.2 验证顺序

1. 修改前保存Peking旧政策一次正常平台基线及三场景冻结配置；先写freeze再运行B，旧数据不覆盖。
2. 数值/支持夹具先行：阈值两侧、零/正/未知进展、目标以下允许有限损伤、超标点不得恶化、不可见H、共享边去重、float转换、过期证书和预算。复用两个历史坏补丁作回归，不为它们重跑第四条composite轨迹。
3. 验证E/B/F/H/R与Free/paired均经过新条件；质量失败不会启用额外R；首个旧成功但新失败时可尝试原目录后项。验证三种样本更新入口的请求键及下一轮状态。
4. 旧模式定向回归及Peking基线复测。出现超过开发规范阈值的工程回归，最多一次定向复测并报告原因；不追逐微秒噪声，不隐藏较慢值。
5. 三条B持续路线各一次正常计时/采集；A匹配正常计时按需同环境各一次，质量捕获与正常计时分离。Peking额外一次B单线程逻辑对照，只验证选中事务、几何和证书结论确定性，不展开多线程性能矩阵。
6. 复用现有独立evaluator评价表中12个质量帧，运行时Q约束与Q_eval结果分别报告。每条路线最多两个预定批次导出独立精确审计：Peking15/23、Sierra48/95、Canyon24/95；批次为空照实保留，不换帧找正例。

所有已接受事务均走在线保守证书；离线精确核查只覆盖选定批次，不宣称它已独立穷验全轨迹。原子Apply、预算和后续状态用现有必要专项检查，渲染接口未改，不重跑双后端或完整CTest。

### 7.3 质量、恢复与视觉

报告每帧请求/执行账本及关键帧Emax、RMS、Hmax、Dmax、实际N、共同可见样本超额分布。DOD是独立比较基线，不是reference；Dmax正值本身不等于违反本政策。固定Q上的损伤条件允许低于目标的有限上升，不能要求每帧全局Emax无条件单调。

原QPC-01见证、04B可见退化见证及Canyon04D返回新最大见证全部保留；继续区分当前view、固定未来view和实际高度三种口径。重点回答：Sierra95原未出生请求是否出现；Canyon坏删点是否被拒；新轨迹是否恢复种子残差，还是停在Fit、shape、prefix、donor、冲突或预算层。

对“有超标请求但无事务”逐层解释；记录连续停滞机会和每次成功进展幅度。几何不变但投影变好不算恢复。新目录仍可能无解，本轮不加轮数、前缀、源高重拟合或新原语补结果。

复用基础设施输出Peking15/23、Sierra95、Canyon95的同相机A/B图、误差热图和见证局部图，由Agent实际查看并记录裂缝、异常尖峰、覆盖、明暗及图例范围；用户视觉签收单列。有限截图不等于popping全面验收。

### 7.4 有界执行与出口

编排使用Ubuntu WSL，平台仍为冻结Windows原生程序。沿用旧采样访问限额；独立捕获每批200万闭样本记录上限。单进程180秒、内存8GiB；自然采集/评价总45分钟到限停止并保留已有结果，未知/超时不算通过，不提高限额或换容易输入。

| Gate | 通过条件 / 失败解释 |
|---|---|
| 契约对应 | 所有事务路径、实际输出、版本与完整闭支持均有检查；任一已接受反例立即阻止质量准入 |
| 损伤控制 | 两已知坏例被拒，选定独立审计无接受违规；跨轮H包络及采样边界准确说明 |
| 自然可执行 | 新政策在Peking和至少另一个场景各出现非空正进展事务；不要求沿用04E的旧批宽，不能只靠全部拒绝过关 |
| 恢复证据 | 单独报告目标超额、种子残差和固定见证是否改善；未达目标/停滞保持OPEN或受限，不被安全Gate覆盖 |
| 成本可行性 | 新增闭支持/精确费用、事务工作量和完整CPU可对账；没有预设速度承诺，超限或严重成本单列失败 |

只有质量与恢复事实支持时才建议冻结新政策；否则以“安全成立但恢复受限”“证书/成本失败”等结束。即使部分Gate通过也不更改默认政策。本阶段结果不能自动授权QPC-05或生产默认切换。

## 8. 实施步骤与交付

实施顺序为：冻结/纸面义务 → 定向数值复用和夹具 → 新政策配置/请求/证书 → 原目录与预留接线/续接 → 有限自然A/B → 分层原因报告/视觉/审查。任何依赖阶段失败即停，不为完成后续设施强行放宽契约。

结果写`docs/research/cpu_refinement/qpc_04f_target_quality_results.md`，事实写`docs/codebase/cpu_refinement/qpc_04f_target_quality_facts.md`，实施审查写对应`docs/reviews/cpu_refinement/`。精简freeze、摘要、图与独立证据写`docs/research/cpu_refinement/data/qpc_04f_target_quality/`，大原始产物进忽略的`benchmark-output/cpu-refinement/qpc-04f/run-01/`。更新QC推导日志、总数学页、索引、大规划与本页实现结果。

代码采用DOD展开风格和开发规范summary注释。实施后按真实职责复核数值重复、控制器膨胀、缓存所有权、默认关闭成本及计时边界；取得本阶段实施/提交授权后再按QPC节奏提交并交用户验收。

## 9. 实现结果

2026-09-17，用户确认开始实施。已读取规范与相关源码，保存Peking旧政策原生平台基线。接入前确认核心binary64与公开float曲面存在表示差异；将复用实际位置转换，分别认证内部/公开曲面，局部包围盒枚举补足公开曲面的Q支持。为此有限扩展Samples的局部枚举接口和Mesh的共享位置转换，属于原“实际输出对应”义务，不修改原语、拟合值或渲染契约；新增成本全部登记。后续实现和自然结果见下，不将该最初核查记录当作最终出口。


### 9.1 实施闭环

已完成默认关闭的显式请求、双表示域逐点证书、原目录内筛选、独立回收证书、原子发布核对及政策身份透传。新增QualityEvaluation与PointwiseQuality各司其职；Samples只补公开域局部枚举，Mesh复用相同float位置转换。ProposalEvidence主体不改，新闭样本由证书拥有。

三场景216个机会、12个独立质量帧完成；三组A/B种子相同，B正常/视觉/诊断一致，Peking B单/8线程一致。六预定批次47个非空交换在核心与实际float曲面都通过独立有理条件核查，三个空批不计进展。旧坏补丁只读回归保留，不重跑额外自然路线。

分层结果：损伤与可执行获得有限验证；Peking转向1.996557px、Sierra末帧1.123568px、Canyon转向9.274477px仍未解决，后两者停滞35/33机会。Sierra/Canyon新政策暖CPU为旧政策2.89×/2.00×。本阶段不升级默认，不进入QPC-05。

完整结果、原因与视觉见[结果报告](../../research/cpu_refinement/qpc_04f_target_quality_results.md)，实现边界见[代码事实](../../codebase/cpu_refinement/qpc_04f_target_quality_facts.md)，架构核查见[实施审查](../../reviews/cpu_refinement/qpc_04f_target_quality_review.md)。

### 9.2 核查后登记的实施差异

- 公开曲面认证还需包围框候选c_i，实际成本在原公式之外显式补计；未假定关联数量等于最终闭支持数量。
- QualityProvenance也必须适配新请求/接受政策，已加入只读观察器范围；其旧规则导致过一次诊断失败，正常结果不受影响，失败产物保留。
- 每次接收在线验证严格正下界，但精确进展幅度只导出到六个预定批次；未独立穷验全轨迹或保存全事务幅度。
- 证据字节为累计近似负载，完整进程峰值有记录；没有单证书存活峰值或全部有理中间量位长。成本归因据此受限，不声称完全计量。
- 精确历史坏补丁回归通过独立Python完成，C++采用解析损伤/共享/生命周期夹具。没有把两者合称为对历史整个C++路径的重放证明。

本阶段用户已确认实施；本轮完成后停下交验收，不自动继承ATT阶段的提交/后续实施授权。
