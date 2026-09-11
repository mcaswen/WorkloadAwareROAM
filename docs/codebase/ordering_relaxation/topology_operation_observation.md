# 拓扑操作观察与严格步进：代码事实

> 日期：2026-09-11
> 范围：GATE-01 实验边界及共用严格循环，不是完整 DOD 源码说明
> 实现规划：[GATE-01](../../plans/ordering_relaxation/gate_01_topology_operation_observation_plan.md)
> 原阶段协议：[CPU 配对事实](../formal_experiment/cpu_pass_pairing_contracts.md)

## 1. 文件、职责和依赖

以下除显式标注外均为 FACT。

| 文件 | 当前职责与符号 |
| --- | --- |
| `src/algorithms/data_oriented_roam/DataOrientedRoamTopologyExperiment.h` | 实验边界的普通值类型与四个入口声明；只依赖 DOD 类型和标准容器，前向声明 State |
| `src/algorithms/data_oriented_roam/DataOrientedRoamTopology.cpp` | 原拓扑操作、串并行提交、严格收敛和旧回放继续存在；新增局部观察、预算包装和共用单步助手，无 CSV 或质量逻辑 |
| `tests/OrderingGateTests.cpp` | 小型语义夹具、四个冻结自然输入、旧版对照和手动记录输出；不是生产 benchmark 入口 |
| `tests/CMakeLists.txt` | `parallel_roam_OrderingGate_tests` / CTest `OrderingGate`，复用 DOD、formal input、`FormalCpuInput.cpp` 源文件和既有库 |

命名空间为 `ParallelRoam::Algorithms::DataOrientedRoam`。调用方向为测试/实验 → 窄接口 → 原拓扑操作及状态/队列/评分；生产完整收敛也使用同一个内部单步助手。没有新增 Settings 字段、State 成员、线程、渲染依赖或跨帧缓存；没有修改队列和评分实现。

所有可写 State 均由调用方独占，试算自行深复制；复制状态借用原高度图和线程池，但观察入口不派发线程、不修改借用对象。值结果自有 vector，不持有副本指针。迭代位置不拥有 State，调用方负责绑定同一阶段且不在步进途中换帧或重置。

## 2. 公开类型与状态

| 类型 | 字段与实际语义 |
| --- | --- |
| `TopologySplitAttempt` | `Path/Forced/Completed`；按递归进入次序记录一次基础尝试，失败和重复尝试保留 |
| `TopologyOperationWork` | 基础尝试/完成、forced 完成、创建/复用节点、邻接赋值、活动操作、队列 API/成员更新、mesh edit、路径插入计数 |
| `TopologySplitObservation` | `InputHash/RootPath/RootSucceeded`；去重 `ReadPaths/WrittenPaths/CreatedPaths`、按实际完成次序的 `CompletedPaths`、完整 `Attempts`；预算起止、峰值、拒绝次数及余量轨迹；局部覆盖和串行维护标记 |
| `TopologyConvergenceObservation` | 最多指定数量的 `Roots`，整段严格收敛的根尝试/合并尝试/合并失败/基础合并数，以及协调器队列调用数；未详细记录的根仍累计总次数 |
| `TopologySplitStop` | `NotStarted/Running/NoEligibleSplit/BudgetBlocked/IterationLimit`，当前枚举值顺序分别为 0～4 |
| `TopologySplitIteration` | `MaximumIterations/Iteration/Stop`；默认未开始，由 Begin 初始化，后续单步推进 |
| `TopologySplitStep` | split/merge 路径及分别的尝试、成功标志，另有 `BudgetRejected/SplitBlocked`；允许一次循环同时包含失败 split 与合并 |

这些类型没有继承关系；内部 `ObservedSerialCommitPolicy` 唯一继承原 `SerialTopologyCommitPolicy`，只包装预算和完成事件。生产串行及并行 policy 的 `ObservesOperations=false`，观察 policy 为 `true`；观察函数通过 `if constexpr` 消除关闭分支。

## 3. 单根试算与局部依赖

