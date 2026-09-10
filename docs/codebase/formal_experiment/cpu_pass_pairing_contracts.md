# CPU 阶段配对与最小分析：当前实现事实

> 扫描范围：PREP-04 工作区实现与收尾记录，2026-09-10～2026-09-11\
> 依据：[阶段规划](../../plans/formal_experiment/prep_04_minimal_cpu_pairing_plan.md)、[开发规范](../../standards/development_guidelines.md)及本文索引的当前源码\
> 状态：实现与最后排查已收尾，严格等价通过；保留环境限制并按用户决定移交 P5，当前事实见第 13 节

## 1. 模块边界

FACT：`src/algorithms/data_oriented_roam` 拥有阶段状态副本、动作配置、实际生产执行、计时与停表后验证。命名空间为 `ParallelRoam::Algorithms::DataOrientedRoam`，不依赖 Benchmark、CSV 或 Python。原 `ExecuteDataOrientedRoamPass` 仍是实际执行入口，没有另写实验专用评分、拓扑事务或网格写入算法。

FACT：`src/experiment/formal` 拥有 `CpuPairSelection`、配置快照、策略行、目标摘要、运行摘要与标定值，以及它们的 CSV 契约。该层不读取 DOD 状态。`CpuRecordStatus` 移至基础 `FormalExperimentTypes.h`，配对记录只依赖基础类型，避免 Records 与 PairRecords 循环包含。

FACT：`src/benchmark/formal` 创建场景 HeightMap/Pipeline、重建目标、转换领域值并组织文件发布。它通过阶段观察器接收同步借用的 const 状态，调用 DOD 输入哈希、探测器和配对函数；不直接改写节点、长期队列、网格槽位或算法预算。

FACT：`scripts/cpu_pilot_artifacts.py` 拥有准备与发现产物的关联核验；`run_cpu_pilot.py` 拥有单个真实子进程尝试；`scripts/formal_experiments` 拥有落盘审计、纯统计、分组与中文报告。分析不会重新选择目标，也不决定研究继续。

## 2. 文件与接口索引

以下路径以仓库根为基准；`.h/.cpp` 分别为公共契约与实现。

