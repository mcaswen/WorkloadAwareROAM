# PQ-01：质量来源追溯与高度策略代码事实

2026-09-15。范围为本次新增诊断、共享回放协议及高度策略的受影响路径，**不是全部事务核心的重新审计**。依据[规划](../../plans/cpu_refinement/pq_01_quality_provenance_plan.md)、当前源码和[实测报告](../../research/cpu_refinement/pq_01_quality_provenance_results.md)。FACT为源码事实，INFERENCE为推断，UNCERTAIN集中列于末尾。

## 1. 模块边界与文件

| 文件（相对仓库根） | 当前职责/依赖 |
|---|---|
| src/benchmark/TransactionalPlatformProtocol.h | FACT：PlatformReplayViews和PlatformReplayCamera；原平台24视图/float相机公式原样共享；依赖RenderContext、Formal相机和GLM，只产生只读输入 |
| src/benchmark/TransactionalPlatformReplay.cpp | FACT：原生图形回放；解析normal/export及其immutable变体，公共设置传入、环境字段输出；继续使用原渲染/更新计时边界 |
| src/experiment/greedy_transactional_lod/TransactionalQualityProvenance.h/.cpp | FACT：两个固定UV的来源、局部变化预测、恢复链、旧高度不变诊断；类自有文件流，不修改核心状态 |
| tests/TransactionalQualityProvenanceProbe.cpp | FACT：原生无渲染探针，用共享相机和公共种子创建核心；直接调用现有Reservation/Pipeline，输出核心身份和真实float网格 |
| tests/TransactionalHeightPolicyTests.cpp | FACT：高度策略夹具；正常目录与收紧目录比较、真实提交、禁止中途改策略 |
| src/algorithms/TransactionalLodSettings.h | FACT：公共配置增加默认false的PreserveSurvivingHeights；默认值比较包含它 |
| src/algorithms/greedy_transactional_lod/TransactionalTypes.h | FACT：核心Configuration增加同名默认false字段 |
| src/algorithms/greedy_transactional_lod/TransactionalSeedBuilder.cpp | FACT：ConfigurationFor把公共字段传入核心 |
| src/algorithms/greedy_transactional_lod/TransactionalPipeline.cpp | FACT：SetView把高度策略列为不得中途改变的规则 |
| src/algorithms/greedy_transactional_lod/TransactionalProposals.cpp | FACT：Prepare构造Free时对H执行高度策略；拟合前确定，不事后回写高度 |
| tests/CMakeLists.txt | FACT：runtime开启时建立原生诊断和高度测试；只有高度测试加入CTest，诊断不是自动自然回放 |
| scripts/run_transactional_platform.py | FACT：native_run新增keyword-only可选arguments；保留默认argv和原生进程超时/内存/隐藏启动规则 |
| scripts/run_transactional_quality_audit.py | FACT：固定Peking采集/离线评价/身份与结果归约；复用旧DOD质量，只读检查完备后建立原始目录链接 |

实验诊断没有链接到正常核心或应用。应用只新增配置传递与回放模式；核心不依赖experiment层。共享协议依赖render的数据类型，没有调用渲染器的状态更新方法。没有新增资源预留算法或第二个目标发现器。

## 2. 高度策略的数据流和生命周期

FACT：

~~~text
normal-immutable / export-immutable
→ TerrainRenderSettings.Transactional.PreserveSurvivingHeights=true
→ TerrainLodBuildInput.Settings.Transactional
→ TransactionalSeedBuilder::ConfigurationFor
→ Configuration
→ TransactionalProposals::Prepare
→ Proposal.Free
→ 原Fit / Measure / Accepts
→ 原Reservation / Apply / ConsumeMesh
~~~

- 普通normal/export以及默认GUI配置保持false；没有增加GUI控件。
- 公共适配的SeedKey包含整个TransactionalLodSettings；值比较变化触发Clear并重建，因此更改策略会重置公共持续状态。
- 直接调用核心Pipeline::SetView时更改策略会抛异常，不在现有状态上混合规则。
- 此次未新增旧JSON原型输入的策略解析/序列化，有限验证入口为原生公共设置与专用探针。
- E/F仍只有新点自由度；H在A是旧中心+新点，在B只有新点。H的支持面、连接、临时ID和初始Points保持同目录规则；没有通过缩小足迹同时改变冲突模型。
- 所有存活点坐标与高度仍从批前State复制；新点先由原面重心插值赋高，然后沿原Fit求增量。
- donor路径没有修改；保留顶点几何沿用原状态，删除中心仍然允许。
- PreserveSurvivingHeights不是HeightGuard，也不保证删点/重连后的内部插值曲面与旧曲面相同。

