# QPC-04B：单侧边界细分的代码事实

日期：2026-09-16。范围是本阶段新增 B 原语及其直接接线，不替代原事务核心事实文档。对应[规划](../../plans/cpu_refinement/qpc_04b_boundary_integration_plan.md)。下述 FACT 均来自实现扫描；自然结果另见研究报告。

## 1. 模块与依赖

FACT：生产实现位于 `src/algorithms/greedy_transactional_lod/`，命名空间 `ParallelRoam::Algorithms::GreedyTransactionalLod`。唯一新核心组件为 `TransactionalBoundaryRefinement`，它不拥有持久状态，只构造/认证有限提案和检查提交证书。

```text
实验 boundaryRefinement → 公共设置 → SeedBuilder::ConfigurationFor
    → ReceiverCursor：E/B → F → H
    → Proposals::CertifyReceiver → B 专用认证 / 原 Fit
    → Reservation：按实际成本命名 → 原顺序冲突预留
    → Commit::Prepare：借用 Source，检查 B 证书
    → Samples/Mesh::Prepare → 唯一 Publish → 公共 mesh/Pending
```

FACT：生产代码不调用 `TransactionalBoundaryAudit`、离线质量评价器或未来视图。04A 观察器保留独立公式；生产源高查询按同一 raw U16 双线性算术计算。没有新增渲染后端接口。

## 2. 文件与职责

| 文件（除注明外位于核心目录） | 实际职责 |
|---|---|
| `TransactionalBoundaryRefinement.h/.cpp` | 三个无状态静态入口，单侧构造、当前视图认证、源高结构复核 |
| `TransactionalProposals.h/.cpp` | 根内目录、八项身份、按种类认证派发、翻边资格过滤；不发布 |
| `TransactionalSamples.h/.cpp` | 新纯函数 `SourceHeightAt`；原 Samples 继续唯一持有 HeightSource |
| `TransactionalReservation.h/.cpp` | 纯命名步骤 `AssignFreeFaces`、每意图资金来源、原资源冲突顺序、面数账本 |
| `TransactionalTypes.h` | 默认关闭配置、B 事务种类、`BudgetFunding`、`IntentBudget`、批次及工作统计 |
| `TransactionalCommit.h/.cpp` | 在原统一准备/发布中复核 B，创建边界点记录、核对实际目标预算 |
| `TransactionalState.h/.cpp` | 初建政策前提与持久 `VertexRecord::Boundary`，无第二份源 |
| `TransactionalPipeline.cpp` | SetView 政策冻结，准备时向 Commit 借用 `_samples.Source()` |
| `TransactionalSeedBuilder.cpp` | 公共输入合法性和核心配置传递 |
| `TransactionalTerrainLodAlgorithm.cpp` | 原 SeedKey 全设置身份与失败生命周期；账本改在异常保护内构造 |
| `TransactionalExecution.cpp`、`TransactionalRenderBridge.cpp` | 合并线程局部 B 计数；向公共统计导出数量/面数 |
| `src/algorithms/TransactionalLodSettings.h`、`TransactionalLodStats.h` | 对外政策与统计，不承担几何 |
| `src/experiment/infrastructure/ExperimentCase.*`、`configs/experiments/schema/case.schema.json` | 共同输入字段与合法组合 |
| `src/benchmark/experiment/ExperimentReplay.*` | 公共设置接线、正常路径预算旁表 |
| `scripts/experiment_infrastructure/catalog.py`、`runner.py`、`suite.py` | 配置冻结、启用时进入 taskId、跨算法配置不泄漏政策 |
| `src/benchmark/experiment/TransactionalRecoveryTrace.cpp` | 从真实 B 历史推进期望边界，再独立扫描实际边界；真实面数差复核 |
| `src/experiment/greedy_transactional_lod/TransactionalQualityProvenance.cpp` | 诊断认证使用同一派发入口；按显式资金来源解释失败 |
| 同实验目录 `TransactionalDynamicReference.cpp` | 本阶段不支持 B，入口显式拒绝 |
| `tests/TransactionalBoundaryRefinementTests.cpp` | 几何、真实认证、混合预算、净减一、反序与连续续接、有限终止 |
| `tests/TransactionalFlipRecoveryTests.cpp` | 原测试增加公共边界政策切换/Reset 路径 |
| `scripts/run_transactional_boundary_integration.py` | 冻结/调用既有 runner、质量与有限归约；不实现另一套质量公式 |