| 文件 | 类型或主要方法 | 当前职责与直接依赖 |
| --- | --- | --- |
| `src/algorithms/data_oriented_roam/DataOrientedRoamPassExperiment.h/.cpp` | Config、Sample、Result、`RunFrozenDataOrientedRoamPassExperiment`、`RunDataOrientedRoamPassExperiment` | 策略集合、绝对块序、预热、整体等价与旧入口包装；依赖 Measurement、PassInput、WorkloadProbe、PassExecution |
| 同目录 `DataOrientedRoamPassMeasurement.h/.cpp` | `ConfigureDataOrientedRoamPassAction`、`PrepareDataOrientedRoamPassWorkers`、`MeasureDataOrientedRoamPass`、`Detail::MeasurePreparedCpuPass` | 单个状态副本、线程准备、连续计时与执行统计读取；调用实际 PassExecution 和 Evidence |
| 同目录 `DataOrientedRoamPassEvidence.h/.cpp` | `DataOrientedRoamPassEvidence`、`CaptureDataOrientedRoamPassEvidence` | 评分、拓扑和网格结果验证；复用 Queues、Validation、MeshPlan 与公共哈希 |
| 同目录 `DataOrientedRoamTopology.h/.cpp` | 原冻结诊断与 `CollectFrozenReplayEvidence` | 原事务与冻结诊断保留，结果哈希实现移入 Evidence |
| `src/benchmark/formal/FormalCpuInput.h/.cpp` | `MakeCpuSourceSettings`、`MakeCpuDiscoveryInputRecord`、`MakeCpuPairInputRecord`、`IsCpuSourceFrameValid` | 发现/配对共用设置、工作量值映射与来源帧诊断判断；不持有流水线 |
| 同目录 `FormalCpuPassBenchmark.h/.cpp` | `MakeCpuPairConfiguration`、`MeasureCpuScenarioTargets` | 每场景根状态推进、目标身份核对、样本映射与帧末确认 |
| 同目录 `FormalTimingCalibration.h/.cpp` | `CalibrateCpuPassTiming`、`FormalTimingCalibrationResult` | 空计时与真实根阶段无工作测量、原值及分位数 |
| 同目录 `FormalExperimentRunner.h/.cpp` | `RunCpuPilotPairing` | 全量清单加载后筛选、目录所有权、前后标定、CSV 写入和发布 |
| 同目录 `FormalWorkloadDiscovery.cpp` | `DiscoverCpuScenarioWorkloads` | 原发现流程保留，设置与值映射改用 FormalCpuInput |
| `src/experiment/formal/FormalCpuPairRecords.h` | Configuration、Record、TargetSummary、Summary、CalibrationRecord | 全部为值结构，无 DOD 运行时所有权 |
| 同目录 `FormalCpuPairCsv.h/.cpp` | `ReadCpuPairCsv`、`WriteCpuPairCsvRow`、`ValidateCpuPairTarget` 等 | v2 序列化、逐行验证、整个目标完整性与摘要输出 |
| 同目录 `FormalExperimentTypes.h` | `CpuPairSelection`、`CpuRecordStatus` | 显式可选覆盖与基础状态枚举 |
| 同目录 `FormalExperimentRecords.h`、`FormalExperimentCsv.h/.cpp` | 原输入/发现记录及兼容包含 | 配对定义迁出，旧包含入口继续转发，不保留第二套配对写入实现 |
| `src/benchmark/TerrainLodBenchmark.h/.cpp`、`TerrainLodBenchmarkCommandLine.cpp` | `CpuPassPairPilot`、`BenchmarkOptions::CpuPair` | 新模式早期分流、筛选与覆盖存在性；旧工程 CSV 升 v3 |
| `scripts/cpu_pilot_artifacts.py` | `load_prepared_inputs`、`verify_discovery_outputs`、`load_discovered_inputs`、`verify_output_identities` | 冻结输入、发现完成态、资产与产物摘要关联 |
| `scripts/run_cpu_pilot.py` | `run_pairing`、`archive_records`、`withdraw_outputs` | 单尝试目录、真实进程、前后身份、成功/失败归档 |
| `scripts/formal_experiments/input_validation.py` | `select_frozen_targets`、`validate_pairing_outputs`、`validate_calibration`、`load_pair_attempt` | 完整尝试、版本、唯一键、策略块、目标与标定审计 |
| 同目录 `paired_statistics.py` | `summarize_pair`、`run_drift` | 配对差、百分位、MAD、固定阈值与三次短运行漂移 |
| 同目录 `crossover_analysis.py` | `analyze_attempts`、`short_run_stability` | 按运行、冻结目标及固定层组织描述；回退排除、双向信号和跨运行方向 |
| 同目录 `report_writer.py` | `write_report` | 中文 Markdown 与聚合 CSV，消费已有统计 |
| 同目录 `__init__.py` | Python 包 | 入口与测试使用同一实现 |
| `scripts/analyze_pass_crossover.py` | `analyze` | 命令、独占分析目录、输入审计、报告与分析耗时/峰值内存 |
| `CMakeLists.txt`、`tests/CMakeLists.txt` | 既有 DOD、formal、应用源组 | 两后端编译共同 CPU 文件，并注册新增测试 |

## 3. DOD 配对数据流与所有权

FACT：`DataOrientedRoamPassExperimentConfig` 的默认模式为 `Diagnostic`，默认 W=5、R=30、P=8，阶段集合为公共五阶段枚举。`PilotMeasurement` 是显式测量模式。配置拒绝零正式重复、零线程、重复/空阶段集、上传阶段、未知模式及可序列化/容器数量溢出。

FACT：`RunFrozenDataOrientedRoamPassExperiment` 输入已经位于真实阶段边界。它先计算完整阶段输入哈希和只读工作量，统一扩容并预热借用线程池，然后逐块逐动作调用 Measurement。任何块失败都使整个目标失败；已产生的预热与正式记录仍保留，但 `Equivalent` 全部撤销。全部块完成后再次核对来源输入哈希。

FACT：两动作按绝对块号交替 AB/BA；网格按 ABC、ACB、BAC、BCA、CAB、CBA 循环。块号从 0 到 W+R−1，预热与正式记录分开保存；正式 `RepeatIndex` 从 0 起，但不重置绝对顺序。每个动作重新复制相同来源，前一动作只提供结果比较，不能推进下一动作输入。

FACT：旧 `RunDataOrientedRoamPassExperiment` 复制上一帧状态，调用公共帧准备；五个来源动作统一为串行，其他输入参数保留。公共阶段编排始终执行全部阶段，观察器只测选中的阶段，所以过滤不会跳过来源状态变化。

FACT：状态副本拥有算法容器，HeightMap 和 ThreadPool 是从来源同步借用的非拥有指针。线程任务在测量返回前结束，没有跨回调保留引用。线程池扩容是允许的资源准备，算法源状态、容器内容、地址、容量和统计通过独立 StateSnapshot 测试核对。

