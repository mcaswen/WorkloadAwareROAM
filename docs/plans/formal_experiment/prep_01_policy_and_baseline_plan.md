# PREP-01：策略下限与事实基线小规划

> 规划类型：小规划（Minor Plan）\
> 状态：PREP-01 已完成实现、验证及架构核查\
> 确认日期：2026-09-09，用户指示“可以，开始实施吧”\
> 编写日期：2026-09-09\
> 上位规划：[正式实验准备大规划](formal_experiment_preparation_major_plan.md)；用户已审阅大规划方向并要求细化第一阶段\
> 实施前源码基线：`main`，`a168701`；代码实现提交：`996c3a9`\
> 本阶段交付：可配置的三个 CPU 并行下限、可独立测试的无窗口参数解析、配置传递与输出证据、相关代码事实和验证记录

## 1. 问题、目标与范围

修改前，合并评分、细分评分和 CPU 网格提交受文件内固定 256 门槛限制。即使请求并行，低于该数量时也只走串行路径，后续 CPU pilot 无法观察低工作量下的真实并行成本。本阶段把数量门槛变成可记录、可覆盖的公共策略字段，同时保持默认算法行为。

完成后应能通过现有无窗口入口，为三个阶段分别指定下限；参数经过预设选择、场景设置、DOD 转换和实验副本后仍然有效，输出可以辨认实际配置。该能力服务于 PREP-02～PREP-05，不要求先建正式框架。

**范围内**：

- 增加三个独立下限，默认均为 256；现有细分/合并拓扑默认下限保持 32/160。
- 在现有 DOD 并行辅助文件中统一评分/网格的数量门槛与线程数量解析，阶段文件保留动作判断。
- 提取无窗口参数解析，接入三个已有协议名称，覆盖普通无窗口基准和两个既有 CPU 配对配置。
- 保留预设与配置传递，更新公共输入哈希、普通设置 CSV 和既有 CPU 配对 CSV 的配置列与版本。
- 编写本阶段代码事实、参数及执行语义测试、实现后的架构审查记录。

**范围外**：

- PREP-03 的真实阶段边界提取、工作负载发现；PREP-04 的诊断隔离、拓扑冻结执行线程上限修正和可靠配对整合。
- 正式场景/相机/目标清单、计时标定、完整 `configHash`、SHA-256、恢复系统和统计分析。
- 新的运行时命令行覆盖项、GUI 控件、上传重构及图形实验接口。运行时已有的整份策略传递和公共 CSV 写入随公共字段扩展继续工作；图形正式参数在 PREP-06 细化。
- 改变评分公式、队列成员、拓扑资格、预算、槽位组织、分块规则或增加 `ParallelFull`。
- 运行 CPU crossover pilot 或把当前配对时间标为正式实验结果。

依据：[规划指南](../plan_guideline.md)、[开发规范](../../standards/development_guidelines.md)、[代码事实指南](../../codebase/codebase_guideline.md)、[审查指南](../../reviews/review_guideline.md)、[24 号文档](../../parallel-roam/24-formal-experiment-preparation-plan.md)第 2.2、4 节及实施 1；历史职责边界沿用[源码改造规划](../../parallel-roam/22-workload-aware-source-refactoring-plan.md)。正式计时相关的旧描述以大规划第 2.2 节的源码核对结论为准。

## 2. 实施前事实与复用依据

以下保留批准规划时基于 `a168701` 核对的实施前事实，用于解释改动原因。实施后的当前事实已整理到 [CPU 策略与回放事实](../../codebase/formal_experiment/cpu_policy_and_replay_baseline.md)，不能把下表旧常量或旧 CSV 版本当成当前实现。

