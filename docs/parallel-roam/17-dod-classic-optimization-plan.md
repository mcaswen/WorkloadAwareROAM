# DOD 与 Classic ROAM 优化问题规划

> 日期：2026-08-10
>
> 状态：实施规划 v1.5，B1-B4、C1-C2、D1 增量基础已完成；D1 自适应选择暂缓
>
> 范围：Data-Oriented CPU ROAM 相对 Classic CPU ROAM 的 CPU 热路径
>
> 基准：`RelWithDebInfo`、统一 ROAM 误差公式、持久 `Q_s/Q_m`、相同阈值与固定活动叶三角形预算

## 1. 目标

本计划解决两个容易混淆的问题：

1. 让 DOD 的串行 split/merge 在相同算法职责和相同拓扑事务数量下，尽量接近 Classic 的单操作成本；
2. 保留并证明 DOD 在连续扫描、批量评分、并行提交和高变化率 Mesh 输出中的额外收益。

本计划不把“DOD 单核一定超过 Classic”设为结论。Classic 使用裸指针直接修改少量离散节点，这种布局可能天然适合串行拓扑事务；DOD 的目标是先消除当前实现额外支付的原子操作、节点代理、队列整理和全量输出成本，再判断剩余差距是否来自指针与 SoA 索引布局本身。

本文把后续工作封闭为八类问题。除非 profiler 在相同输入、相同拓扑事务数量下发现一个稳定占用显著时间、且不能归入这八类的新阶段，否则不继续增加新的优化问题。

## 2. 当前结论与基线

当前实现已经完成以下对齐：

- Classic 与 DOD 共用 ROAM 1997 公式 (1) 的嵌套楔形厚度，以及公式 (2)/(3) 的保守像素投影；
- 两者共用 split/merge thresholds、FOV、可绘制区域尺寸、视锥和活动叶三角形硬预算；
- 两者都持久维护 `Q_s/Q_m`，并在预算饱和时持续执行低损失 merge 与高收益 split 的资源交换；
- DOD 已直接复用 `ActiveLeafNodes`，不再递归收集或复制最终活动叶节点；
- DOD 的队列评分和候选标记已经快于 Classic，不再是下一阶段的主要优化对象；
- DOD 串行 topology 已使用普通预算计数和编译期专用入口；atomic token 只供并行 worker commit；
- Classic 已实现增量 Mesh slot、dirty range 和增量 upload，DOD 仍完整 emit CPU Mesh。

最新单轮参考报告：

- [默认选项路径报告](../../benchmark-output/runtime-benchmark-20260810-002908.md)
- [极限压力路径报告](../../benchmark-output/runtime-benchmark-20260810-003052.md)

| 路径 | 阶段 | Classic CPU | DOD CPU | 当前判断 |
| --- | --- | ---: | ---: | --- |
| 默认 | Merge mark | 0.4333 ms | 0.1870 ms | DOD 已更快，不优先优化 |
| 默认 | Merge topology | 0.0047 ms | 0.0210 ms | DOD 单操作维护成本仍高 |
| 默认 | Split scan/mark | 0.4486 ms | 0.3225 ms | DOD 已更快，不优先优化 |
| 默认 | Split topology | 0.0093 ms | 0.0342 ms | DOD 单操作维护成本仍高 |
| 默认 | Mesh emit | 0.0048 ms | 1.0635 ms | Classic 增量输出优势明显 |
| 默认 | CPU upload | 0.0040 ms | 0.2092 ms | DOD 仍支付完整 Mesh 上传成本 |
| 极限压力 | Merge mark | 14.6630 ms | 2.9499 ms | DOD 批量评分优势明显 |
| 极限压力 | Merge topology | 32.2559 ms | 32.6699 ms | 帧耗时接近，但 DOD 每帧事务数更少 |
| 极限压力 | Split scan/mark | 43.6063 ms | 8.5509 ms | DOD 批量评分优势明显 |
| 极限压力 | Split topology | 67.7276 ms | 57.2506 ms | DOD 帧耗时更低，但单操作仍有差距 |
| 极限压力 | Mesh emit | 44.5763 ms | 17.7158 ms | 高变化率下 DOD 全量并行 emit 有优势 |

以上数据只用于确定优化方向，不作为正式性能结论。正式 A/B 必须多轮重复，并排除 `timeSeconds == 0` 的初始化样本。

## 3. 八类优化问题总表

