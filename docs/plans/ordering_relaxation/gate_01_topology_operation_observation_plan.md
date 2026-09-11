# GATE-01：拓扑操作观察与窄实验接口小规划

> 日期：2026-09-11
> 类型：小规划，落实离线 Gate 的第一个停止检查点
> 状态：已完成；局部观察检查点通过，严格单步、定向核验及工程性能对照已闭环，用户已授权提交
> 上位规划：[排序松弛离线 Gate](ordering_relaxation_offline_gate_plan.md)
> 研究契约：[讨论记录](ordering_relaxation_discussion_record.md)

## 1. 本阶段要回答什么

先回答：**真实 split closure 的逻辑依赖，能否用少量局部观察可靠描述？** 若答案是否定或成本明显过高，保存证据并停止，不为了完成后续阶段继续建设接口。

支持继续时，再完成生产与实验共用的严格单步执行入口，让后续能够在同一初态上真实演化状态。当前只执行严格顺序，不实现排序窗口、batch selector、排列审计、质量评价、成本模型或新批量平台；不修改原研究问题、P5 结论、正式配对协议和渲染器。

阶段完成有两种形式：取得可复核的停止结论；或交付窄接口、覆盖证据、严格行为对照和性能报告。后者只说明可以继续离线实验，不说明已有可独立 batch，更不说明存在多核加速。

## 2. 已核对的事实与职责归属

规划前已阅读开发规范、规划规范、审查规范、上位规划及讨论记录，并核对 [P5 结论](../formal_experiment/prep_05_cpu_crossover_pilot_gate_plan.md)、[CPU 配对事实](../../codebase/formal_experiment/cpu_pass_pairing_contracts.md) 和相关源码。

- `DataOrientedRoamTopology.cpp` 已有真实的递归 `SplitNodeImpl`、serial/parallel commit policy 和 `RunSplitSerialConvergence`。应在这里复用操作，不复制一套实验算法。
- 一次严格循环可能先合并，也可能在 split 失败后执行预算交换；它不是“一次成功 split”。迭代上限在收敛入口计算一次，队首清理、同分、迟滞和停止均属于原行为。
- split 先预留预算，再展开 forced closure；根返回失败前可能已有强制子操作成功。返回 `false` 不意味着状态未变，不能把失败改成事务回滚。
- 队列、活动索引和增量网格日志在串行提交中维护。堆交换会触及无关条目，不能把所有堆槽冲突直接当作局部拓扑依赖，也不能把这些维护成本和控制影响抹掉。
- 状态已有深复制能力，输入重建和 `DataOrientedRoamExperimentTestSupport.h` 已能验证来源不变；无需新 snapshot 框架。评分依赖的相机、地形和既往 split 路径在阶段内冻结。

| 动作 | 文件 | 归属及边界 |
| --- | --- | --- |
| Extend | `src/algorithms/data_oriented_roam/DataOrientedRoamTopology.cpp` | 在原操作边界记录依赖和粗计数；通过检查点后提取内部单步助手。继续拥有真实操作，不加入 CSV、选样或质量计算 |
| Create | 同目录 `DataOrientedRoamTopologyExperiment.h` | 窄实验声明及简单迭代状态、操作描述、足迹和计数类型；独立存在是为了让测试/后续实验调用真实操作，不建立新操作模块 |
| Create | `tests/OrderingGateTests.cpp` | 定向断言、来源重建和手动自然观察入口；少量输出辅助函数放在此处，不提前建立正式 CLI |
| Extend | `tests/CMakeLists.txt` | 接入 `OrderingGate` 测试，复用 DOD/formal input 源列表及 `FormalCpuInput.cpp` |
| Reuse | `State`、`StateOps`、`Scoring`、`Queues`、现有测试支持及性能脚本 | 复用副本、原评分/队列、证据和采集能力；首选不改这些文件 |

不新增 `.cpp` 操作层、cursor 类、通用 observer 基类、逐字段 accessor、事务/恢复系统或正式实验 schema。依赖方向为 `测试/后续实验 → 窄接口 → 既有拓扑与状态能力`；生产算法不依赖测试或 benchmark。普通函数和编译期空观察策略足够，不引入策略注册或虚调用框架。