| FACT | 源码定位与实施影响 |
| --- | --- |
| 公共策略已有两个拓扑下限，四个主要预设均从默认策略构造 | [TerrainLodPassTrace.h](../../../src/algorithms/TerrainLodPassTrace.h) 的 `TerrainLodPassPolicy` 和 `MakeTerrainLod*Policy`；新增下限的唯一语义归属是公共策略 |
| 评分按实际队列条目数判断固定 256，网格按归一化后的待写脏槽位数判断固定 256 | [Queues.cpp](../../../src/algorithms/data_oriented_roam/DataOrientedRoamQueues.cpp) 的 `ResolvePriorityRefreshWorkerCount`；[MeshEmit.cpp](../../../src/algorithms/data_oriented_roam/DataOrientedRoamMeshEmit.cpp) 的 `ResolveEmitWorkerCount`/`EmitDirtyMeshSlots`；不能把网格门槛改成活动三角形总数 |
| 两处线程数量计算基本相同：无工作 0、串行/请求 1/低于门槛为 1、自动模式参考硬件且最多 8、显式请求只钳制到工作量 | 自动上限 8 不是显式请求的统一上限；不能借此次提取改变原线程请求语义 |
| DOD 设置已经直接保存公共 `PassPolicy`；两条设置转换和状态复制均整份复制策略 | [Types.h](../../../src/algorithms/data_oriented_roam/DataOrientedRoamTypes.h)、[DOD 适配器](../../../src/algorithms/data_oriented_roam/DataOrientedRoamTerrainLodAlgorithm.cpp) 的 `ToDataOrientedSettings`、[Benchmark.cpp](../../../src/benchmark/TerrainLodBenchmark.cpp) 的 `MakeDataOrientedSettings`、[State.cpp](../../../src/algorithms/data_oriented_roam/DataOrientedRoamState.cpp) 的复制构造；无需再增加一套 DOD 下限字段 |
| 无窗口 `ApplyPassPolicy` 会重建策略，并且目前仅保存/还原拓扑下限及限定字段 | `Benchmark.cpp` 的 `ApplyPassPolicy`、`ApplyTopologyExperimentSettings`；新增下限必须进入保存/覆盖逻辑，尤其覆盖 `PassPolicyReplay` 内的多次预设切换 |
| `RunPassCrossoverReplay` 独立构造串行来源策略，没有经过普通入口的设置覆盖流程 | `Benchmark.cpp` 的 `RunPassCrossoverReplay`；新参数必须明确接入这条路径。现有拓扑冻结执行还会强制下限 0 和最大线程请求，不能把场景设置误报成其实际配置 |
| 无窗口解析、帮助、场景与执行目前集中在 `Benchmark.cpp`；应用解析已经分离 | `Benchmark.cpp` 的 `RunTerrainLodBenchmarkFromCommandLine`/`BenchmarkUsage`；[ApplicationCommandLine.h](../../../src/app/ApplicationCommandLine.h) 的解析结果结构可借鉴，但 Benchmark 不依赖 App |
| 普通设置/统计 CSV 公共版本为 3；CPU 配对 CSV 由另一组函数写出，版本为 1 | [TerrainLodExperimentCsv.h](../../../src/experiment/TerrainLodExperimentCsv.h)、[实现](../../../src/experiment/TerrainLodExperimentCsv.cpp)；`Benchmark.cpp` 的 `WritePassCrossoverCsvHeader/Row`；更新公共版本不会自动覆盖配对 CSV |
| 公共输入哈希显式列举策略字段，策略结构新增字段不会自动进入哈希 | [ITerrainLodAlgorithm.h](../../../src/algorithms/ITerrainLodAlgorithm.h) 的 `HashTerrainLodBuildInput`；必须补写三个字段 |
| 没有线程池时，`RunDataOrientedRoamWorkers` 会在调用线程顺序执行多个任务编号 | [Parallel.h](../../../src/algorithms/data_oriented_roam/DataOrientedRoamParallel.h)、[ThreadPool.cpp](../../../src/algorithms/data_oriented_roam/DataOrientedRoamThreadPool.cpp)；统计中的数量不能单独证明执行了多个系统线程 |

两条拓扑回放调用链、诊断开关和各后端上传边界已有大规划中的核对依据；本阶段将其记录为基线，不提前修改这些执行路径。规划编写时未重新运行构建或 CTest；实施时已另存修改前基线并完成修改后双后端验证，结果见第 8 节。

## 3. 设计与关键契约

### 3.1 参数、默认值与覆盖语义

| 公共策略字段 | 类型/默认值 | 本阶段无窗口参数 | 使用的工作量 |
| --- | --- | --- | --- |
| `MergeScoreMinParallelEntryCount` | `std::size_t` / 256 | `--merge-score-min-parallel-entries` | `MergeQueue.size()` |
| `SplitScoreMinParallelEntryCount` | `std::size_t` / 256 | `--split-score-min-parallel-entries` | `SplitQueue.size()` |
| `MeshEmitMinParallelTriangleCount` | `std::size_t` / 256 | `--mesh-min-parallel-triangles` | 实际待写 `DirtySlots.size()` |

