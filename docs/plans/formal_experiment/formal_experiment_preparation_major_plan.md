# 正式实验准备：代码修改大规划

> 规划类型：大规划（Major Plan）\
> 状态：PREP-01～PREP-03 已完成实现、验证与架构核查；PREP-03 代码已提交为 `4d7ab68`；PREP-04 核心实现提交为 `7522db7`，文档单独归档，完整集合的细分拓扑不等价与重复性能恶化信号阻断验收，修复方案待另行规划与确认；PREP-05～PREP-10 未开始\
> 审阅进展：2026-09-09，用户确认大规划方向与 PREP-01 小规划；随后授权 PREP-02 自主规划、实现和注释补充，并许可按 PREP-01 方式提交；本次确认 PREP-03 小规划并授权实现，验收后另行许可提交\
> 编写日期：2026-09-07\
> 上传边界澄清：2026-09-08；各后端内部复用对应上传实现，公共接口及实验性能代表性在 PREP-06/07 分别验证\
> 阶段编号与顺序：PREP-01～PREP-10 连续编号；PREP-05 为 CPU crossover pilot gate，PREP-06 起为通过 gate 后的条件性建设\
> 核对基线：`main`，`a168701`\
> 对应模块：正式实验基础设施，跨 `experiment`、`benchmark`、`app`、`algorithms`、`render` 和 `scripts`\
> 文档用途：总体实现规划与阶段实施记录；不替代正式采样结果

## 1. 规划定位与审阅入口

本规划首先交付五个 CPU 阶段的可靠配对和探索性性能交叉证据，用于判断是否值得继续研究工作负载感知策略。只有 CPU pilot gate 支持继续投入后，才补齐正式框架、重构 OpenGL/D3D12 上传组件，并扩展到上传配对、固定配置端到端验证和完整数据恢复。

首要问题是各阶段是否存在具有实际收益、可复现且能由工作负载特征解释的性能胜负反转。完整实验基础设施是通过研究投入门槛后的条件性目标，不能成为观察第一批 CPU 性能交叉结果的前置条件。PREP-05 的 CPU 探索性 pilot 与 PREP-10 的完整流程试运行是两种不同验收。

这是大规划：新增能力跨越数据契约、状态重放、图形资源生命周期和实验编排，并涉及公共接口。各实施阶段开始前，必须在本目录另写对应小规划；大规划确认不等于任意后续接口变动已获确认。

人类优先审阅以下架构提议：

1. 在 `src/experiment/formal/` 放置共享场景、清单、目标选择和运行文件契约；该层不依赖具体 DOD 状态、应用或图形后端。
2. 无窗口执行归 `src/benchmark/formal/`，运行时实验状态机归 `src/app/formal/`；`Application` 保留生命周期和帧循环接入。
3. 冻结状态、阶段执行及只读工作负载规划留在 DOD 模块；实验层通过返回值取得证据，不直接修改内部节点和队列。
4. 分别审查并提取 OpenGL 与 D3D12 后端内部的网格上传职责，使各后端的生产路径与实验路径复用对应实现。资源生命周期、同步、状态恢复和验证由各后端分别设计；独立实验资源的性能代表性须验证后才能成立，不预先确定统一的完整上传接口。
5. C++ 负责运行单元的校验和原子发布，PowerShell 负责环境采集与顺序调度，Python 负责离线统计。恢复判定只有一个权威实现。
6. 增加计时外的公共结果证据读取接口，用于比较实际计时帧与正确性重放帧。

上述架构方向已于 2026-09-09 经用户审阅无异议，PREP-01 已按确认后的小规划完成实现，代码提交为 `996c3a9`。各阶段具体文件职责、接口和协议仍在对应小规划中细化与 Review；第 13 节保留后续阶段需要落实的取舍。

## 2. 依据、优先级与前置核验

### 2.1 已阅读的规范和规划

| 文档 | 在本规划中的作用 |
| --- | --- |
| [规划指南](../plan_guideline.md) | 大规划结构、文件职责、阶段小规划与实现结果要求 |
| [开发规范](../../standards/development_guidelines.md) | 模块边界、依赖方向、中文文档、构建和验证约束 |
| [代码事实指南](../../codebase/codebase_guideline.md) | 事实可追溯、事实与目标设计分离 |
| [审查指南](../../reviews/review_guideline.md) | 后续架构审查的严重程度和证据要求 |
| [源码改造规划](../../parallel-roam/22-workload-aware-source-refactoring-plan.md) | 已有阶段 0–5 的能力与兼容边界 |
| [研究问题](../../parallel-roam/19-workload-aware-problem-definition.md)、[策略定义](../../parallel-roam/20-workload-aware-strategy-definition.md) | 合法策略、正确性与阶段边界 |
| [实验设计](../../parallel-roam/21-workload-aware-experiment-design.md)、[具体实验说明](../../parallel-roam/23-pass-crossover-experiment-specification.md) | 实验层次、性能交叉和研究继续条件 |
| [正式实验准备与执行计划](../../parallel-roam/24-formal-experiment-preparation-plan.md) | 场景、轨迹公式、清单、参数、实验轮次与统计规则 |

本规划不重新定义正式研究假设、场景坐标或统计阈值。24 号文档继续约束正式实验协议；本规划按用户意见增加正式框架投入前的 CPU 探索性 pilot，缩减前置工程范围并调整实施顺序。探索数据独立标记，不替代 24 号文档的正式性能交叉与研究继续条件。发现文档与源码不符时，以源码确定当前事实，并把需要调整的协议写为待确认事项，不能默默改变原规范。

规划建立时 `docs/codebase/` 仅有指南。PREP-01 已补齐 [CPU 策略与回放事实](../../codebase/formal_experiment/cpu_policy_and_replay_baseline.md)，区分修改前基线和当前实现；后续小规划引用该记录。本规划第 4 节只保留影响整体方案的最小索引。

PREP-02 已补齐 [CPU pilot 输入与记录事实](../../codebase/formal_experiment/cpu_pilot_input_contracts.md)，记录共享清单、NO 相机、目标引用、输入准备入口和 Python 摘要边界。后续 CPU 发现与配对优先复用该输入契约。

PREP-03 规划前补充 [CPU 阶段边界与工作负载事实](../../codebase/formal_experiment/cpu_pass_boundary_and_workload_baseline.md)，记录评分/拓扑嵌套、现有规划写入、网格元数据与几何耦合及身份限制；对应小规划待用户确认，不能把规划中的组件当作已实现。

### 2.2 本次核验对既有描述的修正

**拓扑配对存在两条不同调用链，不能混为一谈。**

- `RunDataOrientedRoamPassExperiment → RunTopologyAction → ReplayFrozen*TopologyAction`：当前串行分支直接执行 `Run*SerialConvergence`，没有调用 `BuildInterior*Chunks` 或 `CommitInterior*ChunksSerial`。串行样本为输入编号生成的候选快照也没有加入其 `wallMs`。
- `EnableTopologyPairEvidence → ReplayFrozen*TopologyPair → ExecuteFrozen*Topology`：正确性回归中的两种方式都可消费同一安全分块，串行方式使用 `CommitInterior*ChunksSerial`。

定位：[阶段配对执行器](../../../src/algorithms/data_oriented_roam/DataOrientedRoamPassExperiment.cpp)、[拓扑入口与回归](../../../src/algorithms/data_oriented_roam/DataOrientedRoamTopology.cpp)。因此，24 号文档第 1.3 节关于阶段配对串行时间的描述需要澄清；本次不把“再次实现直接串行分支”列为待完成工作。

大规划制定时的剩余事项包括：`PrepareFrozenReplayState` 会开启拓扑验证，并行冻结执行会把拓扑线程上限设置为内部最大值，评分副本继承源状态诊断开关。PREP-04 的新测量核心已绕开旧冻结准备，统一诊断隔离并保留请求线程；上述旧路径仅保留诊断兼容用途。当前真实边界见[配对契约](../../codebase/formal_experiment/cpu_pass_pairing_contracts.md)。评分和网格固定 `256` 门槛已在 PREP-01 参数化，默认值保持不变。

24 号文档第 7.1 节还保留了 `tools/cmake/bin/ctest.exe` 和 OCBT 等历史验收描述。当前使用系统 CMake/CTest，现有测试清单中没有 OCBT 测试。后续文档同步应改成当前实际构建、CPU 回归与上传验证入口，不创建已移除研究路径的测试来满足旧文字。

## 3. 目标、范围与验收标准

### 3.1 目标与范围

第一交付范围为 PREP-01～PREP-05：策略与基线、最小 CPU 输入、CPU 发现、可靠配对与最小分析，以及 CPU pilot gate 的证据和投入决定。通过 gate 并确认继续后，才扩展到 24 号文档实施 1–8 与正式第 0–5 轮所需的完整工程入口：

- 三个新增 CPU 并行下限参数和统一诊断计时约定。
- 18 个场景、三条 64 点轨迹、相机和目标清单。
- 无性能标签的工作负载发现、确定性目标选择。
- 可按场景、目标和阶段单独执行的 CPU 配对。
- 精确上传目标重建、同一后端内可检查的前置逻辑状态、资源隔离和最终读取验证；分别定义同步与计时协议，验证独立实验资源能支持的性能结论范围。
- 固定五配置的端到端计时与逐帧正确性重放。
- 环境与构建记录、文件指纹、单元发布、失败报告和中断恢复。
- 统计脚本、计时标定、试运行和正式开跑门禁。

### 3.2 非目标

- 不实施整帧离线最优参考、在线决策模型、策略迟滞或最小驻留机制。
- 不增加 `ParallelFull`、增量评分、全量拓扑或 GPU ROAM 算法。
- 不改变 ROAM 评分、候选资格、预算、强制闭合及规范化结果等价条件。
- 不重写全部 `Application`、Classic 或图形后端，不建立通用实验平台、插件系统或分布式调度器。
- 不把探索性 pilot 或完整流程试运行数据并入正式论文数据。可靠配对和可解释负面结果可以完成前半程交付；未发现足够性能交叉时，不把后续框架建设视为必须继续的工作。
- 本规划覆盖正式执行能力的准备；完整正式采样及研究结论属于随后执行任务。

### 3.3 可验证的完成条件

AC-00 由 PREP-05 验收，是前半程的投入门槛。其余条件描述保留相应研究范围并完成正式准备时的验收；不得以满足上传、恢复或整套正式矩阵条件为由，把 CPU gate 推迟到全部框架建成之后。

| 编号 | 完成条件 |
| --- | --- |
| AC-00 | 五个 CPU 阶段已有可信配对和独立重放结果，完成 PREP-05 CPU pilot gate 的继续/收缩/停止/证据不足判断；PREP-06 及上传组件重构只能在该判断支持继续并经 Review 后启动 |
| AC-01 | 默认策略保持 `256/256/256/32/160` 的对应保护值；正式 CPU 配对可将五个下限设为 0，安全回退仍有效 |
| AC-02 | 场景清单恰好 18 个唯一场景；完整相机清单恰好 1152 个采样点；两入口读取同一冻结清单 |
| AC-03 | 同逻辑相机在 OpenGL/D3D12 上姿态一致，NO/ZO 投影分别校验；实际可绘制尺寸和裁剪面符合协议 |
| AC-04 | 目标选择仅接收获准的执行前与规划期间字段；修改所有耗时及胜负列不能改变目标清单 |
| AC-05 | 五个 CPU 阶段均可独立配对；相同输入编号、预热/正式块顺序、实际策略及正确性均可检查 |
| AC-06 | 同一后端内上传两策略的前置有效内容、容量和相关元数据一致，遵循已确认的资源使用与同步协议；最终读取的顶点、索引与绘制规模一致，生产资源不被实验污染；单独验证测量对生产路径的代表性，不能仅凭内容相同判定性能条件等价 |
| AC-07 | 每次端到端计时轨迹之后执行同配置正确性重放，逐帧结果一致才允许发布该运行单元 |
| AC-08 | 原始输出绑定源码、可执行文件、资产、清单、参数、环境和配置指纹；中断及损坏文件不能误判完成 |
| AC-09 | 分析保持配对关系，区分回退、无工作、失败和平局；轨迹 C 的性能输出默认封存 |
| AC-10 | 双后端构建、现有回归、新增语义测试、默认与压力运行时验收和小规模完整试运行通过 |
| AC-11 | 每阶段有已确认小规划、实际验证结果和架构审查记录；大规划中的实现情况同步更新 |
| AC-12 | 每个小阶段实现前保存性能基线报告，实现后完成同条件性能复测和逐项比对；性能问题须单独记录分析过程与原因，由用户确认新增修复小规划或调整后续规划，修复并复测后才能关闭问题，具体要求见第 12.4 节 |

## 4. 当前架构摘要与复用依据

下表只记录影响方案的 FACT；详细流程仍以所列源码为定位依据。

