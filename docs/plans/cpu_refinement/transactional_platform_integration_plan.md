# 事务化 CPU LOD 平台接入大规划

> 日期：2026-09-15；类型：大规划；编号：TPI（Transactional Platform Integration）。
> 状态：**用户已批准，按阶段自主闭环。** 2026-09-15 用户确认“可以，根据大规划自己闭环就行”；按 TPI-01→05 顺序实施、验证、审查并逐阶段提交。SVE-01 基线为 `c7e6cd1`。
> 本规划回应用户“先接到 DOD 共用平台试运行，再评估优化空间”的决定；不要求先证明优化空间耗尽，也不把接入解释成性能或持续质量已经通过。

## 1. 问题、目标与证据

把当前独立事务化原型作为第三种 CPU LOD 算法接入 Classic/DOD 共用应用，使用真实相机、公共 CPU 网格输出、现有 OpenGL/D3D12 上传和运行时实验。首先回答它能否持续运行、切换与重置，真实更新/上传成本是多少，以及视图变化如何暴露质量与响应问题；随后给出剩余性能空间和相对 Legacy 的判断。

研究主线保持“全局优先级驱动的事务化分阶段执行”，不是 NMP 严格目标规划的续做。接入的新算法自行形成需求与事务；一般 one-ring 连接不要求属于原 ROAM 二叉层次。它与 DOD 共用平台，不共用可变拓扑状态，也不声称逐请求或最终网格等价。

已读依据：