如果实际发现必须改动 `Queues.h/.cpp` 等相邻文件，先列出无法在原操作边界覆盖的具体依赖和最小修改，再 Review；不把上位规划“必要时”理解为可以展开全量埋点。

## 3. 操作观察契约

### 3.1 正确性足迹与成本计数分开

**Dependency footprint** 使用去重的逻辑资源集合 `R/W`。首版以节点逻辑身份及资源类别为单位，可以保守地把节点拓扑字段归为一个资源，不追踪每一次 C++ load。身份使用已有 `PathId`，绑定输入/阶段；新子节点按父路径和左右关系识别，不用内存地址或临时数组编号比较。

| 操作位置 | 必须覆盖的依赖或效果 |
| --- | --- |
| 资格检查、递归入口及 forced 链判断 | 节点存在性、叶状态、深度、底边关系、递归终止条件；未成功执行的分支读取也要保留 |
| `PrepareSplitNodeState`、子节点创建/复用 | 父子拓扑与激活标记的读写、新逻辑子节点及父子挂接；新节点内部读写归于本根，不伪装成其他根可读取的初态信息 |
| `LinkSplitNeighbors`、`ReplaceNeighborReference` | 父、子、底边及其子节点、两侧邻居的条件读取和邻接写入；即使赋值前后相同也记写，不能仅靠最终状态 diff 取证 |
| 预算预留/释放 | 每次尝试、同时持有的预留峰值、实际净消耗、失败后的剩余预算；资源约束单列，不将全局预算计数器当成可随意并行写 |
| 活动集合、队列、路径集合、统计和 mesh 日志 | 保留维护事件与粗计数，作为串行协调效果；影响本根资格、递归或停止的读取必须另行显式记录，不能归入维护后忽略 |

观察发生在真实分支和写入附近，覆盖先读后写及递归中的动态变化；对已由本根产生的状态记录其来源。共享只读地形、设置、相机和上一帧路径集合用冻结身份约束，不人为制造跨根写冲突。

这里的 `R/W` 是拟议可独立局部内核的足迹，**不是整个 serial root 连同全局堆维护都已经可并行的证明**。完整操作描述同时包含串行维护和控制依赖。必须核对维护是否只影响后续协调；无法证明时标 `coordinator_dependency` 或 `dependency_unknown`，不能宣布完整独立操作。推迟维护后的正确性与批内可交换性留给 GATE-03 验证。

**Work counters** 只在稳定边界计数：根尝试/失败、基础 split/merge 尝试及完成数、forced split、节点创建/复用、邻接赋值、活动集合操作、队列 API 查询/更新次数。队列一次调用不代表一次字段访问，不展开堆 sift 的精确访存。各类别分别报告，局部操作与串行维护不重复归属；试算、实际严格执行和未来审计分别计数。不使用 `W_touch` 名称。

### 3.2 两个窄能力

1. **单根观察**：`AnalyzeSplitOperation(const S_j, root)` 在自有副本上调用真实 split，返回逻辑闭包、足迹、预算轨迹、粗计数、部分成功及完成状态。借用的原地形/输入只读，不持有销毁后副本的引用。它不代替严格控制器批准该根，也不包含改变阶段顺序的合并。
2. **严格单步**：简单迭代状态保存入口上限、当前位置和停止状态；内部助手执行原 `RunSplitSerialConvergence` 的一个循环体。生产完整收敛和实验步进共用它。merge-first、预算交换、队首清理、失败屏蔽、同分、停止及计时包络原样保留；单步不重复初始化上限或额外调用可能清理队列的查询。

单根观察先做；严格单步只在第 5 节检查点允许继续后补齐。早期可在原严格循环的 Requested 调用边界加局部观察，用于看到真实预算交换后的请求，不必先完成 cursor 或 selector。

原生产入口实例化空观察策略，关闭时不收集集合、不复制状态、不额外遍历或分配；不在生产 Settings 中增加 Gate 开关。启用观察只属于显式实验调用。截断、异常和未知覆盖均返回非完整状态，不能用“没有记录冲突”推定安全。

## 4. 用旧执行前字段冻结输入