- 0 只解除数量门槛。零工作仍不派发；单条工作仍至多一个任务；串行动作和显式线程数量 1 仍串行。
- 边界使用严格小于：N < 下限回退，N == 下限允许尝试并行，不等于必然并行。
- 四个主要预设及旧别名继续保留默认下限；“最大安全并行”预设不会暗中把下限改成 0。
- `BenchmarkOptions` 保存三个解析值，默认从公共策略默认值取得；实际算法配置以最终 `TerrainLodSettings.PassPolicy` 为准。
- 普通入口先选预设，再应用下限覆盖；运行中重新选择预设时保留五个下限及原有拓扑限定字段。三个新参数与 `--pass-policy` 的先后顺序不影响最终覆盖值。
- CPU 配对入口保持串行来源策略和原有线程设置，再显式应用三个新下限。现有评分/网格实验副本只改动作和线程数量，继续继承下限；本阶段不全局强制置零。
- 本阶段不改变两个既有拓扑参数在冻结配对中的行为；该限制进入事实文档，正式五阶段的请求/实际配置统一由 PREP-04 完成。

### 3.2 线程解析归属与执行语义

**Extend** 现有 `DataOrientedRoamParallel.h`，加入无持久状态的 `ResolveDataOrientedRoamWorkerCount(workItemCount, requestedWorkerCount, minimumParallelWorkItemCount)`，统一两个既有计数算法。此函数只解析数量，不派发任务、不读取队列，不接管拓扑的安全分块决策。

`Queues.cpp` 和 `MeshEmit.cpp` 保留各自的动作适配：串行评分、`SerialDirty`、`SerialFull` 将请求归一为 1；自动/并行动作传入原请求和对应阶段下限。零工作优先处理。`SerialFull` 继续使用现有全量串行写入入口，不改成脏写或并行全量。

共享解析规则：

1. 工作量为 0，返回 0。
2. 请求为 1 或工作量低于下限，返回 1。
3. 请求为 0，采用 `hardware_concurrency`，未知时按 1，自动上限保持 8。
4. 返回值在 1 到工作量之间；显式请求不新增硬件或 8 线程上限。

**Reuse** `RunDataOrientedRoamWorkers` 和持久线程池。三个阶段调用、任务区间、评分后建堆、槽位写入及等待返回位置保持原职责。此抽取仅共用原来一致的数量规则，不新增调度框架、缓存或测试专用生产开关。

### 3.3 无窗口参数解析边界

新增 `TerrainLodBenchmarkCommandLine.h/.cpp`，继续使用 `ParallelRoam::Benchmark` 命名空间：

- `BenchmarkOptions` 及已有配置枚举仍在 `TerrainLodBenchmark.h`；运行选项不迁入 App 或未来 experiment/formal。
- 新解析结果 `TerrainLodBenchmarkCommandLineParseResult` 包含 `Options`、`ShowHelp`、`Error`，并提供 `Succeeded()`；字段与现有应用解析风格一致，不跨模块继承。
- 新入口 `ParseTerrainLodBenchmarkCommandLine(int argc, char** argv)` 返回解析结果，不输出控制台、不读资产、不创建目录、不运行算法。
- `ParseSize`、`ParseAlgorithm`、`ParseProfile`、`ParsePassPolicy`、`ParseParallelTopologyPhase` 及参数消费逻辑迁入解析实现。场景生成、`ApplyPassPolicy`、设置覆盖、执行、报告和 CSV 仍归运行器。
- `RunTerrainLodBenchmarkFromCommandLine` 留在原实现作薄适配：解析失败输出错误并返回 1；帮助输出 `BenchmarkUsage` 并返回 0；成功调用既有 `RunTerrainLodBenchmark`。`BenchmarkUsage` 原声明保持兼容，实现迁入解析文件，不复制两套帮助文本。

保留旧参数、配置名和别名、重复选项最后一次生效、按当前位置处理帮助的行为。新下限仅接受 `[0-9]+` 形式的完整非负十进制整数；缺值、负数、显式正号、前后空白、尾随字符和超出 `std::size_t` 的值失败，0 和可表示的最大值合法。旧 `--pass-*` 的范围和配置适用性沿用原约束，不将新下限规则套到重复次数或线程数量上。

尚未接入的 `pass-crossover-formal`、`workload-discovery` 及相关正式参数仍明确拒绝；不能先注册名称并回落到 `smoke` 或既有配对入口。`--benchmark` 仍由 main/App 分流，本次不改变启动模型。