| ID | 问题 | 性质 | Classic 现状 | 优先级 | 主要依赖 |
| --- | --- | --- | --- | --- | --- |
| O1（已完成） | 串行路径仍使用 atomic triangle budget | 串行对齐 + 保留并行能力 | 普通 `size_t` 预算 | P0 | 与 O2 同阶段 |
| O2（已完成） | 串行和并行共用带运行时分支的拓扑函数 | 串行对齐 + 保留并行能力 | 精简串行函数 | P0 | O1、O7 |
| O3 | `ActiveLeafNodes` 同时承担活动集合和 split heap | 追平 Classic 的 heap 局部性 | 独立 `{Node, Score}` heap | P1 | O8；为 O6 提供稳定槽位基础 |
| O4 | 串行拓扑事务反复分配、排序和去重队列邻域 | 主要追平 Classic | 小邻域即时去重 | P0 | O2 |
| O5 | merge 并行批处理的整理成本高于 worker 提交成本 | DOD 额外能力 | 无并行 merge 对应物 | P2 | O3、O4、O8 稳定后再做 |
| O6 | DOD 每帧完整 emit 和 upload CPU Mesh | 追平 Classic + 自适应扩展 | 增量 Mesh slot/range | P1 | O3 的稳定叶节点/slot 语义 |
| O7（已完成） | 拓扑热路径仍构造完整 SoA proxy | 追平 Classic 的直接字段成本 | 指针直接访问字段 | P0 | 与 O2、O4 一起完成 |
| O8 | 活动索引和 merge queue 旁路数组访问分散 | 混合：对齐 + DOD 连续索引能力 | 节点内 intrusive 状态 | P1 | O3、O5 |

## 4. 优先级定义

### P0：先完成，否则串行对比不公平

P0 只处理串行 topology commit 中 DOD 比 Classic 多支付的固定成本：

- atomic budget；
- `TopologyCommitCounters*` 和 `counters == nullptr` 运行时分支；
- 每事务临时邻域容器和 `sort + unique`；
- 只读或少量写入仍构造完整 proxy。

P0 完成后，串行 split/merge 的控制流应当与 Classic 基本对应，剩余核心差异主要是裸指针访问与 SoA 索引访问。

### P1：解决队列布局和完整帧输出差距

P1 包含：

- 把活动叶节点连续视图与 split heap 排序状态分离；
- 压缩和重组活动索引、反向位置及 merge queue 元数据；
- 为 DOD 增加稳定 Mesh slot、topology edit、dirty range 和增量 upload；
- 根据拓扑变化比例，在增量 emit 与全量并行 emit 之间选择实测更快的路径。

P1 完成后，默认路径不应再因为全量 Mesh 构建和上传掩盖 DOD 的评分收益。

### P2：优化 DOD 独有的并行 merge 能力

P2 不用于追平 Classic，而用于证明 DOD 的额外能力是否值得保留：

- 复用 merge candidate、chunk 和 worker result 缓冲；
- 批量合并邻域失效和刷新；
- 降低 worker join 后的主线程索引/队列整理；
- 根据候选规模选择串行或并行执行策略，但不限制拓扑事务总量，也不改变持续收敛语义。

## 5. 阶段 A：建立不可变基线

### A1. 固定对比口径

每个优化提交之前和之后都必须使用：

- 相同提交基础、构建配置和图形后端；
- 相同 HeightMap、terrain size、height scale、max depth；
- 相同 split/merge thresholds、FOV、可绘制区域尺寸和 triangle budget；
- 相同离散相机采样点数，并逐个核对 `sampleIndex` 与相机姿态；
- DOD 串行 topology 对比时关闭并行 split；
- Classic 和 DOD 都启用相同拓扑验证设置；
- 默认选项路径和极限压力路径各运行不少于 5 轮。

### A2. 统一统计

核心指标：

```text
稳定样本 CPU update
Merge candidate mark
Merge topology
Split candidate mark
Split topology
Mesh emit
Finalize
CPU upload
split/merge 总操作数
queue membership update 数
forced split 数
活动叶三角形数
拓扑错误、无效邻接、T-junction
```

必须额外计算：

```text
split 单操作成本 = sum(cpuSplitTopologyMilliseconds) / sum(splits)
merge 单操作成本 = sum(cpuMergeTopologyMilliseconds) / sum(merges)
```

不得仅比较每个采样点的 topology 时间；当前 runtime benchmark 已固定三种算法的路径采样点数和姿态序列，还必须结合每点与累计的 topology 事务数计算单操作成本，避免把工作量差异误读为实现效率差异。

## 6. 阶段 B：串行 topology 公平对齐

### B1. O1 + O2：串行预算与串行拓扑专用路径

#### 实现前问题