FACT：`TransactionalMesh`、Samples 局部 repair、StateInvariant 和 Validation 沿用原实现。新增类型正确进入既有准备记录后，边界关联、闭面样本、法线和 Pending 均由原所有者更新，而非新增边界旁路。

## 3. 几何与证书

### 3.1 Construct

`Construct(state, source, root, edge, newVertex)` 的输入均为只读引用/值，返回自有 `Proposal`。

1. 初始化 `Kind='B'`；规范边键，要求关联数量为一且唯一面就是 root。
2. 端点必须同处 `u=0/1` 或 `v=0/1`。域内单侧边不被当作外边界。
3. 创建 UV 中点。源宽高至少二；两段长度都至少为对应 `1/(resolution−1)`；float 中点不能与任一端点同坐标。
4. 新身份不大于 `NextVertexId`，且不能等于最小 int64；目录另检查八项序号及整体减法范围。
5. 通过 `Samples::SourceHeightAt` 取得唯一新高度。复制旧三个点；以旧面另外两条有向边连接新点，得到两个正向面。
6. 原 `Predicates::Shape` 核对两面，失败保留明确 `Reason`。

FACT：`Support={root}`、`Free={newVertex}`；Free 在这里表达新点写入，不表示允许高度拟合。旧点没有高度自由度。

`SourceHeightAt` 校验完整源长度、有限 UV/尺度及单位域；使用末栅格单元表达 UV=1，raw U16 插值后乘尺度/65535。旧六分格 `SourceHeight` 未改动。

### 3.2 Certify

`Certify` 先记录 B 尝试/源尺度拒绝，几何通过后读取 `VisibleSupport`，调用原 `SetProgressTarget`、`Measure` 和 `Accepts`。不调用 Fit。认证依赖当前配置、样本证据和原 `0.01px` 进展目标；无可见样本或不确定结果不放行。

FACT：源高新点不等于整个替换曲面逐点更优，当前视图最大值规则仍允许误差交换。

### 3.3 IsSourceMidpointSplit

提交复核要求种类 B、恰一个根支持、两个面、四个点和唯一新点写入。枚举根的三条边重新 Construct；必须存在一个有效构造与提交的点逐值相同、方向规范后的面集合相同。仅伪造 Kind 或净 +1 不足以通过。

## 4. 目录与严格顺序

FACT：`ReceiverCursor::Next` 在排序边阶段遇双侧边走原 E，开启时遇单侧边返回 B（包括带拒绝原因的提案），然后原 F/H。E+B≤3、F≤2、H≤3。每个实际提案消耗序号；关闭时单侧边仍跳过，不改变旧身份或顺序。

`ProposalIdentity` 返回 `NextVertexId−8·rootSlot−ordinal`，拒绝 ordinal≥8 和不可表示减法。失败提案不推进生产分配器。

`CertifyReceiver` 只按 B 分派，其他仍走原 Fit。并行认证完成后，Reservation 依全局前缀索引收集成功项；线程完成顺序不参与选择。

`NeedsFlipRecovery` 只看 E/F/H：至少尝试一个，且全部 `shape_infeasible` 才返回 true。B 的质量/分辨率失败不会取消或独自触发原翻边。

## 5. 预算模型和发布

FACT：`IntentBudgets` 与 `IntentIds` 等长，未认证项为 None/0，翻边为 ZeroCost/0，B 成本1，其他接收成本2。`AssignFreeFaces` 按意图顺序整笔命名；不足则记 Donor，不消耗部分余量。此记录只活到批次销毁。