## 4. 连续计时与执行证据

FACT：`Detail::MeasurePreparedCpuPass` 以静态模板固定 `prepare → timer start → execute → timer stop → capture`。测试可替换 Timer 验证顺序，实际计时路径没有为测试增加虚调用、锁或线程 ID 采集。

FACT：Measurement 在计时前完成深复制、Stats 清零、当前动作/线程配置、诊断模式应用、必要的线程准备以及副本输入哈希。它不调用旧 `PrepareFrozenReplayState`，不清空先前拓扑修改，不强制把线程请求改为 8，也不重设并行下限。

FACT：两个模式都关闭递归拓扑配对。`PilotMeasurement` 进一步关闭 `EnablePassEvidence` 与 `EnableTopologyValidation`，并记录开表时三个开关已全关。计时区间只调用一次 `ExecuteDataOrientedRoamPass`；生产内部的线程唤醒、排队、同步、快照、分块、串行收敛和网格容量变化仍在其中。

FACT：`wallMs` 来自一对连续 steady_clock 时间点，未用内部子项相加或减去标定值。复制、输入检查、验证、线程准备分别记录；执行统计先于验证器读取，以免验证器写入的统计影响线程和阶段子项。

FACT：请求线程是策略配置值，有效线程沿用生产任务/派发口径，不等于保证同时运行的物理线程数。串行拓扑在调用线程执行；缺少线程池时，多个任务编号仍只能记为一个调用线程。记录区分 `thread_pool`、`caller_thread`、`no_work`，以及请求 1、下限、安全块不足、阶段/构建门控、缺池和无工作原因。

FACT：拓扑记录中的共同候选分类来自计时前 WorkloadProbe；成功提交、最终活动叶、实际脏写和范围数量单列为 `post_`。不会用某个策略的执行后结果反写共同选择特征。

## 5. 正确性与规范化

FACT：评分证据检查长期队列不变量、有效节点与有限分数，按路径排序后编码每个分数的位模式。组内和跨块比较输入、结果哈希、Correct 与 ValidationPerformed，不只比较队列长度或堆顶。

FACT：拓扑证据复用 `ValidateTopology`，检查预算、队列、邻接、裂缝和活动拓扑。规范化结果包含分裂节点路径集合、活动叶集合、队列成员、修改条目多重集合及活动数量。

FACT：拓扑证据还在独立网格元数据副本上重放修改序列。首次初始化允许发生；已有槽位发生错误序列回退则验证失败。计划最终槽位集合必须等于活动叶，并核对反向槽位。它不更新真实元数据、不生成几何、不修复来源拓扑，从而允许独立事务的合法换序而不掩盖非交换序列损坏。

FACT：旧 `CollectFrozenReplayEvidence` 只取公共 Evidence 的 `Topology` 明细，保持旧冻结诊断的集合/违规字段用途；新的测量路径使用完整 `Correct`。旧诊断准备仍有清空修改记录等历史语义，不能作为 pilot 测量入口。

FACT：网格证据复用 `ValidateIncrementalMesh`，再核对全部顶点属性有限、每个三角形索引互异。公共规范化网格哈希刻意不包含调试颜色和高亮；配对证据按稳定路径/槽位顺序补充这些有效属性。脏区间、更新数量和槽位顺序可以不同，最终可渲染内容须等价。

## 6. 目标重建与来源确认

FACT：`RunCpuPilotPairing` 先加载全部场景、完整相机和全部目标，再筛选执行列表。采样筛选必须同时指定唯一场景及具体阶段；拒绝样本 0、未知/重复选择和非法数量。选择不会重排 selectionRank，不重写冻结清单。

FACT：`FormalCpuInput` 为发现与配对提供同一份场景设置和工作量映射；来源是已校验的固定串行策略，并启用原帧诊断。`MakeCpuPairConfiguration` 同时保留清单 5/30/8 与实际覆盖。覆盖只属于本次探索尝试。

FACT：`MeasureCpuScenarioTargets` 每场景创建独立 HeightMap/Pipeline，沿 k=0 到最后一个目标推进，最多 64 帧。命中阶段时核对相机、视图、完整输入哈希、主要值、原始特征向量与选择特征哈希；不向 DOD 注入热计划。

