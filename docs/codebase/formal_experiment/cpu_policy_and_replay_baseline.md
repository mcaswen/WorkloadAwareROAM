# CPU 策略与回放事实基线

> 状态：保留 PREP-01 策略/回放基线；PREP-02 输入准备增量见独立事实文档\
> 日期：2026-09-09\
> 实施前源码基线：`main` / `a168701`；当前代码实现提交：`996c3a9`\
> 范围：CPU 下限、无窗口配置与记录、DOD 阶段调用和回放约束；不是完整 DOD 或渲染模块事实文档\
> 对应规划：[PREP-01 小规划](../../plans/formal_experiment/prep_01_policy_and_baseline_plan.md)

## 1. 模块与入口

FACT：无窗口入口为 main → `RunTerrainLodBenchmarkFromCommandLine` → `RunTerrainLodBenchmark`。`Benchmark` 使用公共 `TerrainLodSettings`；生产算法经 ITerrainLodAlgorithm，既有 CPU 配对经 `DataOrientedRoamPipeline` / `RunDataOrientedRoamPassExperiment`。命名空间分别为 `ParallelRoam::Benchmark`、`ParallelRoam::Algorithms` 和 `ParallelRoam::Algorithms::DataOrientedRoam`。

FACT：`TerrainLodBenchmarkCommandLine.h/.cpp` 负责纯参数解析和帮助文本；`Benchmark.cpp` 保留场景、相机、执行、验证和既有配对 CSV。原入口是薄包装，仅解析、输出错误/帮助或调用运行器。共享设置/统计 CSV 在 `src/experiment/TerrainLodExperimentCsv.h/.cpp`。算法不依赖 `Benchmark`、`App` 或图形资源；CPU 阶段读取高度图、视图和设置，更新队列、拓扑与 CPU 网格。

FACT（PREP-02 增量）：无窗口增加 `cpu-pilot-inputs` 准备配置，提前分流至 `benchmark/formal/FormalExperimentRunner`；外部清单、轨迹 A/NO、目标引用、独立 pilot 记录和 Python 摘要已落地，见 [CPU pilot 输入与记录事实](cpu_pilot_input_contracts.md)。本文件中 PREP-01 验证及旧配对路径仍是该阶段的历史基线，不表示新增准备入口尚不存在。

## 2. 文件与符号索引

以下路径均相对仓库根目录；符号可以直接用于源码检索。