### 3.4 设置、哈希与 CSV

**设置转换**：复用整个 `PassPolicy` 的赋值链。`DataOrientedRoamTypes.h`、DOD/Classic 适配器、State 复制构造和运行时的整份策略传递原则上无需新增逐字段代码；通过行为测试验证新值未丢失。Benchmark 内的 `ApplyTopologyExperimentSettings` 更名为表达实际范围的 `ApplyPassExperimentSettings`，统一覆盖新旧下限；三个 CPU 字段的覆盖可提为同文件私有辅助供两条入口复用，CPU 配对入口仅应用这三项，不改变既有拓扑冻结规则。

**输入身份**：`HashTerrainLodBuildInput` 按固定顺序追加三个下限。默认策略与显式默认值在新版本内产生相同输入哈希；改变任一字段产生不同配置输入身份。由于哈希编码增加字段，不要求与旧版本数值相同；前后行为回归比较规范化结果和计数。

阶段 `FrozenStateHash` 用于标识共同冻结输入，保留当前语义；不把动作或新下限随意塞进去以破坏策略配对。本阶段没有正式 `configHash`，配置可追溯性由完整命令、构建基线及实际下限列提供。

| 输出 | 本阶段变更 | 边界 |
| --- | --- | --- |
| 普通无窗口与运行时共享设置 CSV | `TerrainLodExperimentCsvSchemaVersion` 从 3 升至 4；增加 `mergeScoreMinParallelEntryCount`、`splitScoreMinParallelEntryCount`、`meshEmitMinParallelTriangleCount` | 表头/值顺序一致，来自最终公共策略；运行时复用同一写入器，无需重写上传 CSV |
| 既有 CPU 配对 CSV | 独立 `schemaVersion` 从 1 升至 2，追加上述三个配置列 | 从实际传入并由评分/网格副本继承的设置写出；保持入口验收用途，不伪称完整正式配置身份 |
| 既有逐阶段统计 | 复用请求/实际动作、线程数量和回退列 | 不重新定义回退枚举；现有 `BelowParallelThreshold` 也用于并行意图下只剩一个线程的情形，不能单靠该标签推断数量门槛触发 |

不迁移或覆盖历史 CSV。普通设置版本与配对版本分别测试；不提前创建 `FormalExperimentCsv`，也不为三个新增配置列另建一套统计类型。已有 `WritePassCrossoverCsvHeader/Row` 仅扩展本格式的配置字段，正式记录层在后续阶段落地。

### 3.5 数据流与依赖

~~~text
原始参数
  → Benchmark 参数解析 → BenchmarkOptions
  → 原运行器：场景/预设 + 明确下限覆盖
  → TerrainLodSettings.PassPolicy
      ├─ 公共输入哈希、普通设置 CSV
      └─ 整份映射至 DOD Settings
           ├─ 生产评分/网格 → 动作适配 → 共用数量解析 → 既有线程池
           └─ 既有 CPU 配对副本 → 同一评分/网格执行 → 配对 CSV v2
~~~

解析器只依赖 Benchmark 选项和公共值类型；算法不依赖参数解析或实验输出。共用数量解析留在 DOD 内，拓扑、App 和图形后端不为本次修改反向依赖它。

## 4. 文件归属与操作清单

以下保留已确认的创建/修改清单。实际修改的 17 个代码、构建与测试文件均落在该范围，复用文件未作无必要修改；文档与验证记录见第 8 节。

~~~text
src/benchmark/
  TerrainLodBenchmark.h                         扩展三个选项，保留运行入口
  TerrainLodBenchmark.cpp                       运行与配置接入，移出解析
  TerrainLodBenchmarkCommandLine.h              新增解析契约
  TerrainLodBenchmarkCommandLine.cpp            新增解析与帮助实现
src/algorithms/
  TerrainLodPassTrace.h                         公共下限与默认预设
  ITerrainLodAlgorithm.h                        配置输入哈希
  data_oriented_roam/
    DataOrientedRoamParallel.h                  共用数量解析
    DataOrientedRoamQueues.cpp                  评分动作适配及独立下限
    DataOrientedRoamMeshEmit.cpp                网格动作适配及下限
src/experiment/
  TerrainLodExperimentCsv.h/.cpp                共享设置 CSV v4