FACT：同一帧的样本先保留在 pending 值集合中。来源完整执行后，通过 `IsCpuSourceFrameValid` 检查诊断开关、序号、预算、队列、拓扑、邻接、裂缝及非零结果身份；通过后才写出有效行。帧中异常或帧末失败会撤销当前帧所有已测目标的成功资格。先前帧的记录保留作审计，但整个尝试失败。

FACT：重建计时扣除观察器中的测量/验证工作，仅作为来源推进成本记录。每次测量只保留当前目标/帧的值记录，不缓存全部目标状态。配对核心每块每动作只复制并执行一次。

## 7. CLI 与产物状态

```text
--benchmark --profile cpu-pass-pair-pilot
  --scenario-manifest <path> --camera-manifest <path> --target-manifest <path>
  --output-dir <new-directory>
  [--scenario-id <id>]... [--pass-id all|mergeScore|mergeTopology|splitScore|splitTopology|meshEmit]
  [--sample-index 1..63] [--pass-warmups W] [--pass-repeats R] [--pass-workers P]
```

FACT：W 可为零，R/P 为正；CLI 跟踪参数是否显式出现。`CpuPairSelection` 的 optional 数量在参数缺席时保持空，不把结构体默认值当作用户覆盖。新模式拒绝普通算法、策略、下限、CSV、旧目标数量和发现目标数量选项。旧模式的有效行为保留。

FACT：裸 C++ 独占创建不存在的输出目录，父目录须存在；脚本独占创建尝试目录，再把尚不存在的 `records/` 交给 C++。脚本归档前先核对全部目标路径无冲突。已有目录包括空目录均不覆盖。

FACT：配对策略行使用 v2/`measurementProtocolVersion=1`，80 列；场景、相机、目标维持 v1，发现维持 v2。旧工程 `pass-crossover-replay` CSV 升为 v3，并增加诊断模式、真实阶段输入身份和计时外成本。两种 wallMs 不能混称同一测量口径。

FACT：标准文件是 `selected-targets.csv`、`pair-samples.csv`、`warmup-samples.csv`、`target-summary.csv`、`pair-summary.csv` 和 `timing-calibration.csv`。C++ 先写 pending 文件，重读策略 CSV 并核对全部目标后才发布。`selected-targets.csv` 是保留原 rank 的执行列表，不应重新用要求每组 rank 连续的发现目标加载器解释。

FACT：C++ 成功状态为 `pairing_internal_complete`；脚本记录真实 PID、进程起止、命令、进程生成的 RunId、源码/构建/输入前后摘要、环境及输出 SHA-256，全部核对后才写 `pairing_complete`。复制目录保留同一 RunId，分析拒绝将其视作独立运行。

FACT：失败保留日志、pending 或 rejected 文件及错误；成功名称会被撤下。子进程被终止时，外层不声明完成。没有恢复、自动续跑或自动覆盖机制。没有冻结目标的组使用摘要中的 sample=0 与 `targets_unavailable`；整个请求零目标返回失败而不伪造策略行。

## 8. 标定与分析

FACT：每次尝试在目标采集前后各执行 1000 次空计时和 1000 次根阶段合并评分无工作测量，前后分别创建首个所选场景的独立流水线。无工作项使用相同 Measurement，检查零工作/零有效线程、关闭诊断以及实际结果验证。状态引用不离开根观察器。

FACT：标定保留原值、批次位置、种类、名义时钟分辨率和最近秩 P50/P95/P99。Python 重算分位数、核对四个完整批次以及前后根输入/结果身份。空计时 P99 超过 0.01 ms 时，整次尝试保留量级但不解释胜负。

FACT：输入审计按运行/场景/阶段/采样/绝对块/位置验证唯一键、完整动作集合、共同输入、配置及结果。预热与计时文件同列但相互独立。缺行、重复、额外动作、NaN/Inf/负时间、错误版本或目标摘要冲突均拒绝，不从损坏尝试中挑部分成功目标。

FACT：`summarize_pair` 先逐块求 A−B 再取中位数；相对收益为配对中位差除以 A 中位时间，零分母为 None。阈值为 max(0.01 ms、较快策略中位数的 3%、前后空/无工作 P99 最大值、配对差 MAD 的三倍)。差值必须严格越过阈值；MAD 不解释为置信区间。

FACT：任何块出现回退都会排除整个目标中涉及该动作的策略对；三策略网格仍可保留完整的串行脏写/串行全量比较。按每次运行的冻结 low/middle/high/coverage 分层报告，原始工作量只用于排序展示，不按耗时重新分箱。