仅关联以下既有数据的 `scenarioId/passId/sampleIndex/replayInputHash`，24 个 split 目标均唯一匹配；没有读取新 Gate 质量、批宽或计时结果来选样。

| 来源 | 路径（相对 `benchmark-output/`） | SHA-256 |
| --- | --- | --- |
| P5 原目标 | `prep-05/pilot-20260911/run-1/selected-targets.csv` | `be85c7c81de1590a1d559fd3e523da6e7b9fe8488bb5fa590b55f69291731b9c` |
| 旧执行前特征 | `prep-03/cpu-discovery-20260909/discovery.csv` | `9509a3cd0094df0e3834a971443f4e49a8fbb64dab8f85115fb93d920d01216b` |
| 场景 | `prep-02/input-freeze-20260909/inputs/scenarios.csv` | `7cd5e4f3c0fd0961a7cf3566f32603a8b8ee46e9365a8c8d0f729c289e2a2721` |
| 相机 | 同上目录 `camera-samples.csv` | `b6e9015fc597204f9a9162e8ded618ab12f961d39afb6386bc58b802905b0899` |

固定规则：按旧队列数量升序排序，并列按场景 ASCII 序、数值 sampleIndex、数值输入哈希；每连续 8 项为低/中/高层。组内距离使用活动数量和剩余预算比例两个维度，在全部 24 项上做 min-max 归一化，常量维度距离为零。活动数量范围为 `[511,199997]`，剩余预算比例范围为 `[0,11835/20000]`；队列数量与活动数量完全相等，队列只用于分层，不再次计入距离。

各层第一项取距离该层逐维中位数最近者，偶数中位数取中间两值平均。未来补足覆盖时，每次选择到已选集合最小平方欧氏距离最大的未选项，直到每层 4 项；所有距离并列使用上述身份顺序。选样计算使用精确有理数比较，避免极近距离因浮点误差改变清单。

| 本阶段用途 | 场景 / sampleIndex | 输入哈希 | 旧队列/活动数 | 剩余预算 / 上限 |
| --- | --- | --- | --- | --- |
| 低层代表 | `test129-a-b4096 / 5` | `2878997366482998522` | 4095 | 1 / 4096 |
| 中层代表 | `peking547-a-b20000 / 9` | `10820725050463347417` | 19992 | 8 / 20000 |
| 高层代表 | `peking547-a-b80000 / 60` | `2459220430983787707` | 80000 | 0 / 80000 |
| 既有反例锚点 | `peking547-a-b20000 / 14` | `4677929227313376363` | 19990 | 10 / 20000 |

原三层的队列范围分别为 `511–4096`、`8165–20000`、`79987–199997`。三名首选均接近预算上限，不能声称这四项已覆盖低预算压力；高层首选也没有覆盖 20 万规模。预算耗尽不通过临时加预算绕过，沿原严格规则观察预算交换，未获得的闭包覆盖照实记录。

按同一规则计算的后续覆盖顺序如下，仅冻结选择，不在本阶段运行额外输入：

| 层 | 四项顺序，场景 / sampleIndex |
| --- | --- |
| 低 | `test129-a-b4096/5`、`test129-a-b512/33`、`test129-a-b512/5`、`test129-a-b4096/8` |
| 中 | `peking547-a-b20000/9`、`test129-a-b20000/10`、`test129-a-b20000/55`、`test129-a-b20000/29` |
| 高 | `peking547-a-b80000/60`、`peking547-a-b200000/47`、`peking547-a-b80000/24`、`peking547-a-b200000/3` |

原 P5 的选择分层和哈希不改写。闭包复杂度尚无旧冻结值，本轮只解释，不据此换样。锚点用于已知行为回归，不充当独立留出样本。

## 5. 按步骤实施，允许提前结束

### 步骤 1：留存基线，做最少观察

按第 7 节保存修改前程序、源码和性能报告。先逐项核对第 3 节的读写/维护归属，在原函数边界接观察，运行解析夹具及四个输入的严格演化。每输入只详细记录最早 8 次实际 Requested 根尝试，包含失败/重试，最多 32 根；每根绑定自己的开始状态，不能把这段连续轨迹当成同一 batch。