## 3. Fit的实际选值及诊断对应

FACT：TransactionalCertification.cpp的Fit实现未修改。其关键行为为：

1. 拟合前检查形状和可见样本；空样本返回no_screen_samples。
2. 选出当前核心样本误差见证；以精确旧误差向下取整得到目标，再减10000 micropixels。
3. 各自由高度绝对范围[-HeightScale,2*HeightScale]；相对增量表示。
4. TransactionalProposalEvidence::Get定位新提案面及重心系数，对可见样本建立保守投影/误差约束。
5. 一维选择区间中最接近零的增量；二维选择裁剪多边形顶点算术平均。
6. 高度写入Proposal.Points后，再执行Measure和Accepts；通过的才成为认证提案。

FACT：freeVisibleSupport复用同一ProposalEvidence，在已批准提案每个Free变量上累加对应顶点重心系数，统计非零样本数。该额外证据有独立WorkLedger，不污染正常计时/工作量。

INFERENCE（实测已印证两次）：二维第二变量所有系数为零时，其绝对高度可以保持完整[-12,24]可行范围；顶点平均可选中6。不能把这项推断扩展成任意多边形选择都必然这样，报告限定于具体两次记录。

## 4. 追溯类与所有权

TransactionalQualityProvenance，namespace ParallelRoam::Experiment::GreedyTransactionalLod；由探针栈对象持有，与整个24帧回放等寿命。

| 状态 | 所有权与用途 |
|---|---|
| _future / _returned | 自有Configuration副本，分别固定暴露/返回视图，只用于误差投影，不传回核心 |
| _witnesses / _transactions / _recovery | 自有ofstream；构造时开启fail/bad异常和17位精度，析构关闭 |
| _expected[2] | Before写入的同批局部预测，After比对真实内部曲面 |
| _survivors | 仅B在Before记录全部活动逻辑顶点高度，After检查仍活动的同ID高度是否完全不变 |
| State/Samples/Batch参数 | 借用const引用；不跨异步任务保存，不修改生产缓存或成员集合 |

全域旧高度快照是诊断成本，不是B正常算法为保证高度不变而必须复制状态。正常不变性由提案自由变量限制与原提交路径实现。

### 方法与控制流

- Owner：按固定UV全扫活动面，用robust Contains；多个闭面覆盖时选最小逻辑Face.Id，缺失则异常。
- CurrentHeight/Interpolate：按内部double顶点作分片线性插值。
- ReplacementHeight：只在提案插入/保留面中定位，判断事务是否覆盖见证及其局部预测。
- ReferenceHeight：从原始16位高度样本按固定UV双线性求值，不把核心Q样本编号当作独立见证。
- Visible：判断reference点的完整视锥可见性。
- Error：按固定UV的reference/mesh两高度求屏幕偏移；即使reference不可见但可投影，仍可输出误差，由visible单列定义域。无正投影分母或mesh近面条件失败时写null。
- Rank：用与生产相同的PriorityKey全序求覆盖面名次；非有限/不超过split阈值记0；只诊断不改排序。
- Observe：输出当前/固定未来/返回三个视图误差及高度；Before调用Recovery，After检查预测误差不超过1e-8。
- Before：先Observe，再输出所有批准receiver/donor的点、支持、Free、系数覆盖、证书和固定见证变化，全部基于同一批前状态。
- After：Observe后核查B存活点高度；实际float网格由独立导出评价，故接口不再接受未使用mesh参数。

### 恢复链诊断

Recovery仅追踪固定见证当前覆盖面的根；不是所有潜在修复根的完备搜索。

~~~text
root是否在IntentIds
├─ 否：outside_prefix
└─ 是：输出已记录目录尝试与IntentResult
   ├─ 已批准：selected
   ├─ 未认证：保留原原因
   └─ 已认证未批准：
      原Cursor+Fit重建同一个first certified receiver
      → 收集比本root更早的已批准成员足迹/已用donor
      → 有named credit：只诊断priorConflict
      → 否则遍历同一PoolIds，累计certified/quality/internalFeasible/unused/available
~~~