旧版 [`TryAcquireSplitBudget`](../../src/algorithms/data_oriented_roam/DataOrientedRoamTopology.cpp#L312) 在串行 split 中仍执行 atomic load/CAS；旧版 [`SplitNode`](../../src/algorithms/data_oriented_roam/DataOrientedRoamTopology.cpp#L451)、`MergeSingleNode` 和多个统计 helper 通过 `TopologyCommitCounters*` 同时服务串行和并行路径。

#### 实施内容

1. 为串行收敛维护普通预算计数；
2. atomic token 只供并行 commit 使用，或者由主线程批量预留后分配给 worker；
3. 建立编译期可裁剪的串行/并行拓扑入口；
4. 保持 forced split closure 的预算预留和失败归还语义；
5. 邻接修复、child 复用和 diamond 规则仍共用底层 helper，避免复制两套拓扑算法；
6. 串行路径不创建 worker-local counter，也不执行 `counters == nullptr` 分支。

#### 实现结果（2026-08-16）

- `DataOrientedRoamState` 分离 `RemainingSerialSplitBudget` 与 `RemainingParallelSplitBudget`；
- `SerialTopologyCommitPolicy` 直接修改普通预算和全局统计，`ParallelTopologyCommitPolicy` 只修改 atomic token 与 worker-local counters；
- `SplitNodeImpl`、`MergeSingleNodeImpl` 和 `MergeNodeOrDiamondWithScoreLimitImpl` 通过模板策略在编译期裁剪串行/并行差异，forced split、邻接修复、child 复用和 diamond 规则仍只有一份实现；
- 并行 split join 后按 `ActiveLeafNodes.size()` 一次性恢复串行剩余预算，串行收敛期间不再访问 atomic token；
- 修改前后 smoke 6 帧、budget-reentry 5 帧和 standard 64 帧逐帧对照，活动叶、节点数、split、forced split、merge、预算拒绝和拓扑错误均无差异；RelWithDebInfo 构建和 CTest 10/10 通过；
- 应用级串行路径 8 个离散采样点完整通过，DOD 并行 Split 关闭时最大活动叶为 12591、拓扑问题为 0；20 万预算的 budget-saturation 24 帧保持 199999-200000 个活动叶，使用最多 8 workers，发生并行 split/merge commit 且拓扑问题为 0；
- 本轮 headless profile 只用于正确性回归，不能据此给出稳定性能结论；串行 split/merge 单操作收益仍需在新的离散采样 runtime benchmark 中隔离进程并多轮验证。

当前代码证据：[`SerialTopologyCommitPolicy`](../../src/algorithms/data_oriented_roam/DataOrientedRoamTopology.cpp#L279)、[`ParallelTopologyCommitPolicy`](../../src/algorithms/data_oriented_roam/DataOrientedRoamTopology.cpp#L340)、[`SplitNodeImpl`](../../src/algorithms/data_oriented_roam/DataOrientedRoamTopology.cpp#L503)、[`SynchronizeSerialSplitBudget`](../../src/algorithms/data_oriented_roam/DataOrientedRoamTopology.cpp#L413)。

#### 验收

- `roam_budget_reentry_dod` 通过；
- 默认与压力路径 topology issue 均为 0；
- 活动叶三角形不超过预算；
- 相同固定相机序列下，优化前后最终活动叶集合、split/merge 总量和 forced split 总量一致；
- 串行 split/merge 单操作成本下降；
- 并行 split 打开时结果不退化。

#### 回退条件

若普通预算与 atomic 预算之间需要每次事务同步，导致串行路径没有稳定收益，则改为主线程按批次预留，而不是维护两份逐事务同步状态。

### B2. O4：小邻域无分配去重

#### 当前问题

DoD 的 [`NormalizeQueueNeighborhood`](../../src/algorithms/data_oriented_roam/DataOrientedRoamTopology.cpp#L1071) 对并行批处理的邻域执行 `sort + unique`；Classic 的 [`AppendQueueNeighborhood`](../../src/algorithms/classic_roam/ClassicRoamQueues.cpp#L386) 在插入时完成小集合去重。

#### 实施内容

1. 根据邻域扩张规则推导单次事务的最大候选数量；
2. 使用栈上固定容量数组或项目已有的小型容器；
3. 插入时即时去重，不在串行路径排序；
4. 容量不足时保留可验证的安全回退，不允许静默丢失节点；
5. 并行批处理仍允许集中收集后统一去重；
6. 失效和刷新尽量复用同一份规范化邻域。

#### 实现结果（2026-08-16）

- 新增 `DataOrientedRoamNeighborhood`，常规串行邻域使用 96 个索引的栈内固定容量；该容量覆盖一次事务最多四个 seed 的邻域扩张，并保留超限后的可扩容安全回退；
- 串行 `SplitNodeImpl` 和 `MergeNodeOrDiamondWithScoreLimitImpl` 改为插入时线性去重，不再创建邻域 `vector`，也不调用 `NormalizeQueueNeighborhood`；
- 并行 split/merge 的批量邻域仍使用 `vector + sort/unique`，因此没有改变 worker 提交和主线程整理语义；
- RelWithDebInfo 编译、CTest 10/10 和 [runtime-benchmark-20260816-220741.md](../../benchmark-output/runtime-benchmark-20260816-220741.md) 通过；当前 benchmark 用于回归确认，B2 的单操作成本收益仍需与 B1 基线做独立多轮 A/B。

#### 验收

- 每次串行 split/merge 不发生邻域 vector 动态分配；
- 串行路径不调用 `NormalizeQueueNeighborhood`；
- queue membership 和 heap validator 无新增错误；
- 单操作 topology 成本下降。

### B3. O7（已完成）：完成热路径 proxy 清理

#### 当前问题

评分路径已经使用标量访问器，但以下热路径仍存在完整 proxy：

- [`SplitNodeImpl`](../../src/algorithms/data_oriented_roam/DataOrientedRoamTopology.cpp#L544) 的 parent/child 写入；
- `SafeInteriorSplitChunkId`、`SafeInteriorMergeChunkId` 和 chunk 安全检查；
- [`EvaluateMergeCandidateImpl`](../../src/algorithms/data_oriented_roam/DataOrientedRoamCandidateMarking.cpp#L10)；
- [`AppendPersistentMergeQueueNeighborhood`](../../src/algorithms/data_oriented_roam/DataOrientedRoamQueues.cpp#L632)。

#### 实施内容

1. 只读判断改用标量访问器；
2. 多字段写入改为专用操作，例如设置 split 状态、清空 child 邻接、更新 parent 邻接；
3. 保留完整 proxy 给低频验证代码；
4. 不为每个 SoA 字段机械增加公开接口，只覆盖实际热路径。

#### 实施结果（2026-08-16）

- [`DataOrientedRoamNodePool`](../../src/algorithms/data_oriented_roam/DataOrientedRoamState.h#L143) 只增加 neighbor、chunk 和调试状态所需的标量只读访问器，写入仍由拓扑专用操作统一完成；
- [`EvaluateMergeCandidateImpl`](../../src/algorithms/data_oriented_roam/DataOrientedRoamCandidateMarking.cpp#L10) 与 [`AppendPersistentMergeQueueNeighborhood`](../../src/algorithms/data_oriented_roam/DataOrientedRoamQueues.cpp#L632) 已改为按 node index 读取单个 SoA 字段；
- [`PrepareSplitNodeState`](../../src/algorithms/data_oriented_roam/DataOrientedRoamTopology.cpp#L455) 和 [`PrepareMergedNodeState`](../../src/algorithms/data_oriented_roam/DataOrientedRoamTopology.cpp#L478) 集中完成 split/merge 的多字段状态写入，neighbor 修复和 chunk 安全检查也不再构造完整 proxy；
- [`WriteDomainTriangle`](../../src/algorithms/data_oriented_roam/DataOrientedRoamMeshEmit.cpp#L47) 与活动叶统计直接接收 node index，正常 Build 的 emit/finalize 路径不再为了读取少数字段构造 proxy；
- `DataOrientedRoamNodeRef`、`DataOrientedRoamNodeConstRef` 和 `operator[]` 继续保留给 reset、递归诊断和拓扑 validator，不再出现在候选评分、持久队列、拓扑提交、chunk 检查及 emit 热路径；
- RelWithDebInfo 编译通过，CTest 10/10 通过；汇编级内联结果仍需 profiler 或反汇编确认，因此本阶段只确认源码热路径已经消除 `operator[]` 调用。
- [B3 默认路径报告](../../benchmark-output/runtime-benchmark-20260816-224629.md) 中 Classic 与 DOD 的平均/最大活动叶三角形数完全一致，拓扑问题为 0；DOD Split topology 为 0.0817 ms，Merge topology 为 0.0519 ms。相对 B2 单轮报告的 0.0932 ms / 0.0618 ms 没有出现回退，但两次是不同进程采样，不能代替同进程 A/B 或 profiler 结论。

#### 验收

- 正常 Build 热路径不再构造 `DataOrientedRoamNodeConstRef`；
- `DataOrientedRoamNodeRef` 只保留在确实同时修改大量字段且实测无损的位置；
- 汇编或 profiler 中不再出现热路径 `operator[]` 调用；
- 正确性结果不变。

### B4. 阶段 B 决策点（已完成）

阶段 B 完成后先停止实现并重新测量：

- 若默认和压力路径的 DOD 串行 split/merge 单操作成本已接近 Classic，进入阶段 C；
- 若差距仍明显，使用 profiler 只检查 `SplitNode`、`MergeSingleNode`、heap 维护和邻域队列更新；
- 若 profiler 显示主要剩余时间来自随机 SoA 索引访问，而不是额外函数、分支或分配，则把该差距记录为当前数据布局对随机拓扑修改的成本，不继续增加新问题。

#### 决策结果：2026-08-17

- RelWithDebInfo 构建与 CTest 10/10 通过；
- 默认路径剔除 `timeSeconds == 0` 的初始化样本后，DOD 相对 Classic 的单次 split/merge 拓扑成本仍分别高约 33% 和 52%，但绝对差值合计只有约 0.0185 ms/帧；
- 20 万预算压力路径剔除初始化样本后，两者都持续保持 199999–200000 个活动叶三角形，Classic 执行 878879 次 split/merge，DOD 执行 879591 次，工作量差异只有约 0.08%；
- 压力路径中 Classic 的 split topology 为 80.041 ms/帧，DOD 总计为 94.236 ms/帧，但 DOD 的串行收敛只有 78.324 ms/帧；多出的时间主要来自 15.384 ms/帧的并行 chunk 构建，不来自串行 `SplitNodeImpl`；
- Classic 的 merge topology 为 47.265 ms/帧，DOD 总计为 51.020 ms/帧；DOD 串行收敛为 38.818 ms/帧，额外成本主要是 8.800 ms/帧的活动索引/队列刷新和 2.115 ms/帧的邻域失效；
- WPR 共取得 18396 个目标进程 CPU 样本。`ClassicRoamMeshBuilder::SplitNode` 有 2126 个包含样本，DOD 串行 `SplitNodeImpl` 有 2031 个；Classic `MergeNodeOrDiamond` 有 1112 个，DOD 串行 `MergeNodeOrDiamondWithScoreLimitImpl` 有 897 个。串行拓扑核心已经不慢于 Classic；
- DOD 的 `SplitEntryPrecedes`、`RefreshPersistentMergeQueueNeighborhood` 和 `InsertMergeQueueNodeIfEligible` 仍明显高于 Classic 对应函数，剩余优化对象与阶段 C 的 O3/O8 完全一致；
- B4 决策为进入阶段 C，不再为串行 `SplitNode`/`MergeSingleNode` 增加新的优化问题。完整数据见 [B4 阶段决策报告](../../benchmark-output/b4-stage-b-decision-20260817.md)。

## 7. 阶段 C：队列和活动索引布局

### C1. O3：活动叶视图与 split heap 分离

#### 当前问题

[`SplitEntryPrecedes`](../../src/algorithms/data_oriented_roam/DataOrientedRoamQueues.cpp#L84) 从 heap 中取得 node index 后，再按 node index 读取 `ScreenErrors` 和 `PathIds`。`ActiveLeafNodes` 的 heap 交换还会持续改变最终叶节点输出顺序。

Classic 的 [`SplitQueueEntry`](../../src/algorithms/classic_roam/ClassicRoamMeshBuilder.h#L261) 把 `Node` 和 `Score` 放在同一个 heap entry 中，并使用节点内反向位置。

#### 实施内容

1. 保留稳定、稠密的活动叶节点列表；
2. 独立维护 split heap 的 node、score 和反向位置；
3. heap 比较直接读取 heap position 对应的 score，不再通过 node index 跳转；
4. split/merge 局部事务同时更新活动叶列表和 split heap；
5. 不重新引入每帧活动叶复制或递归收集。

#### 验收

- `CpuFinalLeafCollectMilliseconds` 保持为 0；
- split candidate mark 不退化；
- split/merge 单操作 heap 维护成本下降；
- 活动叶列表顺序不再因 score heapify 改变；
- 为阶段 D 的稳定 Mesh slot 提供可用基础。

#### 回退条件

若维护两份索引使 topology membership update 成本高于 heap 局部性收益，则保留活动叶/heap 融合结构，只增加按 heap position 连续存储的 score 数组，并用独立 Mesh slot 解决输出稳定性。

### C2. O8：压缩活动状态与旁路元数据

#### 当前问题

[`DataOrientedRoamState`](../../src/algorithms/data_oriented_roam/DataOrientedRoamState.h#L302) 为活动 internal、活动 leaf 和 merge queue 维护多组 node-to-position、representative 和 partner 数组。它们支持连续扫描和并行提交，但单次拓扑事务会写入多个分散数组。

#### 实施内容

1. 保留 `ActiveInternalNodes` 和 `ActiveLeafNodes` 的连续索引，把 node-to-position、queue position、merge representative/partner 收拢到单一 `DataOrientedRoamNodeMembership` sidecar；
2. 在 `DataOrientedRoamNodeIndex == uint32_t` 的容量边界内，将所有 position 改为 32 位 sentinel 索引；
3. 用 sidecar 的 sentinel position 作为热路径 active 状态，完整的正向列表/反向位置交叉检查只留在 topology validator；
4. 将 merge queue 的 position、representative、partner 按“按 node 随机定位、同一事务连续更新”的访问模式重组；
5. 批量 commit 继续集中更新 sidecar，保持 Classic 的 O(1) swap-remove、O(log N) indexed heap 删除和 canonical diamond 语义；
6. 不删除 `ActiveInternalNodes`。已有 A/B 已证明它能避免扫描历史 node pool，并显著降低 merge candidate mark。

#### 实现边界：Classic 对齐与 DOD 专属区间

本阶段不把 Classic 也能实现、但 Classic 尚未实现的通用拓扑或 heap 优化只施加给 DOD。Classic 已经把活动状态和 queue 反向位置放在节点内；C2 只在 DOD 的 SoA 节点池之外建立语义等价的紧凑 sidecar，因此两条路径仍共享以下行为契约：

- 活动 membership 由显式索引维护，而不是依赖历史 node pool 的 `IsSplit` 推断；
- split/merge 事务使用 O(1) swap-remove 和反向位置更新；
- 持久 Q_s/Q_m 仍使用 indexed heap、canonical diamond 和相同的优先级/删除语义；
- validator 仍检查正向连续列表、反向位置、heap 成员和拓扑闭包。

DOD 特有的实现收益只来自数据布局：连续活动索引服务并行 candidate mark，sidecar 将六组按 node 随机访问的元数据压成一个 24-byte/node 的 `uint32_t` 容器，同时不把动态状态重新塞回数值 SoA。旧布局为四组 64 位 position 加两组 32 位 node metadata，共 40-byte/node；这不是新的通用算法，而是 SoA 索引设计下对 Classic intrusive 字段的紧凑外置表达。

#### 验收

- active leaf/internal 判断减少随机数组读取；
- queue membership update 数保持一致；
- merge candidate mark 不退化；
- node pool 和旁路数组总内存不增加，或增加量有明确收益依据；
- 对照 Classic 的行为契约不变，新增收益必须能归因于 DOD 连续索引或 sidecar 数据布局，而不是只在 DOD 引入的通用算法变化。

#### 实现结果（2026-08-17）

- `DataOrientedRoamState::NodeMembership` 取代活动 internal、活动 leaf、Q_s position 以及 Q_m position/representative/partner 的六组分散 node-to-position 数组；
- 热路径 `IsActiveInternalNode`/`IsActiveLeafNode` 只读取 sidecar sentinel，validator 继续做完整反向一致性验证；
- position 从 `size_t` 压缩为 `uint32_t`。因为 node index 本身就是 `uint32_t`，活动列表和 queue 都是 node pool 的子集，不改变可表示的有效状态；
- sidecar 理论占用从 40-byte/node 降为 24-byte/node，node pool 的 SoA 数值字段和 Classic 路径均未改变；
- CTest、budget-reentry 和 split queue view 回归继续使用原有队列/拓扑契约，C2 不新增另一套 DOD-only 拓扑规则。

## 8. 阶段 D：DOD 增量 Mesh 与后续全量策略

### D1. O6：补齐增量 Mesh 输出契约

> 状态：增量基础已完成。按 2026-08-18 的实现决定，本阶段暂不加入 dirty 比例选择策略；
> 交叉点实验和高变化率完整并行 emit 保留为后续独立步骤。

#### 当前问题

历史实现曾在 [`DataOrientedRoamPipeline::BuildInternal`](../../src/algorithms/data_oriented_roam/DataOrientedRoamPipeline.cpp#L77) 中把全部 `ActiveLeafNodes` 交给完整 emit，每帧重新 resize 和写入 CPU Mesh；该问题已由本节的持久 slot/replay 路径解决。

Classic 已通过 [`ApplyIncrementalMeshUpdates`](../../src/algorithms/classic_roam/ClassicRoamMeshEmit.cpp#L89) 实现稳定 slot、topology edit、dirty slot 和 update range，并由 adapter 发布借用 Mesh 与增量上传契约。

#### 实施内容

1. 为 DOD 节点维护 Mesh slot 或等价反向索引；
2. split 时一个 child 继承 parent slot，另一个 child 追加新 slot；
3. merge 时 parent 继承一个 child slot，另一个 child 通过 move-last 回收；
4. topology commit 记录 Mesh edit，拓扑稳定后统一重放；
5. 生成 dirty ranges 并接入 `TerrainLodRenderPacket` 的 borrowed Mesh/update range 契约；
6. 增加 DOD 版 incremental-emit 回归测试；
7. 用 A/B 实验确定增量 emit 与全量并行 emit 的交叉点；
8. 当本帧 dirty triangle 比例超过交叉点时，允许完整并行 emit，不设置拓扑操作数量上限。

#### 实施结果（2026-08-18）

- DOD 使用独立的 `NodeSlots<uint32_t>` SoA 反向索引和连续 `SlotOwners<NodeIndex>`；没有把 Mesh slot 塞入 C2 的 queue membership sidecar；
- split/merge 仍执行与 Classic 相同的 slot 继承、尾部追加和 move-last 回收语义；并行 topology worker 不写 Mesh，edit 在 join 后的共享活动索引更新点由主线程记录；
- 拓扑稳定后按提交顺序重放 edit，同一 generation 的 dirty slot 去重后批量生成顶点并合并连续 update ranges；
- `DataOrientedRoamTerrainLodAlgorithm` 改为发布 borrowed persistent Mesh、generation、full-upload 标记和 update ranges，OpenGL/D3D12 继续消费既有统一 packet；
- 新增 `roam_incremental_mesh_emit_dod`，并把 Mesh slot owner、反向索引、局部索引、UV、range 边界和 `updated + reused == active` 纳入 DOD validator；
- 这部分是补齐 Classic 已有的增量输出能力，不记为 DOD 独有优化。DOD 与 Classic 的逻辑契约一致，差异仅来自 SoA node index 和并行提交后的集中 edit 表达；
- 本次没有实现第 7、8 项的自适应选择，也不把 dirty 批次的 worker 分段宣称为 DOD 独占能力。

默认 OpenGL runtime benchmark 的同机单轮对比为：DOD `Mesh emit` 从 `0.6923 ms` 降到 `0.0498 ms`，`CPU upload` 从 `0.2048 ms` 降到 `0.0227 ms`，`CPU update` 从 `1.6710 ms` 降到 `0.9924 ms`。该数据证明默认路径的目标阶段明显下降；最终性能结论仍按多轮中位数规则验收。

预算饱和单轮中，固定增量路径每帧中位更新约 `138923 / 200000` 个三角形并产生 `36738` 个 dirty ranges，`Mesh emit` 中位数为 `19.5109 ms`；C2 全量并行基线为 `17.1440 ms`。这说明高变化率已经进入增量路径的不适用区间。按当前决定保留该边界，不在 D1 基础实现中加入自动选择，也不把默认路径收益外推到压力路径。

#### 验收

- 拓扑不变帧：updated triangles 和 dirty ranges 为 0；
- 少量变化帧：`updated + reused == active`；
- full/incremental 两条路径生成相同 Mesh 内容；
- 默认路径 emit 和 upload 明显下降；
- 极限压力路径不得因为强制增量更新而稳定慢于当前全量并行 emit；
- OpenGL 与 D3D12 都正确消费 update ranges。

## 9. 阶段 E：并行 merge 批处理

### E1. O5：降低结果整理成本

#### 当前问题

[`MergeWithDiamondQueue`](../../src/algorithms/data_oriented_roam/DataOrientedRoamTopology.cpp#L1297) 当前执行候选快照、全量排序、chunk 分桶、邻域失效、worker 提交、结果合并、主线程索引/队列刷新和串行收敛。

极限压力路径的单轮参考值：

| Merge topology 子阶段 | 平均耗时 |
| --- | ---: |
| 候选排序/分桶 | 0.3746 ms |
| 队列邻域失效 | 1.1586 ms |
| chunk 并行提交 | 0.6366 ms |
| worker 结果汇总 | 0.0119 ms |
| 活动索引/队列刷新 | 4.9412 ms |
| 串行收敛 | 25.5251 ms |

并行 worker 提交不是最大成本，主线程的索引和队列刷新才是优先对象。

#### 实施内容

1. 将候选、chunk、计数器和已提交结果缓冲变成跨帧复用工作区；
2. worker 输出紧凑的 topology transition 记录；
3. 主线程按 node index 或受影响邻域批量应用索引变化；
4. 对多个 merge 的邻域做一次批量规范化、失效和恢复；
5. 使用实测候选交叉点选择串行或并行执行策略；
6. 不限制本帧 merge/split 总事务数，持续收敛语义保持不变；
7. 保留边界 candidate 串行回退和 diamond 拓扑检查。

#### 验收

- 并行路径的 `index/queue refresh` 明显下降；
- worker commit 收益不被结果整理抵消；
- 候选不足时串行路径不支付快照、分桶和 worker 调度成本；
- 极限压力路径 CPU update 改善；
- 默认路径不回退；
- 拓扑事务总量、最终活动叶集合和拓扑正确性与优化前一致。

## 10. 实施顺序与提交边界

每个编号阶段必须独立提交，禁止把多个无法单独 A/B 的大改动压入同一提交。

| 顺序 | 提交主题 | 包含问题 | 必须附带的验证 |
| ---: | --- | --- | --- |
| 1 | 串行预算与拓扑专用路径 | O1、O2 | CTest、budget reentry、默认/压力 A/B |
| 2 | 串行队列邻域无分配去重 | O4 | queue validator、默认/压力 A/B |
| 3 | 清理剩余热路径 proxy | O7 | CTest、汇编或 profiler 证据、A/B |
| 4 | split heap 与活动叶视图分离 | O3 | heap/active index validator、A/B |
| 5 | 活动状态和旁路元数据重组 | O8 | topology validator、内存统计、A/B |
| 6 | DOD 增量 Mesh 基础路径 | O6 | 新增回归测试、OpenGL/D3D12 smoke |
| 7 | 增量/全量 Mesh 自适应选择 | O6 | 默认/压力交叉点实验 |
| 8 | merge 并行结果批量整理 | O5 | 子阶段计时、默认/压力 A/B |

若某一步没有达到自身目标，应先解释、回退或调整该步，不进入下一步后再用其他改动掩盖结果。

## 11. 正确性门槛

所有性能优化必须满足：

```text
ActiveTriangleCount <= TriangleBudget
PersistentSplitQueueSize == ActiveTriangleCount
T-junction == 0
InvalidNeighbor == 0
InvalidTopology == 0
同一固定相机序列具有确定性结果
```

必须运行：

```text
RelWithDebInfo build
完整 CTest
roam_budget_reentry_classic
roam_budget_reentry_dod
默认选项路径 runtime benchmark
极限压力路径 runtime benchmark
OpenGL GPU smoke
D3D12 GPU smoke（涉及 render packet、Mesh 或 upload 时）
```

对 Mesh 改动还必须验证：

```text
顶点、索引、绕序和 debug 属性一致
updated + reused == active
dirty range 不越过 Mesh 尾部
full upload 与 range upload 输出一致
frame slot 延迟消费 ranges 时不会漏更新
```

## 12. 性能判断规则

### 12.1 报告统计

- 每组至少 5 轮；
- 优先比较多轮中位数，同时记录 P95；
- 排除初始化样本；
- 保留原始 CSV 和自动生成的 Markdown；
- 报告 Classic 与 DOD 的实际 split/merge 总量；
- topology 必须同时给出每帧耗时和每操作耗时；
- 不把 candidate mark、topology 和 Mesh emit 混成单一 `CPU update` 结论。

### 12.2 改动接受条件

满足以下条件才保留优化：

1. 目标阶段在默认或压力路径中出现稳定改善；
2. 另一条路径没有无法解释的显著退化；
3. 拓扑事务数量和最终输出没有通过减少工作量伪造性能收益；
4. 正确性门槛全部通过；
5. 代码复杂度与收益相称。

单轮小于计时波动的变化不得写成性能结论。若目标阶段改善但完整帧不变，应明确记录瓶颈转移，而不是否定局部优化。

## 13. 停止条件

### 13.1 串行 topology 停止条件

完成 O1、O2、O4、O7、O3 和 O8 后，若 profiler 表明：

- 不再存在 atomic、运行时串并行分支、邻域动态分配或完整 proxy 热点；
- heap 和队列元数据访问已经是主要剩余成本；
- 差距主要来自指针直接访问与 SoA 索引随机访问；
- DOD 在压力路径整体 CPU update 已稳定优于 Classic；

则停止继续追求串行 split/merge 与 Classic 完全相等，把剩余差距记录为当前数据布局的适用边界。

### 13.2 不新增“第九个问题”的条件

只有同时满足以下条件，才允许在本计划增加新问题：

1. profiler 指向一个不能归入 O1-O8 的独立执行阶段；
2. 该阶段只在 DOD 存在，或 DOD 稳定明显慢于 Classic；
3. 默认和压力路径至少一条可重复复现；
4. 控制拓扑事务数量后差距仍存在；
5. 该阶段占 DOD CPU update 的比例足以影响结论。

以下内容不得作为新增问题：

- O4/O5 已覆盖的临时 vector 和排序；
- O5 已覆盖的 worker 调度、join 和结果合并；
- O6 已覆盖的 CPU/GPU buffer 完整上传；
- O3/O7/O8 已覆盖的 cache miss、索引跳转、边界检查和节点代理；
- Classic 与 DOD 都执行的 `CollectActiveSplitPaths`；
- 已经快于 Classic 的 candidate mark；
- 单轮 benchmark 波动。

## 14. 非目标项

本轮优化不修改：

- ROAM 1997 误差公式和屏幕投影；
- split/merge thresholds 的语义；
- forced split、diamond merge 和裂缝约束；
- 持久 `Q_s/Q_m` 的全局优先级口径；
- triangle budget 和持续资源交换语义；
- 已归档的 GPU ROAM-like shader 算法（保留在 `archive/gpu-roam-like` 分支）；
- 后续独立研究变体；
- ROAM 1997 完整最优性证明、triangle strip 或延期优先级复现。

候选评分、方差树和最终活动叶收集已经不是 Classic/DOD 当前差距的主要来源，不在本计划中重复优化。

## 15. 最终交付

本计划完成后应交付：

1. 一条精简且与 Classic 可解释对齐的 DOD 串行 topology 路径；
2. 保留正确性约束的 DOD 并行 split/merge 路径；
3. 稳定的活动叶视图和独立 split heap；
4. DOD 增量/全量自适应 CPU Mesh 输出；
5. OpenGL/D3D12 增量 upload；
6. 默认与极限压力路径的多轮 A/B 报告；
7. 对剩余 Classic/DOD 差距的 profiler 证据；
8. 明确结论：哪些收益来自数据导向和并行，哪些成本来自随机拓扑修改，哪些功能只是补齐 Classic 已有能力。

最终报告不得只写“DOD 比 Classic 快或慢”，必须按 candidate mark、topology commit、Mesh emit、finalize 和 upload 分阶段说明原因。