夹具覆盖直接 split、forced 链、新建与复用、预算失败前的部分成功。无需先输出新 CSV schema，直接保存根级文本/简单表和具体未覆盖位置即可。

**停止检查点**：若超过一半已观察根存在无法可靠覆盖的依赖，或补齐必须包装大量数组 accessor、扩展到原定文件之外的广泛热路径修改，停止补齐接口，记录原因供 Review。分母包含失败根，并单列有实际基础修改的根；若观察几乎只有预算拒绝，结论是覆盖不足，不是闭包不可观测。少量局部遗漏可以在本阶段修正；不追加自然样本或无限扩展观察数量。

### 步骤 2：通过检查点后，补齐单根接口与严格步进

复用已有状态副本，完成单根观察入口和内部单步助手，原完整收敛改为调用同一助手。保留原计时包络；实验步进的离线耗时另计，不混入生产阶段统计。输出明确的继续/停止、根结果、预算交换和部分成功信息，不用一个布尔值表示整个控制流程。

用同一来源分别执行严格完整入口和逐步入口；对照修改前保存的结果，避免两个入口共享新 bug 时互相比对仍通过。当前严格步进是未来 `α=0` 的基础，本阶段没有实现 α 参数或 relaxed 配置。

### 步骤 3：定向核验、性能比对与阶段审查

完成第 6、7 节中受实际改动影响的验证，记录覆盖表、未决依赖、成本和是否值得继续。若提前停止，也核验保留的生产改动及其性能；不保留不可达实验分支或未接入空文件。

自然观察总墙钟上限 20 分钟、单进程峰值 8 GiB；运行中达到限制立即结束该尝试，保留 `resource_limited`、已观察分母及未运行清单。逐根释放副本和足迹，不保存每个根的完整状态。性能复测单独计入第 7 节预算；上述上限不是必须用满的额度。

## 6. 最小充分正确性验证

| 验证 | 覆盖风险 |
| --- | --- |
| 新 `OrderingGate` 解析用例 | 读后未写分支、相同值写、forced 链、子节点创建/复用、部分失败；检查关键逻辑资源在集合中，不能只比数量；全状态 diff 辅助检查漏写，但不作为漏读证明 |
| 新 `OrderingGate` 步进对照 | 同分和迟滞停止、紧急合并、预算交换、失败屏蔽、入口迭代上限只计算一次；单根观察不改变来源及借用对象 |
| 现有 `FormalSplitTopologyEquivalence` | `sampleIndex=14` 及现有有界/非空前缀行为，不运行全目标扫描模式 |
| 现有 `DataOrientedRoamSplitPrefix` | 原默认并行前缀资格与保守回退不受共享操作改动影响 |
| 四个自然输入的定向对照 | 旧结果、完整严格入口、逐步入口的逻辑拓扑、三角形数、预算、候选/队列资格及排序、停止与工作计数；复用规范化证据及必要的 mesh emit，不只看拓扑合法 |

新增 CTest 只执行小型确定性用例。自然模式实际为 `parallel_roam_OrderingGate_tests.exe --observe-natural <scenarios> <camera-samples> <output> <baseline-results.csv>`，四项身份及 8 根上限固定于测试入口，输出目录必须新建。`--baseline-natural` 在改动拓扑前留存旧结果；`--verify-natural` 可只核验严格状态而不记录根足迹。首次观察用于投入检查点，共享循环提取后只复核同四项受影响来源，不扩大采样或重复当作统计样本。

构建使用 `relwithdebinfo-fetch`，目标限于 `parallel_roam`（输出 `ParallelROAM.exe`）、runtime probe、新 `OrderingGate` 和上述两个既有测试。CTest 选择：

```powershell
ctest --test-dir build/relwithdebinfo-fetch -C RelWithDebInfo -R "^(OrderingGate|FormalSplitTopologyEquivalence|DataOrientedRoamSplitPrefix)$" --output-on-failure
```

不重跑全量 P5、全部 CTest、双后端渲染矩阵或与本阶段无关的输入解析测试。若实际修改了额外边界，按具体风险追加。注释按开发规范逐段审读：自然语义和句内标点，摘要正文不机械凑行，不使用 `@brief` 或阶段编号。