| 文件 | 类型/关键符号 | 实际职责、状态与调用关系 |
| --- | --- | --- |
| `src/main.cpp` | main | 应用参数分流，进入无窗口包装入口 |
| `src/app/ApplicationCommandLine.h/.cpp` | `ApplicationCommandLineParseResult`、`ParseApplicationCommandLine` | 已分离的应用解析；遇无窗口模式保留原参数 |
| `src/benchmark/TerrainLodBenchmark.h` | `BenchmarkOptions`、`BenchmarkProfile`、`BenchmarkPassPolicySelection` | 无窗口选项；值类型由调用方持有 |
| `src/benchmark/TerrainLodBenchmarkCommandLine.h/.cpp` | `TerrainLodBenchmarkCommandLineParseResult`、`ParseTerrainLodBenchmarkCommandLine`、`BenchmarkUsage` | 不执行 I/O 的字符串到 `BenchmarkOptions` 转换；原帮助声明和运行入口保留 |
| `src/benchmark/TerrainLodBenchmark.cpp` | `RunTerrainLodBenchmark`、`ApplyPassPolicy`、`ApplyPassExperimentSettings`、`RunPassCrossoverReplay` | 场景生命周期、配置覆盖、生产与实验调用；配对分支独立配置 |
| `src/algorithms/TerrainLodPassTrace.h` | `TerrainLodPassPolicy`、`MakeTerrainLod*Policy` | 公共策略、五阶段动作/线程、五个数量下限、逐阶段记录 |
| `src/algorithms/ITerrainLodAlgorithm.h` | `TerrainLodSettings`、`HashTerrainLodBuildInput` | 公共输入与设置；哈希显式枚举字段 |
| `src/algorithms/data_oriented_roam/DataOrientedRoamTypes.h` | `DataOrientedRoamSettings` | 保存整份公共 `PassPolicy`，不维护第二套策略类型 |
| `src/algorithms/data_oriented_roam/DataOrientedRoamTerrainLodAlgorithm.cpp` | `ToDataOrientedSettings` | 公共设置到 DOD 设置；复制整份策略并兼容旧开关 |
| `src/algorithms/data_oriented_roam/DataOrientedRoamState.h/.cpp` | `DataOrientedRoamState` | 跨帧节点、队列、网格、脏集合与统计；复制保留非拥有线程池指针 |
| `src/algorithms/data_oriented_roam/DataOrientedRoamPipeline.h/.cpp` | `DataOrientedRoamPipeline`、`Build`、`State` | 拥有 `State` 和持久线程池，串行编排各阶段 |
| `src/algorithms/data_oriented_roam/DataOrientedRoamParallel.h` | `ResolveDataOrientedRoamWorkerCount`、`RunDataOrientedRoamWorkers` | 评分/网格共用数量解析及任务调用；没有线程池时顺序回放 |
| `src/algorithms/data_oriented_roam/DataOrientedRoamThreadPool.h/.cpp` | `ParallelFor`、`EnsureWorkerCount`、`WorkerCount` | 创建系统线程、任务队列、完成等待、析构回收；不选择策略 |
| `src/algorithms/data_oriented_roam/DataOrientedRoamQueues.h/.cpp` | `RefreshPersistentMergeQueuePriorities`、`RefreshPersistentSplitQueuePriorities` | 当前成员全量评分、分区写入、主线程建堆；成员由拓扑维护 |
| `src/algorithms/data_oriented_roam/DataOrientedRoamScoring.h/.cpp` | 评分函数 | 从只读几何/视图取得误差，不决定线程策略 |
| `src/algorithms/data_oriented_roam/DataOrientedRoamMeshEmit.h/.cpp` | `ApplyIncrementalMeshUpdates`、`EmitDirtyMeshSlots`、`EmitFullMeshSerial` | 归一化脏槽位、写顶点/索引、生成上传区间；全量串行独立 |
| `src/algorithms/data_oriented_roam/DataOrientedRoamTopology.h/.cpp` | `ReplayFrozen*TopologyPair`、`ReplayFrozen*TopologyAction`、`PrepareFrozenReplayState` | 拓扑修改、两种冻结回放与证据，不能混同计时路径 |
| `src/algorithms/data_oriented_roam/DataOrientedRoamPassExperiment.h/.cpp` | `RunDataOrientedRoamPassExperiment`、`RunScoreAction`、`RunMeshAction` | 状态副本、交替顺序、阶段计时、正确性比较、样本集合 |
| `src/experiment/TerrainLodExperimentCsv.h/.cpp` | `WriteTerrainLodSettingsCsv*`、`WriteTerrainLodStatsCsv*` | 普通无窗口与运行时共用设置/统计字段 |
| `src/terrain/HeightMap.h/.cpp` | `HeightMap` | 高度数据与采样，来源必须覆盖状态副本使用期 |
| `tests/TerrainLodExperimentCsvTests.cpp` | `CheckSerializedFields` | 表头、列值、唯一性和列数验证 |
| `tests/TerrainLodBenchmarkCommandLineTests.cpp` | `Parse`、main | 纯解析、旧别名、帮助、缺值、非法值、下限零值及最大值 |
| `tests/DataOrientedRoamPassPolicyTests.cpp` | `CheckPolicyAndCounts`、`CheckThreadDispatch`、`CheckScores`、`CheckMesh`、`CheckPublicMapping` | 数量边界、有效状态副本、真实线程池及公共设置接入；无图形依赖 |
| `tests/TerrainLodBenchmarkPolicyTests.cmake` | run_case、read_csv、expect_all | 进程退出、默认结果与配置列；配对下限高/低对照及冻结输入/结果一致 |
| CMakeLists.txt、`tests/CMakeLists.txt` | `PARALLEL_ROAM_DOD_SOURCES`、24 项 CTest | 同一组 11 个 DOD 源供两后端和 CPU 测试复用；解析文件两后端均注册 |