`AnalyzeSplitOperation(const State&, root)` 复制输入，调用内部 `ExecuteObservedSplitRoot`，返回描述。它不根据活动数量同步预算，不执行合并、不判断严格头资格；因此调用方须传入真实阶段/根开始状态，不能用它绕过协调器。

`ExecuteObservedSplitRoot` 先记录 `HashDataOrientedRoamPassInput(..., SplitTopology)`、根路径、普通预算和成员更新计数，再用观察 policy 执行原 `SplitNodeImpl`。完成后计算预算峰值及成员更新差，并只对读、写、创建路径去重。`Attempts` 的失败位与 `CompletedPaths` 的顺序保留。

### 3.1 足迹覆盖位置

- `SplitNodeImpl`：进入时读取根资源；记录叶/深度资格、底边递归条件、递归后刷新底边、历史子节点存在性；新子节点和父子挂接加入写/创建集合。
- `PrepareSplitNodeState`：父节点 split/build 标记和两个子节点的邻接/激活标记加入写集合；即使值没变，也记录赋值。
- `LinkSplitNeighbors`：父子、底边及对侧子节点条件读取；子节点连接和对侧连接加入写集合。
- `ReplaceNeighborReference`：先记录邻居读取，再按三个实际匹配分支分别记写。分支不匹配仍保留读资源。

资源以节点逻辑路径为保守单位，覆盖局部拓扑及激活/创建状态；不是每个字段一条资源。`CreatedPaths` 标明根内新资源，读取它们不代表从根开始快照取得信息。父子、底边等引用字段的读取归于拥有该字段的节点；无效哨兵无需一个虚构节点资源，非哨兵越界索引使 `LocalFootprintComplete=false`。

当前正常来源的局部覆盖判断结合上述静态访问核对和定向断言；不是通过“无异常”“无冲突”或最终状态 diff 自动推断完整。该标志不认证任意手工损坏状态，也不证明多个操作可以并行。

### 3.2 明确留在协调器的内容

堆槽、活动数组位置、`NodeMembership`、队列屏蔽/评分维护、路径集合、统计、mesh 日志和物理追加分配不当作相互独立的局部写。它们继续由原 serial policy 立即执行；`RequiresSerialMaintenance` 标注发生了索引/队列维护，预算另外用预留轨迹表达。此标记为 false 也不免除失败统计和预算协调义务。

INFERENCE：当前 `SplitNodeImpl` 的递归决策不读取堆优先级或活动数组位置；队列维护改变的资格/次序会影响后续严格循环。因此可以分开描述局部内核和协调效果。**尚未验证推迟维护是否与批次应用相容**；未来不能把本轮局部覆盖直接改名为 batch correctness。共享维护影响无法解释时仍应拒绝或标未知。

### 3.3 预算与成本口径

观察 policy 调用原预算实现后保存余量；峰值为初始余量减轨迹最小值，包含递归祖先尚未提交的预留。失败前已完成 forced split 不回滚，失败根可有写足迹和非零净预算消耗。

`PrimitiveAttempts−PrimitiveCompleted` 是基础失败数，不是根失败数。`NodesReused` 按成功进入复用分支的两个子节点计；邻接计实际赋值，活动计四个调用边界。正常成功 primitive 的 `QueueCalls=9`：前维护 3 次、索引转换中的 split 队列 3 次、后维护 3 次；不计这些函数内部的 sift、比较或逐字段访问。`QueueMembershipUpdates` 另外从原 Stats 的实际变动取差。根/试算/严格执行的计数不得累加冒充同一算法工作。

## 4. 严格控制流

`BeginStrictSplitIteration(State&)` 先按活动叶与 cap 同步普通预算，再通过 `InitializeSplitIteration` 更新 CandidatePeak、冻结 `max(1024, B*8+Nodes.size()*4)`，返回 Running 状态。生产完整入口已在原位置同步预算，内部初始化不重复同步。

`RunStrictSplitStep` 是生产和实验共用的一个原循环体：