tests/
  TerrainLodBenchmarkCommandLineTests.cpp       新增纯解析测试
  DataOrientedRoamPassPolicyTests.cpp           新增策略/执行语义测试
  TerrainLodBenchmarkPolicyTests.cmake          新增进程级配置/CSV 接入测试
  TerrainLodExperimentCsvTests.cpp              扩展共享字段断言
  CMakeLists.txt                               注册新测试
CMakeLists.txt                                 注册解析文件及复用测试依赖
docs/codebase/formal_experiment/
  cpu_policy_and_replay_baseline.md             实施时建立并维护代码事实
docs/reviews/formal_experiment/
  prep_01_policy_and_baseline_architecture_review.md
~~~

| 操作 / 文件 | 职责及独立存在理由 |
| --- | --- |
| Extend：TerrainLodPassTrace.h | 三个下限的唯一公共策略定义；默认预设继承相同默认值，不复制新的策略体系 |
| Extend：ITerrainLodAlgorithm.h | 已有输入身份编码覆盖新字段；不扩展公共算法虚接口 |
| Extend：`DataOrientedRoamParallel.h` | 在既有 DOD 并行辅助边界共用数量解析，避免评分和网格对 0、边界和自动线程产生不同解释 |
| Modify：`Queues.cpp`、`MeshEmit.cpp` | 各自解释动作与工作量，传入对应下限；移除两处固定 256，仅迁移共同计数规则 |
| Extend：`TerrainLodBenchmark.h` | 保存解析后的运行选项，供解析与程序化运行共用；保持现有公共入口 |
| Split / Create：`TerrainLodBenchmarkCommandLine.h/.cpp` | .h 定义纯解析结果与入口，.cpp 负责字符串到配置及帮助；独立于资产和算法执行，错误用例无需链接整个应用 |
| Modify：`TerrainLodBenchmark.cpp` | 保留场景与运行职责，统一预设/覆盖行为，接入配对分支并更新其已有 CSV 格式；移出命令行词法和帮助实现 |
| Extend：`TerrainLodExperimentCsv.h/.cpp` | 修改现有共享设置输出版本及字段，供两个入口复用 |
| Create：`TerrainLodBenchmarkCommandLineTests.cpp` | 验证合法/非法输入、帮助、别名和顺序语义；只链接解析实现及公共依赖 |
| Create：`DataOrientedRoamPassPolicyTests.cpp` | 验证策略默认值、输入哈希、数量边界及真实 DOD 阶段的参数传递/结果等价；夹具留在测试文件，不扩大生产 API |
| Create：`TerrainLodBenchmarkPolicyTests.cmake` | 用既有可执行文件验证普通/配对两条 CLI 到 CSV 的完整链；负责进程退出与字段断言，不实现算法或正式统计，不新增 Python 测试依赖 |
| Extend：`TerrainLodExperimentCsvTests.cpp` | 扩展已有列名、值、唯一性和列数一致性测试 |
| Modify：根与 tests/`CMakeLists.txt` | 两后端均注册解析实现；DOD 执行测试只链接 CPU 依赖。根文件可将两后端已有的 DOD 源列表提为唯一列表供应用和该测试共用，限定在这组已有源码，不重构整体构建层次 |
| Create：`cpu_policy_and_replay_baseline.md` | 按事实指南记录策略/解析/设置/线程池/回放及外部上传边界，区分 FACT、PLANNED、UNCERTAIN；供后续阶段直接引用，实施后同步实际符号 |
| Create：`prep_01_policy_and_baseline_architecture_review.md` | 实现后按 Critical/Major/Minor 记录边界、依赖和验证证据；规划完成时不生成虚假的实施审查 |

**Reuse / 核验文件**：`DataOrientedRoamTypes.h`、DOD/Classic 适配器、`DataOrientedRoamState.cpp`、`DataOrientedRoamPassExperiment.cpp`、`Pipeline.cpp`、`ThreadPool.h/.cpp`、`ApplicationCommandLine` 和两后端渲染器。它们的已有效映射/执行被本阶段复用，不能因为出现在扫描清单就机械修改。若核验发现传递缺口，在本小规划补具体修改归属后实施。

## 5. 实施步骤与逐步验收