| 当前事实 | 定位与可复用能力 |
| --- | --- |
| 无窗口配置解析与帮助已由 PREP-01 独立提取；场景、结果验证和既有阶段配对 CSV 仍在运行器 | [TerrainLodBenchmark.h](../../../src/benchmark/TerrainLodBenchmark.h)、[TerrainLodBenchmark.cpp](../../../src/benchmark/TerrainLodBenchmark.cpp)；复用既有配置名称与回归入口，分离新增正式职责 |
| 应用参数分流已独立；运行时路径推进、预热、算法轮换及结果采样仍由 `Application` 驱动 | [ApplicationCommandLine.h](../../../src/app/ApplicationCommandLine.h)、[Application.cpp](../../../src/app/Application.cpp)、[RuntimeBenchmarkConfig.h](../../../src/app/RuntimeBenchmarkConfig.h)；正式配置不能继续经过面板钳制和内置压力路径覆盖 |
| 两种实验报告共用设置和统计 CSV 写入，当前公共 schema 为 `4`，无窗口 CPU 配对独立 schema 为 `2` | [TerrainLodExperimentCsv.h](../../../src/experiment/TerrainLodExperimentCsv.h)、[实现](../../../src/experiment/TerrainLodExperimentCsv.cpp)；它不是清单解析器或运行调度器 |
| PREP-02 新增独立 pilot v1 的场景/相机/目标与最小记录，六个 A 场景生成 384 个 NO 点；准备脚本核对实际 SHA-256 并记录来源与环境 | [输入事实](../../codebase/formal_experiment/cpu_pilot_input_contracts.md)；`cpu-pilot-inputs` 不执行发现或策略配对，`inputs_ready` 仅表示输入准备完成 |
| DOD 已有持久状态深复制和五阶段配对；副本借用源高度图和线程池，容器状态独立 | [DataOrientedRoamState.cpp](../../../src/algorithms/data_oriented_roam/DataOrientedRoamState.cpp)、[配对接口](../../../src/algorithms/data_oriented_roam/DataOrientedRoamPassExperiment.h)；复制后不能并发释放或推进其来源依赖 |
| 正式串行阶段推进、队列刷新、拓扑回放和网格更新已有可复用入口 | [Topology.h](../../../src/algorithms/data_oriented_roam/DataOrientedRoamTopology.h)、[Queues.h](../../../src/algorithms/data_oriented_roam/DataOrientedRoamQueues.h)、[MeshEmit.h](../../../src/algorithms/data_oriented_roam/DataOrientedRoamMeshEmit.h) |
| OpenGL 上传当前直接使用 VBO/IBO；D3D12 使用持久映射的上传堆，并维护两个帧槽的版本和积压区间 | [TerrainRenderer.cpp](../../../src/render/TerrainRenderer.cpp)、[D3D12TerrainRenderer.cpp](../../../src/render/D3D12TerrainRenderer.cpp)；不能假定 D3D12 当前经过默认堆复制 |
| 两后端已有同 CPU 数据包的 AB/BA 重放，且各自调用对应生产路径的 `UploadMeshData`；尚无逐策略恢复前置逻辑状态和最终内容读取契约 | 两后端的 `ReplayCurrentCpuUploadPair`；已有后端内部代码复用，D3D12 仅保存/恢复部分积压元数据，不能据此推断资源已隔离或测量可代表生产条件 |
| D3D12 在帧开始时等待当前帧槽，渲染入口还可能补做该帧槽的网格版本同步 | [D3D12GraphicsBackend.cpp](../../../src/render/D3D12GraphicsBackend.cpp) 的 `BeginFrame`/`WaitForFrame` 与 `D3D12TerrainRenderer.cpp` 的 `Render`；同步和延后上传不能被一个通用上传计时字段自动覆盖 |
| 统一算法适配器已执行公共结果检查；完整证据扫描由显式开关控制 | [统一接口](../../../src/algorithms/ITerrainLodAlgorithm.h)、[结果检查](../../../src/algorithms/TerrainLodResultValidation.h)、Classic/DOD 适配器 |

本会话此前在该基线上完成双后端 `RelWithDebInfo` 构建和 21 项 CTest，均通过。该结果只作为规划前历史基线。PREP-01 实施后两后端各 24/24 CTest 通过，详情及原始日志见小规划第 8 节，仍不代替正式运行环境冻结。

## 5. 目标架构与依赖方向

### 5.1 分层

```text
PowerShell 环境采集与批量顺序调度
  ├─ 无窗口入口 → Benchmark/Formal
  └─ 图形应用入口 → App/FormalRuntimeBenchmarkSession

Benchmark/Formal ────────→ Experiment/Formal 共享数据与运行文件契约
  └─ DOD 阶段执行器        ↑
App/Formal ───────────────┘
  ├─ 公共 LOD 接口与计时外结果证据
  └─ Render 上传实验编排
       ├─ OpenGL 实验适配 → OpenGL 网格上传组件 → OpenGL API
       └─ D3D12 实验适配 → D3D12 网格上传组件 → D3D12 API/后端帧同步

Experiment/Formal → 公共 LOD 类型、Terrain、Tools
DOD → 公共 LOD 类型、Terrain、Tools、DOD 内部组件
各后端生产渲染器 → 各自的网格上传组件
各后端网格上传组件 → 公共网格/上传类型、Tools、对应图形 API/后端服务

已发布 CSV/清单 → Python 统计与报告
```

命名空间继续遵循 `ParallelRoam::<目录>`，正式子模块使用 `Experiment::Formal`、`Benchmark::Formal`、`App::Formal`；不创建与目录无关的全局服务定位器。

### 5.2 依赖禁区

- `Experiment/Formal` 不包含 `DataOrientedRoamState.h`、`Application.h`、SDL、OpenGL 或 D3D12 头文件。
- DOD 不读取 CSV、运行目录或 `runId`，不依赖 `Benchmark`、`App` 或 `Experiment/Formal`。
- 各后端网格上传组件不读取目标清单、不选择研究样本、不决定 LOD 策略；公共上传数据不包含图形资源句柄、帧槽或 fence 状态。后端间不相互依赖上传实现。
- Python 不重新实现相机公式、DOD 状态重放或正式目标选择算法。
- PowerShell 不推导正确性、重算策略胜负或另写一套配置指纹/恢复校验。
- 既有普通实验与交互路径关闭正式会话时，不生成清单、不复制实验缓冲区、不扫描发现特征。

## 6. 组件、职责与所有权

| 组件及选择 | 所属位置 | 职责与上下游 | 所有权和边界 |
| --- | --- | --- | --- |
| **Create** 正式数据契约 | `experiment/formal` | 定义场景、相机行、目标引用、运行单元键和记录类型，供两个入口共享 | 值类型和只读集合；不持有算法、窗口或 GPU 资源 |
| **Create** 清单读写与校验 | `experiment/formal` | CSV 解析、列/版本/关系验证、资产与清单指纹；产生不可变输入 | 启动时加载一次；不在计时区解析文件 |
| **Create + Reuse** 相机生成器 | `experiment/formal` | 实现 24 号文档 A/B/C 公式，复用公共视图构建和高度图采样 | 公式只在生成和启动校验时运行，正式逐帧读取冻结行 |
| **Create** 确定性目标选择器 | `experiment/formal` | 按分层、固定散列和特征空间距离选择目标 | 输入是允许字段的窄类型；接口中没有耗时、胜负或执行后提交数 |
| **Create** 运行文件存储 | `experiment/formal` | 单元键、配置指纹、临时文件、完成校验、原子发布、恢复检查 | 一个执行进程独占一个单元；不管理线程或训练模型 |
| **Extend + Wrap** CPU 配对执行器 | DOD 与 `benchmark/formal` | DOD 负责复制、执行和验证阶段；上层负责清单映射、场景推进和输出 | 源流水线、高度图、线程池覆盖全部副本生命周期；同一池的策略按序执行 |
| **Create + Extend** 工作负载探测与规划 | DOD | 在真实阶段边界读队列、候选和网格槽位规划，返回语义化特征 | 源状态只读；临时规划数据由调用持有，释放后不影响生产流水线 |
| **Create** 正式运行时会话 | `app/formal` | 管理正式预热、计时、证据、正确性重放、上传发现/配对和结束状态 | `Application` 以 `unique_ptr` 持有；会话借用渲染器，不能晚于图形上下文销毁 |
| **Split + Reuse** 各后端网格上传组件 | `render/upload` | OpenGL 与 D3D12 分别提取资源、写入和上传统计，供各自生产路径与实验路径调用；D3D12 保留帧槽版本与积压语义 | 资源所有权和安全访问条件分别定义；帧推进、队列提交及 fence 管理继续归图形后端，不强行归入公共上传组件 |
| **Create + Wrap** 上传配对编排与后端适配 | `render/upload` | 共用配对顺序、参数和结果组织；通过各后端适配执行逻辑状态重建、上传与最终读取 | 自有 CPU 数据包快照；优先隔离实验资源，具体资源所有权、同步和销毁由对应后端定义，性能代表性另行验收 |
| **Extend** 公共结果证据读取 | `algorithms` 及两适配器 | 在帧计时结束后读取预算、队列、拓扑和规范化网格证据 | 同步、无拓扑修改；禁止在进行中的算法任务上调用 |
| **Create** 统计模块 | `scripts/formal_experiments` | 数据审计、配对统计、性能交叉判定、中文报告 | 只读原始数据，输出写入 `analysis/`；留出性能默认不参与 |

不新增冻结状态缓存。一次运行单元内可以按采样编号连续推进根流水线；重新执行单元时从根重建。是否加入跨目标缓存属于后续独立决策，不能改变输入来源。

## 7. 目录与文件规划

### 7.1 目标目录

下列 `.h/.cpp` 表示同名声明与实现文件，两者组成同一职责组件；不是一个实际扩展名。这是通过 CPU gate 后的条件性完整目标结构，不是 gate 前的创建清单。只创建对应实施阶段需要的文件，不预建空目录或占位类；前置文件范围见第 7.5 节。

```text
docs/
  plans/formal_experiment/
    formal_experiment_preparation_major_plan.md
    prep_01_policy_and_baseline_plan.md
    prep_02_minimal_input_plan.md
    prep_03_workload_discovery_plan.md
    prep_04_minimal_cpu_pairing_plan.md
    prep_05_cpu_crossover_pilot_gate_plan.md
    prep_06_runtime_and_upload_foundation_plan.md
    prep_07_upload_and_fixed_baseline_plan.md
    prep_08_analysis_plan.md
    prep_09_orchestration_and_resume_plan.md
    prep_10_pilot_and_acceptance_plan.md
  codebase/formal_experiment/                 后续按代码事实指南记录
    cpu_policy_and_replay_baseline.md         PREP-01 已建立并同步
  reviews/formal_experiment/                  后续按审查指南记录
  parallel-roam/
    formal-experiment-scenarios-v1.csv         沿用 24 号文档约定的资产位置
src/
  tools/
    Sha256.h/.cpp
  experiment/
    ExperimentCsvCodec.h/.cpp
    TerrainLodExperimentCsv.h/.cpp             现有
    formal/
      FormalExperimentTypes.h
      FormalExperimentRecords.h
      FormalExperimentManifest.h/.cpp
      FormalExperimentCamera.h/.cpp
      FormalExperimentTargetSelector.h/.cpp
      FormalExperimentFingerprint.h/.cpp
      FormalExperimentRunStore.h/.cpp
      FormalExperimentCsv.h/.cpp
  benchmark/
    TerrainLodBenchmarkCommandLine.h/.cpp
    formal/
      FormalExperimentRunner.h/.cpp
      FormalWorkloadDiscovery.h/.cpp
      FormalCpuPassBenchmark.h/.cpp
      FormalTimingCalibration.h/.cpp
  app/formal/
    FormalRuntimeBenchmarkSession.h/.cpp
    FormalRuntimeBenchmarkReport.h/.cpp
  algorithms/
    TerrainLodResultEvidence.h
    data_oriented_roam/
      DataOrientedRoamPassExecution.h/.cpp
      DataOrientedRoamTopologyPlan.h/.cpp
      DataOrientedRoamMeshPlan.h/.cpp
      DataOrientedRoamWorkloadProbe.h/.cpp
  render/upload/
    CpuMeshUploadTypes.h
    ICpuMeshUploader.h                       候选窄接口，PREP-06 论证后才决定是否创建
    OpenGlCpuMeshUploader.h/.cpp
    D3D12CpuMeshUploader.h/.cpp
    CpuMeshUploadReplay.h/.cpp
scripts/
  run_formal_experiments.ps1
  analyze_pass_crossover.py
  formal_experiments/
    __init__.py
    input_validation.py
    paired_statistics.py
    crossover_analysis.py
    report_writer.py
tests/
  Sha256Tests.cpp
  ExperimentManifestTests.cpp
  ExperimentCameraTests.cpp
  ExperimentTargetSelectorTests.cpp
  ExperimentRunStoreTests.cpp
  TerrainLodBenchmarkCommandLineTests.cpp
  DataOrientedRoamPassPolicyTests.cpp
  TerrainLodBenchmarkPolicyTests.cmake
  FormalExperimentCommandLineTests.cpp
  DataOrientedRoamPassExperimentTests.cpp
  TerrainCpuUploadReplayTests.cpp
  FormalRuntimeBenchmarkSessionTests.cpp
  test_pass_crossover_analysis.py
  fixtures/formal_experiment/                 仅放确定性小型清单/错误输入
```

### 7.2 新增和提取文件的职责