FACT：两个中预算场景最小 rank 的 W=2/R=10/P=8 样本用于三次独立短运行漂移。每个目标/动作三次中位数极差超过其中位数 5% 时要求整轮重测，最多保留两轮。第二轮稳定不会重新赋予第一轮不稳定样本胜负资格；第二轮仍不稳定时对相同执行来源停止胜负解释。没有足够短运行时报告证据不足，不宣称已证实跨进程稳定。

FACT：报告可标记同一次运行内存在相反方向且均超过阈值的目标，但仅称“双向描述性信号”。跨运行方向另列，未执行 bootstrap、Holm、正式拟合或研究继续决定。

FACT：跨运行方向按后端、源码、构建文件摘要、冻结输入摘要、环境及 W/R/P 配置分组，完整组身份保存为 `comparisonGroup`。相同目标在不同源码、环境或不同重复配置下不会合并为一个一致性结果。环境比较排除进程 PID；前后环境配置改变时停止该尝试的全部胜负解释。短运行漂移的执行身份也包含冻结输入与环境。

FACT：`target-costs.csv` 与中文报告汇总每目标的复制、输入检查、结果验证和线程准备成本，包含预热与正式行，CSV 同时保存合计与中位数。目标摘要的 `rebuildMs` 是到达该阶段边界的来源累计耗时，扣除了之前观察器测量时间，不能把不同目标值相加当作整个尝试时间。报告分别列出 C++ 内部总耗时、真实子进程墙钟和外层尝试起止间隔。

FACT：目标输入匹配探测的 `featureCollectionMs` 从公共输入记录复制到所有策略行。完整性核查要求它在同一目标内一致；成本汇总只取一次并命名 `inputFeatureProbeMs`，不按策略行数重复累加，也不将其称作所有探测工作的总成本。

FACT：完成尝试必须保存非空源码与构建身份、实际命令、进程时间和前后环境；源码总摘要由保存的文件身份重新计算。两边同时缺失或清空来源不能通过一致性校验。历史文件无需仍在原路径，已保存身份不要求等于当前工作区。

## 9. 测试与当前证据

本节保留连续前缀修复前的实现与失败证据；修复后的当前行为和验证见第 12 节。

FACT：新增 `DataOrientedRoamPassExperimentTests.cpp` 覆盖真实阶段、源快照、诊断开关、连续边界、1/2/8 请求、缺池、受控真实派发、单/多安全块、W/R 顺序组合、旧入口筛选一致性与破坏输入。`FormalCpuPairTests.cpp` 覆盖五阶段 CSV 往返、块完整性、版本、溢出和真实根标定。

FACT：新增 `test_cpu_pilot_pairing.py` 使用真实准备/发现/配对子进程，覆盖独立重建、过滤保持 rank、目录所有权、摘要与语义损坏、结构自洽但真实输入不符、运行后输入变动、失败审计及子进程中断。`test_pass_crossover_analysis.py` 使用明确合成的统计值验证方向、噪声、零分母、三策略、回退、顺序不变、重复运行及短运行漂移；合成值不作为研究数据。

FACT：中间工程产物见小规划第 11 节。单目标与 Test129 中预算 18 目标均已跑通，脚本完整尝试 `benchmark-output/prep-04/step4-pair-01/` 具有外部身份核验；首份报告在 `step4-analysis-01/`。这些中间证据不替代最终完整集成和性能验收。

FACT：两后端全量构建与各 36/36 CTest 已完成，后续记录与分析核查修改也通过专项复测；最终 Python 真实进程测试为 8 项，分析测试为 12 项。55,440 条显式合成策略行和 12,000 条标定行，加上一个真实完成模板的三次来源审计，最终读取、完整性核查和报告生成耗时 2.819 秒，峰值 255.09 MiB。重复审计只有一个真实 RunId，不构成三次研究运行；分析程序启动与导入未计入。合成测试只提供容量证据。

FACT：完整六场景配对在 `peking547-a-b20000 / splitTopology / sampleIndex=14` 被正确拒绝。进入阶段的输入哈希为 `4677929227313376363`，来源保留 10 条前序合并修改；串行与并行 P=2/8 均各自满足拓扑和网格正确性，但 20,000 个活动叶中有 6 个各自独有，最终网格不同。诊断开关不改变结果；P=1 回退与串行相同。实际 OpenGL/D3D12 应用均复现 P=8 失败。详见[不等价分析](../../reviews/formal_experiment/prep_04_split_topology_equivalence_analysis.md)。