| 步骤 | 工作 | 本步验收 |
| --- | --- | --- |
| 1. 基线与事实 | 固定源码、构建配置和代表性命令；建立事实文档，记录预设重置、两条拓扑回放、诊断、线程池借用及后端上传限制；保存修改前普通/配对输出 | 基线结果可定位；原始输出不覆盖；区分本阶段待修和 PREP-03/04/06/07 的后续事项 |
| 2. 公共策略与 DOD 执行 | 增加字段，共用数量解析，接入两评分及网格下限，补输入哈希与策略/执行测试 | 默认行为、各字段独立性、N/线程边界、`SerialFull` 与真实并行派发验证通过 |
| 3. 解析、覆盖与记录 | 提取解析及帮助；普通与配对入口接入新字段，保留预设切换；更新两套 CSV 与对应测试 | 参数不会在下游丢失；旧 CLI、错误退出、独立 schema 和实际配置列通过 |
| 4. 集成与审查 | 双后端构建，现有 CTest 和新增测试，修改前后固定输入对照；同步事实、小规划及大规划实现记录 | 第 6 节条件通过；审查无未处理的关键职责/依赖问题；PREP-01 才标为完成 |

依赖严格为 1 → 2 → 3 → 4。性能任务不并发执行；本阶段的运行只验证工程行为，不作速度胜负判断。

## 6. 验证计划与完成条件

### 6.1 数量解析与动作边界

N 为当前评分条目数或脏写工作量，T 为该阶段下限；下表的并行动作使用显式请求，避免依赖测试主机的自动线程提示。

| 输入 | 期望 |
| --- | --- |
| N = 0，任意 T、动作/请求 | 对应无工作路径为 0，无任务派发 |
| N = 1，T = 0，请求 2/8 | 最多 1，不因解除门槛伪报并行 |
| N = 2/255，T = 256，请求 2/8 | 1 |
| N = 2/255，T = 0，请求 2/8 | min(请求, N)，允许实际并行派发 |
| N = 256/257，T = 256，请求 2/8 | 2/8，验证严格小于边界 |
| N = 255/256/257，T = 257 | 前两者 1；257 允许并行 |
| 非零 N，串行动作或请求 1 | 始终 1，即使 T = 0 |
| 请求 0 | 保留自动硬件提示、未知按 1、上限 8 及工作量钳制；不假定测试机一定有 8 个线程 |
| 可表示的最大下限，小型工作量 | 串行回退，不因下限参与分配或加法产生溢出 |
| 显式请求大于 8，足够工作量，仅测试数量解析 | 不新增 8 线程钳制；不为数值边界测试创建极大量系统线程 |

`SerialFull` 另验：非空活动槽位时始终完整串行写入，哪怕脏集合为空；因此它的工作量不能用脏集合的 N 代替。活动三角形很多、脏集合很小时，`ParallelDirty` 只按脏集合判断门槛。

### 6.2 配置传递与结果语义

- 默认策略与显式 256/256/256 逐帧规范化结果、活动数量和阶段动作一致；与修改前基线比较不要求旧输入哈希或耗时完全相同。
- 逐个更改三个字段，确认只有对应阶段数量门槛改变；其他两个阶段的设置保持原值。两拓扑默认门槛保持不变。
- 覆盖四个主要预设及旧别名；`PassPolicyReplay` 多次切换预设后仍保留显式下限；两个 CPU 配对配置都能把新值传到评分/网格副本。
- 使用有效 DOD 状态副本和仍存活的源线程池，对每个新增参数对应阶段至少覆盖一个 2 ≤ N < 256 的状态。以工作量选夹具，不按耗时筛选；缺少覆盖不能记为通过。
- 评分比较按节点对应的分数和队列成员/不变量；网格比较有效顶点、索引及规范化结果。输入副本和来源均校验，不能只检查返回 true 或数量相等。
- 派发验证同时检查阶段记录、非空任务与真实线程池；用受控任务记录不同系统线程的执行证据，并在一次非计时诊断中确认评分/网格任务进入多个系统线程。不能以无池顺序回放、线程池容量或统计值单独替代证据；不为此给生产接口增加永久测试钩子。
- 新字段经过公共到 DOD 转换及 State 复制保持原值；相同输入哈希可重现，任一新字段变化能改变公共配置输入身份。`FrozenStateHash` 的策略配对关系仍成立。
- 请求 1/2/8 的新增专项断言针对评分和网格；现有拓扑冻结线程覆盖缺陷留到 PREP-04，不把本阶段完成误写成五阶段可靠配对已经完成。

### 6.3 CLI 与输出

纯解析测试覆盖无参数默认、三个 0、三个不同数值、下限与预设的两种顺序、重复选项、旧别名、缺值、负数、空串、字母、浮点、尾随字符、溢出及带空格路径；路径只作为值保存。`--help`/`-h` 不启动算法；未知正式配置返回错误。

