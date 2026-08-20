# ROAM 论文与当前 Classic CPU ROAM 实现详细对比

> 对比基线：2026-08-08 当前源码，包含 nested wedgie 公式 (1)、保守屏幕投影公式 (2)/(3)、持久双优先队列与 Classic 增量 indexed Mesh 输出；Classic/DOD 对比已按当前 SoA、持久活动集合、条件并行拓扑提交和 CPU mesh 输出路径重新核对。
> 论文依据：项目内转写文档 [`roaming_terrain_paper.md`](roaming_terrain_paper.md)，未重新解析 PDF。  
> 实现范围：以 `src/algorithms/classic_roam` 为核心，并追踪统一 LOD 接口、渲染上传和 Benchmark。  
> 本文讨论的是“论文描述的 ROAM”与“项目当前名为 Classic CPU ROAM 的实现”之间的对应关系，不把同名类型或函数自动视为论文机制的完整复现。

## 1. 结论先行

当前 `Classic CPU ROAM` 已经实现了 ROAM 最重要的拓扑骨架：

- 两个根三角形组成根 diamond；
- 以 base edge 二分的 Binary Triangle Tree；
- `base/left/right` 三类邻居关系；
- recursive forced split；
- 成对 diamond merge；
- 跨帧保留 active topology；
- 默认启用 local constraints 时，CPU 端生成目标为无 T-junction 的连续三角网格。

它也实现了若干论文所需但早期版本曾缺失的工程能力：论文公式 (1) 的 nested wedgie thickness tree、公式 (2)/(3) 的保守像素投影、near-plane 人工最大优先级、FOV/aspect/drawable 感知、视锥感知、持久 `Q_s/Q_m`、统一 crossover、硬 Triangle Budget 和 split/merge 迟滞。

当前实现采用工程等价复现口径，不把“逐项还原论文格式和证明”作为目标。与论文严格版本相比，最关键的差异有三项：

1. 当前 geometric component 已是论文公式 (2)/(3) 的保守像素上界，但最终 `ComputeScreenErrorScore()` 仍取 `max(geometricBound, edgeDensity)`；后者是论文基础误差度量中不存在的项目扩展，且最终 priority 的 parent/child 单调性尚未验证。
2. 当前已持久维护 `Q_s/Q_m` 并在同一循环比较最高 split 与最低 merge，但以像素阈值和 hard upper bound 作为终止条件；满预算时采用 merge-first 事务，不实现论文的精确 target count、worst-case top-down fallback，也尚不能继承最小拓扑改动证明。
3. 当前已经按 topology changes 增量维护 CPU indexed Mesh 并部分上传，但不是论文的 incremental triangle-strip 数据结构；priority 与 frustum 状态仍会全量刷新，所以端到端复杂度尚未达到论文的 `O(Delta N)` 目标。

因此，以下论文结论目前不能直接套用到项目实现：

- 不能把局部 geometric bound 直接扩张为完整管线的 guaranteed screen-space error bound；
- 不能声称在给定三角形数下最小化最大误差；
- 不能声称每帧更新复杂度与 `Delta N` 成正比；
- 不能声称可以精确达到指定 triangle count；
- 不能声称运行时拓扑内存始终与当前 output mesh 大小成正比。

这不表示当前实现只具有外形相似性。公式 (1)-(3) 已恢复论文的嵌套几何误差和保守像素投影；forced split、diamond merge 与邻接维护实现了相同的连续拓扑目标；持久队列 membership、拓扑 edit 和增量 indexed Mesh 使相应阶段的工作量主要随局部变化增长。项目接受每次 Build 仍有全队列优先级刷新这一 `O(N)` 边界，不再以补齐完整证明、triangle strips 或端到端严格 `O(Delta N)` 为后续目标。

更准确的项目定位是：

> 当前 Classic CPU ROAM 是“采用经典 ROAM bintree/diamond 拓扑、公式 (1)-(3) 局部误差界、持久 dual queues 和增量 indexed output 的串行、对象式 CPU 基线”。队列 membership、拓扑与 Mesh 槽位一起跨帧保留，但最终 LOD priority、accuracy threshold、严格容量事务和输出格式仍是项目变体。

## 2. 判定口径

本文使用四种状态：

| 状态 | 含义 |
| --- | --- |
| 已实现 | 论文机制的关键语义与当前执行代码一致；允许数据结构和语言层面的常规差异 |
| 部分实现/变体 | 实现了相同目标的一部分，或采用了不同公式、调度、生命周期，因而不能继承论文全部性质 |
| 未实现 | 当前执行路径没有该机制 |
| 项目扩展 | 论文没有要求，当前项目为了可调试性、公平 Benchmark 或现代渲染接口额外加入 |

“论文已提到”不等于“论文完整规定了工程细节”。例如论文介绍 vertex morphing，但没有要求本项目一定采用特定缓冲格式。反过来，“项目中存在 priority queue”也不等于实现了论文的 persistent dual-queue optimization。

## 3. 总体流程对照

### 3.1 论文每帧流程