| 操作 | 文件或同名文件组 | 关键内容 | 独立存在理由 |
| --- | --- | --- | --- |
| Create | `src/tools/Sha256.h/.cpp` | 字节流与文件 SHA-256 | 当前源码没有可直接复用的 C++ SHA-256；摘要算法属于通用工具，不属于场景或统计。优先纯 C++、固定向量验证，不增加第三方依赖 |
| Create | `src/experiment/ExperimentCsvCodec.h/.cpp` | 引号、转义、行列诊断、UTF-8/LF 编解码 | 三类清单和正式记录共享词法规则；不把列语义放入通用编解码器 |
| Create | `FormalExperimentTypes.h` | `FormalScenario`、`CameraSample`、`TargetStateRef`、`ExperimentUnitKey`、已解析请求 | 稳定输入与身份契约；不混入大批统计结果字段 |
| Create | `FormalExperimentRecords.h` | 发现、CPU 配对、上传、计时标定与单元完成记录 | 输出格式独立于运行选项；只包含公共标量/值类型，不暴露 DOD/图形状态 |
| Create | `FormalExperimentManifest.h/.cpp` | 三类清单加载、关系校验、规范化输出 | 负责外部输入可信边界，不负责状态重放 |
| Create | `FormalExperimentCamera.h/.cpp` | A/B/C 生成、姿态及两种投影校验 | 两入口共用唯一实现，避免不同插值或角度转换 |
| Create | `FormalExperimentTargetSelector.h/.cpp` | 分层选择、覆盖补点、覆盖不足报告 | 选择算法与文件 IO、性能统计分离，支持无耗时输入测试 |
| Create | `FormalExperimentFingerprint.h/.cpp` | 规范化配置编码、资产/二进制/清单/环境身份组合 | 区分文件摘要算法与实验身份规则；避免多个入口各自拼接散列 |
| Create | `FormalExperimentRunStore.h/.cpp` | 临时输出、单元占用、完成侧车、恢复校验 | 文件生命周期独立于实验运行器；统一 C++ 与脚本的完成判定 |
| Create | `FormalExperimentCsv.h/.cpp` | 正式记录 schema、表头及类型写入 | 普通统计 CSV 不承担全部正式数据类型；版本独立且显式绑定 |
| Split | `TerrainLodBenchmarkCommandLine.h/.cpp` | 从原入口提取参数解析、互斥检查及帮助文本 | 便于测试错误输入，避免正式参数继续堆入运行算法的文件 |
| Create | `FormalExperimentRunner.h/.cpp` | 校验请求、选择实验入口、建立运行单元及统一错误退出 | 只做上层编排，不实现各轮算法 |
| Create | `FormalWorkloadDiscovery.h/.cpp` | DOD 顺序重建、探测结果转换、目标选择调用 | 发现不执行策略计时；与 CPU 配对分离 |
| Create | `FormalCpuPassBenchmark.h/.cpp` | 目标到真实阶段边界的映射、调用 DOD 配对、逐单元输出 | 不直接读写 DOD 节点或实现重复拓扑事务 |
| Create | `FormalTimingCalibration.h/.cpp` | 空计时、无工作阶段及短基准标定记录 | 环境准入与正式性能记录具有不同语义和输出 |
| Create | `FormalRuntimeBenchmarkSession.h/.cpp` | 正式运行状态机、场景/相机输入、结果比较和上传目标编排 | 从 `Application` 隔离新增跨帧实验职责；不复制普通 GUI 实验全部逻辑 |
| Create | `FormalRuntimeBenchmarkReport.h/.cpp` | 渲染统计/上传样本到正式公共记录的转换 | 适配层可依赖 `Render`，共享记录层不反向依赖渲染器 |
| Create | `TerrainLodResultEvidence.h` | `TerrainLodResultEvidence`：结果哈希、计数、已执行检查及失败掩码 | 证据读取接口不返回可变算法状态，也不继续扩大计时统计结构 |
| Split + Extend | `DataOrientedRoamPassExecution.h/.cpp` | 真实五阶段边界、共同串行推进与执行调用 | 生产流水线和冻结构造共享物理执行边界，避免各维护一条近似流程 |
| Split + Extend | `DataOrientedRoamTopologyPlan.h/.cpp` | 候选快照、稳定排序、安全分块与分类结果 | 规划可供生产并行、发现和诊断复用；不提交拓扑、不处理预算最终收敛 |
| Split + Extend | `DataOrientedRoamMeshPlan.h/.cpp` | 修改记录到槽位迁移/脏集合/区间的规划 | 在顶点写入前取得预计脏特征；不能通过先完成整次网格写入伪造执行前特征 |
| Create | `DataOrientedRoamWorkloadProbe.h/.cpp` | 各阶段边界的窄特征结果与采集成本 | DOD 内部解释状态，外层只转换返回值；不让发现器读取所有内部字段 |
| Split | `CpuMeshUploadTypes.h` | 公共 CPU 数据包描述、策略请求、结果及回退类型 | 提取两后端确实共用的值类型；不统一图形资源、快照内部表示或同步模型，兼容原包含入口 |
| Create（候选） | `ICpuMeshUploader.h` | 仅在 PREP-06 证明必要时定义语义一致的窄上传契约 | 不预设统一的上传/快照/恢复/读取接口；可改为两个后端适配直接接入公共配对编排，最终选择写入小规划 |
| Split + Extend | `OpenGlCpuMeshUploader.h/.cpp` | OpenGL 网格缓冲的容量与写入；明确上下文、绑定和资源所有权，与 OpenGL 实验适配衔接 | 同一 OpenGL 实现供生产与实验复用；恢复/读取路径按该后端设计，独立缓冲的使用历史及驱动同步差异另行验证 |
| Split + Extend | `D3D12CpuMeshUploader.h/.cpp` | D3D12 上传堆、持久映射、帧槽版本/积压及写入；与后端帧同步和实验适配衔接 | 同一 D3D12 实现供生产与实验复用，保留当前上传堆语义；不新增默认堆管线，也不接管后端队列/fence 所有权 |
| Create | `CpuMeshUploadReplay.h/.cpp` | AB/BA 配对块、计时记录、验证结果组织及后端适配调用 | 共用实验编排；资源恢复、读取、同步与销毁的实现属于对应后端，适配文件边界在 PREP-06 小规划明确，不管理场景文件或相机 |
| Create | `run_formal_experiments.ps1` | 环境快照、逐进程执行、顺序轮换、日志和失败清单 | 只调度串行性能任务；不复制单元完成校验 |
| Create | `analyze_pass_crossover.py` | 分析 CLI | 入口参数与统计计算分离 |
| Create | `formal_experiments/__init__.py` | Python 包标识 | 支持入口和测试使用相同模块导入路径 |
| Create | `input_validation.py` | 配对键、版本、完整性、分组和回退检查 | 错误数据不能进入统计函数 |
| Create | `paired_statistics.py` | 百分位、配对 bootstrap、区域重采样和多重比较 | 不依赖项目文件布局，可用合成数据验证 |
| Create | `crossover_analysis.py` | 平局、获胜区域、稳定交叉及训练集阈值分析 | 研究判定与底层统计计算分离；不自动调整实验清单 |
| Create | `report_writer.py` | 中文 Markdown 和派生 CSV | 表现层不修改原始记录或重新计算另一套统计 |

表中未写完整前缀的正式 C++ 文件分别位于第 7.1 节标明的目录。

### 7.3 现有文件的修改边界

| 操作 | 文件 | 计划修改与理由 |
| --- | --- | --- |
| Modify | `src/algorithms/TerrainLodPassTrace.h` | 增加三个独立数量下限，默认各为 256；更新预设/散列相关公共策略处理 |
| Modify | `src/algorithms/ITerrainLodAlgorithm.h` | 输入散列覆盖新策略字段；增加计时外只读证据接口。正式场景字段不全部塞进 `TerrainLodSettings` |
| Reuse / 必要时 Modify | `src/algorithms/data_oriented_roam/DataOrientedRoamTypes.h` | 当前已整体保存公共策略；PREP-01 核验字段传递，不复制新增下限，后续统计扩展按对应阶段需要处理 |
| Extend | DOD `DataOrientedRoamParallel.h` | PREP-01 在既有并行辅助中共用评分/网格的数量解析；动作判断留在阶段文件，拓扑安全决策不迁入此处 |
| Modify/Split | DOD `Queues.cpp`、`Topology.h/.cpp`、`MeshEmit.h/.cpp`、`Pipeline.cpp` | 参数化门槛，提取规划/执行边界，复用原队列、事务、预算和槽位逻辑 |
| Modify | DOD `PassExperiment.h/.cpp` | 增加阶段筛选、统一诊断模式、请求线程传递和结果输出；保留既有回归入口 |
| Modify | DOD `State.h/.cpp`、`Validation.h/.cpp` | 仅在新增规划状态或证据读取确有需要时调整复制/检查接口；线程池借用关系不改变 |
| Modify | 两种 `*TerrainLodAlgorithm.h/.cpp` | 实现公共只读证据接口，保持现有 `BuildRenderData` 结果检查 |
| Modify | `classic_roam/ClassicRoamMeshBuilder.h/.cpp`、`ClassicRoamValidation.cpp` | 为适配器提供当前状态的证据读取；不修改 Classic 策略与事务 |
| Modify | `src/experiment/TerrainLodExperimentCsv.h/.cpp` | 新门槛进入普通统计设置列并递增公共 schema；既有数据不能被新表头解释 |
| Modify/Split | `src/benchmark/TerrainLodBenchmark.h/.cpp` | 提取解析，注册正式配置，旧配置继续路由原回归逻辑 |
| Modify | `src/app/ApplicationCommandLine.h/.cpp`、`RuntimeBenchmarkConfig.h` | 区分普通覆盖与正式请求，传递清单/算法/预算/输出和重复编号；解析不执行实验 |
| Modify | `src/app/Application.h/.cpp` | 持有并推进正式会话；正式输入绕过 GUI 钳制与内置压力参数；释放会话早于图形资源 |
| Modify | `src/app/RuntimeBenchmark.h/.cpp` | 复用已有运行时记录和中文报告能力，仅同步共享字段，不放入正式目标选择 |
| Modify/Split | `src/render/TerrainRenderer.h/.cpp`、`D3D12TerrainRenderer.cpp` | 分别委托各自后端的网格上传组件；按对应协议接入目标包/前置逻辑状态捕获及正式视图输入，保留普通渲染契约和后端特有的绑定、帧推进边界 |
| Modify（必要时） | `src/render/D3D12GraphicsBackend.h/.cpp` | 复用现有设备、围栏和帧槽能力，保留队列提交与同步所有权；`ExecuteImmediate` 仅在验证其调用时机和等待语义后用于计时外验证，确有缺口时增加窄接口 |
| Modify | `CMakeLists.txt`、`tests/CMakeLists.txt` | 注册新文件与测试；共享纯 CPU 组件不链接图形依赖；只抽取本次共享目标，不重构全部构建系统 |
| Modify | 既有 CSV、命令行、结果验证测试 | 对新增字段和公共契约补边界断言，保留原测试覆盖 |
| Create/Modify | `docs/parallel-roam/formal-experiment-scenarios-v1.csv`、24 号文档及 README | 实施时落地唯一场景清单，澄清失实调用链、补参数与实际命令；实验协议变化先确认 |

不计划删除旧回归入口、历史原始 CSV 或改动第三方库。提取完成后删除原文件内已迁出的重复定义与函数体，保留必要兼容声明，不在同一后端长期维护生产/实验两份上传逻辑，也不重复阶段规划逻辑；OpenGL 与 D3D12 各自的上传实现继续独立。

### 7.4 测试文件职责

| 文件 | 独立验证对象 |
| --- | --- |
| `Sha256Tests.cpp` | 标准摘要向量、分块输入、空文件和二进制文件一致性 |
| `ExperimentManifestTests.cpp` | CSV 词法、schema、引用关系、资产/尺寸错误和正式参数冲突 |
| `ExperimentCameraTests.cpp` | A/B/C 固定点、浮点往返、逻辑姿态及 NO/ZO 约定 |
| `ExperimentTargetSelectorTests.cpp` | 分层、并列、覆盖补点、零分母、少于 8 点和耗时污染隔离 |
| `ExperimentRunStoreTests.cpp` | 配置敏感性、中断、重复单元、文件截断/篡改、发布和恢复 |
| `TerrainLodBenchmarkCommandLineTests.cpp` | PREP-01 的既有无窗口参数提取、新下限、帮助和错误输入 |
| `DataOrientedRoamPassPolicyTests.cpp` | PREP-01 的策略默认、数量边界、配置哈希及真实 DOD 参数传递/执行 |
| `TerrainLodBenchmarkPolicyTests.cmake` | PREP-01 的进程级参数到普通/配对 CSV、预设覆盖与退出行为 |
| `FormalExperimentCommandLineTests.cpp` | 后续 CPU/运行时正式参数组合及未知、不合法、冲突参数 |
| `DataOrientedRoamPassExperimentTests.cpp` | 五阶段真实边界、诊断隔离、线程请求、源状态不变和规范化结果 |
| `TerrainCpuUploadReplayTests.cpp` | 公共编排的块顺序、失败传播和验证结果处理；两后端逻辑状态重建、资源安全与性能代表性分别注册集成验证，模拟测试不证明 GPU 同步等价 |
| `FormalRuntimeBenchmarkSessionTests.cpp` | 预热/重置/计时/证据/验证/结束状态转换和输入权威性 |
| `test_pass_crossover_analysis.py` | 已知胜负、平局、回退、伪重复、留出隔离和错误配对的统计结果 |
| `fixtures/formal_experiment/` | 小型有效/错误清单及固定期望数据；不存大规模运行结果 |

### 7.5 CPU gate 前后的文件投入边界

| 范围 | PREP-01～PREP-05 的投入 | PREP-06～PREP-10 的条件性投入 |
| --- | --- | --- |
| 共享输入 | PREP-02：轨迹 A 的 CPU 场景子集、NO 相机输入、五 CPU 阶段目标与最小记录；PREP-05 仅在一次补测需要时扩展轨迹 B | PREP-06：全部 18 场景/1152 点、ZO/上传清单和图形输入关系，复用同一输入实现 |
| 可追溯性 | PREP-02：使用 PowerShell `Get-FileHash` 或 Python `hashlib` 在计时外记录源码/二进制/资产/清单摘要、命令和环境；PREP-05 的每次重放使用新目录 | PREP-09：C++ `Sha256`、`FormalExperimentFingerprint`、完整 `FormalExperimentRunStore`、恢复与原子发布 |
| 算法与执行 | PREP-01/03/04：下限参数、诊断隔离、线程控制、阶段边界和规划特征；复用现有冻结执行器 | PREP-07：公共端到端证据读取；PREP-09：同一 CPU 执行器与正式 CLI、全目标矩阵及单元存储集成 |
| 数据输出 | PREP-02/03/04：`FormalExperimentCsv` 的 CPU 发现/配对最小列集，显式 `dataPurpose=exploratory`，保留失败和完整性摘要 | PREP-06：上传发现字段；PREP-07：完整正式 schema；PREP-09：完成侧车发布、聚合与恢复校验 |
| 分析 | PREP-04：在 `analyze_pass_crossover.py` 和统计模块中实现配对检查、中位差、噪声/收益判断和简单分组；PREP-05：产出 gate 判断与报告 | PREP-08：扩展同一模块，实现完整 bootstrap/Holm、正式阈值/二维表、上传和端到端统计；PREP-09 接入完整运行目录校验 |
| 渲染与调度 | PREP-01～PREP-05：直接顺序调用 CPU CLI，不创建 `render/upload` 或 `app/formal` | PREP-06：各后端内部上传提取与正式运行时；PREP-07：按后端协议上传配对；PREP-09：完整 PowerShell 调度与恢复；PREP-10：完整流程验收 |