进程级 CMake 测试使用独立测试输出目录，调用现有可执行文件，覆盖：

1. 普通基准默认与显式默认值，逐行对照规范化结果；检查共享设置 schema 为 4。
2. 三个不同下限、预设切换，检查每行实际配置列，防止三列都误取同一个字段。
3. `pass-crossover-replay` 和 `pass-crossover-stress-replay` 各以 0 预热、1 次记录、1 个目标验证新字段，检查独立 schema 为 2、11 条策略记录和既有正确性字段。保留失败诊断，不输出正式数据标记。
4. 非法参数和未支持正式配置以非零退出，且不创建请求的结果 CSV。

共享 CSV 单元测试检查 3 个新增列名各出现一次、值与最终设置一致、表头和值列数相等，确认公共 schema 与配对 schema 没有混用。测试脚本只解析固定夹具涉及的受控 CSV 字段，不实现通用实验恢复。

### 6.4 构建与回归

以下是本阶段的完整应用验收命令，实际结果见第 8 节。D3D12 原缓存关闭测试，执行时显式启用，防止把缺少测试目标的构建算作通过：

~~~powershell
cmake --preset relwithdebinfo-fetch
cmake --build --preset relwithdebinfo-fetch
ctest --test-dir build/relwithdebinfo-fetch -C RelWithDebInfo --output-on-failure

cmake --preset relwithdebinfo-d3d12-fetch -DPARALLEL_ROAM_BUILD_TESTS=ON
cmake --build --preset relwithdebinfo-d3d12-fetch
ctest --test-dir build/relwithdebinfo-d3d12-fetch -C RelWithDebInfo --output-on-failure
~~~

新增测试实际注册名为 `terrain_lod_benchmark_command_line`、`dod_pass_policy`、`terrain_lod_benchmark_policy`；复用既有 `terrain_lod_experiment_csv`、`application_command_line` 及全部既有 CPU 回归。DOD 执行测试链接现有 DOD 源、公共视图、HeightMap、PerformanceTimer、GLM、STB 和 Threads，保持无窗口。完整应用依赖缺失导致目标退化或关键测试未注册时，不算验收通过。

本阶段不要求上传专项和完整图形性能验收；两后端构建及无窗口回归用于验证公共策略/CSV 的兼容性。CPP 注释覆盖检查沿用现有 CTest。

**完成条件**：上述验证通过，事实文档与实现审查完成，实际结果写入本小规划及大规划 PREP-01。结果数据仍是工程验收用途；PREP-02 可随后建立最小 CPU 输入。

## 7. 风险、取舍与 Review 重点

| 问题 | 采用的处理 |
| --- | --- |
| 阶段局部常量换成字段，却遗漏预设或配对分支 | 三段数据流分别验证：纯解析、最终设置、实际阶段/CSV |
| 共享数量解析意外改变动作或自动线程上限 | 阶段文件保留动作适配，共享辅助仅迁移既有计数规则；不用于拓扑安全决策 |
| 单纯增加统计值被误认为真实并行 | 使用有效线程池与执行证据，另列无池顺序路径的已知限制 |
| 新输入哈希使旧基线看似全部变化 | 新旧版本不比较哈希数值，比较规范化结果；新版本内验证配置敏感性 |
| 普通 CSV 升版后配对 CSV 仍无法追踪配置 | 两套 schema 分别更新与测试；不提前引入正式记录系统 |
| 为测试暴露内部可变状态或改造整个构建 | 测试拥有隔离夹具，复用已有 State/线程池边界；构建仅复用本次所需 DOD 源列表 |
| 将阶段范围扩成完整实验实现 | 拓扑计时/线程修正、发现、上传和正式文件系统按大规划后续阶段交付 |

建议 Review 重点为：公共下限的默认与零值语义；DOD 并行辅助中的共同数量解析；解析器与运行器的边界；两套 CSV 升版及输入哈希兼容口径。上述方案已于 2026-09-09 经用户确认，并完成实施核查。

当前没有需要重新讨论的大规划模块划分。若实现发现必须改公共算法接口、线程池所有权、阶段执行顺序或拓扑语义，先记录影响并更新规划，不能作为此次门槛参数化的附带修改。

## 8. 实现情况

代码实现提交：`996c3a9`（feat: 完成 CPU 并行下限参数化与无窗口配置接入）。本阶段已完成；PREP-02 及后续阶段尚未开始。