论文将运行时划分为四阶段：增量视锥更新、候选 priority 更新、双队列 split/merge、受影响 triangle strip 更新。依据见 [`ROAM 运行时四阶段`](roaming_terrain_paper.md#L69)。

```text
预计算 nested wedgie bounds
        |
        v
增量更新 bintree frustum labels
        |
        v
只重算可能影响决策的 priorities
        |
        v
持久 Q_s / Q_m 交替执行 split 与 merge
        |
        v
增量修补受影响的 triangle strips
```

### 3.2 项目当前每次 Build 流程

项目入口是 [`ClassicRoamTerrainLodAlgorithm::BuildRenderData()`](../../src/algorithms/classic_roam/ClassicRoamTerrainLodAlgorithm.cpp#L30)，它把统一参数映射给 [`ClassicRoamMeshBuilder::Build()`](../../src/algorithms/classic_roam/ClassicRoamMeshBuilder.cpp#L21)。真实执行顺序如下：

```text
规范化 MaxDepth / TriangleBudget
        |
        v
必要时按源分辨率重建 nested wedgie trees 或重置 topology
        |
        v
刷新持久 Q_s/Q_m 中全部现有成员的 view-dependent priority
        |
        v
统一 crossover 循环比较 max(Q_s) 与 min(Q_m)
        |
        v
低于 MergeThreshold 时回收低误差 diamond
        |
        v
高于 SplitThreshold 时 split；预算满载则先 merge 低分 diamond 再提交 closure
        |
        v
可选全局 topology validator
        |
        v
按 topology commit 顺序 replay split/merge Mesh edits
        |
        v
只重写 dirty triangle slots 并合并 update ranges
        |
        v
统计、保存 split paths、借用持久 Mesh 并部分上传
```

入口证据集中在 [`ClassicRoamMeshBuilder::Build()`](../../src/algorithms/classic_roam/ClassicRoamMeshBuilder.cpp)；双队列调度位于 [`OptimizeWithPersistentDualQueues()`](../../src/algorithms/classic_roam/ClassicRoamQueues.cpp)，增量输出位于 [`ClassicRoamMeshEmit.cpp`](../../src/algorithms/classic_roam/ClassicRoamMeshEmit.cpp)。正常输出不再递归收集 active leaves；只有 validator 会独立遍历 roots 与 dense slot owners 交叉核对。

### 3.3 阶段级对应关系

| 论文阶段 | 当前对应 | 状态 | 核心差异 |
| --- | --- | --- | --- |
| view-independent nested bounds 预计算 | `Roam::BuildNestedWedgieSubtree()` + `RebuildVarianceTrees()` | 已实现 | 最细层为 0；父层严格执行公式 (1)，深度上限为 20 |
| incremental frustum update | `Roam::IsTriangleVisible()` | 部分实现/变体 | 每次评分重新做 6-plane AABB 测试，没有跨帧标签和 subtree early-out 状态 |
| deferred priority recomputation | `RefreshPersistentQueuePriorities()` | 未实现 | membership 持久，但每次 Build 仍刷新两个队列全部成员的 priority |
| persistent dual queues | intrusive indexed `Q_s/Q_m` | 已实现 | membership 跨帧保留，拓扑变更只局部维护；每帧 key refresh 仍为 O(N) |
| forced split | `SplitNode()` | 已实现 | 使用递归传播和预算 token 预留；统计口径以单个 parent split 计数 |
| diamond merge | `CanMergeNode()` + `MergeNodeOrDiamond()` | 已实现 | merge 后 parent 立即进入 `Q_s`，新可合并 diamond 局部进入 `Q_m` |
| direct target triangle count | `TriangleBudget` | 部分实现/变体 | 当前参数是 hard upper bound，不是必须达到的 target count |
| incremental output | stable `MeshSlot` + edit replay + dirty ranges | 部分实现/现代等价 | 工作量按受影响 slots 变化，但输出仍是每叶 3 顶点/3 索引，不是 triangle strips |
| progressive optimization | 无 | 未实现 | 没有 frame deadline 或可中断的优化工作预算 |
| vertex morphing | 无 | 未实现 | split/merge 后顶点立即跳到真实高度 |

## 4. Binary Triangle Tree 与根 Diamond

### 4.1 论文要求

论文将根三角形写为 `T=(v_a,v_0,v_1)`，沿 base edge `(v_0,v_1)` 的中点 `v_c` 分成两个孩子，递归形成 triangle bintree。依据见 [`§4.1 Triangle Bintree`](roaming_terrain_paper.md#L119)。对于矩形 terrain，典型 base mesh 是一个由两个根三角形构成的 diamond，见 [`§4.2`](roaming_terrain_paper.md#L143)。

### 4.2 当前实现

[`TriangleDomain`](../../src/algorithms/classic_roam/ClassicRoamMeshBuilder.h#L25) 保存三个 UV 顶点，其中 `A/B` 是 base edge，`C` 是 apex。Classic 与 DOD 的薄封装最终都调用共享 [`Roam::SplitTriangleDomain()`](../../src/algorithms/RoamGeometry.h#L29)，计算 `midpoint=(A+B)/2` 并生成：

```text
Left  = { C, A, midpoint }
Right = { B, C, midpoint }
```

顶点排列与论文记号顺序不同，但几何集合和 base-edge 二分语义相同。两个根域由 [`ResetTopology()`](../../src/algorithms/classic_roam/ClassicRoamState.cpp#L48) 创建：

```text
rootA = {(0,1), (1,0), (0,0)}
rootB = {(1,0), (0,1), (1,1)}
```

二者共享方形对角线，并互设 `BaseNeighbor`。这正是论文 terrain base diamond 的项目对应物。

### 4.3 判定

**状态：已实现。**

当前实现完整保留了 domain-space 右等腰二叉三角形结构。世界空间高度会使三角形不再是严格的平面右等腰三角形，但这与 height-field ROAM 的通常语义一致：规则性指的是二维 domain triangulation。

当前实现固定为一个方形高度图的两个根，尚未支持论文所说的任意 genus、带边界 manifold base mesh。后者属于“通用输入曲面”扩展，不影响当前 height map 用例的核心拓扑正确性。

## 5. 节点、邻接与生命周期

### 5.1 当前结构

[`ClassicRoamNode`](../../src/algorithms/classic_roam/ClassicRoamMeshBuilder.h#L183) 具有：

- `Parent/LeftChild/RightChild`：Binary Triangle Tree；
- `BaseNeighbor/LeftNeighbor/RightNeighbor`：连续网格邻接；
- `Depth/PathId`：层级和跨帧稳定路径；
- `VarianceTreeIndex/VarianceIndex/GeometricError`：误差树映射；
- `IsSplit`：child 是否进入 active topology；
- build id 与 forced split 标志：调试着色和统计。

对象由 [`AddNode()`](../../src/algorithms/classic_roam/ClassicRoamState.cpp#L20) 创建为 `unique_ptr`，统一存入 `_nodes`；拓扑关系仍通过裸指针表达。`vector<unique_ptr<T>>` 扩容只移动 `unique_ptr`，不会移动其堆上 `T` 对象，因此 `_nodes` 扩容本身不会使节点裸指针失效。

[`MergeSingleNode()`](../../src/algorithms/classic_roam/ClassicRoamTopology.cpp#L553) 只将 `IsSplit=false`，保留 child 对象供以后复用。普通帧不会释放节点；只有 [`ResetTopology()`](../../src/algorithms/classic_roam/ClassicRoamState.cpp#L48) 清空整个 pool。

### 5.2 与论文的差异

**拓扑表示状态：已实现。** 裸指针邻接、parent/child 和 diamond 语义与经典实现风格一致。

**运行时内存目标状态：部分实现/变体。** 论文声称 runtime structures 与 output mesh size 成正比，见 [`论文内存目标`](roaming_terrain_paper.md#L86)。当前 `_nodes` 记录相机历史上曾展开过的最高水位 topology；merge 不回收节点，因此长时间遍历全地形后，`NodeCount` 可以远大于当前 active triangle count。

这不是悬空指针 bug，而是明确的空间换时间选择：re-split 不再分配 child，但当前内存规模更接近“历史访问过的 bintree 子集”，不是“当前 output mesh”。

## 6. 世界空间误差预计算

### 6.1 论文 nested wedgie

论文为每个 triangle 定义 thickness `e_T`，满足该 triangle 的仿射高度平面与真实曲面之间的世界空间偏差上界。最细层 `e_T=0`，父节点按公式计算：

```text
e_T = max(e_T0, e_T1) + abs(z(v_c) - z_T(v_c))
```

其中局部项只使用本次 split 新增的 base midpoint `v_c`。依据见 [`§6.1 Nested World-Space Bounds`](roaming_terrain_paper.md#L253)。加法很关键：child thickness 是相对 child plane 的误差，局部 midpoint displacement 是 child plane 相对 parent plane 的偏移，二者需要组合才能形成 parent plane 相对所有后代曲面的保守界。

### 6.2 当前 nested wedgie tree

[`ResolveNestedWedgieTreeDepth()`](../../src/algorithms/RoamNestedWedgie.h#L28) 先解析预计算最细深度：

```text
sourceAxisLevel = max(ceil(log2(width-1)), ceil(log2(height-1)))
sourceDepth = min(2 * sourceAxisLevel, 20)
finestDepth = max(clamp(runtime MaxDepth, 0, 20), sourceDepth)
```

bintree 每两个层级才会把 height map 的两个轴各缩小一半，所以 129x129 与 513x513 输入分别需要深度 14 与 18。预计算深度不再被较小的运行时 `MaxDepth` 截断。

[`BuildNestedWedgieSubtree()`](../../src/algorithms/RoamNestedWedgie.h#L60) 在最细层写 0，其他层严格执行：

```text
thickness = max(leftThickness, rightThickness)
          + abs(ComputeBaseMidpointDisplacement(domain))
```

共享 [`Roam::ComputeBaseMidpointDisplacement()`](../../src/algorithms/RoamGeometry.h#L40) 只计算当前 base edge `A-B` 的 midpoint 实际高度与端点线性插值之差，并保留符号；共享递推负责取绝对值。Classic 与 DOD 共用 `RoamGeometry.h` 和 `RoamNestedWedgie.h`，GPU ROAM-like 则从 DOD topology snapshot 直接读取传播后的 `GeometricError`。

### 6.3 与论文一致的部分

- view-independent，且相机移动不需要重建；
- 按完整 triangle bintree 和二叉堆索引组织；
- 最细层 thickness 为 0；
- parent 按 `max(child)+abs(local displacement)` 向上累计；
- parent thickness 不小于任一 child thickness。

### 6.4 当前边界

公式层面的旧差异已经消除。当前剩余限定来自输入和容量边界：

- `2^k+1` 规则网格且 `2k<=20` 时，finest bintree level 与源采样格对齐；
- 非 `2^k+1` 尺寸会向上取整到下一 dyadic extent；
- 所需 source depth 超过 20 时会被截断；
- `HeightMap::SampleBilinear` 若被解释为连续双线性曲面，finest triangle thickness=0 的严格性还需另外证明，而论文公式首先约束离散 bintree terrain。

### 6.5 判定

**状态：已实现（带输入/深度限定）。** 递推公式、最细层初始化和两个根的完整 heap-indexed tree 均与论文公式 (1) 对齐。该结论只覆盖 world-space nested thickness，不自动证明后续 screen projection 和最终 priority 的保守性。

### 6.6 预计算代价

每棵树容量为 `2^(finestDepth+1)-1`，两棵树总空间和构建时间随预计算深度指数增长。项目把深度限制为 20，证据见 [`MaximumSupportedDepth`](../../src/algorithms/classic_roam/ClassicRoamMeshBuilder.cpp#L13) 和 [`CompleteBinaryTreeNodeCount()`](../../src/algorithms/RoamNestedWedgie.h#L46)。深度 20 时两棵树约包含 419 万个 `float`，数组本体约 16 MiB；129x129 深度 14 约 256 KiB，513x513 深度 18 约 4 MiB。每个非叶节点采样 base 两端和 midpoint，仍适合单独建立 initialization benchmark。

## 7. 屏幕空间误差与 Priority

### 7.1 论文定义

论文基础 metric 是真实曲面点和当前三角网格点的最大二维 screen-space displacement。实际调度使用把 wedgie thickness segments 投影到屏幕后的保守上界，见 [`§6.2 Geometric Screen Distortion`](roaming_terrain_paper.md#L279)。

论文公式会：

- 把世界空间 thickness vector 转换到 camera space；
- 考虑 perspective denominator 在 triangle 内的极值；
- 对 near plane 穿越设置 artificial maximum priority；
- 得到 monotonic local bounds，供最优性证明使用。

### 7.2 当前公式

共享 [`Roam::ComputeScreenErrorScore()`](../../src/algorithms/RoamScreenError.h#L30) 先调用 [`Roam::IsTriangleVisible()`](../../src/algorithms/RoamGeometry.h#L105)，再调用 [`ComputeConservativeScreenDistortionPixels()`](../../src/algorithms/RoamScreenProjection.h)。后者令世界高度 thickness vector 为 `(0, worldError, 0, 0)`，并把 triangle corner 与该方向乘 `ViewProjection`。对每个角点，以齐次裁剪坐标的 `clip.x/clip.y/clip.w` 作为已包含 projection scale 的 `(p,q,r)`，以 `thicknessClip.x/thicknessClip.y/thicknessClip.w` 作为 `(a,b,c)`。实现分别求：

```text
denominator_i = r_i^2 - c^2
numerator_i^2 = (0.5*width *(a*r_i-c*p_i))^2
              + (0.5*height*(b*r_i-c*q_i))^2

geometricBoundPixels = 2 * sqrt(max_i(numerator_i^2))
                         / min_i(denominator_i)
```

这就是论文公式 (3) 的角点保守组合，只是提前把 projection 和 NDC-to-pixel scale 合入变量。齐次写法同时覆盖 perspective、orthographic、D3D/OpenGL depth convention 和非方形 drawable。若 wedgie 触碰或穿过 inward near plane，返回 `std::numeric_limits<float>::max()`；完全位于视锥外的节点仍先由 `Roam::IsTriangleVisible()` 得 0。

项目仍保留独立的平坦区域密度项：三个 triangle corner 直接投影到 pixel 后求最长边 `projectedLongestEdgePixels`，最终：

```text
edgeDensityPixels = projectedLongestEdgePixels * 0.20
score = max(geometricBoundPixels, edgeDensityPixels)
```

### 7.3 已实现的能力

**像素单位：已实现。** `ViewProjection` 与 `DrawableWidth/DrawableHeight` 共同把齐次投影差换算到实际 pixel。统一输入定义在 [`TerrainLodViewInput`](../../src/algorithms/ITerrainLodAlgorithm.h#L95)，adapter 映射见 [`ToClassicSettings()`](../../src/algorithms/classic_roam/ClassicRoamTerrainLodAlgorithm.cpp#L82)。

**完整投影感知：已实现。** score 使用三个角点的齐次深度和投影后的 thickness direction，不再用 center depth 近似；FOV、aspect、相机 pitch/roll、drawable width/height 都参与结果。

**平坦近景几何密度：项目扩展。** 即使 height error 为 0，projected edge term 仍会细分过大的近景三角形。这有利于视觉网格密度和其他非高度误差，但它不是论文的基础 geometric distortion bound。

### 7.4 与论文的关键差异

1. `edgeDensityPixels` 是额外 tessellation density，不是地形近似误差上界。
2. score 没有归一化到论文示例的 `[0,1]`；项目直接使用 pixel threshold，这不影响公式的保守性。
3. geometric component 对应论文局部 bound，但最终 `max(geometricBound, edgeDensity)`、visibility 置零和 hysteresis 共同作用后的 queue priority 尚无 parent/child 单调性测试。
4. Classic 与 DOD 的 CPU 几何、可见性和最终 score 已分别收敛到 `RoamGeometry.h`、`RoamScreenProjection.h` 和 `RoamScreenError.h`。OpenGL 的两个 compute pass 也共用 `GpuRoamShaderCommon.h`；D3D12 HLSL 仍保留独立等价表达，因此跨 CPU/GLSL/HLSL 的数值一致性仍需测试保护。

### 7.5 判定

**状态：公式 (2)/(3) 已实现，最终 priority 仍是项目变体。** `geometricBoundPixels` 可以称为 `conservative screen distortion bound`；包含 edge-density 后的 `ComputeScreenErrorScore` 应继续称为 `screen-space priority`。

## 8. 单调性与论文最优性证明是否适用

论文 greedy 最优性依赖 `child priority <= parent priority`，见 [`Dual-Queue Optimization`](roaming_terrain_paper.md#L178)。公式 (1) 的 nested thickness 和公式 (2)/(3) 的局部投影现在已经具备；near-plane crossing 也按论文设人工最大值。当前仍不能证明**最终** screen priority 单调，原因包括：

- child/parent 的额外 projected edge-density 项不只由 nested thickness 决定；
- visibility 会把完全离开视锥的 parent/child priority 直接置 0；
- hysteresis 区间内还会根据上一帧 path 状态改变 split 决策；
- 新测试验证了公式 (3) 对 triangle 内密集公式 (2) 采样的保守性，并覆盖一个带 midpoint displacement 的公式 (1) parent/child 构造；真实 HeightMap 全树与包含 edge-density 的最终 priority 尚未穷举。

因此当前 split max-heap 能把“已发现候选中的当前最高 score”优先处理，但不能据此推出论文的全局最优 triangulation。项目不再把补齐这项证明作为完成条件；若以后需要研究最终 priority 的数学性质，可以增加父子优先级单调性检查，但它属于可选研究工具，不进入当前路线图。

## 9. Split Queue 与 Forced Split

### 9.1 Split priority queue

[`ClassicRoamQueues.cpp`](../../src/algorithms/classic_roam/ClassicRoamQueues.cpp) 实现 intrusive indexed max-heap `Q_s`：

1. `ResetTopology()` 只在 base triangulation 初始化时插入两个 roots；
2. `Q_s` 始终保存当前 triangulation 的全部 active leaves，而不是只保存超过 threshold 的候选；
3. 每个 node 的 `SplitQueueIndex` 是稳定可删除 handle，同分时按 `PathId` 稳定排序；
4. 每帧只遍历现有 `Q_s` 成员刷新 view-dependent score，并原地 heapify；
5. split/forced split 局部删除 parent、插入两个 active children；
6. merge 局部删除 children、插回 parent；
7. 因 closure 预算不足而失败的 leaf 只在当前 Build 沉到 heap 底部，下一帧重新评分。

**持久 `Q_s`：已实现。** active membership 不再从 roots 递归发现；但是相机变化仍要求本实现刷新全部现有 queue keys，尚未实现论文 priority deferral。

### 9.2 Forced split

[`SplitNode()`](../../src/algorithms/classic_roam/ClassicRoamTopology.cpp#L317) 在 local constraints 开启时：

- 如果 base neighbor 尚未与当前 node 互为 base，沿邻接链递归 split；
- 如果合法 base neighbor 仍是 leaf，先 forced split 它；
- 然后创建或重新激活当前 node 的两个 children；
- 最后由 [`LinkSplitNeighbors()`](../../src/algorithms/classic_roam/ClassicRoamTopology.cpp#L431) 更新四个 child 之间及外部 neighbor 的指针。

这与论文 [`forced splitting`](roaming_terrain_paper.md#L172) 的拓扑目标一致：先补齐较粗 base neighbor，使目标 split 不产生 T-junction。

**状态：已实现。** 递归顺序和内部函数拆分不必与论文伪代码相同，只要最终形成相同合法 triangulation。

### 9.3 项目特有的预算处理

每个单 triangle parent split 让 active leaf count 增加 1。`_remainingSplitBudget` 是剩余 leaf token；recursive forced split 通过 `reservedSplitSlots` 给尚未提交的调用者预留 token，见 [`SplitNode()` budget guard](../../src/algorithms/classic_roam/ClassicRoamTopology.cpp#L335)。

这避免普通 forced chain 明显突破 hard budget，是项目扩展。需要注意当前 `SplitCount` 统计的是“被展开的 parent 数量”；论文语境中的一次 diamond split 会同时展开两个 parent，因此两边的 split 次数口径并不完全相同。

### 9.4 约束开关

`EnableLocalConstraints=false` 时可以绕过 forced split。该模式不再满足经典 ROAM 连续 triangulation 前提，应只作为实验/故障注入模式。当前 [`Capabilities()`](../../src/algorithms/classic_roam/ClassicRoamTerrainLodAlgorithm.cpp#L19) 无条件报告 `SupportsCrackFix=true`，表示算法具备该能力，不表示任意设置下都启用。

## 10. Diamond Merge

论文将同层级、互为 base neighbor 的两个 triangle 定义为 diamond；只有当两侧 children 都在当前 triangulation 中时才可 merge，见 [`论文 diamond 定义`](roaming_terrain_paper.md#L151)。

当前 [`CanMergeNode()`](../../src/algorithms/classic_roam/ClassicRoamTopology.cpp#L499) 检查：

- node 当前是 internal；
- 两个 children 都是 active leaves；
- parent score 不超过本 pass 限制；
- 若 base neighbor 也是 internal，则必须互为 base、对方 children 也都是 leaves、对方 score 也不超限。

[`MergeNodeOrDiamond()`](../../src/algorithms/classic_roam/ClassicRoamTopology.cpp#L580) 对内部 base neighbor 成对 merge；terrain 边界或无需成对时只 merge 当前 node。外部邻居由 [`MergeSingleNode()`](../../src/algorithms/classic_roam/ClassicRoamTopology.cpp#L553) 改回 parent。

`Q_m` 是 [`ClassicRoamQueues.cpp`](../../src/algorithms/classic_roam/ClassicRoamQueues.cpp) 中的持久 indexed min-heap。互为 base 的两个 parents 只选择较小 `PathId` 一侧作为 canonical representative；diamond 两侧都保存指向该 representative 的 association。priority 是两侧 parent score 的最大值。

**拓扑 merge 状态：已实现。** 这部分与论文连续 bintree triangulation 语义一致。

**持久 `Q_m`：已实现。** split/merge 前先失效局部邻域中的旧 diamond membership，完成拓扑事务后只重新检查 node、parent、children 和直接邻接邻域。validator 会从 active topology 独立推导 canonical diamonds，并验证与 `Q_m` 一一对应。

## 11. Dual-Queue Optimization

### 11.1 论文算法

论文持久维护：

- `Q_s`：当前 triangulation 中可 split triangles，取最大 priority；
- `Q_m`：当前 mergeable diamonds，取最小 priority；
- diamond priority 为两侧 parent priority 的最大值；
- 当 mesh 未达到 target，或 `max(Q_s) > min(Q_m)` 时继续；
- 每次 topology 改动只局部更新两个 queues。

完整伪代码见 [`§5.2 Merge Queue`](roaming_terrain_paper.md#L201)。这套机制同时承担：目标规模控制、质量再平衡、跨帧 coherence 和最小 split/merge 操作数。

### 11.2 当前算法

当前执行模型是：

```text
跨帧保留 active topology、Q_s membership、Q_m membership
更新 Q_s/Q_m 全部现有成员的 priority 并 heapify
while 仍有 accuracy demand 或可改善的预算交换:
    min(Q_m) < MergeThreshold -> merge
    max(Q_s) > SplitThreshold 且有 token -> forced split closure
    预算不足且 max(Q_s) > min(Q_m) -> 先 merge，再重试 split closure
局部更新两个 indexed heaps，保留到下一帧
```

与论文仍有差异：

- `TriangleBudget` 是 hard upper bound，不要求把 mesh 填到精确 target count；
- accuracy 停止条件使用 split/merge 双阈值和上一帧 path 迟滞；
- 为保证固定容量绝不瞬时越界，crossover 使用 merge-first，而论文伪代码在目标大小时可能先 split 再 merge；
- forced closure 通过 reserved leaf tokens 提交，没有论文的精确 representable-count 处理；
- worst-case 变化过大时自动回退 top-down rebuild 的论文检测。

### 11.3 判定

**状态：部分实现/变体。** “持久 dual queues、局部 membership、统一 crossover”已经实现；“论文 exact target、最终最优性、priority deferral 和 worst-case fallback”尚未实现。固定相机时 topology、membership 和最终 Mesh dirty ranges 可以为零，但每次 Build 的 priority refresh 仍是 O(N)。

## 12. Triangle Budget 与迟滞

### 12.1 论文 triangle-count 控制

论文把 triangle count 作为 optimizer 的 target，并宣称可以直接得到指定数量的 mesh，见 [`论文 criteria 6`](roaming_terrain_paper.md#L83)。在满足 monotonic bound 与合法 split/merge 空间的前提下，队列顺序使给定规模下的最大 bound 最小。

### 12.2 当前 hard upper bound

项目统一设置 [`TerrainLodSettings`](../../src/algorithms/ITerrainLodAlgorithm.h#L67) 默认：

```text
MaxDepth       = 14
SplitThreshold = 4 px
MergeThreshold = 2 px
TriangleBudget = 20000
```

当前 `TriangleBudget` 是 active leaf 的硬上限。算法只有在 score 超过 threshold 时才尝试 split，所以 flat/far 场景可以远低于预算；forced chain token、`MaxDepth` 和候选数量也会阻止它达到预算。

**状态：部分实现/变体。** 实现了安全的最大 triangle budget，但没有实现论文的 direct target count optimizer。

### 12.3 Hysteresis

[`ShouldSplitWithScore()`](../../src/algorithms/classic_roam/ClassicRoamScoring.cpp#L35) 采用：

```text
score > SplitThreshold  -> split
score < MergeThreshold  -> coarse
中间区间                 -> 沿用上一帧 split path
```

这是实用的抗抖动扩展。论文主要依赖小幅、按优先级进行的 topology changes，并另提 vertex morphing；当前明确的双阈值迟滞是项目设计，不是论文核心证明的一部分。

### 12.4 满预算 crossover

[`OptimizeWithPersistentDualQueues()`](../../src/algorithms/classic_roam/ClassicRoamQueues.cpp) 在预算不足时直接比较 `max(Q_s)` 和 `min(Q_m)`。只有最高 split score 大于最低 merge loss 才回收 diamond；回收释放的 leaf tokens 随后由同一循环中的 forced split closure 消费。旧版固定 `128` 的有限 batch 与 threshold-gap 再平衡已移除。

同一 Build 还有一层反向事务保护：刚 split 的 parent 仍保留 `Q_m` membership，但 merge score 暂时置为最大值；刚 merge 回来的 parent 也保留在 `Q_s`，split score 暂时沉底。下一 Build 的 key refresh 自动恢复资格。这避免项目扩展 priority 不满足单调性时出现 split/merge 即刻互相撤销。

**状态：论文结构已实现、容量事务与反向保护为项目变体。** 交换顺序采用 merge-first 以保证任何中间状态都不超过 hard budget。论文证明不需要这层同帧保护；当前需要它本身也是最终 priority 单调性尚未成立的工程证据。因此不能直接继承论文 optimal crossover 证明。

## 13. 视锥处理

### 13.1 论文机制

论文为每个 bintree triangle 维护六个 halfspace `IN` flags 和 `OUT/ALL-IN/DONT-KNOW` label。跨帧递归更新时继承 parent flags，并在 label 仍有效时停止整个 subtree，见 [`§7.1 View-Frustum Culling`](roaming_terrain_paper.md#L345)。

### 13.2 当前机制

[`Roam::IsTriangleVisible()`](../../src/algorithms/RoamGeometry.h#L105) 用 triangle 三个顶点的 world AABB，并在 Y 方向按 `GeometricError*HeightScale` 扩张，然后逐一测试六个 frustum planes。不可见时 [`Roam::ComputeScreenErrorScore()`](../../src/algorithms/RoamScreenError.h#L30) 返回 0；Classic 和 DOD 的各自 wrapper 都调用这条共享路径。

这带来两种效果：

- 视锥外 leaf 不会主动占用新的 split budget；
- 已细分的视锥外区域会因 merge score 为 0 而回收，从而把预算让给屏幕内区域。

但是持久 Mesh 仍为所有 active leaves 保留槽位，没有跳过视锥外 leaf。因此当前实现的是“frustum-aware LOD priority”，不是完整 draw culling。全地形仍由 active leaves 覆盖并提交绘制；只有实际发生变化的槽位会重新 emit/upload。

### 13.3 正确性限制

AABB 的 Y 扩张使用公式 (1) 的 nested thickness，已经消除旧 `max(local,left,right)` 的跨层低估。其严格保守性仍受输入是否与 finest bintree 对齐、深度 20 截断和连续双线性曲面解释限制，因此当前 frustum test 还不能无条件继承论文 wedgie culling 的全部证明。

### 13.4 判定

**视锥感知：已实现。** 视锥外区域的 LOD priority 会降低。

**论文增量 culling：未实现。** 没有 per-node flags、跨帧 label、parent flag inheritance 或 subtree 状态缓存。

**最终渲染裁剪：未实现。** CPU Mesh 仍包含整个 terrain topology，尽管增量帧只上传 dirty ranges。

## 14. Mesh 输出与 Triangle Stripping

### 14.1 当前输出

[`ClassicRoamMeshEmit.cpp`](../../src/algorithms/classic_roam/ClassicRoamMeshEmit.cpp) 维护持久 `_meshData` 和 dense `_meshSlotOwners`。每个 active leaf 通过 `MeshSlot` 对应 3 个独立 vertices 和 3 个 indices：

- split 复用 parent slot 写 left child，再追加 right child；
- merge 用一个 child slot 写 parent，删除另一 child slot；
- 删除非末槽时把末槽搬入空洞，并同步 owner 的 `MeshSlot`；
- `WriteMeshLeaf()` 只为 dirty slot 采样位置、法线、高度、UV/debug 数据并修正 winding；
- `FinalizeIncrementalMeshUpdate()` 把连续 dirty slots 合并为 ranges。

相邻 triangle 仍不共享 vertex，也没有 vertex cache 或 strip；增量性来自稳定槽位与局部覆盖，不来自顶点去重。

[`ClassicRoamTerrainLodAlgorithm::BuildRenderData()`](../../src/algorithms/classic_roam/ClassicRoamTerrainLodAlgorithm.cpp) 通过 `BorrowedCpuMesh` 发布 builder 持有的 Mesh，避免复制 vectors。OpenGL [`UploadMeshData()`](../../src/render/TerrainRenderer.cpp) 只对 dirty ranges 调用 `glBufferSubData()`；D3D12 为所有 frame slots 记录 full/range pending state，在各 slot 再次可用时先合并跨多个 Build 的重叠 vertex/index ranges，再写入对应持久映射资源。首次初始化或 buffer 扩容仍需全量上传。

### 14.2 论文输出

论文增量维护 triangle strips，只对 split、merge 或 culling 改变所影响的 strips 做最小 relink，见 [`§7.2 Incremental T-Stripping`](roaming_terrain_paper.md#L351)。

### 14.3 判定

**生成可渲染连续 mesh：已实现。**

**incremental T-stripping：未实现。** 当前没有 strip link、strip continuation 或论文式局部 strip relink。

**现代等价的增量 indexed output：已实现。** topology edit replay、stable dense slots、dirty ranges、借用 packet 和两个 renderer 的部分上传共同覆盖论文“只修改受影响输出”的职责。

**复杂度边界：** 无拓扑/调试属性变化时 Mesh updated triangles 与 upload bytes 可以为 0；变化帧的 CPU emit 约为 `O(edits + dirty slots log dirty slots)`，最坏 reset 仍为 `O(active leaves)`。由于 priority refresh 未增量化，不能据此声称整次 Build 为 `O(Delta N)`。

## 15. Priority 延期与 Progressive Optimization

### 15.1 Priority recomputation deferral

论文通过 viewpoint velocity bound 和 crossover priority 的时间界，把 triangle 放入未来若干帧的 deferral lists，只在可能影响 split/merge 决策时重算 priority，见 [`§7.3`](roaming_terrain_paper.md#L355)。

当前实现：

- merge pass 扫描所有 active internal nodes；
- budget rebalance 扫描所有 active leaves；
- split pass再次扫描所有 active leaves；
- split candidate 弹出时重新计算 score；
- merge candidate 合法性复查时也重新计算 score。

**状态：未实现。** 当前 `PathId` 和 `_previousSplitPaths` 只服务 hysteresis，不是 priority deferral。

### 15.2 Progressive optimization

论文允许 optimizer 在接近 frame deadline 时停止，并保证已完成的操作仍按重要性排序，见 [`§7.4`](roaming_terrain_paper.md#L361)。

当前 `Build()` 没有时间预算、deadline、最大 topology operations 或可恢复 continuation。只要调用开始，就会完成本轮 priority refresh、merge/split crossover、增量 emit 和统计流程。

**状态：未实现。** `TriangleBudget` 限制空间规模，不限制本帧 CPU 时间；再平衡的 batch limit 也只限制额外回收数量，不是 deadline。

## 16. Vertex Morphing、动态地形和扩展 Metrics

### 16.1 Vertex morphing

论文建议 split 时让新 midpoint 从旧 base midpoint 高度逐渐移动到真实高度，merge 反向执行，见 [`论文 morphing`](roaming_terrain_paper.md#L155)。当前 node 和 vertex 没有 morph start/end/time，mesh emit 总是直接采样真实高度。

**状态：未实现。** 当前双阈值 hysteresis 能减少 topology 抖动，但不能消除一次 split/merge 的几何 pop。

### 16.2 动态地形

论文 nested bound 支持从被修改 vertex 沿 dependents 局部更新，见 [`论文 dynamic terrain 目标`](roaming_terrain_paper.md#L87) 和 [`§6.1`](roaming_terrain_paper.md#L275)。

当前只在 height map 对象地址变化或解析出的 nested tree depth 变化时完整重建；判定见 [`ClassicRoamMeshBuilder::Build()`](../../src/algorithms/classic_roam/ClassicRoamMeshBuilder.cpp#L37)。如果同一个 `HeightMap` 对象的内部样本原地变化，指针与预计算深度都不变，当前缓存不会自动失效。

**状态：未实现。** 更准确地说，当前支持“换一个 height map 后全量重建”，不支持论文式局部 dynamic terrain updates。

### 16.3 LOS 与其他 priority corrections

论文还讨论：LOS correction、backface detail reduction、normal distortion、texture-coordinate distortion、silhouette、fog 和 object positioning，见 [`§6.3/§6.4`](roaming_terrain_paper.md#L323)。当前 `ClassicRoamSettings` 只暴露 depth、两个 pixel thresholds、budget 和拓扑开关，没有这些输入或 priority modifier。

**状态：未实现。** 对当前项目的核心“比较 CPU/GPU terrain LOD 数据组织”目标而言，这些不是第一优先级。

## 17. 拓扑验证与调试统计：项目扩展

论文依赖算法构造保证连续性，当前项目另外提供 [`ValidateTopology()`](../../src/algorithms/classic_roam/ClassicRoamValidation.cpp#L149)：

- 从 roots 收集 active leaves；
- 独立于 neighbor 指针，用量化共线 edge 检测 T-junction；
- 检查非空 neighbor 是否指向 active leaf、是否共享完整边、是否反向引用；
- 检查 root diamond、parent/child 和 split flag 不变量。

该 validator 默认关闭，只统计不修复。它是很有价值的项目扩展，因为 forced split/merge 的裸指针拓扑最容易在局部改动后出现静默错误。

[`ClassicRoamStats`](../../src/algorithms/classic_roam/ClassicRoamMeshBuilder.h) 还把 merge candidate、merge topology、budget leaf collect、split scan、split topology、final leaf collect、mesh emit 等阶段分别计时，并记录 full rebuild、updated/reused triangles 和 dirty ranges，经 [`ToTerrainLodStats()`](../../src/algorithms/classic_roam/ClassicRoamTerrainLodAlgorithm.cpp) 接入 UI、CSV 与运行时 Markdown。这比论文结果章节的粗粒度统计更适合当前 Classic/DOD/GPU 对照实验。

## 18. 完整实现状态矩阵

| 论文概念/能力 | 当前项目对应 | 状态 | 能否继承论文性质 |
| --- | --- | --- | --- |
| Height-map domain mapping | `TriangleDomain` + `DomainToWorld()` | 已实现 | 是，针对固定矩形 height map |
| 两根三角形 base diamond | `ResetTopology()` | 已实现 | 是 |
| Binary Triangle Tree | parent/children + `SplitTriangleDomain()` | 已实现 | 是 |
| base/left/right neighbors | `ClassicRoamNode` neighbor pointers | 已实现 | 是，启用 local constraints 时 |
| Dynamic continuous triangulation | split/merge 邻接维护 | 已实现 | 原则上是；validator 默认关闭，仍需测试保障 |
| Forced split | recursive `SplitNode()` | 已实现 | 是，受 `MaxDepth` 和 hard budget 限制 |
| Mergeable diamond | `CanMergeNode()` | 已实现 | 是 |
| Diamond merge | `MergeNodeOrDiamond()` | 已实现 | 是 |
| 跨帧 active topology | persistent nodes + `Active/IsSplit` | 已实现 | topology 与 queue membership 均持久 |
| View-independent error tree | two `_varianceTrees` + shared builder | 已实现 | 结构与公式 (1) 均成立，受输入/深度边界限制 |
| Nested wedgie thickness | `Roam::BuildNestedWedgieSubtree()` | 已实现 | 可继承公式 (1) 的离散 bintree 语义；不自动包含 screen bound |
| Conservative screen-distortion bound | `Roam::ComputeConservativeScreenDistortionPixels()` | 已实现 | 局部 geometric bound 是；最终 priority 另含 edge-density |
| Monotonic priority | 无 invariant/check | 未实现 | 否，最优性证明前提缺失 |
| Highest-priority split heap | indexed `_splitQueue` | 已实现 | 保存全部 active leaves，key 每帧刷新 |
| Lowest-priority merge heap | indexed `_mergeQueue` | 已实现 | canonical diamond priority 为两侧最大值 |
| Persistent `Q_s` | `SplitQueueIndex` + local updates | 已实现 | membership 持久；priority deferral 未实现 |
| Persistent `Q_m` | representative/partner + local updates | 已实现 | membership 持久并由 validator 独立核对 |
| Dual-queue crossover loop | `OptimizeWithPersistentDualQueues()` | 部分实现/变体 | 比较两端 priority；使用 hard-budget merge-first 终止语义 |
| 最小 `Delta N` topology changes | 无证明 | 未实现 | 否 |
| Direct target triangle count | hard `TriangleBudget` | 部分实现/变体 | 只保证上限，不保证精确目标或最优分配 |
| Error tolerance | Split/Merge pixel thresholds | 部分实现/变体 | 是启发式 tolerance，不是 guaranteed bound |
| Frustum-aware priority | `Roam::IsTriangleVisible()` 返回 false 后 score 为 0 | 已实现 | 功能上成立 |
| Incremental frustum labels | 无 | 未实现 | 否 |
| Draw culling outside frustum | Mesh 保留全部 active leaves | 未实现 | 否 |
| Priority deferral lists | 无 | 未实现 | 否 |
| Incremental T-strips | 无 strip 数据结构 | 未实现 | 否；未复刻论文输出格式 |
| Incremental indexed Mesh | stable `MeshSlot`、edit replay、dirty ranges | 已实现/现代等价 | 覆盖变化量输出职责，不继承 strip 特性 |
| Incremental GPU mesh update | OpenGL range upload；D3D12 per-frame pending ranges | 已实现 | 首次/容量增长仍全量上传 |
| Progressive time-bounded optimization | 无 | 未实现 | 否 |
| Vertex morphing | 无 | 未实现 | 否 |
| Dynamic terrain local update | 无 | 未实现 | 否 |
| LOS correction | 无 | 未实现 | 否 |
| Backface/normal/texture/silhouette metrics | 无 | 未实现 | 否 |
| Arbitrary manifold base mesh | 固定矩形双根 | 未实现 | 否 |
| Split/Merge hysteresis | two thresholds + paths | 项目扩展 | 改善稳定性，但不属于论文最优性证明 |
| Budget-full view rebalance | persistent queue crossover | 部分实现/变体 | 改善转向响应；exact target/optimality 前提仍缺失 |
| Topology validator | `ValidateTopology()` | 项目扩展 | 提供诊断，不是运行时修复 |
| 细分阶段 Benchmark | `ClassicRoamStats` | 项目扩展 | 有利于与 DOD/GPU 公平对比 |

## 19. 论文强结论的使用边界

论文的两个强结论不是单一模块提供的，而是一条依赖链：

```text
正确 nested world-space bound
        +
保守 screen-space projection bound
        +
child priority <= parent priority
        +
highest split / lowest merge 的 greedy 调度
        +
forced split 是保持连续性的最小必要 refinement
        =
给定 bintree mesh 空间内的最大误差最优性与全局误差上界
```

当前公式 (1)-(3)、near-plane 处理、forced split、连续 topology、持久双队列结构与增量 Mesh 输出已经具备，但最终 priority、hard budget、迟滞和输出格式仍是项目变体。因此即使局部 geometric component 已有保守像素上界、active triangle 不超过 budget，整条 LOD 管线也不表述为论文的完整全局数学保证。

这是一项主动的工程范围选择，不再作为待修复缺口处理。项目只继承能够由当前实现直接支持的局部结论：几何误差分量是保守投影界、局部约束维持连续拓扑、持久状态避免从根重建 topology、增量 Mesh 避免重写和上传全部活动三角形。其余结论均按 benchmark 与拓扑验证结果描述，不借用论文证明扩大表述。

建议在 UI、报告和代码注释中使用以下术语：

- `Conservative geometric bound (px)`：适合公式 (2)/(3) 分量；
- `Screen-space priority (px)`：适合包含 edge-density 的最终分数；
- `Split/Merge threshold (px)`：适合当前实现；
- `Triangle budget upper bound`：比 `target triangle count` 更准确；
- `ROAM topology baseline`：比 `paper-complete ROAM` 更准确；
- 当前文档不使用 `guaranteed error bound` 或 `optimal mesh`；公式与局部更新的工程效果不能替代论文完整证明。

## 20. 当前工程路线与非目标

### 20.1 当前目标

项目只保留“统一质量口径的工程基线”这一目标：在相同像素优先级、迟滞、预算和相机输入下，比较 Classic、DOD 和 GPU 的执行效率与拓扑效果。Classic 保留单线程、对象式和裸指针特征；DOD 负责连续布局、批处理和多核优化。论文忠实模式、完整最优性证明与逐格式 triangle strips 不再进入当前实施路线。

公式 (1) 已同时接入 Classic、DOD 和由 DOD snapshot 驱动的 GPU ROAM-like，因此三条路径没有发生 quality contract 分裂。但 thickness 数值改变后，旧 benchmark 的 `4 px/2 px` 与新结果不再是同一误差语义；性能趋势可以保留作历史记录，三角形数量和画质结论必须重跑。

### 20.2 已完成的误差公式与保留验证

#### 已完成 1：实现论文 nested wedgie thickness

共享 [`RoamNestedWedgie.h`](../../src/algorithms/RoamNestedWedgie.h) 已严格使用：

```text
e_T = max(e_left, e_right) + abs(baseMidpointDisplacement(T))
```

Classic/DOD 的旧 `_varianceTrees`/`VarianceTrees` 成员名保留以减少结构改动，但内容语义已变为 nested thickness。GPU 沿 snapshot 继承同值。

当前自动测试已覆盖：

- 最细层 thickness 为 0；
- parent thickness 不小于任一 child thickness；
- 有符号局部位移取绝对值；
- 多层累计严格等于公式 (1)；
- 小型几何 bintree 中，每个 ancestor thickness 覆盖所有最细后代顶点相对 ancestor plane 的高度偏差；
- 129/513 输入深度解析与深度 20 上限。

若以后扩展输入范围，可以补充真实小图、非 `2^k+1` 输入和深度截断测试；这些验证不阻塞当前固定高度图实验范围。

#### 已完成 2：实现保守 screen-space wedgie projection

共享 `RoamScreenProjection.h` 按论文公式 (2)/(3) 把世界高度 thickness vector 变换到 homogeneous clip space，求 triangle corners 上 numerator/denominator 的保守组合；wedgie 触碰或穿越 near plane 时给 artificial maximum priority。Classic/DOD 调用同一 CPU helper，OpenGL/D3D12 compute 使用相同代数和参数。

验收条件：

- `RoamScreenProjectionTests.cpp` 已验证多组 triangle/camera 的密集公式 (2) 采样不超过公式 (3) bound；
- FOV、aspect、drawable width/height、camera pitch/roll、perspective/orthographic 与 near crossing 已覆盖；
- 已有一个带 child-plane displacement 的 parent/child geometric-bound 单调构造；最终 priority 的随机/全树测试和运行时统计仍待实现。

#### 口径 3：把 edge density 与 error bound 分开

如果仍需要平坦近景细分，保留 `edgeLengthPixels` 作为独立可选 quality term，但不要把其结果称为论文 geometric error bound。可以同时报告：

```text
GeometricBoundPixels
EdgeDensityScorePixels
FinalPriority = max(...)
```

这样可以区分“地形近似误差”与“项目希望的最小几何密度”。

### 20.3 已完成：persistent dual queues

当前已完成：

- 每个 active leaf 通过 `SplitQueueIndex` 在 `Q_s` 中获得稳定 handle；
- 每个 mergeable diamond 通过 canonical representative 在 `Q_m` 中唯一存在；
- split/forced split/merge 只局部增删受影响元素；
- 每帧从上一帧 topology 和 queues 继续；
- 同一循环比较 `maxSplitPriority` 与 `minMergePriority`；
- strict hard capacity 下采用 merge-first crossover；
- validator 独立检查 `Q_s == active cut`、`Q_m == canonical mergeable diamonds` 和 heap order。

以下论文机制不再作为当前路线要求，只保留为能力边界：

- direct target-count reference mode；
- 候选 crossover 区间过大时的 top-down fallback；
- priority deferral，使轻微相机移动的评分量也从 O(N) 降到接近 O(Delta N)；
- 小地形穷举合法 triangulations，对照 representable count 下的最大 bound；
- 最终 priority 的 parent-child monotonicity。

### 20.4 非当前目标：增量视锥状态与优先级延期

论文用六平面状态、`OUT/ALL_IN/DONT_KNOW` 标签和未来重算列表减少重复裁剪与优先级计算。这两项能把完整帧更新进一步推向 `O(changes)`，但当前项目接受全队列优先级刷新成本，不再计划实现。现有视锥测试继续用于抑制屏幕外主动细分，不将其表述为论文的增量裁剪。

### 20.5 已完成的现代增量输出与剩余验证

论文在 1997 年硬件上选择 triangle strips。Classic 当前实现的是现代 indexed 等价路径：每个活动叶三角形拥有 dense stable slot，split/merge 记录 edit，emit 阶段只重写 dirty slots，renderer 只上传 dirty ranges。D3D12 还为交换链每个 frame slot 保留 pending ranges，避免轮转时漏更新。DOD 普通 CPU `Build()` 尚未复用这套增量 mesh 槽位；它直接遍历 `ActiveLeafNodes` 完整写出本帧 CPU mesh，而供 GPU adapter 使用的 `UpdateTopology()` 可以完全跳过 CPU emit。

已通过 `incremental-emit` 回归验证：相同视点连续三次 Build 时，首帧全量初始化，第二帧只清除一次 Rebuilt 调试属性，第三帧 `updated triangles=0`、`dirty ranges=0`、全部 leaf 复用。

当前仍应完成的工程正确性验证：

- 建立独立 full-emit reference，与增量输出逐帧比较 triangle set、属性和 winding；
- 在随机 split/merge、末槽移动和 topology reset 序列上做性质测试；
- 用 runtime benchmark 验证小幅移动时 CPU emit 与 GPU upload bytes 随 changed slots 缩放；
- triangle strips 不再进入当前路线；现代 indexed slots/ranges 是项目确定采用的输出方案。

### 20.6 非当前目标：按时间逐步优化与顶点渐变

论文允许在合法 split/merge 事务之间按剩余帧时间暂停，并用 midpoint 渐变减轻跳变。当前项目使用完整 Build 和 split/merge 迟滞，不增加事务截止时间或顶点渐变状态；相关字段、同步规则和性能结论不属于当前实现范围。

### 20.7 非当前目标：动态地形与高级误差项

论文还讨论动态地形、视线、背面、法线、轮廓和雾等扩展。当前实验使用固定高度图和统一几何优先级，这些扩展不进入项目路线，也不作为 Classic、DOD、GPU 对比的完成条件。

## 21. 实现状态与工程取舍

| 优先级 | 工作 | 首要收益 | 前置依赖 | 主要风险 |
| --- | --- | --- | --- | --- |
| 已完成 | nested wedgie 公式 (1) | 恢复 world-space 累计 thickness 语义 | 无 | 已改变三种算法质量口径，历史数据需重跑 |
| 已完成 | conservative projection 公式 (2)/(3) | 恢复局部 screen-space bound | nested thickness | 已覆盖 near-plane 与角点分子/分母极值 |
| 可选研究 | 父子优先级单调性检查 | 研究最终 priority 的数学性质 | 公式 (1) 已有递推测试 | 不属于当前完成条件 |
| 已完成 | persistent dual queues + hard-budget crossover | topology membership 局部更新、满预算资源交换 | intrusive indexed heaps | key refresh 仍为 O(N)，不能单独恢复完整 `Delta N` 证明 |
| 非当前目标 | exact target-count reference 与 top-down fallback | 论文严格预算语义 | dual queues | 当前采用 hard upper bound |
| 非当前目标 | incremental frustum flags | 减少重复 plane tests | 保守 bound | 当前接受每次评分重新测试 |
| 非当前目标 | priority deferral | 固定/慢速相机减少评分工作 | monotonic conservative priority | 当前接受全队列 key refresh |
| 已完成 | incremental indexed output | 降低 emit/upload `O(N)` 成本 | topology edit log + stable slots | full-reference 随机轨迹对照仍待补齐 |
| 非当前目标 | incremental T-strips | 论文历史输出格式 | changed-node tracking | 当前采用增量 indexed Mesh |
| 非当前目标 | progressive optimization | 严格 frame-time 控制 | persistent queues | 当前按完整 Build 统计 |
| 非当前目标 | vertex morphing | 减少 popping | 稳定 shared midpoint identity | 当前使用迟滞 |
| 非当前目标 | dynamic terrain local updates | 支持实时地形修改 | dependency graph + bounds | 当前地形在一次实验中固定 |
| 非当前目标 | LOS/其他 metrics | 扩展应用语义 | metric plugin contract | 不属于当前研究问题 |

## 22. 不建议直接做的改动

### 22.1 不要只把两个临时 heap 改名为 `Qs/Qm`

名称不会带来持久 membership、局部更新、crossover 或 `Delta N` 复杂度。应先定义 queue invariant 和 topology transaction。

### 22.2 不要把公式 (1)-(3) 已实现等同于完整 guaranteed bound

旧 `max(local,left,right)` 与 center-depth projection 均已移除，persistent dual queues 也已实现；但最终 priority 还叠加 edge-density/visibility/hysteresis，且缺少 parent-child 单调性与 exact-target 对照验证，不能据此宣称完整全局保证。

### 22.3 不要只增加 worker 数来模拟论文增量性

全量 scan 并行化仍然是 `O(N)`。它可能加速工程实现，但不是论文 `O(Delta N)` 思路的对应物。Classic 作为串行对象式基线尤其不应因追求单项耗时而混入 DOD 的核心数据并行特征。

### 22.4 不要立即删除当前 edge-length term

它可能是项目平坦地形视觉质量的重要约束。正确做法是拆分和命名，再用画质与 Benchmark 数据决定默认组合，而不是为了论文形式一致直接移除。

### 22.5 不要让 Classic、DOD、GPU 静默使用不同误差语义

若项目要比较数据布局和执行平台，三条路径必须共享可验证的 quality contract。当前统一公式、像素阈值、迟滞与硬预算就是正式对比口径，不再规划额外的论文严格模式。

## 23. 测试缺口与验证建议

当前 `tests/RoamNestedWedgieTests.cpp` 已覆盖共享公式递推、叶层为 0 和分辨率深度解析；`tests/RoamScreenProjectionTests.cpp` 已覆盖公式 (3) 对密集公式 (2) 采样的保守性、投影/相机变化、正交投影和 near crossing；Classic/DOD budget-reentry 测试与 smoke benchmark 覆盖拓扑合同。下面 1-8、11-12 属于工程正确性增强；9-10 只服务论文证明研究，已不再是当前完成条件：

1. `SplitTriangleDomain()` 在多层后仍覆盖 parent、无重叠、winding 一致。
2. root diamond 和任意 forced split chain 后无 T-junction。
3. split 后立即 merge 能恢复原邻接，重复多次不产生 stale pointer。
4. random camera path 下每帧 `ValidateTopology()` 的三个错误计数始终为 0。
5. forced split 在预算边界上不突破 hard limit，失败后 topology 仍合法。
6. 视锥快速旋转时，新入视野高 score 区域在同一 Build 获得预算。
7. nested wedgie 对真实 height samples 的 ancestor bound property。
8. CPU helper、OpenGL GLSL、D3D12 HLSL 在相同输入上的逐值一致性。
9. priority parent-child monotonicity。
10. persistent dual queues 与 top-down reference 在小输入上的最终 priority/triangle set 对照。
11. incremental emit 与 full emit 的 triangle set 对照。
12. 原地修改同一个 `HeightMap` 对象时缓存失效行为。

建议把现有 `ValidateTopology()` 的核心检查抽成可从测试调用的纯诊断接口；默认运行路径仍可关闭昂贵验证。

## 24. 当前性能含义

### 24.1 源码可直接确认

- nested wedgie rebuild 是完整深度递归，复杂度与完整预计算 bintree 节点数成正比；
- Classic 与 DOD 每次 Build 都会刷新持久 `Q_s/Q_m` 的全部现有视点相关分数，并以一次自底向上建堆恢复顺序；持久的是队列成员，不是跨相机状态继续有效的分数；
- Classic 串行刷新两个队列；DOD 对至少 256 项的队列评分使用持久线程池并行写入互不重叠的 `ScreenErrors` 或 merge queue entry，自动模式最多使用 8 个 worker；
- DOD 的 split/merge 候选快照直接复用已缓存的 `ScreenError`/`Score`，不再为候选收集重复评分；真正提交 merge 前仍会重算所选 parent 或 diamond 双方分数，作用是确认并发或前序拓扑修改后候选仍然合法；
- Classic 的 budget 与最终活动叶视图分别直接读取 `_splitQueue.size()` 和 `_meshSlotOwners`；DOD 直接读取 `ActiveLeafNodes.size()` 和 `ActiveLeafNodes`。两条默认路径都不再为输出递归收集活动叶三角形；
- 两者仍会递归遍历 active internal topology 以重建跨帧迟滞使用的 split paths；
- Classic mesh emit 只为 dirty slots 重采样 3 个 vertices，连续 slots 合并为 ranges，并通过 borrowed packet 让 OpenGL/D3D12 局部上传；首次、reset 或 buffer 扩容才全量上传；
- DOD 普通 CPU `Build()` 每帧把全部 `ActiveLeafNodes` 写成 3 顶点/3 索引的完整 mesh；至少 256 个活动叶三角形时 emit 可并行，自动模式最多 8 个 worker；`UpdateTopology()` 则不生成 CPU mesh；
- DOD 只对满足同一 8x8 terrain chunk 写入约束的 interior 候选并发提交；split 至少 32 个、merge 至少 160 个且至少分布在两个非空 chunk 才启用并行，forced split、首次分配 child 和跨 chunk 候选回退到串行路径；
- 两套 indexed heap 的局部插入、删除和恢复都带有对数因子，全量 priority refresh 后的建堆为线性复杂度。

### 24.2 基于结构的推断

固定相机且 topology 不变化时，Classic 的 mesh emit/upload 可降为 0，但 queue key refresh 与 active split path 遍历仍是 `O(N)`；DOD 还会完整写出 CPU mesh，除非走只更新拓扑的 `UpdateTopology()`。因此两者的整次 CPU `Build()` 都不能呈现论文“几乎无变化时成本接近零”的完整优势，且当前 Classic 与 DOD 的总耗时还混入了不同的 mesh 输出策略。

Classic 裸指针 node 在堆上分散，拓扑访问更容易出现 cache miss；DOD 的同字段批量访问连续，但一个完整拓扑事务仍需跨 `Domains`、child、neighbor、flag 等多组数组，并不等于所有访问都连续。DOD 的候选快照、排序、分 chunk、邻域排序去重和线程同步也会形成额外固定成本，因此小批量阶段可能慢于 Classic 的直接串行指针修改。

### 24.3 需要 profiler 才能确认

- 当前场景究竟由 score 刷新、heap 操作、指针/索引追踪、候选分块、线程同步、HeightMap 采样、mesh 分配还是 upload 主导；
- DOD 在多少活动节点和候选数量下，SoA 与并行收益能够覆盖快照、排序和 chunk 提交成本；
- Classic 增量 mesh 对稳定视点的收益，与 DOD 完整并行 emit 在大变化帧的收益交叉点；
- 两套只增不缩的历史节点池高水位对长时间运行的真实内存压力。

## 25. 对 Classic 与 DOD 比较口径的影响

当前研究问题采用“对象式串行 Classic 工程基线 vs DOD 与 GPU 优化版本”的口径，不再把比较简化为“单核对多核”，也不宣称 Classic 是论文源码的逐项复刻。当前两套 CPU 实现已经共享大部分 LOD 语义，因此需要先把共同算法合同、真正的数据导向优势和只在某一版本中完成的工程优化分开。

### 25.1 两套实现共享的算法合同

以下能力不能记作 DOD 相对 Classic 的独有收益，因为当前两套实现都具备：

- 两个根三角形组成根 diamond，并使用相同的 base-edge 二分规则；
- 共享 [`Roam::SplitTriangleDomain()`](../../src/algorithms/RoamGeometry.h#L29)、[`Roam::BuildNestedWedgieSubtree()`](../../src/algorithms/RoamNestedWedgie.h#L60) 和 [`Roam::ComputeScreenErrorScore()`](../../src/algorithms/RoamScreenError.h#L30)，因此几何误差、视锥判断、像素投影和 edge-density 项使用同一 CPU 实现；
- 相同的 `MaxDepth`、split/merge thresholds、Triangle Budget、局部约束和拓扑验证入口；
- recursive forced split、邻接重连、diamond merge 和同一 Build 内的向上级联合并；
- 持久 `Q_s/Q_m`、局部队列成员维护、hard-budget merge-first crossover 和跨帧迟滞；
- merge 后保留历史 child，后续重新 split 时复用已有节点；
- 默认热路径不再从两个 roots 递归收集活动叶三角形；递归收集只保留给 validator 交叉检查；
- 相同的公共 `TerrainLodRenderPacket`、统计结构和 benchmark 参数口径。

所以在相同输入和设置下，Classic 与 DOD 应首先被视为**同一套 ROAM 质量合同的两种 CPU 数据组织和执行策略**。若活动叶三角形数量或拓扑结果不同，应优先检查实现一致性，而不是把差异解释为 DOD 本身的性能优化。

### 25.2 数据布局与生命周期

| 对比项 | Classic | DOD | 实际影响 |
| --- | --- | --- | --- |
| node ownership | [`_nodes`](../../src/algorithms/classic_roam/ClassicRoamMeshBuilder.h#L440) 是 `vector<unique_ptr<ClassicRoamNode>>` | [`DataOrientedRoamNodePool`](../../src/algorithms/data_oriented_roam/DataOrientedRoamState.h#L141) 由多组 `vector` 组成 | Classic node 独立堆分配；DOD 同字段连续 |
| 拓扑引用 | `ClassicRoamNode*` 裸指针 | `uint32_t` node index | Classic 直接解引用；DOD 不受 vector 扩容地址变化影响，并便于批量索引处理 |
| 字段布局 | domain、child、neighbor、误差和状态集中在一个对象 | `Domains/Parents/Children/Neighbors/Errors/Flags` 分离为 SoA | DOD 的窄 pass 可只触及需要的数组；完整拓扑事务仍会跨多数组访问 |
| 可读访问 | 直接访问 node 字段 | `DataOrientedRoamNodeRef/ConstRef` proxy 引用 SoA 字段 | DOD 保持对象式代码可读性，但 proxy 不改变底层数据布局 |
| 活动叶三角形 | 独立 `Q_s` 保存活动叶；`_meshSlotOwners` 保存输出稠密视图 | [`ActiveLeafNodes`](../../src/algorithms/data_oriented_roam/DataOrientedRoamState.h#L228) 同时是活动叶集合和 `Q_s` heap | DOD 少维护一份活动叶 vector，但其顺序是 heap 顺序，不是稳定渲染顺序 |
| 活动 internal 节点 | 没有独立集合，拓扑由 pointers 与 `IsSplit/Active` 表达 | `ActiveInternalNodes` 加反向位置表 | DOD 不必扫描包含历史节点的整个 node pool 来发现当前 internal 集合 |
| merge queue | node 内保存 heap index、representative 和 partner 指针 | 独立 position/representative/partner 数组 | 两者语义相同；DOD 的反向关系连续存储 |
| 历史节点 | merge 后 child 对象继续存活，reset 才清空 | merge 后 child index 继续保留，reset 才清空 | 两者内存都更接近历史高水位，而不是当前活动拓扑大小 |
| 预分配 | node 首次创建时逐个堆分配；mesh arrays 按 Triangle Budget reserve | [`ReserveNodePool()`](../../src/algorithms/data_oriented_roam/DataOrientedRoamState.cpp#L376) 同时 reserve SoA、活动集合和队列反向表 | DOD 减少首次深入细分时多数组反复扩容；index 语义不依赖地址稳定 |
| 线程池 | 无，固定串行 | builder 持有 [`DataOrientedRoamThreadPool`](../../src/algorithms/data_oriented_roam/DataOrientedRoamThreadPool.cpp#L7)，state 借用指针 | DOD builder move 后必须重绑 `ThreadPool`；该裸指针不是拓扑 ownership |

### 25.3 一帧执行链的真实差异

| 阶段 | Classic | DOD |
| --- | --- | --- |
| Build 入口 | [`ClassicRoamMeshBuilder::Build()`](../../src/algorithms/classic_roam/ClassicRoamMeshBuilder.cpp#L16) 返回 builder 持有的 mesh 引用 | [`DataOrientedRoamPipeline::Build()`](../../src/algorithms/data_oriented_roam/DataOrientedRoamPipeline.cpp#L65) 返回 pipeline 持有的持久 Mesh 引用；`UpdateTopology()` 可关闭 CPU emit |
| priority refresh | [`RefreshPersistentQueuePriorities()`](../../src/algorithms/classic_roam/ClassicRoamQueues.cpp#L447) 串行刷新 `Q_s/Q_m` | [`RefreshPersistentSplitQueuePriorities()`](../../src/algorithms/data_oriented_roam/DataOrientedRoamQueues.cpp#L414) 与 [`RefreshPersistentMergeQueuePriorities()`](../../src/algorithms/data_oriented_roam/DataOrientedRoamQueues.cpp#L575) 可按连续区间并行 |
| merge 调度 | [`OptimizeWithPersistentDualQueues()`](../../src/algorithms/classic_roam/ClassicRoamQueues.cpp#L466) 在一个循环内统一处理阈值 merge、split 和预算 crossover | [`MergeWithDiamondQueue()`](../../src/algorithms/data_oriented_roam/DataOrientedRoamTopology.cpp#L1251) 先批量处理低分 merge；[`RefineWithSplitQueue()`](../../src/algorithms/data_oriented_roam/DataOrientedRoamTopology.cpp#L1163) 再处理 split，并在预算不足时继续消费实时 `Q_m` crossover |
| 候选收集 | 直接读取 heap 顶部，不创建全量提交快照 | split/merge 都从缓存 score 生成候选快照；merge 快照排序，split 快照按优先级分 chunk |
| 拓扑提交 | 串行执行完整 forced-split closure 或 diamond merge | 满足安全条件的 interior 候选先 chunk 并行提交；首次 child 分配、forced split、跨 chunk 和不安全候选仍由实时 heap 串行提交 |
| 活动叶视图 | `_meshSlotOwners` | `ActiveLeafNodes` | 两者都避免默认输出前递归 collect |
| CPU mesh emit | [`ApplyIncrementalMeshUpdates()`](../../src/algorithms/classic_roam/ClassicRoamMeshEmit.cpp#L89) 只重写持久 `_meshData` 的 dirty slots | [`ApplyIncrementalMeshUpdates()`](../../src/algorithms/data_oriented_roam/DataOrientedRoamMeshEmit.cpp#L390) 重放 NodeIndex edit，只批量写持久 Mesh 的 dirty slots；初始化或 topology-only 后重新进入 CPU 路径才完整建立 cut |
| renderer 交付 | `BorrowedCpuMesh`、generation、dirty ranges，支持局部 buffer upload | 同样发布 `BorrowedCpuMesh`、generation 和 dirty ranges；NodeIndex/SoA 只改变内部 slot 维护方式 | 两者共享增量输出契约；DOD 当前保留 dirty slot 的批量 worker 写入 |
| 帧末状态 | 递归收集 active split paths；mesh slots 和 queues 跨帧保留 | 同样递归收集 active split paths；活动索引、queues 和 node pool 跨帧保留 | 两者都尚未消除这次 internal topology 遍历 |

### 25.4 真正属于 DOD 的优势

1. **同字段连续存储。** `ScreenErrors`、child indices、neighbor indices、depths 和 flags 分离后，批量 score refresh、统计或验证可以只读取所需数组。Classic 无法在不改变对象布局的情况下获得相同的数据流。
2. **索引化活动集合。** `ActiveLeafNodes/ActiveInternalNodes` 与反向位置表使激活、失活和任意位置删除不依赖对象地址，也不必扫描历史 node pool。`ActiveLeafNodes` 同时作为 `Q_s` heap，进一步避免重复活动叶容器。
3. **批量评分。** 队列不少于 256 项时，DOD 把 score refresh 切成互不重叠的连续区间，通过持久线程池并行执行；Classic 固定单 worker。
4. **受约束的多核拓扑提交。** DOD 用固定 8x8 chunk 作为写入 ownership。只有全部受影响节点都位于同一 chunk、不会首次分配 child、也不需要 forced split 的候选才能并行，保证 worker 不写同一邻接区域。
5. **按实测门槛启用并行。** split interior 候选少于 32、merge interior 候选少于 160，或只有一个非空 chunk 时保持串行，避免小任务调度成本反噬。这里没有限制算法候选总数，只决定提交方式。
6. **持久 worker。** `DataOrientedRoamThreadPool` 只在需要时扩容并跨帧复用，避免每个 pass 重复创建线程。
7. **成组预分配和容量统计。** `ReserveNodePool()` 依据 `MaxDepth` 与 Triangle Budget 为全部 SoA、活动集合和队列反向表预留容量；stats 还能报告节点存储字节数和数组数量，便于验证内存布局成本。
8. **dirty Mesh 批量写入。** DOD 按连续 slot 排序 dirty 列表；数量不少于 256 时沿用持久 worker 分段写入独占三角形槽位，不需要锁或原子追加。高变化率完整 emit 选择仍未在 D1 实现。

这些优势的核心不是“把 Classic 同一段代码开更多线程”，而是先把状态改成可索引、可分区、可批量访问的数据，再对满足无写冲突条件的工作启用并行。

### 25.5 只在当前 DOD 中实现，但不等于 DOD 必然要求

以下做法是当前 DOD 版本的工程选择，也可以移植回对象式实现，因此不能全部归因于 SoA 理论：

- 持久线程池和 256 项评分/emit 门槛；
- 8x8 terrain chunk、split 32/merge 160 的并行提交门槛；
- split/merge 候选快照、排序和分 chunk；
- `ActiveInternalNodes` 的显式维护；
- 先运行独立 merge pass，再运行 split/crossover pass 的调度顺序；
- topology-only 的 `UpdateTopology()` 入口。

反过来，下面这些机制当前 Classic 已有或做得更完整，不能写成 DOD 优势：

- 持久双优先队列、局部 membership 更新、hard-budget crossover；
- nested wedgie、保守像素优先级、视锥感知和迟滞；
- forced split 与 diamond merge；
- 活动叶三角形无需递归收集；
- **增量 CPU mesh 槽位、dirty ranges、borrowed mesh 和局部 GPU buffer upload**。DOD 普通 CPU 路径目前仍完整 emit。

Classic 还具有较低的固定调度成本：它不生成全量候选快照，不排序 split 候选，不分 chunk，也不进行 worker 同步。活动规模较小、拓扑变化稀疏或并行安全候选比例低时，Classic 某些阶段快于 DOD 是合理结果，不能据此否定 DOD 数据布局；同样，也不能仅凭 SoA 名称预判 DOD 必然更快。

### 25.6 公平 Benchmark 应怎样拆

对比报告至少应分别观察：

| 观察项 | 要回答的问题 |
| --- | --- |
| `prepare` | variance rebuild、pool reserve 或 reset 是否混入稳态帧 |
| `merge candidate mark` | 当前字段覆盖 `Q_m` 全量 score refresh、heapify 和候选快照；排序与分 chunk 尚计入后续 merge 总时间 |
| `merge topology` | 当前派生字段还包含候选排序和 chunk 构建；分析时要再结合候选数、非空 chunk 数、并行提交数和串行 boundary 回退 |
| `split candidate mark` | 当前字段覆盖 `Q_s` score refresh、heapify 和候选快照；不能把它笼统称为“误差评估” |
| `split topology` | 当前字段包含 split 候选排序/分 chunk、forced closure、预算 crossover、并行 interior split 和串行回退 |
| `mesh emit` | Classic dirty-slot 增量写入与 DOD 全量并行写入的不同策略 |
| `finalize` | split path 递归、统计聚合和 packet 准备成本 |
| worker/chunk 统计 | 并行路径是否真的启动，候选是否足够多且分布到多个安全 chunk |

应同时保留 DOD 单 worker 诊断结果，用来区分“布局/批处理收益”和“多核收益”；但产品对比仍应使用 Classic 串行基线与 DOD 自动 worker 的完整配置。总 CPU 时间比较必须注明 Classic 使用增量 mesh、DOD 使用完整 CPU emit；若只研究拓扑优化，应单独比较 split/merge 阶段，不能把不同输出策略的成本混为 DOD 数据布局结论。

### 25.7 当前共同边界

- 两者都每帧刷新所有现有 `Q_s/Q_m` 分数，复杂度仍有 `O(N+M)` 下界；
- 两者都在帧末递归收集 active split paths；
- 两者都保留历史 child，节点存储随运行期间拓扑高水位增长；
- DOD 并行 split 只覆盖已经拥有可复用 children、无需 forced closure 的 interior 候选，首次向更深层细分仍主要依赖串行路径；
- DOD chunk 提交前后的候选快照、排序、邻域排序去重和 join 是真实成本，不应隐藏在笼统的 `split/merge` 总时间里；
- Classic 的增量 mesh 与 DOD 的完整 mesh 目前不是同一输出算法，端到端差异不能全部归因于 node layout。

最终报告应写成“Classic 对象式串行基线与 DOD 数据导向优化版本对比”，并明确收益究竟来自 SoA、索引化活动集合、并行 score refresh、chunk 拓扑提交、完整并行 emit，还是来自某一版本独有的增量 mesh。Classic、DOD 和 GPU 继续共享当前 pixel score、hysteresis、hard budget 和相机输入，不再设置额外的论文严格模式。

## 26. 文件与符号索引

| 文件 | 关键符号 | 对比中的职责 |
| --- | --- | --- |
| [`docs/source_analysis/roaming_terrain_paper.md`](roaming_terrain_paper.md) | §4-§7 | 论文拓扑、双队列、误差和性能增强依据 |
| [`ClassicRoamMeshBuilder.h`](../../src/algorithms/classic_roam/ClassicRoamMeshBuilder.h) | `TriangleDomain`, `ClassicRoamNode`, settings/stats | Classic 核心状态和合同 |
| [`ClassicRoamMeshBuilder.cpp`](../../src/algorithms/classic_roam/ClassicRoamMeshBuilder.cpp) | `ClassicRoamMeshBuilder::Build` | 当前每次 Build 的真实 pass 顺序 |
| [`RoamGeometry.h`](../../src/algorithms/RoamGeometry.h) | `SplitTriangleDomain`, `ComputeBaseMidpointDisplacement`, `SampleTerrainWorld`, `IsTriangleVisible` | Classic/DOD 共用几何、HeightMap 采样和视锥逻辑 |
| [`RoamScreenError.h`](../../src/algorithms/RoamScreenError.h) | `ComputeScreenErrorScore` | Classic/DOD 共用最终候选优先级分数 |
| [`RoamScreenProjection.h`](../../src/algorithms/RoamScreenProjection.h) | `ComputeConservativeScreenDistortionPixels`, `ComputeProjectedLongestEdgePixels` | 公式 (2)/(3)、near crossing 与独立 edge-density 投影 |
| [`ClassicRoamState.cpp`](../../src/algorithms/classic_roam/ClassicRoamState.cpp) | `AddNode`, `ResetTopology`, leaf/path collect | 根 diamond、node ownership、持久 topology |
| [`ClassicRoamScoring.cpp`](../../src/algorithms/classic_roam/ClassicRoamScoring.cpp) | Classic domain wrapper、variance tree 接入、score wrapper | 把 Classic state 映射到共享几何和评分函数 |
| [`ClassicRoamQueues.cpp`](../../src/algorithms/classic_roam/ClassicRoamQueues.cpp) | `RefreshPersistentQueuePriorities`, `OptimizeWithPersistentDualQueues` | Classic 持久 `Q_s/Q_m` 和串行 crossover |
| [`ClassicRoamTopology.cpp`](../../src/algorithms/classic_roam/ClassicRoamTopology.cpp) | `SplitNode`, `CanMergeNode`, `MergeNodeOrDiamond` | forced split、neighbor relink 和 diamond merge |
| [`ClassicRoamMeshEmit.cpp`](../../src/algorithms/classic_roam/ClassicRoamMeshEmit.cpp) | edit replay、slot replace/compact、`WriteMeshLeaf`、range finalize | 增量 CPU indexed Mesh |
| [`ClassicRoamValidation.cpp`](../../src/algorithms/classic_roam/ClassicRoamValidation.cpp) | `ValidateTopology` | T-junction、邻接和树不变量诊断 |
| [`ClassicRoamTerrainLodAlgorithm.cpp`](../../src/algorithms/classic_roam/ClassicRoamTerrainLodAlgorithm.cpp) | adapter, stats mapping | 接入统一算法接口和 Benchmark |
| [`DataOrientedRoamState.h`](../../src/algorithms/data_oriented_roam/DataOrientedRoamState.h) | `DataOrientedRoamNodePool`, `ActiveLeafNodes`, `ActiveInternalNodes` | DOD SoA、活动集合、反向位置和持久队列状态 |
| [`DataOrientedRoamPipeline.cpp`](../../src/algorithms/data_oriented_roam/DataOrientedRoamPipeline.cpp) | `BuildInternal`, `UpdateTopology` | DOD 的 merge/split/emit 顺序和 topology-only 入口 |
| [`DataOrientedRoamQueues.cpp`](../../src/algorithms/data_oriented_roam/DataOrientedRoamQueues.cpp) | priority refresh、持久 `Q_s/Q_m`、候选快照 | DOD 批量评分、heap 维护和缓存 score 复用 |
| [`DataOrientedRoamTopology.cpp`](../../src/algorithms/data_oriented_roam/DataOrientedRoamTopology.cpp) | `CommitInteriorSplitChunks`, `CommitInteriorMergeChunks`, `RefineWithSplitQueue`, `MergeWithDiamondQueue` | DOD chunk 分类、条件并行提交和串行回退 |
| [`DataOrientedRoamMeshEmit.cpp`](../../src/algorithms/data_oriented_roam/DataOrientedRoamMeshEmit.cpp) | `ApplyIncrementalMeshUpdates`, `FinalizeIncrementalMeshUpdate` | DOD NodeIndex slot edit replay、dirty Mesh 批量写入和 range 发布 |
| [`DataOrientedRoamThreadPool.cpp`](../../src/algorithms/data_oriented_roam/DataOrientedRoamThreadPool.cpp) | `ParallelFor`, `WorkerLoop` | DOD 跨帧复用 worker 的任务执行器 |
| [`DataOrientedRoamTerrainLodAlgorithm.cpp`](../../src/algorithms/data_oriented_roam/DataOrientedRoamTerrainLodAlgorithm.cpp) | adapter, stats mapping | DOD 接入统一 CPU mesh 和 Benchmark 统计 |
| [`GpuRoamShaderCommon.h`](../../src/algorithms/gpu_roam/GpuRoamShaderCommon.h) | `GpuRoamScoreCommonGlsl` | OpenGL 两个 compute pass 共用的 GPU ROAM-like score 逻辑 |
| [`ITerrainLodAlgorithm.h`](../../src/algorithms/ITerrainLodAlgorithm.h) | settings, view input, stats, render packet | 三种 ROAM 路径的共享质量和统计口径 |
| [`TerrainRenderer.cpp`](../../src/render/TerrainRenderer.cpp) | `BuildRenderData` 消费、`UploadMeshData` | OpenGL CPU Mesh full/range 上传 |
| [`D3D12TerrainRenderer.cpp`](../../src/render/D3D12TerrainRenderer.cpp) | `UploadMeshData`、`UploadMeshForFrame` | D3D12 per-frame pending full/range 上传 |

## 27. 最终分类清单

### 27.1 已实现

- height-map 上的两根 triangle base diamond；
- Binary Triangle Tree 几何细分规则；
- object-style node、parent/child 和三类 neighbor；
- 持久 active topology；
- split max-heap pass；
- recursive forced split；
- neighbor relink 与 crack prevention；
- mergeable diamond 检查；
- diamond merge 和同一 Build 向上级联；
- 受深度 20 上限约束的 view-independent nested tree 数据结构；
- 论文公式 (1) nested wedgie thickness，包括 finest leaf 为 0 和跨层累计；
- Classic/DOD 共享公式实现，GPU snapshot 继承相同 `GeometricError`；
- 论文公式 (2)/(3) 的保守 pixel bound 与 near-plane artificial maximum；
- FOV、aspect、投影、drawable width/height 和三角形角点齐次深度参与 priority；
- frustum-aware LOD priority；
- active leaf hard upper budget；
- stable dense Mesh slots、topology edit replay、dirty ranges 与增量 GPU upload；
- CPU mesh、法线、UV、winding 输出；
- topology validator 和阶段统计。

### 27.2 部分实现或项目变体

- Nested Wedgie Tree：公式 (1) 已实现，但非 dyadic 输入、深度 20 截断和连续双线性曲面仍需验证；
- Screen Priority：geometric component 是 conservative distortion bound；最终值另含 edge-density 项；
- Split/Merge Queues：持久 indexed heaps 与局部 membership 已实现，但 key 仍每帧全量刷新；
- Triangle Budget：hard upper bound，不是 direct target count；
- Frame Coherence：topology、queue membership 与 indexed output 增量；priority/frustum state 不增量；
- Frustum Culling：影响 priority，不维护增量 labels，也不剔除最终输出；
- 抗 popping：有 hysteresis，没有 vertex morphing；
- 预算质量再分配：统一 crossover 已实现，但采用 hard-capacity merge-first 变体。

### 27.3 未实现

- 可验证的 monotonic priority；
- 给定 triangle count 下的最优性；
- guaranteed screen-space error bound；
- incremental frustum flags/labels；
- priority deferral lists；
- incremental T-stripping；
- progressive deadline-based optimization；
- vertex morphing；
- dynamic terrain 局部 bound 更新；
- LOS 与其他高级 priority corrections；
- 任意 manifold base mesh；
- 对实际 HeightMap ancestor bound 的完整性质测试。

### 27.4 当前维护重点

1. 保持 Classic、DOD 和 GPU 的公式、阈值、预算与相机输入一致。
2. 加强随机 split/merge、增量 Mesh 与跨后端数值一致性测试。
3. 按真实阶段记录误差计算、拓扑提交、Mesh 输出、上传和 GPU 工作，不用总耗时掩盖算法差异。
4. 在统一场景下重新采集性能与画质数据，为闭包感知预算调度研究提供稳定基线。

项目不再补齐最终 priority 单调性、exact target、增量 triangle strips 或论文完整最优性证明。文档继续避免使用“论文级 guaranteed error bound”和“给定 bintree mesh 空间中的 optimal triangulation”，改用“保守几何误差分量”“连续拓扑”“局部变化量更新”和“工程基线”等可由当前源码与测试支持的表述。