| 字段 | 含义/来源 |
|---|---|
| AssignedCredits / UnusedCredits | 获空额/未兑现的接收数量，保持旧口径 |
| AssignedFaces | 命名总面数 |
| ConsumedFreeFaces | 成功 Free 接收成本之和 |
| UnusedFaces | AssignedFaces−ConsumedFreeFaces |
| ReleasedFaces | 成功配对的 `2−receiverCost` 之和 |
| NetFaceChange | ConsumedFreeFaces−ReleasedFaces，带符号 |
| BoundaryFreeExecuted / BoundaryPairedExecuted | 总接收执行数的 B 子集，不重复加入分母 |

FACT：已命名失败项不走 donor，也不返还额度；配对 B 的净一面盈余只在下一批由实际面数体现。原 donor 顺序、no-reflow、Conflict/Footprint 和诊断完整扫描选择保留。

Commit 先读取批次版本。B 分支要求政策与只读源，并调用完整结构复核；普通 +2 分支显式拒绝 B/R。捐赠中心不能是边界点，实际支持与目标面差仍必须为 −2。收集真实 removed/inserted 后核对目标预算；有 IntentBudgets 的生产批次还核对 NetFaceChange。旧手造测试批次仍允许没有新账本，但不绕过实际硬预算。

FACT：Pipeline 只借用 Samples 的源，PreparedTopology 不保存引用；直接 `Commit::Apply` 没有源输入，收到 B 明确拒绝。新点记录的 Boundary 来自已检查 B 集合，不能由外部随意指定。

## 6. 持久状态与失败边界

FACT：旧单侧边被删除，两条子边各一面，新内部边两面。新边界点保留邻接路径并排除 donor 索引；旧共享点只读高度，邻接变化在同一 PreparedTopology 合并。Prepared faces/slots、活动数组尾交换、Samples/Mesh repair 和唯一 Publish 沿原路径。

原 Pending 机制处理净 +1/−1 及连续两批未消费，不新增全量 rebuild。核心版本只在成功发布后推进；过期批次拒绝。配置开关进入 SeedKey，公共调用切换时重建；核心 SetView 不允许改策略。

FACT：适配器原 WorkLedger 在 try 外构造；MSVC 有序容器空构造分配使首个故障可能逃出公共错误通道。当前改用无分配的 optional 容器，在原 try 内 emplace；异常时原状态、错误类型和已发布网格恢复路径继续负责处理。这是已复现的生命周期缺口，不是 B 几何产生的全局恢复要求。

## 7. 成本和限制

FACT：B 几何/源查询的元素数量有界，但依赖原有映射查询；认证仍随支持样本数增长。Submit 的三边证书重建完整计入 prepare。预算新增 O(r) 记录/遍历；没有优化原配对与批间冲突复杂度。

FACT：正常路径没有新增全域边界扫描。完整边界核对仅在 trace；期望状态按获批 B 替换边，实际状态独立从所有边重建，再比较旧点及边集合。旁表记录实际边界点数。

INFERENCE：源尺度限制使每条种子边最终段数有限，但边界点仍不可回收，内部可用预算可能随时间下降。有限路线和解析终止不证明长期质量或预算分配最优。

FACT：默认关闭；需要固定旧点且 HeightGuard 关闭。动态参考入口未扩展。共同 schema/runner 支持新政策，未启用时保持既有 taskId；suite 对其他算法清除该政策，防止误传。

## 8. 证据归属

实验产物位于忽略目录 `benchmark-output/cpu-refinement/qpc-04b/run-01`，旧二进制、构建/失败记录、冻结输入、逐帧/事务、质量和截图分开保存。报告脚本调用现有独立 C++ 实际 float 网格评价器，不将内部 double 见证替代全域质量。原始失败保留，定向修复后测试记录另名保存。

最终阶段状态、有限输入结果、视觉观察及限制以 [QPC-04B 结果](../../research/cpu_refinement/qpc_04b_boundary_integration_results.md)为准。