| 项目 | 实际完成情况 |
| --- | --- |
| 需求与确认 | 修改前阅读开发规范、上位规划及本小规划；用户于 2026-09-09 授权实施，完成后按规范和规划逐项核查 |
| 基线与事实 | 修改前 OpenGL 完整构建和 21/21 CTest；保存四组 CSV/日志，已同步 [代码事实](../../codebase/formal_experiment/cpu_policy_and_replay_baseline.md) |
| 公共策略与数量解析 | 三个下限默认 256，旧拓扑 32/160；共用数量规则，动作和脏工作量仍由阶段解释；0、边界和请求 1/2/8 通过 |
| 解析与配置 | 新解析文件不执行 I/O；原入口薄包装；预设切换保留五个下限，两条入口显式应用新增三项；DOD 整份策略转换与复制复用 |
| 输入和 CSV | 公共输入哈希追加三项；共享设置版本 4、配对版本 2；各列来自实际配置，冻结输入编号语义未改 |
| 文件归属 | 17 个实际代码/构建/测试文件全部匹配第 4 节清单；没有修改线程池所有权、公共算法虚接口、App/GUI 或上传实现 |
| 架构审查 | [审查记录](../../reviews/formal_experiment/prep_01_policy_and_baseline_architecture_review.md)已完成；Critical / Major 无问题，注释格式 Minor 已整改，无未处理项 |

### 8.1 实际验证

- OpenGL 完整构建、24/24 CTest 通过；D3D12 完整构建、24/24 CTest 通过。日志为 `benchmark-output/prep-01/ctest-opengl.log`、`ctest-d3d12.log`；D3D12 配置命令明确使用第 6.4 节的测试开关。
- 公共结果/资源契约补充断言后，OpenGL 的 `dod_pass_policy` 再次通过，D3D12 全量已覆盖；记录为 `benchmark-output/prep-01/final-policy-test.log`。
- 有效夹具：合并评分 23 条、细分评分 128 条；128 和 512 个活动槽位各仅写 2 个脏槽位。逐节点分数、队列不变量、全部顶点属性、索引、规范化网格和来源未变均通过。
- 受控派发记录两个系统线程；一次非计时诊断中，合并评分/细分评分/网格任务最多分别观察到 2/5/2 个系统线程。诊断代码已移除，原线程池文件没有最终差异，常规构建已恢复；不要求每个短批次占满全部请求线程。
- 两个既有配对配置各额外运行零下限与高下限对照，三个阶段实际线程结果发生预期变化，冻结输入与结果一致。每个目标仍为 11 条策略记录。
- 默认前后对照：`smoke` 6 行、`pass-policy-replay` 24 行，各 233 个非计时字段不变；两种配对各 11 行、27 个非计时字段不变。明确排除耗时、CPU 利用率、schema 和旧输入哈希；`baseline/` 原始输出未覆盖，修改后写入 `after/`，汇总为 `benchmark-output/prep-01/baseline-comparison.json`。
- 本次两处公共注释已按用户要求改为 `/// <summary>` 格式；`src/tests` 扫描无 `@brief`，注释覆盖门禁再跑通过，记录为 `benchmark-output/prep-01/final-comment-check.log`。

### 8.2 实施细化与剩余限制

没有改变已确认的目录、职责、依赖、线程池生命周期或算法执行顺序。私有三个下限覆盖辅助最终命名为 `ApplyCpuParallelMinimums`；解析错误文本统一格式，旧合法 CLI、别名、帮助顺序与失败退出语义保留。测试增加了配对高/零下限对照，仍属于本阶段配置有效性验收。

原始输出和一次性诊断/比对工件保留在本地 `benchmark-output/prep-01/`，通过本地 `.git/info/exclude` 排除，未改仓库忽略规则。用户另行修改的 `AGENTS.md` 保持原样，不纳入本阶段提交。

拓扑冻结线程覆盖、诊断隔离、真实阶段边界、发现、正式配置身份与上传验证仍按 PREP-02～PREP-07 落地。本次没有采集正式性能数据，也没有得出 CPU 性能交叉结论。

## 9. 目标边界摘要

公共策略拥有下限，DOD 阶段解释动作与工作量，既有并行辅助解析数量，线程池执行任务；Benchmark 解析器只产生选项，运行器负责应用设置与执行；CSV 写入器记录实际配置。本阶段只为可靠 CPU 配对准备可控门槛和可追溯入口，完整研究判断仍在 PREP-05。