1. 非 Running 返回空步骤；保留 `iteration++ < maximumIterations` 后增语义，上限判断失败标 IterationLimit。
2. 读取 merge 堆顶；若 score 小于 MergeThreshold，执行原合并，失败则删除该候选并记录拒绝，本步结束。
3. 读取 split 堆顶；无节点或 `ShouldSplitWithScore` 不成立即 NoEligibleSplit，不能跳到后续合格项。
4. 调用原 split；成功结束本步。观察模式在有界前缀内记录闭包，其余根走同一原 serial 操作。
5. 失败后读取预算拒绝计数变化并重新读取 merge 堆顶；仅预算不足且 split score 严格大于 merge score 时合并交换预算。合并失败照原规则删除候选。
6. 无法交换的预算失败标 BudgetBlocked；其他失败屏蔽当前 build 的 split 根，然后继续下一步。

`AdvanceStrictSplitIteration` 拒绝 NotStarted；停止后再调不改变 State 或迭代位置。`TopologySplitStep` 不把根失败解释为无副作用。

`RunSplitSerialConvergence` 在入口初始化一次，在循环外保留原完整计时包络；`MergeDuringSplitConvergence` 保留合并计时和两项合并统计，完整 split 包络仍扣除这些合并时间。公开逐步调用不累积一个虚构的完整 split 包络，实际离线墙钟由驱动另记。

当前 `TopPersistentMergeQueueNode/Score` 及 split 对应查询只是读取堆顶，不做懒清理；失败处理中的 Remove/Block 才修改队列。这里没有复制评分、排序比较器或强制闭包算法。

## 5. 入口、验证及证据

`OrderingGate` 默认入口只运行解析/控制流用例：直接与 forced split、新建与复用、只读分支、同值写、部分成功、预算释放、未知索引、同分/迟滞、预算同分停止、紧急合并、失败候选删除、非预算屏蔽、迭代边界和未初始化调用。

手动入口：

```text
parallel_roam_OrderingGate_tests --baseline-natural scenarios cameras new-output
parallel_roam_OrderingGate_tests --observe-natural scenarios cameras new-output baseline-results.csv
parallel_roam_OrderingGate_tests --verify-natural scenarios cameras new-output baseline-results.csv
```

自然来源使用既有清单、完整前序相机和 `BuildWithPassObserver` 重建，四项身份见小规划。严格结果用规范化证据、完整后继输入编码、实际预算/工作计数以及真实 full mesh 比对旧文件；这里只比较严格路径相同分配顺序，不把完整输入哈希作为未来不同批次分配顺序的逻辑等价定义。`StateSnapshot` 另检验来源不变。

观察模式记录每输入最早 8 次实际 Requested 尝试；verify 模式不详细记录根，但核验同一严格控制器、逐步和旧版结果。三种模式均需新目录，不自动运行自然矩阵。

当前可复核产物位于 `benchmark-output/ordering-relaxation/gate-01-20260911/`：

- `before/natural/results.csv`：修改拓扑实现前的四项结果及归档旧驱动。
- `observation/`：步骤 1 的检查点记录；32 根、19 根有修改、57 次基础尝试/38 次完成。
- `after/natural/`：最终共用单步后的复核；结果与旧 CSV 完全相同，32 根明细也与检查点逐字节相同。四项根数/迭代数为 37、40、144、9，均以 BudgetBlocked 停止。
- `after/natural-process.json`：最终复核 14.33 秒、峰值工作集约 337.2 MiB，包含来源重建、复制、三种严格执行和校验。每输入观察包络约 16.16～430.27 ms，包含最多八次完整输入编码及整段收敛，不是单 closure 计时；复制在该包络外，但计入进程总成本。
- `ctest.log`：三个选定 CTest 的最终结果；`before/after` 程序、源码及 `comparison/` 保留性能身份和原始帧数据。

PLANNED：排序窗口、不可变 batch、延迟维护、排列审计、独立几何 evaluator 和多核算法均未实现。当前自然前缀没有观察到部分成功后根失败，该分支仅有解析依赖图证据；四项覆盖也不代表全部地形、预算压力或闭包复杂度。