FACT：该反例来自既有生产策略，当前计划会先提交全局优先级 rank=44/353 的安全内部候选，再进行串行收敛；安全分类没有保证有限预算下的严格输出等价。旧拓扑翻译单元的定向复现得到相同差异。当前没有修复生产策略、重选冻结目标或放宽比较条件。

FACT：实施前后普通矩阵的十二组算法非计时字段一致，发现前后 24 次运行的目标清单 SHA-256 一致。性能原规则触发 25 项，三配置交替复测后仍有 OpenGL 帧构建、证据收集和部分评分指标的恶化信号，性能不能判为通过；来源、数值、排查及不确定性见[性能调查](../../reviews/formal_experiment/prep_04_runtime_performance_regression_analysis.md)。

UNCERTAIN：其余完整目标集的最终配对、独立短运行漂移和全部名义重复目标尚未验收；性能问题的代码层面原因未完全定位。没有据现有局部数据判断 CPU 研究继续。跨编译器位级兼容、图形上传及恢复不属于当前实现。

## 10. 修复规划前的补充事实

本节描述修复前代码，不作为当前前缀实现的事实。

FACT：当前 `Queues.cpp::SplitEntryPrecedes` 使用分数降序、同分 `PathId` 升序；`TopologyPlan.cpp` 和 `Topology.cpp::FlattenSplitChunks` 同分使用 `Sequence`。`SnapshotPersistentSplitQueueCandidates` 的 `Sequence` 来自堆数组遍历，且该快照过滤不满足 `ShouldSplitWithScore` 的项。串行收敛在队首不满足该条件时结束，并优先执行低于合并阈值的有效合并队首。现有快照排序与全局串行控制流不能直接视为同一顺序。

FACT：普通 `TerrainLodBenchmark.cpp::MakeScenario` 开启 `EnablePassEvidence`；`standard/budget-saturation` 关闭的是拓扑验证。`Pipeline.cpp::CollectPassEvidence` 在更新内部计时结束后执行，但仍位于 Benchmark 的 `BuildRenderData` 外层包络内。原普通矩阵不能替代三个诊断开关关闭时的生产成本对照。