重建失败或发现无法解释的available交换均抛异常。该诊断专用于本轮HeightGuard关闭协议；没有宣称覆盖其他质量策略或一般预留原因解释。所有实际已批准事务仍记录完整支持是否覆盖固定见证，避免只看root却漏掉邻域影响。

## 5. 线程与成本

FACT：共享核心使用8线程CpuTaskExecutor，Plan/Apply仍为原调度边界。追溯在Plan返回后、Apply前以及Apply/Consume后同步执行，不与核心修改并发。单独额外donor检查串行，不计入正常WorkLedger。

INFERENCE：诊断Owner/Rank和B旧高度比较具有全域扫描成本；额外恢复池认证可能昂贵。它们不是局部生产复杂度主张。正常B增加一个配置检查/提案分支，可能减少自由维数、改变可认证集合，不保证总成本单调下降。

## 6. 原生探针、入口与文件

探针固定Peking设置，从公共SeedBuilder建一次状态；View复用PlatformReplayCamera再走公共BuildTerrainLodViewInput，避免自己复制矩阵乘法/float顺序。

~~~text
读取资产/配置
→ 公共种子 → StateInvariant → Initialize
→ 24次：
  SetView → Reservation::Plan
  → audit.Before → Apply → ConsumeMesh → audit.After
  → 网格hash/决策CSV
  → 关键帧实际float网格导出
→ StateInvariant → 退出
~~~

输出目录必须不存在；不覆盖既有结果。frame0/2/15/16/23导出。三类来源文件为witnesses.csv、transactions.jsonl、recovery.jsonl，另有frames.csv及mesh-N.bin。

脚本用法（WSL中运行，换新输出目录）：

~~~bash
python3 scripts/run_transactional_quality_audit.py collect \
  --output benchmark-output/cpu-refinement/pq-01/NEW \
  --app build/relwithdebinfo-fetch/bin/ParallelROAM.exe \
  --probe build/relwithdebinfo-fetch/tests/RelWithDebInfo/parallel_roam_transactional_quality_provenance_probe.exe \
  --baseline benchmark-output/cpu-refinement/pq-01/run-01/before/ParallelROAM.exe

python3 scripts/run_transactional_quality_audit.py quality \
  --output benchmark-output/cpu-refinement/pq-01/NEW \
  --probe /home/mcaswen/workload-roam-nmp01p-build/tests/parallel_roam_transactional_platform_quality_probe

python3 scripts/run_transactional_quality_audit.py report \
  --output benchmark-output/cpu-refinement/pq-01/NEW
~~~

collect复用native_run的已完成命令缓存；不同实验/程序使用新目录。每次命令记录实际二进制SHA。quality复用旧独立评价器；DOD质量只读链接至已完整评价的旧TPI输出，缺证据直接失败。report复用SEMANTICS/CORE_FIELDS比对及transactional_platform_report.analyze，要求两个配置五个关键帧齐全，然后输出quality-audit.json；不会因缺帧就报告完整通过。

## 7. 验证与变更风险

FACT：高度夹具返回0；A默认24帧前后网格和工作计数一致；独立A/B与平台输出一致；B逐批存活点不变；全域独立评价仍有残余误差，见结果报告。

风险集中在：

- 改Free必须在Fit前；事后强制回写高度会使证书与发布几何脱节。
- 如果未来同时收缩H支持，目录/样本/足迹都可能变化，不能复用本消融的“仅高度自由度”归因。
- 若改变二维可行点选择，应重新验证具体证书与算法结果，不能因为本轮反例就跳过最终接受。
- 不得用固定UV内部double结果代替float网格独立评价；两套采样位置不同。
- 不得把诊断全扫移入普通Update，或让Recovery写回正在测量的批次账本。
- B在返回期没有交换；不能以快速静止时间证明恢复算法有效。

## 8. 规划对照与 Unresolved / Uncertain

实际文件边界符合小规划；补充的native_run可选argv避免重复Windows进程管理，已回填规划。最终审查删除After的无用参数并修订注释/使用提示，没有改变实验策略。

UNCERTAIN：B的frame15新最大见证尚未做完整来源追溯；在规定最小范围内只报告残余。
UNCERTAIN：没有证明当前目录对所有坏区域存在恢复操作，也没有建立恢复速率界。
UNCERTAIN：没有普遍跨视图质量保证，没有同质量性能结论。
当前扫描未发现本次新增对象的关键所有权或销毁顺序不明确事项。