Gate 前不要求实现自动恢复；中断后保留原始输出，使用新尝试目录重跑该单元，缺行或失败数据不得参与判定。允许缩减工程规模，不允许缩减同输入配对、实际线程记录、完整阶段计时和结果等价检查。

## 8. 关键接口和数据契约

以下是条件性完整架构契约，不预定义全部 helper；具体签名在对应小规划中确认。PREP-02～PREP-04 只实现第 7.5 节 CPU 子集：pilot 模式校验已选场景的完整输入，不强制先通过 18 场景、上传与正式恢复契约。模式必须显式区分，不能把不完整 pilot 输出标为正式完成。

### 8.1 场景、相机和目标

- `LoadScenarioManifest(path) → ValidatedScenarios`：严格校验列、唯一身份、18 场景矩阵、资产摘要及实际尺寸。允许通过请求过滤场景，不通过删减正式清单冒充完整矩阵。
- `GenerateCameraManifest(scenarios) → CameraSamples`：使用 binary32 运算和 9 位有效数字输出。逻辑姿态哈希与预算无关，视图输入按后端深度约定区分。
- `LoadCameraManifest(path, scenarios) → ValidatedCameraSamples`：重建校验后返回已解析行；正式逐帧采用行中的 position/target/up 和投影配置，不能通过 yaw/pitch 再生成略有不同的矩阵。
- `SelectTargetStates(allowedFeatures, seed) → TargetSelection`：输入类型不含 `wallMs`、获胜策略、实际提前提交数或实际串行尾部；结果附唯一目标及覆盖不足原因。
- `LoadTargetManifest(path, scenarios, cameras) → ValidatedTargets`：检查阶段、采样编号、分组与引用哈希；CPU 阶段与上传目标具有明确身份，不能把上传混入五阶段筛选枚举。

发现与正式记录的特征字段统一使用 `pre_`、`planning_`、`post_` 前缀。候选分类等在规划期间才取得的字段不得标为 `pre_`；`featureCollectionMs` 只描述采集成本，不能传给目标选择器。建议目标清单中上传阶段使用 `cpu-upload`，而无窗口 `--pass-id` 仍只接受既有五个 CPU 阶段和 `all`；该值在 PREP-06 扩展上传清单 schema 时确认。

相机清单现有单列 `viewInputHash` 的跨后端解释需要确认：建议该列固定表示 NO 输入，并补 `viewInputZoHash`；不能让同一个字段在不同入口指代不同内容。此项属于清单 schema 明确化，见第 13 节。

### 8.2 算法与冻结执行

- `ExecutePass(state, passId, action, executionContext)`：仅执行一个已满足前置条件的物理阶段；统计范围对应实际生产操作，不重复刷新前一阶段队列。
- `ProbePassWorkload(const state, passId) → WorkloadFeatures`：必要的候选/槽位规划在隔离工作区完成，不改变源队列、预算或网格版本；返回字段标记为执行前或规划期间。
- `RunDataOrientedRoamPassExperiment(previousState, nextView, settings, config)`：扩展 `config` 的阶段筛选；未测量的前置阶段仍按串行基线推进，以到达正确边界。
- `ITerrainLodAlgorithm::CaptureResultEvidence(...)`：读取当前已完成状态的规范化拓扑、活动叶、网格和不变量证据；报告检查是否实际执行，不能以全零未检查计数冒充通过。

正式副本设置 `EnablePassEvidence=false`、`EnableTopologyValidation=false`、`EnableTopologyPairEvidence=false`；诊断回归仍可独立开启。执行函数不得为了重放而重新强制打开诊断或覆盖请求线程数。验证和哈希在停表后通过明确调用完成。

### 8.3 各后端上传边界与实验契约

复用范围为各后端内部：OpenGL 生产路径和实验路径调用同一份 OpenGL 上传实现；D3D12 生产路径和实验路径调用同一份 D3D12 上传实现。两后端分别拥有资源表示、绑定方式和生命周期约束。

`ICpuMeshUploader` 仅为候选窄接口。PREP-06 先确定两后端的职责与调用边界，再判断是否有语义一致、值得抽象的操作；不预设一个统一上传、快照、恢复和读取的完整接口。公共层可以复用 CPU 输入值类型、策略请求、配对顺序和结果记录，后端适配负责执行各自协议。

| 职责 | OpenGL 边界 | D3D12 边界 |
| --- | --- | --- |
| 上传资源与写入 | 保留缓冲对象、容量、使用标志及 `glBufferData/glBufferSubData` 路径；明确上下文与 VAO/缓冲绑定归属 | 保留持久映射上传堆、各帧槽缓冲、版本与积压区间；明确资源视图和映射指针生命周期 |
| 安全访问与推进 | 记录缓冲是否被在途绘制使用；不能假定驱动内部状态可被快照复制 | 图形后端拥有帧推进、队列提交和 fence；上传组件只在明确可写的帧槽上执行，并保留延后同步语义 |
| 实验适配 | 分别设计资源重建、使用历史、读取验证及绑定状态处理 | 分别设计帧槽逻辑状态重建、安全访问、读取验证及资源回收 |
| 性能解释 | 识别 CPU API 调用内可能包含的驱动同步成本 | 区分 CPU 写入、帧开始等待和后续帧槽补传成本 |

以下是实验所需能力及责任约束，不是已经确定的公共接口方法表：

| 操作 | 计时归属 | 所有权与约束 |
| --- | --- | --- |
| 创建/预留实验资源 | 计时外 | 优先使用独立资源；容量、资源类型和使用标志按对应生产路径设置，资源隔离本身不证明性能条件等价 |
| 捕获上传前逻辑状态 | 计时外 | 在目标包应用前记录有效内容、容量、长度及后端相关版本/帧槽元数据；不声称捕获了 GPU 执行进度或驱动隐藏状态 |
| 重建前置条件 | 计时外 | 各后端按已确认协议建立逻辑状态和安全访问条件；必要等待记录其位置与成本，不能仅为避开计时省略同步 |
| 应用目标数据包与指定策略 | 后端协议定义的 `wallMs` 包络内 | 复用该后端生产上传实现，记录实际方式、字节、区间与回退；API 内部产生的同步成本保留在实测值中 |
| 最终读取验证 | 计时外独立验证轮次 | 每策略另做恢复/上传/实际资源读取，比较有效内容；使用该后端的读取与同步方法，不以 CPU 镜像代替资源证据 |
| 销毁实例 | 计时外 | OpenGL 满足上下文与线程要求；D3D12 保证资源不再被 GPU 引用，按对应后端生命周期释放 |

CPU 数据包快照深复制顶点、索引、范围和有效绘制规模，不保留下一次算法更新会失效的借用指针。容量未使用尾部可能未初始化，验证比较有效内容并单列容量；不能把未定义尾部或 C++ 结构体 padding 纳入结果哈希。

D3D12 逻辑状态重建覆盖全部相关帧槽的有效内容、容量、版本、积压及全局网格版本；保存这些字段不等于恢复 fence 或 GPU 时间线。CPU 镜像可辅助重建，最终正确性证据仍须来自实际资源读取。现有 `ExecuteImmediate` 是否适用，须先验证调用时机、命令提交和等待语义，不预先承诺统一读取路径。

**测量代表性必须独立验证。** 未被绘制消费的 OpenGL 实验缓冲，可能与生产缓冲有不同的同步成本；D3D12 的当前帧槽写入，也不能自动覆盖帧开始等待和后续帧槽补传。PREP-06 确定各后端协议及验证方案，PREP-07 验证结果和适用范围。记录后端、测量范围及协议版本，分别报告 CPU 上传调用、相关等待和延后工作；不能把 `wallMs` 直接解释为 GPU 传输完成耗时或跨后端同等工作量。

若独立资源只能支持受控上传微基准，应明确该结论范围，并通过固定配置端到端验证评估实际收益；代表性不足时修订对应上传实验协议，不将逻辑状态一致直接写成生产性能等价。CPU pilot 的阶段和验收不受此后续设计影响。

### 8.4 运行文件与身份

`ExperimentUnitKey` 至少包含：运行轮次、场景、后端、阶段或配置、轨迹重复编号；CPU 配对单元为一个“场景 × 阶段”，上传为“场景 × 后端”，端到端为“场景 × 配置 × 重复”。每条行记录另含目标编号和策略重复编号。

- `BeginUnit(key, fingerprint)`：验证运行元数据、输入、占用和已有输出。未指定恢复时，已有完成单元应报错，不能覆盖。
- `CanResumeUnit(key, fingerprint)`：重算相关文件摘要、预期行数和正确性关系；完成标记本身不构成充分条件。
- `CommitUnit(rows, validation)`：关闭临时文件，检查完整性后在同一输出卷内发布文件，再最后发布完成侧车。
- `FailUnit(error)`：保留失败日志及未完成状态；部分结果不得改名为正式完成文件。

配置指纹由 `FormalExperimentFingerprint` 唯一实现：沿用 24 号文档 SHA-256 输入顺序，补明确单元类型、schema/协议版本、环境文件摘要和规范化有效参数。`runId` 只标识同一研究批次，不意味着不同后端、策略和阶段具有相同 `configHash`。

`replayInputHash`、冻结阶段状态编号和 `configHash` 分别表达重建输入、真实阶段来源和实验配置。重建输入以固定串行来源设置计算，被测动作进入配置身份和请求记录；不能在改成不同被测策略后使用包含策略字段的输入散列，再要求两行身份相等。资产内容由 SHA-256 绑定，不能只依赖现有输入散列中的路径和尺寸。

首次采集的 `environment.json` 随运行批次冻结。恢复时重新采集硬件、驱动、构建和电源等实质环境字段并与冻结值比较，检查时间另写日志；不能为更新采集时间而重写冻结环境文件，使本来可恢复的单元产生新摘要。

相机生成没有相机文件，发现阶段还没有目标清单。对应尚不适用的指纹项使用带字段名的稳定 `notApplicable` 标记，不写空字符串，也不回填修改已发布发现单元的身份。目标冻结后，配对单元绑定目标清单摘要及其来源发现摘要，形成向前依赖关系，避免循环指纹。

场景、相机、目标及完成侧车明确各自版本；新增公共策略列时递增既有统计 schema，正式记录使用独立正式 schema。未知版本应失败，不按列位置猜测。

## 9. 数据流、控制流与计时边界

### 9.1 输入冻结与工作负载发现

CPU pilot 先独立执行“CPU 子集输入冻结 → 五阶段工作负载发现 → CPU 目标冻结 → CPU 配对 → 最小分析 → gate”，不依赖上传发现或上传目标。下面是 gate 通过后的完整正式流程，不能作为 PREP-04 的隐含前置条件。

```text
环境与构建快照
  → 18 场景清单验证
  → 生成并冻结 1152 行相机清单
  → CPU 发现：逐场景从根沿 0..63 推进并探测五阶段
  → OpenGL 上传发现：同逻辑输入重放，记录真实上传前资源条件
  → 按场景/相机/结果身份连接发现记录
  → 选择各阶段目标，输出覆盖报告
  → 冻结目标清单
```

**上传可采样性不能完全由无窗口运行决定。** CPU 发现可以获得网格规模和区间，但不能证明实际图形容量、首次上传或 D3D12 帧槽积压。推荐增加不做策略计时的运行时上传发现步骤；OpenGL 用于主目标选择，D3D12 对已冻结子集重新检查可采样性。

未通过后端前置条件的目标标记不可采样，不临时换成“第一个可用数据包”。CPU 和图形发现都记录逻辑相机及规范化网格身份；若后端投影差异造成活动结果不同，应报告协议不满足，不能强行配对。

网格特征来自修改记录到槽位/区间的规划：允许在只读来源的临时元数据上计算，不执行顶点/索引写入。生产和探测复用同一规划规则，规划成本单列；以后在线策略使用这些特征时必须承担该成本。

### 9.2 CPU 配对

1. 加载不可变清单、资产和场景设置，创建源流水线与线程池。
2. 从根按固定串行增量策略推进到目标 `k-1`；同单元内可按排序目标继续推进，不跨场景复用状态。
3. 写入相机 `k`，按串行基线推进至被测阶段前边界；验证目标输入和来源哈希。
4. 在计时外复制状态、准备线程和设置统一诊断模式。
5. 在每个策略自己的计时范围执行完整阶段；前置输入哈希与结果检查不混入该范围。
6. 停表后检查规范化结果、队列、预算、拓扑与对应网格证据；任一失败使整个目标无效。
7. 以绝对块编号运行 AB/BA 或六排列；预热数量不改变正式块的顺序定义。
8. 按目标清单和实际合法策略数检查行数；保留回退记录，但不把回退串行标成并行速度样本。

拓扑串行分支复用当前 `ReplayFrozen*TopologyAction` 的直接收敛语义。用于共同分类的候选快照在串行计时外；并行辅助须包含快照、排序、分块、调度、提交、整理、维护和串行尾部。完整 `wallMs` 优先由连续包络计时产生，内部字段用于解释，不用不完整子项和替代包络。