## 3. 策略、工作量与线程

FACT（修改前）：评分在 `Queues.cpp` 使用固定 `MinParallelPriorityRefreshCount` = 256；网格在 `MeshEmit.cpp` 使用固定 `MinParallelEmitTriangleCount` = 256。公共策略只有细分拓扑 32、合并拓扑 160 两个数量下限。四个主要预设及三个旧别名均从公共默认策略构造。

FACT：评分工作量是 `MergeQueue`.size() / `SplitQueue`.size()；脏网格工作量是 `NormalizeDirtyMeshSlots` 后的 `DirtySlots`.size()，不是活动三角形总数。`SerialFull` 始终走全量串行写入，脏集合为空不能证明全量路径无工作。

FACT：现由 `ResolveDataOrientedRoamWorkerCount` 共用数量规则：无工作 0；串行动作、请求 1 或低于下限时 1；请求 0 参考 hardware_concurrency，未知按 1、最多 8；显式请求只钳制到工作量。任务按向上取整区间划分，最后部分任务可能为空。

FACT：`RunDataOrientedRoamWorkers` 对 0 返回、1 在调用线程执行、大于 1 且有池时 `ParallelFor`，无池则顺序执行任务编号。`ParallelFor` 同步等待完成；线程池容量与统计数量都不能单独证明多个系统线程执行了当前短任务。

INFERENCE：状态副本借用源 `Pipeline` 的线程池和高度图，二者的生命周期必须覆盖副本运行；同一借用池的实验依次运行。共享只读输入、分区写入和回主线程建堆保证评分数据不会由两个任务同时更新同一条目。


FACT（当前）：`TerrainLodPassPolicy` 增加 `std::size_t` 类型的 `MergeScoreMinParallelEntryCount`、`SplitScoreMinParallelEntryCount`、`MeshEmitMinParallelTriangleCount`，默认均为 256。两处固定门槛已移除。`Queues.cpp` 与 `MeshEmit.cpp` 保留动作适配，串行动作将请求归一为 1；数量解析不读取队列，也不参与拓扑分块。N < 下限时回退，N == 下限只代表允许尝试并行。

## 4. 配置和数据流

FACT：普通路径为 `BenchmarkOptions` → `ApplyPassPolicy` → `ApplyTopologyExperimentSettings` → `TerrainLodSettings` → 整份 `PassPolicy` 复制 → DOD。`ApplyPassPolicy` 重建策略后保存/还原全部五个下限及已有拓扑限定字段；`PassPolicyReplay` 再次选预设不会丢失新覆盖值。`ApplyPassExperimentSettings` 通过同文件私有 `ApplyCpuParallelMinimums` 应用三个 CPU 下限。

FACT：`RunPassCrossoverReplay` 绕过普通覆盖入口，自建串行来源策略并设置五个线程请求，再调用 `ApplyCpuParallelMinimums`。`MakeDataOrientedSettings` 和 `PrepareNextFrameState` 整份传递设置。`RunScoreAction` / `RunMeshAction` 的副本只更改相关动作与线程请求，数量下限会从源设置继承。

FACT：`HashTerrainLodBuildInput` 在既有字段之后依次追加合并评分、细分评分、网格提交三个新下限。默认和显式默认值在新版本内身份相同；不承诺旧版本哈希数值兼容。`FrozenStateHash` 来自共同冻结输入，主要用于同一输入的策略配对；它不是完整正式配置身份。

FACT（修改前）：共享 `TerrainLodExperimentCsvSchemaVersion` 为 3；`Benchmark` 私有 `WritePassCrossoverCsvHeader` / `WritePassCrossoverCsvRow` 的 `schemaVersion` 为 1。两套版本独立，前者更新不会自动更新后者。

FACT（当前）：共享设置 CSV 版本为 4，配对 CSV 版本为 2。两个写入器分别增加 `mergeScoreMinParallelEntryCount`、`splitScoreMinParallelEntryCount`、`meshEmitMinParallelTriangleCount`，均取已应用的策略。配对表追加列，不把现有冻结拓扑中被强制覆盖的门槛误报为有效配置。