FACT：规划前追加 144 次真实进程对照，源码前后一致；相同程序自身对照和两个三级缓存分组内的实验均保留。整帧版本方向反转，处理器条件明显影响耗时；96 MiB 分组下建堆仍有微小正差。三个队列函数在排除 COFF 重定位后与新旧 PE 中主体分别唯一匹配，链接地址不同。来源、精确数值和限制见[性能调查第 7 节](../../reviews/formal_experiment/prep_04_runtime_performance_regression_analysis.md#7-规划前补充调查环境对照与函数机器码)。

UNCERTAIN：连续前缀尚未实现验证，不能写成当前行为；历史异常的全部原因及剩余局部差值根因尚未定位。修复提案见[拓扑小规划](../../plans/formal_experiment/prep_04_split_topology_equivalence_fix_plan.md)与[性能小规划](../../plans/formal_experiment/prep_04_runtime_performance_fix_plan.md)。

## 11. 已批准的性能测量补全

FACT：2026-09-10 用户已暂时关闭原性能问题、保留未决原因，授权提交后开始拓扑等价性修复。后续工程门槛由开发规范第 7.3 节统一定义；下文未关闭描述属于作出决定前的技术状态，不再阻断本轮拓扑工作。

FACT：性能小规划经 Minor Revision 批准后，新增普通版本比较的编排/环境/分析模块及公共 CPU Probe，详见[普通运行性能契约](runtime_performance_contracts.md)。Probe 冻结三诊断开关并在计时外验证几何，原普通 benchmark 与 DOD 配对核心未修改。

FACT：16 个成功尝试共 1,488 个有效应用进程完成原/保留/重建矩阵、同程序控制、诊断开关与隔离链接比较；原 25 项已逐项保留原极差复核。少数条件满足限定“无版本相关证据”，细分建堆等项仍未关闭，缺少完整调度/频率和运行时地址证据。性能承接结束时尚未实施生产优化或拓扑前缀策略；后续拓扑实施见第 12 节。

## 12. 连续前缀修复的当前事实

FACT：2026-09-11 用户明确关闭剩余性能问题，保留 P95 实测差值及未隔离的原因，不再阻断拓扑及 PREP-04 收尾。本节后文的成本未关闭描述属于决定前的技术记录。已知第 14 帧不等价及已测目标已通过修复验证，没有新反例记录；自然非空前缀覆盖和未完成的独立运行仍有下述限制。当前处置以[拓扑小规划第 9.7 节](../../plans/formal_experiment/prep_04_split_topology_equivalence_fix_plan.md#97-性能关闭后的拓扑状态)为准，本次没有新增代码或测试结果。

FACT：性能承接已提交为 `ea90d2e`、`662a730`，随后在工作区实施方案 A。`DataOrientedRoamQueues.h::SplitPriorityPrecedes` 是堆、计划、冻结串行展开及提交结果整理的共同排序定义：分数降序，同分路径编号升序。函数保持内联，非同分比较不读取节点路径；串行堆的原比较行为不变。

FACT：`TopologyPlan.cpp::PlanDataOrientedRoamSplitTopology` 对同源候选排序，先用空候选、零预算、初始优先合并和不安全首项判断前缀是否必为空。只有可能形成非空前缀时，才线性扫描真实 `SplitQueue`，找到最高优先级的 `ShouldSplitWithScore == false` 项；这一扫描短路于 2026-09-11 的独立成本修复中加入。只安排首次不安全项、停止项或预算截止之前的连续安全项，关闭前缀后仍完成全部内部/边界分类和块汇总。计划只读，不执行事务、预算交换或串行预演。

FACT：`Topology.cpp::CommittedSplit` 在内部记录提交前冻结分数；线程结束后，主线程按该分数与路径编号排序，再维护共享索引、队列与网格修改记录。节点与邻接写操作仍由既有事务负责，串行收敛正文、合并算法与评分公式未改变。工作量探测复用同一计划，`Sequence` 只保留旧快照身份用途。

FACT：新增 `DataOrientedRoamSplitPrefixTests.cpp` 覆盖同分堆顺序、首项/中途截止、预算 0/1/前缀边界、迟滞停止、优先合并及只读状态。新增 `FormalSplitTopologyEquivalenceTests.cpp` 复用版本化场景/相机和生产阶段执行，默认验证第 14 帧、受控真实多块提交、回退和后续帧；`--targets` 校验旧清单核心输入与结果，`--scan-prefixes` 扫描所有来源帧的自然多块前缀。测试依赖方向为测试 → Benchmark 输入适配/DOD，不向生产新增状态注入或实验策略接口。

FACT：第 14 帧回归保持输入 `4677929227313376363`、串行阶段结果 `1100364126311268133` 和完整网格结果 `159806467330165317`，提前提交为 0。旧 113 个目标的核心输入哈希与严格配对均通过。两后端完整构建及各 40 项 CTest 通过。六场景 384 帧自然扫描没有多块连续前缀，新发现进一步确认全部前缀为空；受控评分夹具的两块提交通过 P=2/8、W=5/R=30，但该夹具不进入研究数据。

FACT：新发现仍选出 113 个目标，与旧集合共享 111 个来源点；1,920 条阶段核心输入与相机身份均相同，162 条计划特征记录变化。旧第 14 帧特征由严格 Runner 以 `Frozen target identity mismatch` 拒绝，新目标使用独立发现目录，输入/目标/发现协议版本未变。

FACT：新全集 OpenGL W=1/R=6/P=8 的外层运行 `1d731f0b16e8-faa7b19a-15914551` 为 `pairing_complete`，113 个目标通过，保存 250 条预热、1,500 条计量和 4,000 条标定。分析程序完成完整块与来源核验，尚无三次独立短运行稳定性证据；其他独立全集与名义重复未完成，不将此单次工程验证当成 CPU gate 研究结论。

FACT：方案 A 首轮普通性能 144 个进程及一次相关配置复测 84 个进程完成。标准场景输出一致，但规划阶段稳定增加约 0.10 ms，已超过现行工程门槛；高预算辅助配置的几何变化另作成本参考。当时的 `PlanDataOrientedRoamSplitTopology` 无条件扫描真实队列，即使前缀必为空也会付出该成本。用户随后批准[独立短路修复](../../plans/formal_experiment/prep_04_split_prefix_planning_cost_fix_plan.md)，当前已按上文四个充分条件调整扫描，性能结果由该小规划记录。问题原始证据见[规划成本分析](../../reviews/formal_experiment/prep_04_split_prefix_planning_cost_analysis.md)，不能与原性能问题的暂时关闭混为一项。

FACT：短路实现后只运行三个相关 OpenGL 专项，总计 27.31 秒通过；两个代表普通场景首轮 36 个进程，仅标准规划 P95 越线后复测该场景一次 18 个进程。B/C 完整非计时字段逐帧一致，标准规划中位数 B/C 为 0.122400/0.028900 ms，配对 C−A 为 +0.004900 ms；P95 配对 C−A 仍为 +0.095875 ms，剩余非空前缀成本未关闭。两批次各五个 C 进程最慢的四个规划帧均有非空前缀，未额外插桩隔离排序与扫描。所选指标没有 C−B 新增越线，当前源码摘要 `ba6e33c4...` 及数据身份见[成本修复规划第 5 节](../../plans/formal_experiment/prep_04_split_prefix_planning_cost_fix_plan.md#5-实施与核查记录)。

UNCERTAIN：以上证据不是静态前缀的普遍等价证明，尚未证明动态子节点插队、事务失效或预算交换下的批次可交换性。拓扑规划第 5 节放宽候选没有进入生产；自然并行覆盖不足必须交给 CPU gate，不能将回退当作并行胜出。完整验证与性能结果以[拓扑修复规划第 9 节](../../plans/formal_experiment/prep_04_split_topology_equivalence_fix_plan.md#9-实现情况)的后续记录为准。

## 13. PREP-04 剩余运行与当前验收事实

FACT：2026-09-11 用户授权继续 PREP-04 后，当前源码 `ba6e33c461a833acb51ea486e67388e9fc5e79f83f782112f6676d67a3b23c56` 的 OpenGL、D3D12 各完成一次 113 目标全集；两个中预算场景各阶段首目标的 10 次 W=5/R=30/P=8 名义运行，以及同一目标两轮各三次 W=2/R=10/P=8 独立短运行全部完成。本轮没有修改生产代码或协议，只增量构建 D3D12 应用，未重跑 CTest 和普通性能矩阵。

FACT：72 个独立应用进程全部通过严格配对与来源核验，保存 4,980 条计量、874 条预热、288,000 条标定。两后端所选目标文件逐字节一致，共同目标的输入/结果哈希在策略、后端及重复配置之间一致，113 个唯一目标没有新不等价反例；72 次空计时标定均满足上限。原已知反例、受控并行正例和后续帧证据继续有效。

FACT：当前 OpenGL 全集中，合并评分和细分评分各 24/24 个目标实际 8 线程，合并拓扑 5/17 个目标实际 2 线程，细分拓扑 0/24 个目标实际并行，脏网格 24/24 个目标实际 6 或 8 线程。拓扑其余目标如实记录 `insufficient_safe_chunks` 回退，未作为并行胜负数据。

FACT：首轮短运行 22 个目标/动作中 21 个越过 5% 漂移上限；冷却 95.554 秒后完成唯一一次复测，仍有 20 个不稳定，仅两个场景的完整串行网格动作稳定。全部 10 个目标仍存在不稳定动作，已停止采样。分析器排除同一 OpenGL 执行来源的全部 259 条比较；D3D12 独立保留 161 条比较，其中 36 条因回退排除，其余只有描述性方向，没有该后端短运行稳定性证据。

FACT：完整离线分析耗时 7.969 秒，峰值 400,359,424 字节（381.81 MiB），审计的独立 RunId 与 72 次真实运行一致，源码和程序匹配归档。索引、各轮原值和来源见[收尾报告](../../../benchmark-output/prep-04-topology-fix/resume-20260911/completion-report.md)及[小规划第 11.9 节](../../plans/formal_experiment/prep_04_minimal_cpu_pairing_plan.md#119-性能关闭后的剩余收尾)。第 12 节的“独立运行未完成”仅是之前时点的记录。

UNCERTAIN：本轮证据表明同版本跨进程计时漂移，未隔离调度、频率等原因，不据此认定新的版本性能退化。用户关闭的性能问题保持关闭；采样环境验收仍未通过，因此不能把实现和记录完成写成 PREP-04 整体通过，也未作 PREP-05 研究继续决定。自然非空前缀覆盖及普遍等价的限制保持。

FACT：用户随后要求提交并作最后核查，仍不能解决时写明限制进入 P5。源码提交为 `ec8792e`；现有 60 次短运行的 CSV 摘要、预热排除、独立进程及两轮漂移逐字段重算与原报告一致，没有新增测量进程，未找到记录或计算错误。环境中记录了全 32 逻辑处理器掩码、普通优先级和平衡电源计划，未记录逐块实际核心、有效频率或抢占；根因仍不确定。

FACT：当前按用户决定结束 P4 排查、带已知限制移交 P5，旧环境验收及胜负排除没有改写。P5 的新探索判断规则尚需在该阶段规划中明确，不属于当前分析器已经实现的新能力。证据见[最后审查](../../reviews/formal_experiment/prep_04_timing_stability_final_review.md)。