- [开发规范](../../standards/development_guidelines.md)、[规划规范](../plan_guideline.md)、[代码事实规范](../../codebase/codebase_guideline.md)、[审查规范](../../reviews/review_guideline.md)。
- [GTP 大规划 §10](greedy_transactional_cpu_prototype_plan.md#10-与原-dod-五阶段及未来生产接入)、[接入前成本分析](../../research/cpu_refinement/gtp_platform_performance_analysis.md)。
- [GWR 结果](../../research/cpu_refinement/gwr_final_results.md)、[SVE-01 规划](sve_01_scaling_viability_plan.md)、[SVE-01 结果](../../research/cpu_refinement/sve_01_scaling_results.md)及其事实/审查。
- 本次直接扫描的[平台接入边界事实](../../codebase/cpu_refinement/transactional_platform_integration_baseline.md)，覆盖公共算法接口、核心构建/线程依赖、初始几何、原始高度、视图、应用枚举和两个上传消费者。

已知风险必须贯穿报告：固定 64 前缀在较大输入可能不产生事务；增长组曾出现约 64～67px 的跨视图采样误差；200k/C8 的预留占移动帧时间约 60%。这些是现有版本的研究风险，不能因 UI 显示正常或少数帧较快而关闭。

### 1.1 完成标准

1. 显式选择 Classic、DOD、事务化 LOD；切回旧算法恢复其原生状态/行为，新算法不成为默认替代。
2. 新算法从合法初网格独立持续更新，不逐帧重新导入 DOD、不复制完整状态求目标、不通过 reset 掩盖退化。
3. 相机静止仍可运行下一批；相机、窗口、设置、资产和算法切换有明确生命周期。
4. 公共网格、更新范围、输出数量与资源生命周期正确，两后端分别验证。
5. 连续轨迹记录真实事务、质量/恢复、完整 CPU 更新、适配、上传与帧时间；不混用环境或计时边界。
6. 最后形成性能空间、时间/空间复杂度和相对 Classic/DOD 的分析，列出下一项值得优化的工作及证据，不承诺倍率。

## 2. 范围与非目标

**范围**：生产可链接核心的归属整理；同步执行复用；公共算法适配；一次性初始化；输入/设置/统计值类型；算法注册与开关；有限 GUI/CLI 接线；静止更新调度；增量输出及失败恢复；两个图形后端的定向接入；短轨迹与接入后成本分析。

**不包含**：新优先级或 refill 策略、不同高度拟合政策、放松证书、层次 Q 查询、reservation 优化、新原语、跨帧异步算法、GPU 算法、重做 uploader、长期实验平台、正式论文矩阵、默认取代 DOD。已有 GWR 优化与失败方案保持；仅允许为正确接入新增深度约定和生命周期能力。

输入首版支持当前方形、至少 2×2 的高度图；自然验收限 test129/Peking，最大验证预算 200k。不以资产名限制核心；非方形、不可表示尺寸或超出声明配额时明确拒绝。任意规模、任意近面穿越、快速从粗态收敛均未获保证。

## 3. 架构判断与复用

```text
Application / GUI / CLI / RuntimeBenchmark
           ↓ 设置与算法选择
TerrainLodAlgorithmRegistry → ITerrainLodAlgorithm
           ↓                    ↓
       Classic / DOD     TransactionalTerrainLodAlgorithm
                                  ├─ SeedBuilder（仅初始化）
                                  ├─ Pipeline（持续核心）
                                  ├─ CpuTaskExecutor（拥有线程）
                                  └─ RenderBridge（借用输出/统计）
                                            ↓
                               TerrainLodRenderPacket
                                  ↙                  ↘
                         OpenGL 消费者           D3D12 消费者
```

| 判断 | 对象 | 原因 |
|---|---|---|
| Reuse | 事务化 State/Samples/Certification/Reservation/Commit/Pipeline/Mesh | 同一实现同时服务探针与应用，避免维护两套算法 |
| Move | 运行核心由 `experiment/greedy_transactional_lod` 移入 `algorithms/greedy_transactional_lod` | 生产算法不能依赖场景白名单、JSON、研究 oracle 或历史实验层 |
| Reuse + Move | DOD 的无业务线程池移入 `tools/CpuThreadPool` | 池只管理任务；DOD 保留兼容入口，其他算法不依赖 DOD 私有实现目录 |
| Extract / Wrap | MPR 执行器的异常排空逻辑进入 `tools/CpuTaskExecutor`，旧适配器转发 | 复用已经验证的同步派发契约，不另造 scheduler；不搬 MPR 状态/算法 |
| Wrap | 公共 `ITerrainLodAlgorithm` 适配器 | 拥有生命周期、执行器与核心，隔离重型头文件；不实现第二套事务选择 |
| Extend | HeightMap 原始数据、视图深度约定、轻量设置/统计和能力 | 补足当前输入契约，保持旧 float 采样与旧阶段含义 |
| Create | 小型算法注册表、种子构造、输出桥接 | 消除多入口创建分歧；初始化与输出各有独立所有权/错误边界 |
| Reuse | 两个 `UploadMeshData`、应用相机、现有 perf/Tracy、离线 evaluator | 适配公共 CPU 输出；资源/采样/评价各归原层 |

设计采用已有 Strategy 接口、薄 Adapter、组合与 RAII。注册表是显式枚举/构造函数表，不建设插件系统；输出桥接是值转换，不建设通用事务框架。

### 3.1 与原 DOD 阶段的边界

| 原阶段 | 新算法中的对应职责 | 接入规则 |
|---|---|---|
| MergeScore / SplitScore | 自有样本/视图优先级与前缀查询 | 不读取旧堆或节点评分；不将新时间硬塞成原两个评分阶段 |
| MergeTopology / SplitTopology | 统一接收/回收交易、冲突预留与发布 | 不在新路径中执行旧两个阶段，也不将交换计数当作 DOD primitive 数 |
| MeshEmit | 新算法的增量实际拟合网格 | 复用公共值类型和消费契约，不调用旧 builder 重新采高度 |
| CPU 上传 | 公共渲染包的消费 | 使用各后端现有资源实现，按实际范围/字节计费 |

Classic/DOD 原控制流、候选规则和默认策略保留。共享线程池只做归属提取，保持原任务队列、分块调用和等待行为；不能顺手优化 DOD 或改变 baseline。

## 4. 目录、文件与依赖方向

### 4.1 目标文件结构

```text
src/algorithms/
  ITerrainLodAlgorithm.h                         [扩展值契约]
  TerrainLodView.cpp                             [保存深度约定]
  TerrainLodAlgorithmRegistry.h/.cpp             [新：可用算法/名称/构造]
  TransactionalLodSettings.h                     [新：轻量公开设置]
  TransactionalLodStats.h                        [新：轻量公开统计]
  greedy_transactional_lod/
    TransactionalTypes.h                        [移动]
    TransactionalPriorityIndex.h                [移动]
    TransactionalState.h/.cpp                   [移动]
    TransactionalSamples.h/.cpp                 [移动]
    TransactionalViewState.h/.cpp               [移动]
    TransactionalPredicates.h/.cpp              [移动]
    TransactionalProposalEvidence.h/.cpp        [移动]
    TransactionalCertification.h/.cpp           [移动]
    TransactionalProposals.h/.cpp                [移动]
    TransactionalReservation.h/.cpp             [移动]
    TransactionalCommit.h/.cpp                  [移动]
    TransactionalPipeline.h/.cpp                [移动]
    TransactionalMesh.h/.cpp                    [移动]
    TransactionalExecution.h/.cpp               [移动]
    TransactionalTerrainLodAlgorithm.h/.cpp     [新：公共入口/生命周期]
    TransactionalSeedBuilder.h/.cpp             [新：初始几何/配置转换]
    TransactionalRenderBridge.h/.cpp            [新：渲染包/统计转换]
src/tools/
  CpuThreadPool.h/.cpp                          [由 DOD 池提取]
  CpuTaskExecutor.h/.cpp                        [由 MPR 同步安全适配提取]
src/experiment/greedy_transactional_lod/
  TransactionalInput.h/.cpp                     [保留：文件/冻结协议]
  TransactionalScalingProtocol.h/.cpp           [保留：SVE 输入]
  TransactionalValidation.h/.cpp                [保留：独立全量核查]
  TransactionalQualityReport.h/.cpp             [保留：离线质量]
  TransactionalDynamicReference.h/.cpp          [保留：A 研究参考]
cmake/TransactionalLod.cmake                    [新：核心/适配构建清单]
```

每个移动文件保留原职责，不再拆几何算法。核心命名空间改为 `ParallelRoam::Algorithms::GreedyTransactionalLod`；研究层仍用 `Experiment::GreedyTransactionalLod`，显式依赖核心。历史报告/采样程序身份保留原路径，不能批量改写旧研究结论；当前探针、测试和源码归档脚本更新真实新清单。

`TransactionalTerrainLodAlgorithm` 用私有实现隐藏 Pipeline、执行器和 Boost 依赖；`SeedBuilder` 不维护跨帧目标；`RenderBridge` 不读文件、不规划事务、不上传 GPU。公开 Settings/Stats 为有限字段值类型，不暴露核心 map/set 或可变节点。

基础线程池从 DOD 源文件迁移后，旧头文件保留薄兼容类型；现有前向声明同步校对。删除旧独立池实现文件，CMake 只链接一次公共实现。MPR 执行器保留原接口并委托 `CpuTaskExecutor`；独立 MPR oracle 保留，不重做原算法。

### 4.2 其他修改位置

| 文件 | 修改职责 |
|---|---|
| `src/terrain/HeightMap.h/.cpp` | 保存加载时的原始 uint16，提供只读样本与成功加载版本；原 float 采样计算顺序不变 |
| `src/render/TerrainRenderer.h` | 公开渲染设置/统计搬运、算法输出失效与上传恢复标志；不加入事务内核 |
| `src/render/TerrainRenderer.cpp`、`D3D12TerrainRenderer.cpp` | 用公共工厂；识别持续更新能力；搬运新设置/统计；范围消费与上传失败恢复 |
| `src/gui/ImGuiLayer.h/.cpp` | 实验算法选项、配置、暂停/单步请求、阶段读数；不直接修改核心 |
| `src/app/Application.h/.cpp`、`ApplicationCommandLine.h/.cpp`、`RuntimeBenchmarkConfig.h` | 设置转发、显式实验选择、离散更新调度；不增加逐帧 JSON/质量扫描 |
| `src/app/RuntimeBenchmark.h/.cpp` | 通用帧统计和事务专属字段输出；旧五阶段报告只处理适用算法 |
| `src/benchmark/TerrainLodBenchmark.h/.cpp`、`TerrainLodBenchmarkCommandLine.cpp` | 无窗口公共入口的算法选择和有限接入 profile；旧严格配对 profile 明确拒绝新算法 |
| `CMakeLists.txt`、`CMakePresets.json`、`tests/CMakeLists.txt` | 核心先于 app/tests 定义；加入实验接入 preset，更新迁移清单与定向测试 |
| `scripts/run_cpu_profile.py`、相关源码归档脚本 | 只修复移动后的源码清单；不扩大 profiler 能力 |
| `scripts/run_transactional_platform.py`（新） | 编排本规划的有限公共入口/图形运行与证据归档，不实现算法 |
| `scripts/transactional_platform_report.py`（新） | 离线配对、质量/计时分层及性能空间报告，不发起测量 |

新测试按边界设 `TransactionalTerrainLodAlgorithmTests.cpp`、`TransactionalRenderPacketTests.cpp`；原线程、核心、协议和 CLI 测试优先扩展复用。图形资源测试在已有后端测试归属内增加对应案例，不新建渲染测试平台。

依赖必须是：应用/组合工厂 → 适配库 → 核心/terrain/tools；初始化组件另依赖 DOD 公共 `ITerrainLodAlgorithm` 实现。核心不依赖 DOD、MPR、实验、GUI、renderer。`tools` 不依赖算法。研究探针依赖核心，生产目标不链接 `TransactionalInput/Validation/QualityReport/DynamicReference`。

### 4.3 构建与数值环境

复用 `PARALLEL_ROAM_BUILD_TRANSACTIONAL_LOD` 的核心开关，并从 tests 移到项目级配置；新增默认关闭的 `PARALLEL_ROAM_ENABLE_TRANSACTIONAL_LOD_RUNTIME`，启用时要求核心可用。`BUILD_TESTS=OFF` 也必须可以构建实验应用。关闭运行接入时，原应用不链接新核心和 Boost。

Boost 继续固定 1.90.0，使用既有显式路径规则，不由系统自动挑版本。严格浮点选项只施加到核心及相关数值适配，不改变 Classic/DOD 编译行为。重型依赖只在需要的目标传播；验证公开头文件是否仍泄漏 Boost。

所有命令由 Ubuntu/WSL 编排。CPU 基线在同一工具链上比较；原生 Windows 构建/启动由 WSL interop 调用现有 Windows 工具。D3D12 不假装在 Linux 原生运行；WSLg OpenGL、原生 Windows OpenGL 与 D3D12 分别标明环境。不可用的工具/后端标为未验证，不用另一环境结果替代。

## 5. 初始状态与设置生命周期

### 5.1 首版选定一次 DOD 公共初始化

推荐首版协议 `DodBootstrapCurrentViewV1`：从干净、临时 DOD 对象对**当前实际视图**调用一次完整 `BuildRenderData`，使用当前资产、尺度、预算、种子深度与 split/merge 像素设置，固定种子阶段串行策略。将公共实际 mesh 转为 `InitialMesh` 后立即释放临时 DOD 和渲染包。

这只提供 M0，不提供下一轮目标 J；后续没有 DOD 调用。不能偷偷推进历史 0..14、循环到预算饱和或根据新算法结果多跑初始化。一次种子不够密也如实报告 N/U；不能把它当作 SVE 历史 seed 的精确复现。

采用此方案是为了让现有资产直接启动，并先验证持续交易。自建规则粗网格、从粗态适应、无 Legacy 冷启动作为替代方案保留，不在本轮研究。冷启动时间必须包括临时 DOD、几何转换、验证、源样本、Q/索引/mesh/线程池初建及临时对象释放。

`SeedBuilder` 复用现有公共几何 Import 规则：精确 UV 身份、实际输出高度、一致共享点、方向规范；不容差焊接、不重采旧点。初建一次检查覆盖、边/点 link、面积、范围与预算；若浮点公共输出不能形成合法种子，停止接入并记录反例，不能删除验证条件。验证代码中的纯几何规则只做窄提取供一次性种子核查，不让生产链接完整实验验证器。

### 5.2 原始 reference

HeightMap 在一次成功加载中同时保留原始 uint16 和既有 float 数组；8-bit 文件仍按 stb 原规则扩展。新算法从只读原始数据复制一次自有 reference，之后资产文件变化不影响该次运行。旧 `SamplePixel/SampleBilinear` 不变，不通过乘 65535 反推源整数。

成本显式增加 HeightMap 的 `2WH` 字节原始数组及新算法源数据所有权；初始化与 RSS 对照记录这一项。相机更新不复制源图，不每帧散列整个资产。成功加载版本、对象身份、尺寸及渲染层 reset 用于识别替换；同路径重新加载仍使状态失效。

### 5.3 设置契约

| 输入变化 | 首版行为 |
|---|---|
| 相机矩阵、实际 drawable 尺寸、深度约定 | `SetView`，保留拓扑与 reference；0 尺寸窗口暂停调用，不伪造可见域 |
| 光照、线框等显示设置 | 只改渲染表现，不重新拟合/初始化 |
| 高度图、尺度、预算、种子深度、split 阈值、种子 merge 阈值 | 显式 reset 后重新初始化；预算降低时不尝试用净零交换缩容 |
| r/m、执行线程数、高度保护、样本配额 | 显式 reset；不在同条计时轨迹静默切换 |
| 算法选择、主动 Reset | 清理借用与消费者旧输出，再销毁状态/执行器；重新选择从新 seed 开始 |
| 暂停/单步 | 只控制是否调用一次更新，不改内部候选或输出高度 |

`TransactionalLodSettings` 首版只公开执行线程数、`PrefixLimit`、`DonorLimit`、现有 `HeightGuard` 和固定资源配额；默认 r=m=64、4 线程、`HeightGuard=false`，与原型默认行为一致。GUI 可显式设置 r/m，CLI 可复现 64/160/320/640；设置后 reset，不自动随预算或成功数量增加。

旧 DOD 五阶段策略不映射成新算法内部策略。`MaxDepth` 和 merge 像素阈值对新算法只控制种子，界面明确标成“初始网格”参数；持续算法使用自己的现有接受规则，不假装遵守 Legacy merge 阈值。`HeightGuard` 是已有可选对照，不称持续视图质量保证。

## 6. 公共入口、状态与失败边界

### 6.1 每帧数据流

```text
BuildRenderData(input)
→ 验证输入/设置身份
→ 无状态：SeedBuilder → 核心初建（单独标记冷启动）
→ SetView（必要时）
→ Update（恰好一个冻结批次）
→ ConsumeMesh
→ RenderBridge 构造公共包与统计
→ 返回借用 mesh
→ renderer 接收范围并上传 / 记录失败
```

适配器拥有 Pipeline 与执行器；回调借用期限覆盖全部任务。释放顺序先结束更新/排空任务，再释放核心，最后销毁执行器。禁止后台更新同时让 renderer 读取可变数组。

同一 `BuildRenderData` 只做一批，不为了画面好看循环追平质量，也不借相机刷新额外执行隐蔽批次。返回 Batch 的正常释放、统计/范围转换均在公共 CPU 就绪时间内，不能搬到帧外省时。

### 6.2 静止视图仍继续

扩展公共能力 `RequiresContinuousUpdate`，新算法为 true，旧算法保留原跳过规则。新算法运行状态下每个应用更新 tick 最多调用一次公共入口；`SetView` 自己处理相同视图的复用。

没有事务不等于已经全局收敛，固定前缀失败也不能触发永久休眠。首版不实现“无变化就停止”的新算法规则。GUI 暂停/单步属于外部明确控制，实验记录是否调用了更新；renderer 跳过更新时不能把上一帧阶段耗时当作本帧耗时。

### 6.3 错误处理

区分输入/种子错误、数值不可定义、样本/时间配额耗尽、任务/分配失败、上传失败。适配器通过公共错误返回和有限状态码报告，不用空 mesh 假装成功。

- 普通更新准备失败：保留最后完整发布的几何；暂停同一失败配置的自动重试，记录当前视图是否已发布。用户重试或视图变化后按错误类别恢复，不能每帧忙等同一失败。
- 资产/预算等 reset 后初始化失败：该新配置没有可绘制的合法新结果；禁止把旧预算 mesh 继续标为新配置结果。可保留 GPU 资源，但不得作为新状态绘制。
- `SetView` 成功、随后 `Update` 失败：明确当前状态是“新视图、旧几何”，不能声称整个帧回滚，也不能将失败纳入正常性能样本。
- 入队失败导致执行器停止：在可控生命周期边界重建执行器或由 Reset 恢复；先排空，绝不让借用回调悬空。

既有近面异常不在本轮升级成新裁剪/细化算法。非法数值域要安全展示错误；可用视图下仍必须连续运行。若通常交互轨迹频繁越出支持域，记录“交互适用范围不足”，不得仅以错误可捕获判为实用接入成功。

## 7. 视图与几何输出契约

### 7.1 深度范围

在 `TerrainLodViewInput` 保存显式深度约定，由 `BuildTerrainLodViewInput` 从现有参数赋值；核心 Configuration 对应保存，旧默认保持 `[-w,w]`。

首版使用输入原生矩阵和约定，不先对矩阵做未经审计的浮点重写。OpenGL 近面为 `z+w≥0`，D3D12 为 `z≥0`；远面均为 `z≤w`。`Samples::Project` 的可见性/被测近面检查、`Certification` 的浮点拟合约束、区间和有理回退必须同步覆盖。拟合中关于自由高度的近面系数也要改变，不只改最终 if。

细分/回收优先级的 x/y 像素度量、参考可见人口和正式 evaluator 使用同一当前 drawable 与深度范围。新增约定参与输入身份和视图失效；旧默认哈希编码保留可追溯性，新约定使用明确扩展，不能无记录改写旧重放身份。

解析透视/正交、近面两侧、同视图两种矩阵的数学等价案例分别验证。实际不同后端 float 矩阵不必逐位相同，但不能因此省掉几何域/边界检查。跨工具链数值差异单列，同构建 B/C 决策必须一致。

### 7.2 借用、范围和 GPU 消费

RenderBridge 返回 Pipeline 自有 `TerrainMeshData`，包内自有 `CpuMesh` 为空，生命周期为 `UntilNextBuildOrReset`。禁止每帧复制全部顶点或通过旧 builder 重新生成。顶点范围与索引范围分别映射，可独立为空，包装为 `O(number_of_ranges)`。

首次、reset、消费者资源丢失/重建或必须扩容时全量上传；正常零字节改变时不产生虚假的全量上传。原型 generation 会随纯视图变化增加，消费者必须结合范围/全量/数量判断，不能把版本递增等同于 mesh 内容改变。

`ConsumeMesh` 已清理内部 Pending，故范围移交后的失败必须有明确归属：输出桥接异常令下一次包要求全量；renderer 上传失败保留恢复标志，下一次对**最新完整公共 mesh** 强制全量同步。不得依赖旧 Pending 重新出现，也不为此清空/重导入算法状态。D3D12 的其他帧资源仍按既有方式累计范围，不保存可能失效的 CPU 裸数组指针供以后直接复制。

核查索引缩短、槽复用、数量变化、跳帧消费和多轮未消费范围。绘制三角形数量使用活动索引，不使用带空槽的顶点容量。面法线与 Height 来自实际拟合几何；不替换成 source 属性掩盖几何变化。

binary64 状态与 float 输出分开核查：共享点转换一致、索引有效、位置有限、无输出退化/翻转。首版在新写入面准备期加入必要检查；遇到 float 不可表示几何时拒绝该次发布，不只依靠离线发现。独立质量报告另评价实际公共 float mesh，不能仅评价更高精度内部状态后声称渲染质量相同。

## 8. 开关、统计与基线兼容

增加 `TerrainLodAlgorithmId::TransactionalCpuLod`，保留原编号；注册表统一名称、可用性和创建。构建未启用时 UI 不提供可运行选项，显式 CLI 请求报不可用，不回退成 DOD。旧默认 `all`/运行时两算法列表保留；通过新显式选择纳入第三种算法。

GUI 至少提供实验算法选择、线程与 r/m、重置、暂停/单步；展示活动三角形/预算、实际事务、失败原因摘要、视图/认证/预留/续接时间和初始化状态。研究计数放在诊断面板，不要求普通用户理解内部文件/模块。

CLI 沿用既有入口，建议新增 `--algorithm transactional` 与 `--runtime-benchmark-algorithms` 显式名单，并提供 `--transactional-workers`、`--transactional-prefix`、`--transactional-donors`；具体拼写由 TPI-02/03 固定后写入实际命令。当前这些参数尚未实现。

公共 Stats 保留总 CPU、网格、上传、资源计数；新增可选 `TransactionalLodStats` 和阶段模型标识。事务字段至少包含：r/m、实际需求/接收/交换、配对/冲突、视图刷新、认证、预留、拓扑准备/发布、样本续接、网格准备/发布、冷启动、状态码和实际是否更新。旧五阶段不适用字段在报告中显示“不适用”，不得用零值解释成没有成本。

正常诊断关闭时不为获取真实线程数开启完整 `Diagnostics` 配对尾扫。请求线程数始终记录；实际参与由已有独立 Tracy/诊断取得，未测时写未测。不能把任务数/配置线程数填成实际核利用率。

现有严格 topology 配对、Legacy heap 证据与 formal 五阶段实验不接收新算法；普通家族总成本报告可接收，但明确任务/质量差异。CPU 上传配对保持原授权范围，本轮只测新输出在正常消费者中的行为，不自动复制全部旧上传矩阵。

## 9. 成本口径与性能空间

同一次调用分开记录：

\[
T_{cpu-ready}=T_{view}+T_{discovery/certification}+T_{reservation}+T_{topology}+T_{continuation}+T_{mesh}+T_{adapter}.
\]

`T_adapter` 包括配置核对、范围/统计转换与正常临时结果释放。初建/销毁、CPU 上传/等待、GPU 绘制和实际帧包络分别记录；CPU/GPU 可能重叠，不能直接把所有子时间相加冒充帧时间。旧 SVE 的调用方保留 Batch 与平台释放边界不同，比较时必须说明。

| 工作项 | 接入后的期望计费边界 |
|---|---|
| 冷启动 | 一次 DOD 初始化 + `O(N0 log N0)` 几何身份/结构构造 + 源样本/Q/关联初建及数值核查；用真实访问计数补齐，不假定初始化线性 |
| 配置核对/公共适配 | 固定字段 `O(1)` + `O(范围数+有限统计字段)`；不允许隐藏全域 mesh/Q 扫描 |
| 相机刷新 | 沿用当前 `Θ(q)` 投影、关联归约 a、候选块索引构造；接入不降低该复杂度 |
| 认证 | 实际被尝试提案、样本支持及区间/精确分支成本；拓扑度数上界不能代替样本成本 |
| 预留 | 仍按 SVE 当前 `O(P(b+1)f log(2+f))` 相关集合操作计费；不在接入中顺手换算法 |
| 续接/输出 | 实际局部样本、关联、脏块、范围与容量增长；不能从局部拓扑推出完整 `O(1)` |
| GPU 消费 | 首次/扩容/恢复全量，普通按脏范围；D3D12 帧资源追赶、OpenGL 上传和各自等待单列 |

其中 N0 为种子面数，q 为固定参考样本数，a 为样本与闭面关联规模，P 为配对检查数，b 为已预留批宽，f 为资源足迹规模。精确整数位复杂度、持久容量和 CPU/GPU 内存峰值另列。保留原型状态，不每帧复制 p 份。

最后的性能空间分析必须给出：

1. 公共适配与图形接入新增多少工作，是否出现意外全量复制/更新；相同核心输入的迁移前后是否等价。
2. 与同平台 Classic/DOD 的完整时间、预算利用率、实际更新和独立质量并列；不拿零事务成本交叉当作胜出。
3. 各主要阶段的函数/时序证据、实际工作量与复杂度相符程度，标明剩余可避免工作和仍未验证的替代方案。
4. “某阶段免费/减半”的固定其他项敏感性分析，用来量化优化优先级；明确不是可达收益或算法下界。
5. 分别回答：接入是否可用、还有多少**已知**性能空间、什么条件下可能比 Legacy 快、现有数据是否支持。不得写成优化耗尽、通用加速或论文竞争力通过。

## 10. 实施阶段

严格按 TPI-01 → 02 → 03 → 04 → 05；一个小规划实现、验证、审查并按届时提交授权归档后，才进入下一阶段。每个小规划提前冻结实际文件和命令。用户已授权进入实施；小规划在各阶段修改代码前冻结，无需重复请求确认。

### TPI-01：运行核心与共享执行依赖

- 移动上述核心，拆开研究专用链接；项目级构建选项与库；提取公共池/安全同步执行，保留 DOD/MPR 调用兼容。
- 先保存 `c7e6cd1` 可执行文件/源码清单；核对移动后的核心只有声明的下层依赖，关闭接入的应用不新增 Boost/原型依赖。
- 验证：现有核心/索引、线程排空/入队失败相关测试；一份既有自然 B/C 逻辑对照。性能用原 Peking 一条八轮及 DOD 受影响线程路径各一次前后对照，不重跑 SVE 矩阵。
- 出口：迁移与调度兼容成立，不能以重写选择策略修复迁移差异。
- 实现情况：未开始。

### TPI-02：公共适配、种子与输入契约

- 新 Settings/Stats、原始高度访问、深度约定；SeedBuilder/RenderBridge/公共算法；先通过无窗口入口运行，不依赖 GUI。
- 完成初始化、reset、失败状态与范围移交策略，确认 r/m、种子参数和一次批次边界。
- 验证：一次合法自然种子；同种子/矩阵下直接核心与公共适配 B/C 对照；静止继续、多次更新后消费、预算减小、同路径重载、非法矩阵、NO/ZO 近面、公共 float 输出和分配失败。
- 性能：直接核心与适配各一次相同短输入；初建/源数组内存新增量单列。HeightMap/共享视图受影响的旧 DOD 路径做同条件前后对照。
- 出口：公共入口和自有持续状态可用，冷启动不依赖研究 JSON；不得按速度胜负代替契约判断。
- 实现情况：未开始。

### TPI-03：开关、应用调度与 OpenGL

- 统一工厂/可用性；GUI/CLI/配置搬运；新算法连续 tick、暂停/单步；公共统计与 normal runtime 选择。
- OpenGL 借用范围、绘制数量、纯视图无字节更新、上传失败后全量恢复；保留旧算法跳过策略。
- 验证：三算法切换/reset/关闭特性；窗口变更、同视图多批、空批、非空批、资源扩容与重新上传。用程序化结果/范围检查为主，必要画面核查只补显示证据。
- 性能：同一个 OpenGL 环境，旧 DOD 修改前后短回放；新算法 CPU 适配/上传/帧包络分项；不跨 WSL/Windows 拼比值。
- 出口：可交互实验路径成立；普通受支持相机仍频繁失败时记录适用域问题，不宣布完成实用验收。
- 实现情况：未开始。

### TPI-04：D3D12 资源消费与双后端边界

- 用同一算法注册、配置和 CPU 输出；D3D12 只处理其自身资源轮转、范围积累、容量和 fence。
- 验证：真实 ZO 输入、连续多帧资源轮转、跨帧范围积累、空/非空输出、重置后首次全量、上传失败/资源恢复；覆盖新值契约在两后端的搬运。
- 性能：原生 D3D12 同配置旧 DOD 前后各一短回放，新算法单独记录。复用 02 已完成的纯 CPU 测试，不再全跑。
- 出口：D3D12 接入与消费成立；工具链/运行环境不可用则单列未验证，不能用 OpenGL 或编译成功替代。
- 实现情况：未开始。

### TPI-05：持续回放、成本与剩余空间闭环

- 固定接入版本，复用应用离散相机与 profiler，不改认证、前缀、配对或几何原语。
- 主回放以 Peking/50k、r=m=160、8 线程为实际交易覆盖点；小输入 test129/4096、r=m=64 作固定成本与生命周期对照。尺度/来源/阈值引用既有冻结协议，平台种子用本规划 V1，不能混称旧 SVE 同状态复测。
- 主后端由 03/04 的可用原生平台在采集前固定，默认 OpenGL；每输入先各一进程运行事务串行/8 线程和匹配线程 DOD。Classic 只在主自然点做一份家族上下文，不扩全矩阵。另一后端只做一个自然短对照，分别报告。
- 每种算法各自独立冷启动，完整记录首个输出与后续状态来源；B/C 必须使用同一中立种子及初始逻辑身份，DOD/Classic 允许各自演化。冷启动、静止/跳过更新与移动帧分别汇总，不用已收敛的前缀或初始化调用数量差异制造优势。
- 每输入至多 24 次计入回放的更新机会，预先包含静止、移动、转向和返回后的多次静止更新；具体矩阵/顺序由小规划在看新结果前冻结。固定一机会一批，不强制达到某质量再停。没有实际交易就报告覆盖不足，不事后换前缀/挑帧。
- 200k/r=m=640 只保留一个至多八轮的成本压力点，用于确认已知预留放大，不能将其最好帧替换主回放结果。优先复用来源/分析，不再扩 20k→200k×多额度矩阵。
- 正常计时与独立质量/诊断分开；Q 密集评价在选定关键帧的独立回放中进行，逐帧几何身份核对后复用。质量取参考可见集合下的 sampled maximum、当前真实 RMS 口径、Hmax、逐点 Dmax 和返回恢复；记录是否未在冻结轮数内恢复。
- 若普通阶段账本已足够解释，不追加 profiler。只对一个决定性输入采一份 Tracy；存在未定位大头时再一份适用环境 perf。Windows 程序不能直接用 Linux perf 归因；必要 WSL 核心采样单列，不拼成原生帧的函数百分比。
- 交付接入结果、复杂度/空间、剩余性能空间、相对 Legacy 的条件分析及适用范围；按类型关闭接入阶段，不自动开启性能优化阶段。
- 实现情况：未开始。

## 11. 验证与资源停止契约

每阶段遵守[开发规范 §5.8/7.3](../../standards/development_guidelines.md#58-按影响选择测试)和[规划规范 §12](../plan_guideline.md#12-测试与验证)：改前保存相关基线，改后同环境前后对照；已有源码/程序/输入身份一致的证据直接复用。独立进程是统计单位，短帧序列不冒充独立重复或稳定 P95。

开发快验每配置先一个独立进程，预热最多一次、内部短重复最多三次；已足够则停止。明显差异按 `max(0.05ms,5%×基线,已有波动)` 筛查，仅对受影响输入再复测一次。共享 HeightMap、线程池和 renderer 需覆盖对应旧路径；不运行无关全 CTest、不默认两个后端各全矩阵。

每个普通开发阶段预估测量 2～5 分钟，采集首次上限 10 分钟；TPI-05 有限总采集上限 20 分钟，构建/离线评价单列。单个自动进程上限 180s，RSS 初定不超过 8GiB；可用内存不足时采集前登记更低限额。配额与超时按截尾结果保存，不提高额度追求成功。交互更新的内部时间保护在 TPI-02 冻结为故障保护，非承诺帧预算，不能通过按耗时停止改变正常成功轨迹。

下列情况立即停止相应路径并归档：预算/拓扑/输出失效、未排空任务、借用悬空、丢脏范围、错误深度域、切换后串用旧状态。接入造成的缺陷在本阶段已确认边界内修复；若必须改变核心算法或关键架构，先写分析与修订再提交评审。

性能实质退化按规范单独写原因和处理归属，不用新算法局部收益抵消旧 DOD 退化。既有 reservation、dense-Q 和持续质量问题不作为本阶段隐含修复任务；新数据若说明它们阻止实际使用，明确限制作出的结论。

## 12. 出口、风险与取舍

| 出口类型 | 通过条件 | 不代表什么 |
|---|---|---|
| 核心迁移/适配正确性 | 同输入核心与适配结果一致，错误与重置契约成立 | 算法等价于 Legacy |
| 平台持续运行 | 实际更新、切换、静止后续批次与资源输出可用 | 连续几何质量已可接受 |
| 后端接入 | 各自输入/资源/上传/恢复经过实测 | 两后端上传模型相同 |
| 连续质量证据 | 记录实际可见误差、峰值与恢复，保留失败 | sampled 值是连续上界或感知阈值 |
| 性能可行性 | 完整成本与实际工作、质量、资源匹配地比较 | 快帧就是同质量胜出 |
| 剩余优化空间 | 大头有工作/函数证据、已知候选有收益上限或消融 | 所有优化已穷尽 |

推荐取舍集中列出，供本次评审：

1. **独立第三算法**，不嵌入旧 DOD 拓扑状态。替代方案会要求重做一般网格到二叉层次的映射，不符合本轮目标。
2. **核心移出实验层，原探针继续使用同一核心**。不复制一份生产内核，也不让正式应用依赖历史 oracle。
3. **一次当前视图 DOD 初始化**，公开冷启动依赖与费用。无 Legacy 初建不是这轮准入条件。
4. **维持当前算法策略**，仅扩展真实视图/生命周期。固定前缀失配和跨视图质量退化保留为待解释现象。
5. **新算法静止时继续一批/更新机会**，不复用旧 renderer 的“视图不变就跳过”作为新算法收敛判断。
6. **先有限接入，再评估优化**，不以接入后必须快于 DOD 为验收条件，也不承诺接入能自动削减核心成本。

需要在 TPI-01/02 定向核实而不能编造的事项：原生工具链/图形能力、一次 DOD 公共种子的几何适配性、公共 float 输出的可表示范围、普通交互近面失败覆盖。若证据否定上述推荐协议，停止相关实现并修改本规划，不静默换种子或放松数值断言。

## 13. 交付与架构审查

各小规划、代码事实、实施审查按 `docs/plans|codebase|reviews/cpu_refinement/tpi_0N_*` 归档。结果与最后性能分析在 `docs/research/cpu_refinement/`；原始数据、二进制、trace 和质量导出放 `benchmark-output/cpu-refinement/tpi-0N/<run-id>/`，保持忽略，来源/命令/版本清单可追溯。

完成后逐项对比：目录与依赖是否符合 §4；核心是否仍独立；控制/种子/输出有没有堆入 renderer；是否意外链接研究设施；是否加入无登记全域复制；Settings/Stats/范围是否跨两后端一致；是否把诊断或 CPU/GPU 重叠时间混入统计。审查结论写入规定目录，不以构建成功代替架构核查。

本轮方案自查见[规划审查](../../reviews/cpu_refinement/transactional_platform_integration_plan_review.md)。用户已确认本规划；当前尚未新增运行代码，接下来从 TPI-01 开始实施，历史实验结果保持原记录。