FACT：新增无窗口参数为 `--merge-score-min-parallel-entries`、`--split-score-min-parallel-entries`、`--mesh-min-parallel-triangles`。`ParseSize` 使用 `std::from_chars` 并检查完整消费；0 和 SIZE_MAX 合法，符号、空白、溢出及非十进制字符失败。已有 `--pass-`* 的 SIZE_MAX 哨兵仍拒绝，零值的运行适用性沿用原逻辑。

FACT：解析按出现顺序消费参数，重复选项最后一次生效；帮助立即返回，先出现的错误优先。失败结果携带 `Error`，包装器输出错误与帮助并返回 1；正式配置名称仍拒绝。解析器不加载资产、不创建目录、不启动算法。新的错误文本统一格式，旧合法参数、别名和退出语义保留。

## 5. 回放、诊断及限制

FACT：生产诊断链在拓扑提交处调用 `ReplayFrozenSplitTopologyPair` / `ReplayFrozenMergeTopologyPair`；其串行对照仍走分块兼容链。CPU 配对链是 `RunDataOrientedRoamPassExperiment` → `RunTopologyAction` → `ReplayFrozen*TopologyAction`；串行动作进入直接候选提交，二者不是同一条串行路径。

FACT：`PrepareFrozenReplayState` 开启 `EnableTopologyValidation`；冻结并行执行把当前拓扑下限置 0、线程请求覆盖为 `MaxTopologyCommitWorkerCount` = 8。评分/网格副本继承源诊断开关。当前 pair CSV 属工程回归数据，不能据此宣称正式计时隔离或五阶段可靠线程配对已完成。

FACT：`EnablePassEvidence` 控制局部高精度计时；`EnableTopologyValidation` 控制拓扑诊断；`EnableTopologyPairEvidence` 控制生产拓扑成对证据。当前配置与回放各有覆盖，不能只依据最外层开关推断计时区域没有验证。

PLANNED：诊断隔离、真实阶段边界、冻结拓扑线程请求修正分别属于 PREP-03/04。本阶段仅参数化评分/网格门槛，不改变这些执行语义。

## 6. 图形外部边界

FACT：`OpenGL` 的 `TerrainRenderer.cpp` 使用 `glBufferData` / `glBufferSubData`；资源与绑定依赖 `OpenGL` context。`D3D12TerrainRenderer.cpp` 持久映射上传堆，按帧槽位持有网格资源、版本和 `PendingUpdateRanges`；`D3D12GraphicsBackend::BeginFrame` 等待相应 fence，实际网格写入在渲染路径按需进行。

INFERENCE：独立实验资源的数据内容一致，不代表缓存、帧槽位积压与同步历史一致。各后端只能先讨论内部生产/实验复用，不能据此确定跨 API 的完整上传抽象。

PLANNED：上传提取、状态恢复及性能代表性验证在 PREP-06/07，本阶段没有新增或修改上传资源。

## 7. 实测基线与实现后验证

FACT：2026-09-09，修改前 `OpenGL` `relwithdebinfo-fetch` 完整配置、构建成功，21/21 CTest 通过（7.91 秒）。原始日志：`benchmark-output/prep-01/baseline/ctest.log`。

FACT：同一源码与二进制下顺序保存 smoke、`pass-policy-replay`、`pass-crossover-replay`、`pass-crossover-stress-replay` 的 CSV 和日志，目录为 `benchmark-output/prep-01/baseline/`；共同参数为 `--benchmark` `--algorithm` dod `--pass-warmups` 0 `--pass-repeats` 1 `--pass-targets` 1，逐项指定 `--profile` 和 `--csv`。输出属于工程基线，不作性能胜负判断。

FACT：修改后两后端完整构建成功，`OpenGL` 与 D3D12 各 24/24 CTest 通过，均包含注释覆盖门禁。D3D12 原缓存关闭测试，本次使用 `-DPARALLEL_ROAM_BUILD_TESTS=ON` 显式启用。`OpenGL` 最后补充公共结果契约断言后，CPU 专项再次通过。

