# QPC-04H 源高度接收提案：代码事实

2026-09-17。范围仅覆盖新增高度政策、准备组件及直接接入点；既有逐点数值和提交机制见[04F事实](qpc_04f_target_quality_facts.md)、[06E事实](qpc_06e_face_evidence_facts.md)。对应[规划](../../plans/cpu_refinement/qpc_04h_source_height_receiver_plan.md)。

## 1. 配置与生命周期

**FACT**：`src/algorithms/TransactionalLodSettings.h` 新增 `TransactionalReceiverHeightPolicy::{LegacyFit,SourceHeight}`，默认 `LegacyFit`；核心 `Configuration` 同名字段由 `TransactionalSeedBuilder` 映射。普通设置比较包含该字段，平台继续走 Reset。`TransactionalPipeline::SetView` 显式禁止热切换。

`TransactionalPointwiseQuality::Validate` 要求源高度只能用于 `PointwiseTarget`；既有逐点配置校验继续要求固定旧点、`ErrorFirst`、关闭 `HeightGuard`。目标像素与高度比仍须有限且为正。

实验字段 `receiverHeightPolicy` 在 `ExperimentCase`、JSON schema、Python catalog、任务身份、suite 过滤和 `ExperimentReplay` 贯通。值为 `legacy-fit/source-height`，省略时沿旧路径；只有非默认值额外进入任务身份，以保留历史默认任务兼容。RecoveryTrace 复用相同 Replay/SeedBuilder，不复制配置决策。

## 2. 独立源高组件

**FACT**：`src/algorithms/greedy_transactional_lod/TransactionalSourceHeightReceiver.h/.cpp` 只负责 `Prepare(state,samples,proposal,work)`。依赖 Samples、Predicates 和共有数据，不依赖实验、渲染或预留控制器。组件没有缓存、虚函数或跨帧可变成员。

输入必须来自当前快照的 `ReceiverCursor`，不是外部任意三角化。只接受 E/F/H，要求唯一自由点为新点、原支持有序且存活、旧点与状态一致、临时身份位于原根的八项范围、无额外旧点。参数坐标必须有限且在单位域内。

仅写私有 `Proposal`：清除此前质量/高度证书和成功理由，置 `HasLegacyQualityEvidence=false`，给新点赋 `TransactionalSamples::SourceHeightAt` 的 binary64 值。源查询继续使用原 U16 + 双线性表达式，不使用拟合区间或样本吸附。旧点、连接、支持、身份和目录次序均不修改。

原 `Shape` 检查在净面数检查之前：舍入边中点可能形成额外薄面，必须继续返回 `shape_infeasible`，否则会改变 `NeedsFlipRecovery` 的资格。形状通过后要求 `Faces.size()==Support.size()+2`。准备返回空串只表示前提齐备；它不产生 `certified`，也不分配预算或修改生产状态。

访问 O(d) 条局部几何记录不等于 O(d) 时间：身份表/有序容器查询上界 O(d log N+d log d)，源高插值固定四个栅格值。形状谓词的精确算术位长另计；没有全活动网格扫描。

## 3. 调用、控制流和发布

**FACT**：`TransactionalReservation::Plan` 原接收任务中新增一个显式分支：

```text
ReceiverCursor::Next
  SourceHeight 且 E/F/H → SourceHeightReceiver::Prepare
  其他                 → Proposals::CertifyReceiver（原 Fit/B 路径）
准备/旧认证成功 → PointwiseQuality::Certify
完整通过       → 写 certified → 按原前缀次序收集
全部形状失败   → 原 FlipRecovery
预算/冲突预留 → ValidateBatch → Commit/Samples/Mesh 原续接
```

源高分支不回退旧 Fit。B、R、donor 仍走原构造/认证规则；状态变化可能改变它们后续被访问的数量，不能说两政策下执行数量相同。

`Certify` 的损伤/进展、双域、完整支持和精确回退公式未改。`ValidateBatch` 额外比较证书配置中的高度政策，原点/面/支持/快照版本绑定保持。缺证书、改高复用证书或过期批次都不能发布。

并发边界未变：每个接收任务只写自己的提案/槽位/局部账本；屏障后按全局根序消费。新增组件只读 state/samples，不并发写生产容器。

## 4. 旧误差字段与观测

**FACT**：新源高项没有旧统一最大值证据。`Proposal::HasLegacyQualityEvidence` 默认 true，源高准备置 false；`Certification::Accepts` 对 false 抛出异常，防止以默认零目标使用旧证据。该字段描述证据类别，不是逐点安全标志。

生产逐点配对不读取源高项的旧 Target。`TransactionalQualityProvenance` 按相同高度政策重建私有提案，旧门槛/误差输出对源高项写为不适用。`TransactionalExchangeQualityAudit` 和 RecoveryTrace 的政策元数据加入该字段。

`TransactionalProposalFeasibilityAudit` 明确仍是04G旧 Fit 固定输入审计，只接受 `LegacyFit`。本轮对其冻结输入添加源高试值，并以原生 `verifiedWitnesses` 复核；不得把新演化轨迹冒充原04G输入。

账本新增 `SourceHeightAttempts/SourceHeightPrepared`，由 `TransactionalExecution` 归并；理由中记录准备成功、形状/结构拒绝与逐点结果。Tracy新增 `gtp.receiver.source_height`，旧 Fit 和逐点认证的 zone 保留。正常采集没有新增逐样本 profiler。

`scripts/analyze_transactional_source_height.py` 复用既有有理局部审计、perf/Tracy归约和独立质量文件，只负责本轮归约与图表。正式原始运行放忽略目录，报告数据放 `docs/research/cpu_refinement/data/qpc_04h_source_height/`。

## 5. 符号/执行路径索引

| 符号与文件 | 所有权、调用者与职责 |
|---|---|
| `TransactionalSourceHeightReceiver::Prepare` | Reservation/专项测试调用；借用只读快照，修改调用者私有提案 |
| `TransactionalReceiverHeightPolicy` | 公共设置与核心配置；Reset/SetView 界定生命周期 |
| `Proposal::HasLegacyQualityEvidence` | 接收准备写，旧 Accepts 与诊断读；不替代 QualityProof |
| `WorkLedger::SourceHeightAttempts/Prepared` | 独立任务拥有，Execution 屏障后归并 |
| `PointwiseQuality::Validate/ValidateBatch` | 配置入口和发布前绑定；不改变认证公式 |
| `TransactionalSourceHeightReceiverTests.cpp` | 原游标、成功/拒绝、额外薄面拒绝类别、证书篡改与续接专项 |

## 6. 实际边界与不确定事项

**FACT**：此分支是显式、默认关闭的算法消融；不等价于原 P 的选择或输出，也没有替代所有 Fit。其前提依赖合法游标；若未来允许外部提案，需要另建结构合法性责任，不能扩张本组件的承诺。

**INFERENCE**：删 Fit 同时删除前置筛选，会增加逐点认证人口和后续事务工作；该解释由本轮调用数和持续运行支持，不能用于推断任意地形的比例。

**UNCERTAIN**：有限采集不证明全输入安全、连续曲面或未来视图恢复；旧路径同结果性能残差是否存在代码因素仍须与实验条件区分。本轮不改旧求解器、数值核、预留策略或渲染接口。