既有 `ReplayFrozen*TopologyPair` 可以继续作为同候选正确性回归。它的时间和正式配对记录使用不同模式身份，不混入正式统计。

### 9.3 按后端协议执行上传配对

```text
按对应后端真实帧推进重放 0..k-1
  → 记录目标上传前逻辑状态、资源使用与同步条件
  → 生成 k 的目标 CPU 数据包并深复制
  → 核对目标身份与该后端两策略适用性
  → 按已确认协议准备隔离实验资源
  → 每次策略：重建前置逻辑状态及安全访问条件 → 计时该后端上传包络
  → 完成全部预热与正式块
  → 每策略另做不计时重建/上传/资源读取验证
  → 检查生产资源内容与元数据未被实验污染
  → 附测量范围和代表性验证结果，发布或记录失败
```

“相同前置状态”仅指同一后端协议中可检查的逻辑状态，以及两策略遵循一致的资源使用和同步规则；不保证 GPU/驱动隐藏状态完全相同。OpenGL 与 D3D12 分别定义协议，不要求共享恢复步骤或快照内部表示。

重建和验证可影响缓存、驱动与管线状态，两策略采用一致规则并单列开销。试运行检查顺序偏差、独立实验资源的使用历史及生产路径代表性；读取安排在独立验证轮次，安全访问所需等待不能一概禁止。D3D12 帧槽补传和后端等待的归属须明确，避免在上传配对与端到端之间漏记或重复解释成本。

### 9.4 固定配置端到端

`FormalRuntimeBenchmarkSession` 的状态转换：

```text
Loaded → Warmup(0..7) → Reset → Measuring(0..63)
       → Reset → CorrectnessReplay(0..63) → Comparing → Committing → Finished
任意状态发生资源/输入/正确性错误 → Failed → 清理资源和写失败日志
```

每个采样点由公共冻结相机行直接构造完整视图输入。正式模式禁用人工相机修改、GUI 参数覆盖及依赖墙钟时间的路径推进。两种场景入口都以场景清单为权威；显式预算/轨迹参数若与清单冲突则报错，不能静默覆盖。

计时轨迹关闭全局诊断；帧外层计时停止后、下一帧更新前调用公共证据读取接口，保存该实际计时帧的哈希。随后正确性轨迹开启诊断，从根重放同配置，并与已保存哈希逐帧比较。

证据读取时间独立记录，不纳入帧时间或算法更新时间。由于其扫描可能影响下一帧缓存，试运行需以关闭额外证据的相同轨迹做扰动检查；若影响足以破坏实验协议，必须调整小规划和取证方案，不把额外扫描成本藏入结果，也不能用另一条重放的哈希替代计时运行本身。

### 9.5 发布与恢复

运行单元按 `Absent → Running → Validated → Complete` 前进；中断留下 `Incomplete`，正确性错误留下 `Failed`。恢复只跳过重新校验通过的 `Complete`。

原始 CSV 与完成侧车作为一个逻辑事务：先写临时文件、关闭并校验，原子重命名 CSV，最后发布包含 CSV 摘要的完成侧车。中途只完成任一步均不得跳过。恢复重跑生成新的临时尝试文件，不覆写已有完整原始数据。

单元预期行数由目标清单推导：两策略每目标 60 行，网格每目标 90 行；覆盖不足时按真实唯一目标数计算并明确记录不足，不伪造满额。固定配置每单元必须 64 个计时帧且有对应 64 个验证帧。脚本调用同一 C++ 校验入口判断完成。

## 10. 关键设计决策与替代方案

| 决策 | 推荐方案 | 替代方案与取舍 |
| --- | --- | --- |
| 研究投入顺序 | 最小 CPU 配对先行，CPU pilot gate 决定是否启动上传重构和完整正式化 | 先建完整框架会在研究信号不足时产生较大沉没成本；原完整顺序不再采用 |
| 场景与轨迹归属 | 共享 `Experiment/Formal` 值类型和生成器 | 分别扩展两个入口会重复坐标、预算和散列规则，不采用 |
| 配置来源 | 正式清单权威，CLI 选择单元或校验同值 | 继续通过 GUI 面板传参会钳制、覆盖正式输入，不采用 |
| 阶段执行 | 提取物理边界并复用既有生产事务 | 新写一套“实验版 ROAM”可能改变队列和收敛语义，不采用 |
| 工作负载特征 | 算法内部窄探测接口、规划结果复用 | 上层直接访问全部状态会形成强耦合；先完整执行后回填会泄漏执行后信息 |
| 上传实现与公共抽象 | 两后端分别提取，生产和实验各自复用对应实现；优先隔离资源，RAII 服从各后端生命周期 | 先确定后端边界再评估窄公共接口，不强行统一资源/快照/同步；临时替换生产私有资源或另写实验上传算法都需要额外论证 |
| 上传测量代表性 | 逻辑状态正确性、同步协议和生产性能代表性分别验证，记录测量范围 | 独立资源可能只支持受控微基准；若无法代表生产条件，修订协议或限定结论，并以端到端验证实际收益 |
| 上传发现 | CPU 发现加运行时后端前置条件发现 | 无窗口推测所有缓冲条件不能形成严格目标资格 |
| 文件恢复 | C++ 单元存储唯一判定，脚本调用 | Python/PowerShell/C++ 各自检查会产生身份规则漂移 |
| 摘要实现 | 窄职责纯 C++ SHA-256 工具，已知向量测试 | 使用平台加密 API 可减少实现，但需平台适配；启动外部命令不适合作为每个 C++ 入口的隐含依赖。若选择第三方库，先更新依赖设计 |
| 统计实现 | Python 标准库，按算法/IO/报告拆分 | 先引入数据科学依赖增加复现负担；只有试运行证明耗时不可接受才讨论固定版本依赖 |
| 运行并发 | 性能运行单进程顺序执行，构建和非性能测试可并行 | 同机多性能任务会争用 CPU/GPU、污染比较，不采用 |
| 快照策略 | CPU 单元内串行推进、逐策略隔离副本；上传按后端重建可检查的逻辑状态 | 预存全部 720 个大型状态会增加内存和失效复杂性；GPU 执行进度和驱动隐藏状态不属于可承诺恢复的快照 |

采用的设计模式限于已有问题所需：值对象表达冻结输入；策略枚举表达合法实现；适配器连接 DOD/渲染记录到公共记录；显式状态机管理跨帧实验；RAII 管理实验缓冲区；事务式文件发布管理中断恢复。无需新增通用缓存、事件总线或多层工厂体系。

## 11. 实施阶段

### 11.1 连续编号、执行顺序与完整职责映射

本规划统一使用 PREP-01～PREP-10，编号即完整路线的执行顺序。每阶段有独立小规划、明确交付和验收，不再保留 A/B 子阶段；CPU pilot gate 本身计为 PREP-05。

```text
PREP-01 策略下限与事实基线
  → PREP-02 最小 CPU 输入与可追溯记录
  → PREP-03 CPU 阶段边界、规划特征与工作负载发现
  → PREP-04 最小可靠 CPU 配对与最小分析
  → PREP-05 CPU crossover pilot gate
      ├─ 继续：PREP-06 正式输入、运行时与上传基础
      │         → PREP-07 上传配对与固定配置端到端
      │         → PREP-08 完整统计与中文报告
      │         → PREP-09 正式运行集成、批量执行与恢复
      │         → PREP-10 完整流程试运行与最终审查
      ├─ 收缩：重审研究范围，只规划有证据支持的后续阶段
      ├─ 停止：归档可靠负面结果，结束当前通用路线
      └─ 证据不足：在 PREP-05 内完成一次有上限的 CPU 补测，再作判断
```

PREP-01～PREP-05 构成可独立交付的 CPU 研究准备与投入判断。PREP-06～PREP-10 是 gate 支持完整路线并经 Review 后的条件性工作，不是前五阶段的前置条件。若决定收缩或停止，无关后续阶段标记为“不进入当前研究范围”；补测属于 PREP-05，不插入额外编号。

下表完整映射 24 号文档的实施职责；它的“实施 1–8”和实验轮次仍是协议引用，本规划的 PREP 编号按新顺序解释。

| 阶段 | 本阶段完整交付 | 对应 24 号文档 | 验收归属 |
| --- | --- | --- | --- |
| PREP-01 | 策略下限参数化、源码事实基线和可测试的参数解析边界 | 实施 1 的门槛、参数与基线 | AC-01 |
| PREP-02 | CPU 子集清单、NO 相机、最小记录和计时外可追溯摘要 | 实施 2 的 CPU 输入子集 | AC-02/03/08 的 pilot 子集 |
| PREP-03 | 五 CPU 阶段边界、只读规划特征、发现和确定性目标选择 | 实施 3 的 CPU 发现；实施 4 的阶段边界与特征 | AC-04 的 CPU 部分 |
| PREP-04 | 五 CPU 阶段可靠配对、计时标定、原始记录和最小分析 | 实施 4 的 CPU 配对；实施 6 的最小分析 | AC-05 的 pilot 部分；支撑 AC-00 |
| PREP-05 | CPU pilot 原始证据、逐阶段判断与继续/收缩/停止决定 | 新增工程投入门槛；不替代正式研究门槛 | AC-00 |
| PREP-06 | 18 场景/1152 相机点、NO/ZO、正式运行时、各后端内部复用的上传组件和真实上传发现 | 实施 2 的完整场景/图形输入；实施 5 的会话、资源与发现 | AC-02/03；AC-04 的上传部分 |
| PREP-07 | 完整正式记录 schema、严格上传配对、公共证据接口与五固定配置运行 | 实施 5；第 5 轮固定配置与正确性重放 | AC-06/07；正式数据契约 |
| PREP-08 | 完整配对统计、稳定交叉判定、正式 CSV/中文报告和封存校验 | 实施 6 的完整分析 | AC-09 |
| PREP-09 | C++ 指纹/存储、CPU 正式 CLI 与 5/30 集成、批量编排、环境冻结和恢复 | 实施 2 的完整身份/存储；实施 4 的 CPU 正式集成；实施 7 | AC-05 的正式部分；AC-08 |
| PREP-10 | 完整流程试运行、文档同步、双后端验收与最终架构审查 | 实施 8 与正式开跑门禁 | AC-10；汇总全部适用 AC |

AC-11、AC-12 贯穿全部阶段。每个小阶段都必须执行第 12.4 节的实现前后性能测试与报告比对，不能只凭构建、正确性回归或架构审查通过判定完成。原先延后的能力现在各有明确归属：完整场景与图形输入由 PREP-06 交付，正式记录契约由 PREP-07 冻结，完整统计由 PREP-08 交付，文件身份/存储及 CPU 正式运行集成由 PREP-09 交付；PREP-02、PREP-04 在各自最小范围验收后即可完成，无需等待后续阶段回填。

### PREP-01：策略下限与事实基线

**依赖**：大规划方向及 [PREP-01 小规划](prep_01_policy_and_baseline_plan.md) 已于 2026-09-09 经用户确认。

**文件归属**：修改公共策略、评分/网格线程解析与现有 CSV；复用 DOD 整份策略传递，在 `DataOrientedRoamParallel.h` 共用数量解析；提取 `TerrainLodBenchmarkCommandLine.h/.cpp` 并增加参数/策略/进程级接入测试；代码事实写入 `docs/codebase/formal_experiment/cpu_policy_and_replay_baseline.md`。

**实施内容**：

- 建立本模块代码事实文档，明确两条拓扑回放调用链、诊断开关、线程池借用和上传资源状态。
- 增加 `MergeScoreMinParallelEntryCount`、`SplitScoreMinParallelEntryCount`、`MeshEmitMinParallelTriangleCount`，默认分别为 256。
- 修改评分/网格线程解析，核验整份设置转换，保留预设及配对入口的显式下限；补输入散列、公共设置 CSV v4、既有 CPU 配对 CSV v2 与测试，版本分别管理。
- 提取无窗口参数解析文件，保持旧命令行行为；正式能力未接入时不提供会静默退回旧路径的成功入口。

**验证**：默认配置与显式默认值的固定轨迹规范化结果一致；0、1、255、256、257 工作量边界；串行强制 1 线程；并行请求 1/2/8 线程；无工作为 0；小于 256 且满足条件时真实使用至少 2 线程；新下限改变输入/配置身份；全部现有 CTest 通过。