## 7. 性能基线与修改后比对

采用[开发规范第 7.3 节](../../standards/development_guidelines.md#73-工程性能回归的实际影响门槛)。修改前保存当前程序及完整运行依赖、源码归档、构建缓存和环境；不能把历史已经关闭的微秒争议报告当作本阶段同期基线。修改后保留独立程序，旧/新交替运行；源码、程序、输入和驱动身份写入 `protocol.md`。

| 固定组 | 用途及诊断状态 |
| --- | --- |
| `standard × serial-incremental` | 严格收敛热路径，原 benchmark 的 pass evidence 开启 |
| `standard × default` | 默认策略及共享操作路径，诊断同上 |
| `budget-saturation × default` | 预算压力与回退，诊断同上 |
| `test129-a-b4096 × serial-incremental × diagnostics-off` | 复用现有 runtime probe，检查实验观察关闭及全部三项诊断关闭的生产 CPU 路径 |

前三组使用 `build/relwithdebinfo-fetch/bin/ParallelROAM.exe` 的修改前/后归档副本：

```text
<version-exe> --benchmark --algorithm dod --profile standard --pass-policy serial-incremental --csv <run>/frames.csv
<version-exe> --benchmark --algorithm dod --profile standard --pass-policy default --csv <run>/frames.csv
<version-exe> --benchmark --algorithm dod --profile budget-saturation --pass-policy default --csv <run>/frames.csv
```

第四组使用 `build/relwithdebinfo-fetch/tests/RelWithDebInfo/parallel_roam_runtime_performance_probe.exe` 的修改前/后副本，保留同一 `tests/TerrainLodRuntimePerformanceProbe.cpp` 驱动：

```text
<version-probe> D:/CPP-Projects/WorkloadAwareROAM benchmark-output/prep-02/input-freeze-20260909/inputs/scenarios.csv benchmark-output/prep-02/input-freeze-20260909/inputs/camera-samples.csv test129-a-b4096 serial-incremental diagnostics-off <run>/frames.csv
```

`diagnostics-off` 精确为 `EnablePassEvidence=false`、`EnableTopologyValidation=false`、`EnableTopologyPairEvidence=false`，操作观察为空策略。前三组实际为 `true/false/false`，不能把原 `--benchmark` 描述成三个开关全关。probe 的结果核验在计时外，其驱动不因本阶段改变。

复用 `scripts/compare_runtime_performance.py`，运行前在产物目录写两份局部 JSON 配置：前三组 app 和第四组 probe 分开，均固定 `comparison=AB`、`layout=pair`、`affinity=all` 及同一 OpenGL 构建。填入归档副本的绝对路径，作为以上命令的完整展开记录；不改采集脚本或历史配置。

```text
python scripts/compare_runtime_performance.py --config <attempt>/app-config.json --output <attempt>/comparison/app --cohort gate01-app
python scripts/compare_runtime_performance.py --config <attempt>/probe-config.json --output <attempt>/comparison/probe --cohort gate01-off
```

每组每版本一次预热、五次独立计量；独立进程为主要统计单位，性能任务串行，固定资产、线程设置与运行环境。比较整体和 split/merge convergence 的每进程中位数/P95、配对差、绝对/相对变化和工作量，开启观察的副本/取证成本另外报告。旧脚本的历史告警只作原始汇总，本阶段工程判断使用当前规范。

调查门槛为 `max(0.05 ms, 5% × 修改前统计值, 修改前五次统计值极差)`；首轮跨门槛才至多完整复测一次，两轮稳定跨门槛才进入问题处理。小于 0.05 ms 的局部差只记录，不开启微秒定位或 AA/BB 扩展矩阵。工程性能运行总墙钟上限 60 分钟；耗尽后标尚未验证，不自动续跑。

产物置于 `benchmark-output/ordering-relaxation/gate-01-<attempt>/` 的 `before/after/comparison/observation`，不覆盖旧结果。新观察能力只报告新增成本及第 5 节资源上限，不虚构旧版观察耗时。持续退化单独写分析，列明事实和推测，由用户决定新增修复小规划或修改后续规划；不自行修复、延期或重开旧问题。

## 8. 交付、核查与实现情况

实现结束按规范逐项比对本规划和上位契约，重点核查：是否复用真实操作、有没有漏掉部分失败/控制依赖、关闭观察是否额外遍历、有没有提前增加 Gate 平台组件。实现后的事实和审查分别进入 `docs/codebase/ordering_relaxation/topology_operation_observation.md` 与 `docs/reviews/ordering_relaxation/gate_01_topology_operation_observation_review.md`，按各目录指南撰写；提前停止时只记录实际留下的能力与证据。

本规划和上位规划的 GATE-01 实现情况同步写入已完成步骤、停止/继续理由、测试范围、性能结果和未决项。未经用户再次许可不提交实现内容，也不自动进入 GATE-02/03。

### 步骤 1 检查点记录

2026-09-11，用户确认开始 GATE-01。修改前程序、源码及四组各一次预热/五次计量已保存至 `benchmark-output/ordering-relaxation/gate-01-20260911/before/`；四个真实阶段的旧版完整结果另存 `before/natural/results.csv`。

首次观察保留在同次尝试的 `observation/`：32 根均获得当前局部内核模型的完整足迹，19 根产生修改，共 57 次基础尝试、38 次完成；创建 72 个节点、复用 4 个节点。自然前缀没有部分成功后根失败，相关行为由定向三根依赖图覆盖。完整结果、后继状态哈希、工作计数及网格与旧版一致。此轮总墙钟 12.31 秒、峰值工作集约 299.5 MiB。

决定继续步骤 2：依赖观察没有要求修改状态 accessor、评分或队列实现。该结论仅支持局部可观测性；共享维护仍独立标注，尚未审计可延迟维护或可并行批次。严格循环随后发生提取，因此最终版本需定向复核四个原输入；这属于受改动影响的版本验证，不增加选样或把重复记录当统计样本。

### 步骤 2、3 实现结果

已完成 `AnalyzeSplitOperation`、`ObserveStrictSplitConvergence`、`BeginStrictSplitIteration` 和 `AdvanceStrictSplitIteration`。生产完整收敛与实验共用 `RunStrictSplitStep`，原操作未复制；文件范围保持第 2 节清单，未修改状态、评分或队列实现。`TopPersistentMergeQueueNode/Score` 当前为只读堆顶查询，失败清理仍在原控制分支处理。

最终自然复核位于 `after/natural/`：旧版结果 CSV 与新版逐字节一致，最终 32 根观察明细也与检查点一致。四项严格根数/迭代数为 37、40、144、9，均以 BudgetBlocked 停止。解析用例覆盖部分失败、只读分支、同值写、新建/复用、同分/迟滞、预算同分停止、紧急与失败合并、非预算屏蔽、迭代上限和未初始化调用。所选三个 CTest 全部通过，记录为同尝试下 `ctest.log`；没有重跑无关测试或后端矩阵。

最终自然复核总墙钟 14.33 秒、峰值工作集约 337.2 MiB；每输入开启观察的完整收敛包络约 16.16～430.27 ms，包含最多八次完整输入编码。状态复制在该局部包络外，计入进程总成本；未把这些离线成本解释为单个 closure 的 CPU 成本或 Speedup。

性能对照位于 `comparison/app/` 和 `comparison/probe/`，修改前及同期环境身份、输入/结果语义一致。`comparison/engineering-report.md` 按原基线冻结门槛核对四组、六个相关包络的中位数/P95，共 48 项，均未触发回归调查；没有追加复测、微秒定位或修复规划。完整原始值、配对差、比例及复现汇总脚本一并保留。

已完成[代码事实](../../codebase/ordering_relaxation/topology_operation_observation.md)和[阶段审查](../../reviews/ordering_relaxation/gate_01_topology_operation_observation_review.md)，核对职责、注释、来源、部分失败、控制顺序和关闭观察的成本。GATE-01 完成，仅支持继续后续小规划；未实现 batch、质量评价或正式并行算法。阶段闭环时未提交代码，随后用户授权单独提交本阶段并继续 GATE-02 小规划。