| 验证 | 实际证据 |
| --- | --- |
| 数量/默认/身份 | 三个独立字段、0/1/255/256/257、默认/显式默认、最大下限、显式超过 8、请求 1/2/8 和自动硬件提示 |
| 有效 CPU 状态 | 合并评分 23 条、细分评分 128 条；逐节点分数、队列成员/不变量、来源未变 |
| 网格 | 128 和 512 个活动槽位各只写 2 个脏槽位；重复槽位归一化、全部顶点属性/索引/规范化哈希一致、空脏集合、`SerialFull` 全量恢复 |
| 设置传递 | 公共算法入口分别覆盖新下限，观察实际阶段回退变化；新值经状态复制保持；两种 CPU 配对高下限与零下限改变线程结果但冻结输入及结果一致 |
| 真实线程 | 受控线程池测试取得两个不同系统线程；临时非计时诊断中，合并评分/细分评分/网格任务最多分别观察到 2/5/2 个系统线程 |
| 默认行为回归 | smoke 的 6 行和 `pass-policy-replay` 的 24 行各 233 个非计时字段不变；两种配对各 11 行、27 个非计时字段不变 |

FACT：线程诊断仅在本地临时记录 `ParallelFor` 任务实际执行线程编号，不修改任务划分或加入阶段等待。诊断使用已验证的 CPU 夹具，部分短批次仍由一个线程完成；证据说明这些生产任务确实可由多个系统线程执行，不保证每次都占满请求线程。临时修改已移除并恢复常规构建；`ThreadPool.cpp` 没有最终代码差异。日志为 `benchmark-output/prep-01/thread-dispatch-diagnostic.log`。

FACT：默认回归明确排除耗时、`cpuUtilizationPercent`、schema 版本和旧 `replayInputHash`；其余共同字段逐行比对。脚本和结果分别为 `benchmark-output/prep-01/compare_baseline.py`、`benchmark-output/prep-01/baseline-comparison.json`。原始基线保留，修改后输出写入 after/。本地工件仅通过 `.git/info/exclude` 排除，未修改仓库忽略规则，也未提交生成数据。

FACT：完整日志位于 `benchmark-output/prep-01/` 的 `configure-opengl.log`、`build-opengl.log`、`ctest-opengl.log`、`configure-d3d12.log`、`build-d3d12.log`、`ctest-d3d12.log`；公共结果断言最后补测记录为 `final-policy-test.log`。两个构建目录的 `tests/benchmark-policy/` 保存进程级 CSV 与错误日志。

## 8. 依赖、变化范围与后续限制

FACT：解析 → `Benchmark` 选项/公共值类型；`Benchmark` → 公共算法/DOD/共享 CSV；DOD 阶段 → 共用数量解析/既有线程池。没有新增反向依赖、公共算法虚接口、线程池所有权、GUI 控件或运行时新命令行项。原有整份策略传递与共享 CSV 继续被运行时复用。

FACT：现有 `Benchmark.cpp` 仍包含多个场景和回归编排；PREP-01 仅移出已确认的命令行职责，旧 CPU 配对 CSV 仍归既有运行器。PREP-02 已在共享层新增独立 pilot 记录与 CSV 编解码，并增加准备入口早期分流；没有替换旧配对运行器或接入完整正式记录系统。

FACT：PREP-02 最小输入/记录已完成，细节和测试见上述独立事实文档。PLANNED：PREP-03 阶段边界/发现、PREP-04 可靠配对、PREP-05 探索性 pilot。PREP-01/02 完成不能视为五阶段正式配对或研究假设已验证。拓扑冻结请求覆盖、计时诊断隔离和上传实验代表性仍按上位规划处理。

UNCERTAIN：没有图形上传性能或跨平台硬件性能结论；自动硬件提示为 0 的分支经源码核对，本机没有该硬件返回条件。当前所有时间记录用于工程验收，不证明性能交叉。

FACT：已按用户确认的注释格式将本次两处公共注释统一为 `/// <summary>` / `/// </summary>`；扫描 `src/tests` 无 `@brief`，注释覆盖门禁再次通过，记录为 `benchmark-output/prep-01/final-comment-check.log`。最后此项只修改注释，不改变 C++ 逻辑。