**实现情况**：已于 2026-09-09 完成，代码提交 `996c3a9`。17 个代码/构建/测试文件均符合小规划归属；三个下限、纯解析、预设/设置传递、输入哈希、共享 CSV v4 与配对 CSV v2 已接入。两后端完整构建和各 24/24 CTest 通过，默认前后对照及低工作量真实线程诊断通过，注释格式已整改并复查。[小规划第 8 节](prep_01_policy_and_baseline_plan.md#8-实现情况)记录命令、原始输出和实施细化；[事实文档](../../codebase/formal_experiment/cpu_policy_and_replay_baseline.md)与[架构审查](../../reviews/formal_experiment/prep_01_policy_and_baseline_architecture_review.md)已同步，无未处理架构问题。后续阶段与 CPU pilot 未执行。

### PREP-02：最小 CPU 输入与可追溯记录

**依赖**：PREP-01 已完成；用户于 2026-09-09 明确授权 [PREP-02 小规划](prep_02_minimal_input_plan.md)的编写与自主实施，在已确认共享输入边界内完成细化与实现核查。

**文件归属**：在 `experiment/formal` 中创建最小 Types、Records、Manifest、Camera 和 Csv 能力，按需建立 `ExperimentCsvCodec`；补对应清单/相机/参数解析测试。以第 7 节文件表为边界，不提前创建 Fingerprint 或 RunStore。

**实施内容**：实现 pilot 所选场景读取、CPU/NO 相机生成与冻结、五 CPU 阶段目标引用和必要 CSV 记录。沿用 24 号文档场景取值及轨迹公式；先支持轨迹 A，确有补测需要时增加轨迹 B，轨迹 C 保持封存。记录完整命令、源码/二进制/资产/清单摘要、线程设置和基本环境；摘要通过现有工具在计时外取得。每次独立运行使用新目录，不实现自动恢复。

**验证**：所选场景和 `0..63` 相机点完整、可重放；高度图实际尺寸和摘要正确；浮点往返及 NO 投影正确；相同地形/轨迹不同预算姿态一致；错误输入被拒绝；已有目录不覆盖，失败/缺行数据有明确标记。

**验收边界**：完成 AC-02/03/08 的 CPU pilot 子集即可交付。完整 18 场景/1152 点、ZO 和上传输入由 PREP-06 扩展同一实现；C++ SHA-256、完整配置指纹和恢复由 PREP-09 交付。

**实现情况**：已于 2026-09-09 完成；实现与注释整改经用户许可提交为 `72f9c5f`，文档单独提交。新增共享 Types/Camera/Manifest/Records/Csv、通用 CSV 编解码、无窗口准备运行器与纯解析接入、Python hashlib 追溯脚本和独立六行 pilot 清单。两后端完整应用构建与各 28/28 CTest 通过；六场景 384 点、子集重放、目标引用、实际尺寸/资产 SHA-256、错误输入与目录保护均验收。独立冻结产物位于 `benchmark-output/prep-02/input-freeze-20260909/`。详细证据和限制见[小规划第 7 节](prep_02_minimal_input_plan.md#7-实现情况)、[代码事实](../../codebase/formal_experiment/cpu_pilot_input_contracts.md)和[架构审查](../../reviews/formal_experiment/prep_02_minimal_input_architecture_review.md)。本阶段只准备输入；DOD 目标身份、发现和策略配对仍未执行。

### PREP-03：CPU 阶段边界、规划特征与工作负载发现

**依赖**：PREP-02 已完成；[PREP-03 小规划](prep_03_workload_discovery_plan.md)已获用户确认并完成实现验收。运行不依赖上传发现或正式运行存储。

**文件归属**：在 DOD 内提取 PassExecution、TopologyPlan、MeshPlan，创建 WorkloadProbe；在 `benchmark/formal` 创建 FormalWorkloadDiscovery，在共享层创建 FormalExperimentTargetSelector；扩展发现记录和相关语义测试。

**实施内容**：为五个 CPU 阶段取得真实冻结边界及获准的工作负载字段；只提取可靠执行、只读规划和结果检查必需的 DOD 职责，复用已有事务和冻结执行器。实现 CPU 发现运行器和确定性目标选择，覆盖队列规模、候选安全性、预算余量和脏槽位/区间。上传资格和图形会话由 PREP-06 负责。

**验证**：提取前后固定串行/最大安全并行轨迹等价；探测前后源状态、队列顺序、版本和网格哈希不变；预计网格写入/区间与后续真实执行相符；所选 pilot 场景每条 64 点连续完整，不要求先跑全部 1152 帧；目标选择覆盖相等特征、零分母、并列和点数不足；修改所有耗时字段不改变目标清单。拓扑规划不得提交候选，网格规划不得写顶点。

**实现情况**：已于 2026-09-09 完成；经用户许可，代码提交为 `4d7ab68`，文档单独归档。公共五阶段观察、只读拓扑/网格规划、阶段输入身份 v1、选择器 v1、发现 CSV v2 与追溯脚本已接入。六场景完整发现 1920 行，生成 113 个唯一目标，3 个合并拓扑组的覆盖不足已记录。目标可从根重建，两个独立进程及两后端目标字节一致。OpenGL 32 项均取得通过结果，其中旧输入准备超时项独立复核通过；D3D12 全量 32/32 通过。CPU pilot 默认每组 4 点，8 点规则单独配置。详细产物与限制见[小规划第 8 节](prep_03_workload_discovery_plan.md#8-实现情况)、[当前事实](../../codebase/formal_experiment/cpu_pass_boundary_and_workload_baseline.md)和[架构审查](../../reviews/formal_experiment/prep_03_workload_discovery_architecture_review.md)。

### PREP-04：最小可靠 CPU 配对与最小分析

**依赖**：PREP-01～PREP-03；完成 [PREP-04 小规划](prep_04_minimal_cpu_pairing_plan.md) Review。独立交付所需能力，不以 PREP-06～PREP-09 的完整框架为前提。

**文件归属**：扩展 DOD PassExperiment 及冻结执行器；在 `benchmark/formal` 创建 FormalCpuPassBenchmark、FormalTimingCalibration，按需接入 FormalExperimentRunner 的 CPU 子集入口；扩展 CPU 原始记录。创建第 7 节规划的 Python 分析入口/模块及 CPU 配对、合成数据测试。

**实施内容**：基于现有 CPU 配对入口增加 pilot 子集、阶段/目标筛选和基本计时标定；统一计时副本诊断设置，保持请求线程上限。五个阶段都必须从同一冻结输入执行完整合法策略，拓扑串行复用当前直接收敛，网格保留三策略。输出逐次原始时间、请求/实际策略、回退、工作负载和正确性证据；显式标记为探索性数据，不以 `pass-crossover-formal` 名义发布。

**最小分析**：实现配对完整性检查、按独立运行分列的中位差/收益、绝对差与噪声阈值、实际并行覆盖和简单工作负载分组表。附能够检测胜负反转、纯噪声、回退和坏配对的合成数据测试。完整 bootstrap/Holm、正式报告模板由 PREP-08 扩展同一组模块，批量恢复由 PREP-09 交付。

**验证**：单场景/单阶段/单目标可独立执行；阶段筛选与完整顺序执行的对应输入结果一致；复制和检查不进入 `wallMs`；不同重建诊断开关不改变规范化结果；串行拓扑计时不建分块，并行完整时间包含快照/分块/调度/提交/维护/收敛；请求 2 线程不被改为 8；AB/BA、六排列和实际预热/重复参数正确；所有合法策略结果等价；回退不被解释为并行胜负；源流水线不受配对影响。

**验收边界**：交付可直接用于 PREP-05 的可靠测量和最小分析。正式 CPU CLI、完整目标矩阵、正式 schema/单元存储集成及 5/30 重复验收归 PREP-09，复用本阶段执行器；探索数据保持原用途。

**实现情况**：小规划已获用户确认并实施，包含 Measurement/Evidence 拆分、真实目标重建、配对 v2、标定、实际进程追溯和最小分析。两后端完整构建及各 36 项 CTest 完成，最终 Python 审计与容量复测也完成。六场景完整集合发现既有细分拓扑策略不等价；普通矩阵与交替复测仍有性能恶化信号，均已单独分析。当前不是验收完成状态，修复承接需用户确认。详见[小规划实现记录](prep_04_minimal_cpu_pairing_plan.md#11-实现情况与收尾记录)、[架构审查](../../reviews/formal_experiment/prep_04_minimal_cpu_pairing_architecture_review.md)、[等价性分析](../../reviews/formal_experiment/prep_04_split_topology_equivalence_analysis.md)及[性能调查](../../reviews/formal_experiment/prep_04_runtime_performance_regression_analysis.md)。

### PREP-05：CPU crossover pilot gate

**依赖**：PREP-01、PREP-02、PREP-03、PREP-04 通过可靠性验收；执行前完成 `prep_05_cpu_crossover_pilot_gate_plan.md` Review，固定场景、目标选择、噪声规则、重复规模和一次补测上限。该 gate 只依赖 CPU 能力。

**文件/产物归属**：复用 PREP-02～PREP-04 的输入、发现、配对及分析组件；原始 CSV、输入摘要和 `cpu-pilot-gate.md` 放在独立探索输出目录，投入决定记录在 `docs/reviews/formal_experiment/prep_05_cpu_crossover_pilot_gate_review.md`。不为本阶段新建上传组件或第二套实验框架。

**回答的问题**：合并评分、细分评分、合并拓扑、细分拓扑、网格提交，是否在工作负载变化下呈现足够明确的双向胜负信号；这些信号是否在独立进程重放中保留，且能用执行前/规划期间特征描述。先观察是否有值得预测的现象，不要求先训练预测模型。

**建议的首轮规模，执行前在 gate 小规划中冻结**：

- 两个既有高度图 × 轨迹 A × 各自低/中/高预算，共 6 个场景、384 个发现帧；不读取轨迹 C。
- 每个阶段、每个场景最多 4 个不同目标，优先覆盖低、中、高工作量和一个辅助特征补点。只按工作量选点，不按耗时挑选；规则沿用既有分层/固定散列/距离选择语义，并显式记录 pilot 的缩减数量。
- 串行固定 1 线程，并行请求 8 线程，五个数量下限为 0，安全条件保留。每策略预热 3 个块、记录 12 个块，AB/BA 与六排列按绝对块编号执行；12 个记录块使每次重放内两策略顺序及网格六排列均衡。
- 独立进程重放 3 次，每次从根重建同一目标输入，保留三次结果，不混合为更多独立冻结输入。满覆盖时最多 120 个阶段专用输入、9504 次计时策略执行；不采集正式全矩阵。
- 该规模用于投入判断。若运行成本明显超出预计，可在采样前缩减目标数并记录限制；不能看过胜负后删掉不利状态或修改判断门槛。

**先过可靠性门槛，再解读性能**：

1. 所有用于比较的策略均通过同输入、规范化结果、预算、队列与拓扑检查；有错误时先修测量/执行，不得给出研究方向的负面结论。
2. 并行候选有真实至少 2 线程的执行覆盖；只有回退、只有无工作或工作量范围不足时标记覆盖不足。若安全条件本身长期不允许并行，单独报告该结构限制。
3. 绝对中位差需超出 `max(0.01 ms, 较快策略中位时间的 3%)`；同时查看三次运行的中位差与漂移。每个候选获胜区域至少有两个不同冻结输入支持，且优势方向在三次重放中一致，才记为“可复现交叉信号”。这只是探索性标记，不声称已满足正式置信区间与区域样本要求。
4. 记录交叉两侧的队列规模、安全候选/非空分块或脏比例/区间特征。信号若不能随这些获准特征稳定分组，应报告可预测性仍不明确，不用执行后字段制造可解释性。

**输出**：保存原始 CPU CSV、冻结 pilot 输入、三次命令/环境摘要、覆盖表、五阶段分组胜负表和中文 `cpu-pilot-gate.md`。报告对每阶段分别给出“单策略占优 / 有双向信号 / 差异无实际意义 / 覆盖或测量不足”，最后给出以下投入建议与证据。

| 结果 | 解释 | 后续动作 |
| --- | --- | --- |
| 继续完整路线 | 至少三个 CPU 阶段呈现可复现且超过噪声/实际收益阈值的双向信号，获准特征能初步区分区域 | Review 确认后启动 PREP-06；继续验证正式统计和后续上传/端到端，不把 pilot 当作已证明正式 H1 |
| 收缩研究范围 | 仅一到两个阶段值得继续，或交叉存在但可预测性证据较弱 | 围绕保留阶段修改研究问题、范围和计划；不自动执行整套 uploader 重构，不先假设上传可以补足第三个阶段 |
| 停止通用路线 | 在覆盖充分且测量可靠的 pilot 范围内，各阶段基本是固定策略占优或差异无实际意义 | 归档结果并讨论替代研究问题；停止 PREP-06、PREP-07 和无必要的正式框架建设。结论限定于已测工作负载，不宣称全域不存在交叉 |
| 证据不足 | 噪声、覆盖、安全候选不足或重放问题使当前结果无法判断 | 仅允许一次针对具体缺口的 CPU 补测；仍不清晰则暂停完整路线并重新讨论，不无限扩充基础设施 |

如果合并/细分评分几乎始终并行胜、合并/细分拓扑几乎始终串行胜、网格也没有有意义的反转，应明确进入“停止通用路线”或“收缩研究范围”，不能因工程入口已完成就默认继续上传重构。

**补测约束**：在小规划中预先固定问题和上限；建议最多增加 24 个阶段专用冻结输入，必要时使用轨迹 B，继续采用相同计时与独立重放规则。选择依据是缺失的工作量/依赖范围，不能从所有结果中搜寻有利胜负；首轮与补测分开保留。一轮补测后必须作继续、收缩或暂停判断。

**与正式门槛的关系**：本 gate 是工程投入与研究方向筛查，允许小样本描述性判断；24 号文档正式交叉的独立输入数、bootstrap/Holm、区域条件和正式研究门槛不降低。CPU gate 通过也不直接授权在线模型、离线最优参考或正式论文结论。

**实现情况**：未执行。执行后记录数据目录、覆盖、噪声、逐阶段结论、是否补测，以及人类 Review 的继续/收缩/停止决定。结果不支持继续时，可靠测量和负面报告仍构成本阶段的完整交付。

### PREP-06：正式输入、运行时与上传基础

**依赖**：PREP-05 给出支持完整路线的证据并经 Review 确认；完成 `prep_06_runtime_and_upload_foundation_plan.md` Review。gate 未通过时不启动本阶段，也不提前实施本阶段上传重构。

**文件归属**：扩展 PREP-02 的 Manifest、Camera、Types/Records 和权威场景清单；创建 `app/formal` 会话与报告适配，在 `render/upload` 分别提取 OpenGL、D3D12 网格上传组件和必要公共值类型。小规划明确各后端实验适配的文件职责、资源所有权及调用边界；`ICpuMeshUploader` 仅在窄公共契约得到论证后创建。修改 Application/Renderer 接入层，保留图形后端同步职责；严格配对编排留给 PREP-07。

**实施内容**：

- 扩展并验证完整 18 场景、1152 相机点及 NO/ZO 投影；补图形输入身份和 `cpu-upload` 目标关系。两入口复用同一清单与相机实现，轨迹 C 的性能数据继续封存。
- 创建正式运行时会话，接入权威场景/相机输入和确定性逐帧驱动；图形入口不使用内置压力路径覆盖正式预算。
- 先分别界定两后端资源、绑定、帧推进与上传的职责，再提取后端内部可复用实现；在发现模式保存真实上传前资格和结构字段，扩展确定性上传目标选择。建立深复制 CPU 包及后端专用前置逻辑状态契约，不要求两后端共用资源/同步模型。
- 分别明确 OpenGL 缓冲使用历史/驱动同步与 D3D12 上传堆/帧槽/后端等待的测量边界，给出隔离资源的代表性验证方案。优先验证 OpenGL，再按 D3D12 自身生命周期接入；不据前者通过推断后者成立。输入与发现可用独立测试输出验证，完整 C++ 发布/恢复在 PREP-09 接入。

**验证**：完整清单数量、关系及 NO/ZO 校验通过；相同地形/轨迹不同预算姿态一致；普通 GUI/内置路径不回归；正式预算不会被压力路径覆盖；实际 drawable、FOV、裁剪面和相机哈希正确；各后端提取前后的字节、区间、回退和绘制结果一致，OpenGL 绑定状态及 D3D12 帧槽推进/安全写入不回归；完成各自测量范围、同步位置和代表性验证方案的 Review；捕获开关关闭时不创建实验资源；无窗口/图形发现的逻辑输入和规范化结果能够对应；缺失或不合法后端资格被记录，不替换目标；目标选择不读取耗时字段。

**实现情况**：未开始，受 PREP-05 约束。完成后填写完整输入验收、各后端资源与同步边界、公共接口取舍、发现结果和审查记录。

### PREP-07：严格上传配对与固定配置端到端

**依赖**：PREP-06；完成 `prep_07_upload_and_fixed_baseline_plan.md` Review，确认各后端上传实验协议、代表性验收标准、完整正式记录契约和公共证据接口。

**文件归属**：扩展 FormalExperimentRecords/Csv，冻结 CPU、上传、端到端和完成元数据的正式字段；创建 CpuMeshUploadReplay、TerrainLodResultEvidence；修改公共算法接口和两个算法适配器，扩展正式运行时会话/报告与上传回放测试。正式文件发布与 CPU 运行适配归 PREP-09。

**实施内容**：精确重建目标包；按各后端协议在隔离资源上逐策略重建前置逻辑状态及安全访问条件，复用该后端生产上传实现；全部计时块后执行该后端的读取验证。分别验证实验测量与生产路径的关系，明确受控微基准和实际运行结论的适用范围。实现五固定配置的单配置运行及逐帧证据/正确性重放。新增公共结果证据读取，保证普通调用无附加扫描。完整 schema 在本阶段冻结，单元完成记录此时仅定义契约；PREP-09 实现身份计算、发布和完成判定，不形成反向依赖。

**验证**：重建/读取处于声明的计时外边界，必要等待的归属明确，API 内部同步不从实测值中扣除；同后端 AB/BA 前置有效内容、容量和相关元数据一致；主动扰动重建内容能够被检出；全量和脏上传均与目标有效内容匹配；OpenGL 生产绑定/资源及 D3D12 生产帧槽、版本和内容不被污染；检查实验资源使用历史、同步与重复顺序对性能结论的影响，D3D12 后续帧槽补传与后端等待不漏记；初始化、扩容、积压分别产生正确标签；单配置 8 帧预热、重置、64 帧计时及 64 帧验证完整；故意破坏一个结果哈希使整个单元失败；检查计时外取证扰动。正式记录的 schema/配对键/用途标签以测试夹具校验，未通过存储集成的测试输出不得宣告正式完成。

**实现情况**：未开始。完成后填写记录版本、各后端读回方法、同步/计时边界、性能代表性及其限制、端到端一致性和审查记录。

### PREP-08：统计、性能交叉判定与中文报告

**依赖**：PREP-05 的继续决定、PREP-04 最小分析、PREP-07 的记录语义和正式 schema 冻结；完成 `prep_08_analysis_plan.md` Review。CPU gate 所需的基本判断已在 PREP-04 完成，不等到本阶段才读取第一批胜负结果。

**文件归属**：扩展 `scripts/analyze_pass_crossover.py`、`scripts/formal_experiments/` 和 `tests/test_pass_crossover_analysis.py`，复用 PREP-04 的分析模块。

**实施内容**：扩展 PREP-04 的既有分析代码，实现完整数据审计、百分位、10000 次配对 bootstrap、网格三组比较的 Holm 校正、正式平局和稳定交叉规则；训练集单阈值/二维表分析以可调用函数实现，禁止自动训练上线。输出每阶段独立 CSV 和中文 Markdown，记录实际有效输入数、回退及覆盖不足；探索报告与正式报告保持不同数据用途标签。

**验证**：合成数据覆盖明显串行胜、明显并行胜、双向交叉、纯噪声平局、阈值相等、回退和不等价输入；30 次重复不能被当作 30 个独立冻结状态；网格多重比较结果符合已知期望；打乱文件行顺序不改变配对；跨后端记录不混合；轨迹 C 即使存在也不能进入默认胜负图、阈值拟合和继续条件报告。

**验收归属**：完成 AC-09 的完整统计与封存校验；正式运行目录的发布/恢复检查由 PREP-09 接入，此阶段使用合成数据及独立测试输出验证。

**实现情况**：未开始。完成后填写算法定义、随机种子、合成数据测试和审查记录。

### PREP-09：正式运行集成、批量执行与中断恢复

**依赖**：PREP-06～PREP-08；复用已验收的 PREP-02 输入和 PREP-04 CPU 执行器；完成 `prep_09_orchestration_and_resume_plan.md` Review。本阶段统一承担正式文件存储与 CPU 正式集成，不回填早期阶段。

**文件归属**：创建 `src/tools/Sha256.h/.cpp`、FormalExperimentFingerprint、FormalExperimentRunStore 及对应测试；扩展 FormalExperimentRunner、FormalCpuPassBenchmark、参数解析、运行时会话/报告；创建 `scripts/run_formal_experiments.ps1`，接入既有分析入口的完成校验调用。身份与恢复规则只由共享 C++ 层实现。

**实施内容**：

- 实现 C++ SHA-256、规范化配置指纹、完整环境绑定、运行单元存储、完成侧车及原子发布；接入 PREP-07 冻结的记录契约。
- 将 PREP-04 同一 CPU 执行器接入 `pass-crossover-formal`、完整目标清单、正式 schema 和单元存储，补每策略 5 次预热/30 次正式重复及完整顺序规则。不得另写阶段算法，也不得把探索数据重新标为正式数据。
- PowerShell 采集硬件、驱动、系统、电源、构建和工作区信息，顺序运行实验单元，调用 C++ 完成校验；生成运行清单、失败清单及日志。
- 接通相机生成、CPU/上传发现、目标选择、CPU/上传配对、固定配置和分析的完整流程，按完成状态恢复。`oracle`、`adaptive` 阶段在本范围内明确返回尚未支持，不空跑成功。

**验证**：SHA-256 固定向量及规范化配置编码正确；CPU 正式与 pilot 入口对同一冻结输入的策略语义和结果一致；正式 5/30 参数、顺序、预期行数与真实线程记录完整；在采样、关闭文件、CSV 改名和完成侧车发布之间模拟中断，恢复只跳过有效单元；改 commit、资产、二进制、清单、后端、参数或环境后拒绝混用；不同配置顺序符合 24 号文档循环；错误退出不自动删除原始数据；从带空格路径运行参数和文件引用正确；未完成/探索数据不能通过正式发布及分析准入。

**实现情况**：未开始。完成后填写正式 CPU 接入、指纹/存储验证、恢复用例、运行清单与审查记录。

### PREP-10：完整流程试运行、文档同步与最终审查

**依赖**：PREP-01～PREP-09 完成，且 PREP-05 的继续决定仍适用；完成 `prep_10_pilot_and_acceptance_plan.md` Review。若研究范围已收缩，先更新本规划及适用验收范围再启动。

**文件归属**：复用前九阶段的执行/分析/编排组件；同步 README、24 号文档、字段说明、`docs/codebase/formal_experiment/` 和本规划实现记录；按实际验证缺口完善集成测试，审查写入 `docs/reviews/formal_experiment/`。

**实施内容**：使用两地形 × 轨迹 A × 中预算，保留完整场景清单并过滤两个场景；配对每策略预热 1 次、记录 3 次。执行发现、选择、CPU 配对、两后端上传、固定配置、分析及中断恢复。使用区别于 PREP-05 CPU 探索的 `integration-pilot` 目录和用途标记，两类试运行均不并入正式数据。本阶段验证完整工程流程；CPU 交叉是否值得继续的首次判断已经在 PREP-05 完成。同步文档和代码事实，移除提取后的重复实现。

**验证**：双后端构建、全部 CTest、默认 600 点与压力 64 点运行时验收；试运行每单元按目标数精确核对行数和正确性；原始数据只读；封存规则有效；确认所有适用 AC 条件和正式开跑检查表。正式计时基线的 P99/漂移门槛未通过时应记录环境阻碍，不修改统计阈值以通过验收。

**实现情况**：未开始。完成后填写试运行目录、验证命令、通过/失败统计、剩余限制和最终架构审查链接。

## 12. 测试策略与研究数据门禁

### 12.1 分层验证

| 层次 | 输入与关注点 | 通过条件 |
| --- | --- | --- |
| PREP-05 CPU pilot gate | 五 CPU 阶段、低/中/高工作量、三次独立进程重放和最小分析 | 配对可靠、顺序均衡、实际收益超出噪声，给出有依据的继续/收缩/停止或有限补测决定 |
| 纯组件 | 小型清单、固定浮点、哈希向量、选择器边界和文件中断 | 返回内容与期望一致，错误包含定位，不能只验证“非空” |
| CPU 集成 | 默认/压力、不同阶段、串并行请求、局部/完整网格 | 同冻结输入结果等价，预算/队列/拓扑检查真实执行，计时边界准确 |
| 图形集成 | 各后端前置逻辑状态、资源使用历史、扩容/积压边界、同步及实际读取 | 内容与绘制规模一致，生产资源隔离，回退真实；分别验证测量代表性，覆盖 D3D12 帧等待/后续补传及 OpenGL 绑定与缓冲使用条件 |
| 运行时会话 | 预热、重置、64 点、错误中断与正确性重放 | 状态顺序及计时帧/验证帧对应正确 |
| 统计 | 固定种子合成数据及损坏配对 | 已知结论、独立输入计数、留出封存和校正行为正确 |
| 完整试运行 | 两个中预算场景的全部流水线 | 单元可重跑、数据可追溯、分析可重建、恢复无覆盖 |

CPU pilot 只要求相关 CPU 构建、现有无窗口回归和新增 CPU 可靠性验证，不以 D3D12 上传专项或完整图形验收为前提。后端测试用显式标签区分需要图形设备的集成测试和无窗口 CTest。没有可用设备时记为未验证/跳过，不能记为通过；gate 后正式参考主机开跑仍要求两后端专项验证均完成。

### 12.2 正式统计规则（gate 通过后）

CPU pilot gate 使用 PREP-05 冻结的小样本描述性规则，不要求先实现本节全部统计能力，也不能引用该探索判断声称已经通过正式研究门槛。

- 使用 24 号文档规定的 10000 次 bootstrap 和种子 `20260831`。
- 单冻结输入内按完整配对块重采样；区域与阈值置信区间以独立冻结输入为重采样单位，不把相邻帧或策略重复当成独立场景。
- 平局：95% 置信区间含 0，或中位差小于 `max(0.01 ms, 较快策略中位时间的 3%)`。
- 稳定交叉：两个相反获胜区域，每区至少 8 个独立冻结输入，至少 70% 支持同一策略，区域置信区间不含 0，且在至少两个场景单元复现。
- 网格三组比较执行 Holm 多重比较校正。具体重采样统计量、校正 p 值与区间展示方式在 PREP-08 小规划固定，不能把普通 95% 区间直接标成已校正区间。
- 回退、无工作、结果失败、缺失配对分别记录。安全回退不等于正确性失败，但不进入主动策略速度比较。
- 主分析只读取 A/B；C 允许采集和正确性校验，但本准备阶段不提供默认解封性能结果的捷径。

### 12.3 数量核对

以下为 gate 通过后正式矩阵在覆盖充足时的规模上限，不是 gate 的样本要求或填充配额。早期 CPU pilot 的独立规模见 PREP-05。

| 数据 | 数量 |
| --- | ---: |
| 完整发现帧 | 18 × 64 = 1152 |
| 五 CPU 阶段正式执行 | 4 × 144 × 2 × 30 + 144 × 3 × 30 = 47520 |
| OpenGL 上传正式执行 | 144 × 2 × 30 = 8640 |
| D3D12 上传交叉检查 | 24 × 2 × 30 = 1440，单独统计 |
| 固定配置计时帧 | 18 × 5 × 5 × 64 = 28800 |
| 固定配置正确性重放 | 另有 28800 帧，不作为性能样本 |

不足 8 个有效唯一目标时保留真实数量及 `coverageInsufficient`。D3D12 子集严格由已冻结 OpenGL 中预算目标的层次和排名产生；某层不存在时报告覆盖不足，不能私自替换选择规则。

### 12.4 每个小阶段的性能回归验收

**每次实现完一个小阶段，都必须运行性能测试，与该阶段实现前的性能报告逐项比对；发现性能问题时，须单独记录分析过程与原因，经用户确认修复规划后处理。** 性能问题未修复并通过复测前，不得标记性能验收通过。本要求适用于 PREP-01～PREP-10 的全部小阶段，不因修改属于重构、实验基础设施或离线工具而省略。测试复用阶段当时已具备的运行入口，不要求提前建设后续正式框架，也不替代 PREP-05 的研究判断。

1. **实现前建立基线**：小规划先固定测试场景、覆盖路径、指标、完整命令、预热与独立重复次数、噪声估计和退化判定方法，再对修改前版本运行性能测试。保留源码与可执行文件身份、原始 CSV、日志和中文基线报告；缺少基线时先补齐，不能实现后凭印象描述原性能。
2. **实现后同条件复测**：使用相同硬件、构建预设、编译器与优化设置、资产、场景、相机轨迹、策略、线程数量和诊断开关。图形路径还须保持后端、驱动、适配器、分辨率及 VSync 一致，各后端分别比较。充分预热并重复独立运行，性能任务顺序执行，测量期间不并行构建或运行其他性能任务；若环境改变，须在新环境重测修改前版本，不能直接比较不兼容报告。
3. **覆盖实际影响**：至少复测既有默认与压力路径，并补充本阶段影响的热点、边界或典型工作量。生产路径关闭实验功能时也必须复测，以检查新增设施是否带来常驻开销；涉及脚本或离线工具时，另测其代表性输入的处理耗时。报告同时比较整体耗时和受影响阶段耗时的中位数、P95，以及绝对差、相对变化和重复间波动，不能以整体平均值改善掩盖局部退化。
4. **保证可比性**：前后使用相同逻辑输入、正确性要求和计时边界，核对输出规模、结果及实际执行方式；不能通过减少工作量、改变策略、跳过验证或迁移计时边界制造加速。新增能力没有旧版对应路径时，单独报告其成本及小规划中的预期上限，既有路径仍须完成前后对照。
5. **单独分析性能问题**：发现超出基线重复波动、可复现的性能退化，先复现和定位，在 `docs/reviews/formal_experiment/` 单独建立 `prep_NN_<topic>_performance_regression_analysis.md`。报告须记录受影响场景和指标、退化幅度、复现条件与原始数据路径，以及排查假设、检查或实验步骤、观测结果、已排除因素和原因判断；区分已证实原因与尚未验证的推测，不能只写“变慢了”或无证据地认定根因。噪声过大、样本不足、环境不一致或缺少报告时，结论为“性能尚未验证”，不能写成“无性能问题”；不得事后放宽判定规则、只保留更快的重复或用其他场景的收益抵消退化。
6. **用户决定修复路径**：完成分析后，提交报告、影响范围及两种处理路径的取舍供用户 Review：一是单独新增修复小规划，二是修改后续小阶段的规划，将修复纳入其目标、文件职责、实施步骤和验收要求。未经用户许可，Agent 不得自行选定路径、新增修复小规划、调整后续规划或实施修复；不能把性能修复作为当前阶段的隐含附带修改。用户选定并许可后再编写或调整规划，具体方案按既有小规划流程确认后实施。
7. **跟踪延期与修复验收**：用户选择后续规划修复时，同时确认承接阶段、依赖、允许继续的范围和限制，并将当前状态明确记为“实现完成，性能问题待后续修复（用户已批准）”，不得写成无条件“已完成”或“性能验收通过”。未经许可，不得自行延后或带着未处理退化进入下一阶段。修复后须对照问题发生前的原始基线复测，并核查承接阶段自身的前后性能；不能将已退化版本重设为唯一基线来掩盖问题。只有复测证明问题已解决，才关闭问题并更新验收状态。
8. **保留报告与核查记录**：实现前、实现后和比对结果分别写入 `benchmark-output/prep-NN/` 下的独立目录，原始基线不覆盖。小规划、大规划的“实现情况”和阶段架构审查须引用性能报告及独立问题分析，记录测试条件、关键差异、用户决定及日期、修复规划链接、承接阶段、处理进展和最终复测结论。已有阶段的正确性对照或 CTest 记录不能追认为性能验收证据；本节工程回归数据也不混入正式研究样本。

## 13. 风险、歧义和待确认事项

### 13.1 主要风险

| 风险 | 影响 | 处理与停止条件 |
| --- | --- | --- |
| 在观察 CPU 研究信号前投入完整框架 | 无交叉时产生较大沉没成本 | 强制执行 PREP-04 → PREP-05 CPU gate → PREP-06；不得以完善架构为由绕过 gate |
| 把小样本覆盖不足当作不存在交叉 | 过早否定研究方向 | 分开报告可靠性、有效并行覆盖、工作量覆盖与胜负；只允许一次有明确问题和上限的补测 |
| 为了通过 gate 反复筛选有利场景 | 研究判断受结果选择污染 | 首轮及补测规则先冻结，保留全部数据，C 不参与，补测后必须作投入决定 |
| 提取阶段边界时重复评分或改变预算交换顺序 | 配对结果与生产语义分离 | 先锁定调用顺序及基线；任何等价失败先修规划/实现，不放宽断言 |
| 规划探测修改了共享队列/槽位 | 后续源状态不再相同 | 只读输入与隔离工作区，探测前后哈希及版本回归 |
| 跨后端矩阵或相机角度转换差异 | 同逻辑相机产生不同工作负载 | 直接消费冻结视图输入，分别校验投影，无法匹配则报告失败 |
| 强行统一两后端资源与同步模型 | 职责泄漏、生命周期错误或改变被测实现 | 先明确各后端边界，公共接口仅在语义一致时创建；后端拥有帧推进和同步 |
| 上传提取遗漏 D3D12 帧等待或其他帧槽积压 | 改变后续上传范围，或遗漏延后成本 | 逻辑状态覆盖相关帧槽元数据与有效内容；检查可写条件、后续轮转和计时归属，不恢复或伪造 fence 进度 |
| 独立上传资源或逻辑状态重建改变使用历史、缓存和管线状态 | 内容正确但测量不能代表生产成本 | 各后端分别验证同步条件、顺序偏差及代表性；单列相关开销，受控微基准限定结论，必要时修订上传协议并验证端到端收益 |
| 源线程池被销毁或多个实验副本同时使用共享池 | 生命周期错误或测量争用 | 源对象覆盖全部副本，同一单元策略同步顺序执行 |
| 原始文件和标记发布顺序不完整 | 恢复跳过损坏数据 | 完成侧车最后提交，每次恢复重算摘要、关系与行数 |
| 大场景快照/纯 Python bootstrap 成本高 | 内存峰值或分析耗时过大 | 逐单元流式输出、不缓存全部状态；先试运行测量，再决定依赖或分批实现 |
| 环境或资产变化没有绑定身份 | 混用不可比较数据 | 环境、资产和二进制摘要进入单元身份，变化使用新运行批次 |

### 13.2 Open Questions：需人类确认的架构决策

| 编号 | 问题与选项 | 推荐及理由 | 影响范围 |
| --- | --- | --- | --- |
| Q1 | 正式输入/单元存储共用 `experiment/formal`，还是分别放两个入口 | 推荐共享层；CPU 最小子集先按此归属实施规划，完整存储后置 | PREP-02 的共享输入；PREP-06 的正式输入扩展；PREP-09 的完整存储 |
| Q2 | 各后端内部提取后，是否需要窄公共接口；隔离实验资源采用什么使用/同步协议，能支持何种性能结论 | 各后端内部复用方向已确定；优先隔离资源，先论证后端适配与测量范围，再决定接口。资源/快照/同步分别实现，逻辑状态一致不构成生产性能等价证明 | PREP-06 的职责/接口及协议设计；PREP-07 的代表性验收 |
| Q3 | 上传目标资格由后端发现补齐，还是无窗口推测 | 推荐新增运行时上传发现步骤；容量和帧槽积压必须来自真实后端 | PREP-06 的上传发现；24 号文档第 1 轮及目标冻结顺序 |
| Q4 | 扩展公共算法接口读取计时外证据，还是让应用读取具体算法内部状态 | 推荐窄公共证据接口；保持 App/Render 不依赖节点和队列实现 | PREP-07 与两个算法适配器 |
| Q5 | C++ 原生单元存储及 SHA-256，还是 Python 统一包办全部文件发布 | 推荐完整正式化阶段采用 C++ 单元存储；pilot 先用已有工具记录摘要，不以本项为前置 | PREP-09 的共享存储、工具及构建 |
| Q6 | 相机单列 `viewInputHash` 如何区分 NO/ZO | 推荐保留 NO 含义并新增 ZO 列；pilot 仅用 NO，不等待 ZO/上传 schema | PREP-06 的跨后端输入及 24 号文档清单契约 |
| Q7 | 24 号文档没有可独立调用的目标生成、完成校验和运行时上传发现命令 | 先确认 CPU pilot 所需的子集/目标选择入口；完成校验、环境接口和上传发现等后置 | PREP-03、PREP-04 的最小 CLI；gate 后 PREP-06、PREP-09 |

建议补充的命令形态仅为待 Review 接口：

| 参数/入口 | 归属与用途 |
| --- | --- |
| `--write-target-manifest <path>` 与 `--discovery-input-dir <path>` | 无窗口正式入口：读取已校验发现数据并调用唯一 C++ 目标选择器 |
| `--experiment-verify-only` | 只执行单元/运行目录校验并返回退出码，供调度和恢复使用；不执行策略、不写原始记录 |
| `--experiment-environment <path>` | CPU 与运行时共用：绑定预检环境快照，避免隐式寻找某个工作目录文件 |
| `--runtime-benchmark-workload-discovery` | 运行时正式入口：采集真实上传前资格及结构，不执行策略速度配对 |

既有 24 号文档参数名称继续保留。`--scenario-id` 为两入口共用场景过滤参数，不能只在无窗口解析；正式相机清单与内置路径互斥。本范围不实现 `oracle`、`adaptive`。

“先 CPU 配对与 pilot gate，后上传重构”的投入顺序，以及“各后端内部复用对应上传实现”的边界已按用户意见确定，不再作为待决定问题。Q2 保留的是具体公共接口取舍和各后端实验协议，不将统一完整上传接口视为既定方案。Q1–Q7 按 PREP-01～PREP-10 中当前阶段涉及的边界分别确认；Q2–Q6 中的上传、完整存储和跨后端事项不得成为 CPU 最小实现的全局阻塞。具体代码实现仍以对应小规划确认的职责和接口为准。确认应记录日期、选择和修改后的边界；若与推荐不同，先更新相关文件表、接口和依赖。

## 14. 兼容、迁移和审查记录

1. 现有 `smoke`、预算重入、增量网格、固定策略、拓扑配对、跨实现契约和两个 `pass-crossover-*` 配置继续可用；它们与正式模式采用明确身份区分。
2. 生产默认策略与普通相机路径不使用正式实验的零下限、场景锁定或资源副本。
3. 各后端上传职责和 CPU 阶段规划定向提取时，先验证各自生产行为，再接入实验能力；同一后端的生产/实验调用同一份对应实现。后端间保留不同资源与同步模型；行为等价和实验性能代表性分别验收。
4. 每阶段按计划更新代码事实，并在 `docs/reviews/formal_experiment/` 输出 `prep_NN_<topic>_architecture_review.md`；CPU gate 的投入决定单独记录为 `prep_05_cpu_crossover_pilot_gate_review.md`，引用实际数据和逐阶段结论。审查按指南使用 Critical/Major/Minor，列证据、影响和处理；这些文件名是本模块建议约定，现有审查指南没有另行规定命名。
5. 每阶段小规划及本大规划的“实现情况”记录实际提交、测试、输出位置和审查结果，并按第 12.4 节引用实现前后性能报告、独立问题分析与用户确认的修复规划。性能退化未解决或证据不足时，不得标记性能验收通过；经用户批准交由后续阶段修复的，明确保留“实现完成，性能问题待后续修复（用户已批准）”状态、承接阶段及继续范围。未实施项保持“未开始”，不能因文档写完改为“已完成”。
6. 目录、职责、依赖、所有权、公共接口或计时/采样协议发生重要偏离时，先说明事实、影响和备选，再更新规划并重新确认；普通局部实现细节按已确认边界处理。
7. CPU gate 决定收缩或停止时，保留可靠执行入口、原始数据和负面结果，将不再需要的后续阶段显式标为“不进入当前研究范围”。不能把工程建设完成度作为继续投入的理由，也不自动删除已产生的研究证据。

## 15. 目标架构摘要

**目录**：共享实验契约在 `experiment/formal`；CPU 编排在 `benchmark/formal`；图形运行时会话在 `app/formal`；阶段语义在 DOD；上传资源在 `render/upload`；统计在 `scripts/formal_experiments`。

**职责**：清单定义输入，算法组件定义合法执行；各后端网格上传组件管理对应资源与写入，图形后端保留帧推进和同步；公共实验编排组织配对，各后端适配完成状态重建和读取验证；运行存储判定数据完成，分析模块按声明的测量范围解释结果。

**依赖**：两个入口共同依赖共享实验契约；具体算法和上传组件不反向依赖实验文件/调度；Python 和 PowerShell 不重新实现算法与目标选择。

**核心流程**：PREP-01 策略与基线 → PREP-02 最小输入 → PREP-03 CPU 发现 → PREP-04 可靠配对与最小分析 → PREP-05 CPU pilot gate；支持继续后，PREP-06 正式输入/运行时/上传基础 → PREP-07 上传配对/固定配置 → PREP-08 完整统计 → PREP-09 正式集成/恢复 → PREP-10 完整流程验收。

**完成边界**：前半程以可信 CPU 测量和明确投入决定为交付，结论可以是继续、收缩或停止。完整正式框架只在研究信号支持保留该范围时实施；CPU pilot 不替代正式研究门槛，离线最优参考和在线策略仍由正式数据与独立确认控制。
