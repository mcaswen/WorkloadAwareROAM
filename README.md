# WorkloadAwareROAM：CPU ROAM 多阶段执行、并行优化与性能边界

本项目以高度图地形上的持久 ROAM 更新为对象，研究共享内存 CPU 上的多阶段执行组织、并行实现与优化，以及不同工作量下的性能边界。Classic CPU ROAM 与 Data-Oriented CPU ROAM（DOD）提供可对照路径，OpenGL/D3D12 消费统一 CPU 网格输出。

**当前研究定义（2026-09-12）：[实验问题、贡献、已有工作与后续工作](docs/plans/formal_experiment/cpu_roam_research_definition.md)。** 多阶段组织、CPU 并行实现与优化、有效对照和实证发现共同构成候选贡献；不再以多个阶段必须出现 crossover 或必须实现在线选择器作为整篇研究成立的前提。

五个 CPU 阶段的显式策略、冻结状态配对、真实执行与回退记录及结果核验已实现。[P5 探索](docs/reviews/formal_experiment/prep_05_cpu_crossover_pilot_gate_review.md)完成，原广泛交叉门槛未满足；网格提交观察到有限双向信号，评分主要显示并行侧优势，拓扑受自然并行覆盖限制。排序松弛 GATE-03 和 [CPU-CBT 原型](obsolete/cpu_cbt/README.md)已归档。下一步先审计贡献与基线，再以最小对照验证固定配置的完整 CPU 更新收益；尚未证明相对有竞争力的 Classic 的固定幅度加速。

## 项目简介

并行实现并不会在所有工作负载下稳定胜过串行实现。候选较少时，线程调度、任务划分、局部结果合并和同步成本可能高于实际计算；候选较多或分布不均时，并行才可能获得净收益。类似地，增量网格提交适合局部变化，但在脏槽位和脏区间大量增长时，维护与合并这些区间的成本可能超过一次全量重建。

因此，本项目把 DOD 更新流程拆成具有明确输入、输出和正确性边界的阶段：

| 阶段 | 当前主要工作 | 计划比较的实现 |
| --- | --- | --- |
| 合并评分 | 刷新 `Q_m` 中活动菱形的优先级 | `SerialRefresh` / `ParallelRefresh` |
| 细分扫描与评分 | 刷新 `Q_s`，形成满足条件的细分候选 | `SerialRefresh` / `ParallelRefresh` |
| 合并拓扑 | 校验菱形、执行合并并维护活动集合 | `SerialImmediate` / `ParallelAssisted` |
| 细分拓扑 | 处理预算、强制细分和邻接闭合 | `SerialImmediate` / `ParallelAssisted` |
| 网格提交 | 把拓扑变化映射到索引网格 | `SerialDirty` / `ParallelDirty` / `SerialFull`，必要时再评估 `ParallelFull` |
| CPU 上传 | 把网格变化交给图形后端 | `DirtyRange` / `FullBuffer` |

这里的“并行辅助”只处理已经证明安全的候选子集，预算裁决、强制闭合、活动索引维护、队列更新和最终收敛仍需要有序完成。

**本项目研究的问题是：**

在保持声明的 ROAM 评分、预算、拓扑与结果语义的条件下，如何组织和优化各 CPU 阶段，哪些工作能够真实并行，这些实现相对公平基线能取得什么局部及完整更新收益，其适用边界如何随工作量变化？

**本项目的研究目标是：**

把执行组织、并行实现与优化及受控实验形成完整论证，区分稳定单侧优势、真实交叉、依赖回退和证据不足。固定混合配置可以成为实践结果；在线选择仅在固定配置之外存在值得利用的收益空间时再研究。

若 `p` 表示处理阶段，`a` 表示该阶段的一种合法实现，`x` 表示冻结的输入状态，则阶段的最佳实现可以写为：

```text
a*(p, x) = argmin_a T(p, a, x)
```

其中 `T(p, a, x)` 必须包含该实现引入的完整成本，而不只是并行循环本身：

```text
T = prepare + schedule + compute + merge + synchronize + finalize
```

上述逐阶段最小值只用于解释候选集合，不能把不同状态的最小值相加当作可执行的整帧最优配置。若后续另行研究在线策略，可以评价它相对明确参考集合的额外耗时：

```text
relative_loss = (T_online - T_reference) / T_reference
```

任何更快的结果只有在以下条件同时成立时才有效：

```text
same_frozen_input
topology_valid
triangle_budget_valid
equivalent_result
```

## 研究问题与历史假设

当前围绕三个问题组织研究，具体约束、候选贡献和未完成工作见[当前定义](docs/plans/formal_experiment/cpu_roam_research_definition.md)：

| 问题 | 验证目标 |
| --- | --- |
| RQ1 执行组织 | 明确阶段依赖与可替换边界，核验真实多线程工作和回退 |
| RQ2 实现与优化收益 | 分离数据组织、串行优化和线程收益，验证完整 CPU 更新成本 |
| RQ3 性能边界与配置 | 解释单侧优势、交叉和依赖限制，在独立轨迹验证冻结的固定配置 |

旧 H1～H4 以广泛 crossover 和在线选择为中心，P5 未满足其中完整路线门槛。该历史结果不改写，也不再作为 CPU ROAM 优化与执行分析整体研究的否决条件。

历史契约见[原研究计划](docs/parallel-roam/18-workload-aware-adaptive-execution-research-plan.md)和[原实验设计](docs/parallel-roam/21-workload-aware-experiment-design.md)。新实验须另行冻结最小范围，不自动恢复 PREP-06、质量恢复或 GPU 路线。

## 快速开始

### 环境要求

- C++20 编译器
- CMake 3.24 或更高版本
- OpenGL 4.1 驱动，或 Windows D3D12 环境
- Visual Studio C++ 工具链和 Windows SDK

仓库包含 SDL2、GLM、GLAD、Dear ImGui 和 stb 等固定依赖。普通预设使用系统 CMake，并优先使用系统包或项目内 `third_party`；默认不主动联网。

### OpenGL

```powershell
cmake --preset relwithdebinfo-fetch
cmake --build --preset relwithdebinfo-fetch --parallel
.\build\relwithdebinfo-fetch\bin\ParallelROAM.exe
```

也可以使用快捷脚本：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/run_relwithdebinfo_fetch.ps1
```

### D3D12

首次使用 D3D12 前，先准备项目固定的 Agility SDK 和 DXC 依赖：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/setup_dx12_dependencies.ps1
```

然后配置、构建并运行：

```powershell
cmake --preset relwithdebinfo-d3d12-fetch
cmake --build --preset relwithdebinfo-d3d12-fetch --parallel
.\build\relwithdebinfo-d3d12-fetch\bin\ParallelROAM.exe
```

依赖版本、离线策略和平台差异见[依赖配置说明](docs/parallel-roam/10-dependency-setup.md)。

## 交互操作

| 操作 | 输入 |
| --- | --- |
| 前后左右移动 | `W` / `S` / `A` / `D` |
| 上升/下降 | `Space` / `Ctrl` |
| 加速移动 | `Shift` |
| 旋转视角 | 按住鼠标右键并移动 |
| 退出 | `Esc` |

右侧面板可以切换高度图、线框、LOD 算法和调试着色，并调整地形尺寸、高度缩放、最大深度、细分/合并像素阈值、三角形预算、局部约束、拓扑验证与光照参数。

## 当前实现

### 算法路径

| 路径 | 数据与执行方式 | 当前能力 | 边界 |
| --- | --- | --- | --- |
| Classic CPU ROAM | 对象式节点、裸指针二叉三角树、串行索引堆 | 持久 `Q_s/Q_m`、统一交叉调度、细分、强制细分、菱形合并、视锥感知、固定活动叶三角形预算、增量索引网格 | 作为正确性和行为对照；不复现原论文的全部最优性机制 |
| Data-Oriented CPU ROAM | SoA 节点池、索引邻接、持久 `Q_s/Q_m`、批量评分和条件并行 | 与 Classic 共用误差、阈值、预算和验证口径；支持评分、拓扑和网格提交的独立策略与线程上限 | 并行拓扑仍是安全候选并行辅助加主线程收敛，不是完全并行实现 |
| 阶段级策略变体 | DOD 内的受控实验路径 | 已实现固定串行/最大安全并行与增量/全量输出的四种组合、自定义阶段策略和冻结拓扑配对；评分、建堆、候选快照和成员维护成本分别记录 | 当前全量网格写入是串行实现；冻结配对只用于显式证据模式，不进入普通交互路径 |
| 工作负载感知在线策略 | 计划中的研究变体 | 特征范围、固定基线、离线参考和验收指标已经定义 | 尚未训练、实现或验证，不应视为当前功能 |

### 各阶段的真实语义

| 阶段 | 当前语义 | 已实现的策略控制与剩余工作 |
| --- | --- | --- |
| 合并评分 | `Q_m` 成员局部维护，活动成员的评分整批刷新；评分计算可并行，堆重建串行 | 已支持 `SerialRefresh` / `ParallelRefresh`；独立记录条目数、评分、建堆、成员维护数量与成本 |
| 细分扫描与评分 | `Q_s` 成员局部维护，活动成员的评分整批刷新；评分可并行，堆重建串行 | 已支持 `SerialRefresh` / `ParallelRefresh`；候选快照独立计入拓扑准备 |
| 合并拓扑 | 安全的内部候选可以并行辅助处理；结果合并、活动索引、`Q_m` 更新和最终收敛由主线程完成 | 已支持 `SerialImmediate` / `ParallelAssisted` 和同一冻结候选的配对回放；状态复制、分块、提交、结果整理、索引刷新与串行收敛分别计时 |
| 细分拓扑 | 已有可复用子节点且无需强制闭合的安全候选可以并行辅助处理；预算、强制细分链和最终收敛由主线程完成 | 已支持 `SerialImmediate` / `ParallelAssisted` 和同一冻结候选的配对回放；不满足安全条件的项目保留在长期队列中顺序收敛 |
| 网格提交 | 拓扑编辑重放与槽位维护串行，随后按策略写脏槽位或完整槽位集合 | 已支持 `SerialDirty` / `ParallelDirty` / `SerialFull` 和规范化网格哈希 |
| CPU 上传 | 图形 API 调用留在渲染线程，可请求脏区间或完整缓冲区 | 已支持 `DirtyRange` / `FullBuffer`，并记录容量、初始化和 D3D12 帧槽积压回退 |

旧的 `EnableParallelSplit` 仅作为兼容入口保留：显式策略未设置时，关闭它会把细分拓扑映射为 `SerialImmediate`，但不会改变 `Q_s` 的评分策略。正式实验使用阶段策略和记录中的请求值/实际值，不再把该旧开关解释为整个细分流程的串行或并行。

### 固定策略预设

| 预设 | 评分与拓扑 | 网格提交 | CPU 上传 |
| --- | --- | --- | --- |
| 固定串行 + 增量输出 | 评分串行刷新，拓扑由主线程顺序处理 | `SerialDirty` | `DirtyRange` |
| 最大安全并行 + 增量输出 | 评分请求并行刷新，拓扑请求安全候选并行辅助 | `ParallelDirty` | `DirtyRange` |
| 固定串行 + 全量输出 | 评分串行刷新，拓扑由主线程顺序处理 | `SerialFull` | `FullBuffer` |
| 最大安全并行 + 全量输出 | 评分请求并行刷新，拓扑请求安全候选并行辅助 | `SerialFull` | `FullBuffer` |

“最大安全并行 + 全量输出”中的全量网格写入仍是串行实现；该预设保留的是执行方式与输出范围的完整组合，不代表已经实现 `ParallelFull`。

### 统一误差、预算与拓扑语义

Classic 与 DOD 当前共享：

- ROAM 1997 第 6.1 节公式 (1)：自底向上预计算嵌套楔形厚度
- ROAM 1997 第 6.2 节公式 (2)/(3)：将楔形厚度保守投影为屏幕空间几何误差
- 楔形误差包围体穿越近裁剪面时使用人工最大优先级
- 完整 `ViewProjection`、可绘制区域尺寸和六平面视锥输入
- 像素单位的细分/合并双阈值和迟滞
- 活动叶三角形 `TriangleBudget` 上限
- 强制细分、菱形合并和可选拓扑验证
- 独立的投影边密度约束，避免几何误差接近零时平坦区域过度粗糙
- 增量索引 CPU 网格输出

DOD 在预算满载时，如果最高收益细分仍高于最低损失合并，就先回收最低损失菱形，再重试最高收益细分，直到队首条件收敛。

### 已完成的工作

- C++20、CMake 与 SDL2 应用框架
- OpenGL 4.1 和 D3D12 双图形后端
- Dear ImGui 参数面板、LOD 调试着色和阶段化性能统计
- 统一的 `ITerrainLodAlgorithm` 接口以及 Classic/DOD 实现
- ROAM 公式 (1)-(3)、连续二叉三角树、强制细分和菱形合并
- Classic/DOD 持久双队列、固定预算和预算重入
- DOD SoA 节点池、活动索引、并行评分和拓扑并行辅助
- Classic/DOD 增量网格提交与脏区间上传路径
- 固定离散相机采样点的运行时基准测试，输出中文 Markdown 和逐帧 CSV
- 统一阶段记录、输入/拓扑/活动叶/网格哈希、队列完整性证据和固定轨迹重放
- 合并评分、细分评分、合并拓扑、细分拓扑、网格提交和 CPU 上传的独立策略控制
- 固定串行/最大安全并行与增量/全量输出组成的四种策略预设及规范化网格等价性回归
- DOD 冻结拓扑状态深复制、串行/并行辅助配对回放及规范化结果证据
- Classic/DOD 公共结果检查、逐帧差异掩码和跨实现契约回归
- CTest、预算重入、增量网格、投影约定和注释覆盖测试

## 测试与验证

运行 CTest：

```powershell
ctest `
  --test-dir build\relwithdebinfo-fetch `
  -C RelWithDebInfo `
  --output-on-failure
```

当前测试覆盖：

- C++ 注释覆盖
- 性能计时器
- 嵌套楔形误差
- 保守屏幕空间投影
- D3D/OpenGL 视图与投影约定
- Classic/DOD 预算重入
- DOD 细分队列视图
- Classic/DOD 增量网格提交
- Classic/DOD 固定轨迹确定性重放
- Classic/DOD 四种执行方式与输出范围组合的策略等价性
- DOD 同一冻结候选的串行/并行辅助拓扑等价性与真实并行合并覆盖
- 公共结果检查及 Classic/DOD 六点跨实现结果契约

截至 2026-08-31，`RelWithDebInfo` 配置下现有 21 项 CTest 均通过。

## 基准测试

### 无窗口算法回归

```powershell
.\build\relwithdebinfo-fetch\bin\ParallelROAM.exe `
  --benchmark `
  --algorithm all `
  --profile standard
```

可选算法为 `classic|dod|all`，可选配置为 `smoke|budget-reentry|budget-saturation|incremental-emit|pass-trace-replay|pass-policy-replay|topology-pair-replay|classic-dod-contract|pass-crossover-replay|pass-crossover-stress-replay|standard`。其中 `incremental-emit` 使用重复视点区分首次建立、一次属性变化和随后无脏区间复用；`pass-trace-replay` 会在重置后重复固定轨迹；`pass-policy-replay` 会依次运行固定串行增量、最大安全并行增量、固定串行全量和最大安全并行全量，并逐帧比较拓扑、活动叶、预算与规范化网格；`topology-pair-replay` 只用于 DOD，将同一冻结候选分别交给串行和并行辅助路径；`classic-dod-contract` 必须与 `--algorithm all` 一起使用，逐帧验证两条实现的公共结果和 Classic 串行阶段映射；两个 `pass-crossover` 配置分别使用默认和压力场景，对五个 CPU pass 做冻结输入配对。

冻结阶段配对可以显式设置预热、重复、线程和目标数量：

```powershell
.\build\relwithdebinfo-fetch\bin\ParallelROAM.exe `
  --benchmark --algorithm dod --profile pass-crossover-replay `
  --pass-warmups 5 --pass-repeats 30 --pass-workers 8 --pass-targets 0 `
  --csv benchmark-output\pass-crossover-default.csv
```

同一 CPU 网格数据包的上传配对通过应用级运行时基准执行，并另存上传 CSV：

```powershell
.\build\relwithdebinfo-fetch\bin\ParallelROAM.exe `
  --runtime-benchmark --runtime-benchmark-upload-pair `
  --runtime-benchmark-upload-warmups 5 `
  --runtime-benchmark-upload-repeats 30 `
  --runtime-benchmark-upload-targets 24
```

普通配置也可以通过 `--pass-policy default|serial-incremental|maximum-parallel-incremental|serial-full|maximum-parallel-full` 固定整套阶段策略。旧名称 `serial`、`maximum-parallel` 和 `parallel` 继续作为增量输出组合的兼容别名。例如：

```powershell
.\build\relwithdebinfo-fetch\bin\ParallelROAM.exe `
  --benchmark `
  --algorithm dod `
  --profile pass-policy-replay
```

需要保存逐帧数据时，可增加：

```powershell
--csv benchmark-output\standard.csv
```

### 应用级运行时实验

```powershell
.\build\relwithdebinfo-fetch\bin\ParallelROAM.exe --runtime-benchmark
```

运行时基准测试会让可用算法分别预热，重置拓扑后再经过相同的离散相机采样点，并在 `benchmark-output/` 生成：

- `runtime-benchmark-<timestamp>.md`：中文汇总和阶段对比
- `runtime-benchmark-<timestamp>.csv`：逐帧原始数据

默认路径包含 600 个采样点，预算饱和压力路径包含 64 个采样点。每种算法按相同 `sampleIndex` 执行，算法快慢不会改变采样密度。可以使用以下参数覆盖实验配置：

- `--runtime-benchmark-heightmap`
- `--runtime-benchmark-path default|budget-saturation`
- `--runtime-benchmark-policy default|serial-incremental|maximum-parallel-incremental|serial-full|maximum-parallel-full`
- `--runtime-benchmark-terrain-size`
- `--runtime-benchmark-height-scale`
- `--runtime-benchmark-max-depth`
- `--runtime-benchmark-split-pixels`
- `--runtime-benchmark-merge-pixels`
- `--runtime-benchmark-samples`
- `--runtime-benchmark-warmup-samples`
- `--runtime-benchmark-order-rotation`
- `--runtime-benchmark-split-topology-min-candidates`
- `--runtime-benchmark-merge-topology-min-candidates`
- `--runtime-benchmark-parallel-topology-target-build`
- `--runtime-benchmark-parallel-topology-phase both|split|merge`
- `--runtime-benchmark-label`

默认路径为每种算法预热 32 个采样点，预算饱和路径预热 4 个采样点。预热帧不写入 CSV；预热结束后会重置算法，使正式采样仍从相同的根拓扑和路径起点开始。重复从界面启动实验时会自动轮换算法顺序；独立进程实验可用 `--runtime-benchmark-order-rotation 0|1` 固定轮换偏移，并在 Markdown 与 CSV 中记录实际顺序。

并行拓扑候选阈值、限定更新编号和限定阶段已经改为显式策略参数，不再读取进程环境变量。更新编号为 `0` 表示每次更新均允许并行辅助拓扑。

阶段改造的应用级验收必须同时运行两条路径：

```powershell
# 默认路径
.\build\relwithdebinfo-fetch\bin\ParallelROAM.exe `
  --runtime-benchmark `
  --runtime-benchmark-path default

# 预算饱和压力路径
.\build\relwithdebinfo-fetch\bin\ParallelROAM.exe `
  --runtime-benchmark `
  --runtime-benchmark-path budget-saturation
```

两条路径都必须完整覆盖全部采样点，并保证预算越界、队列不变量错误、资源验证失败、非法邻接和 T 形裂缝数量为零。

旧参数 `--runtime-benchmark-duration` 仅为兼容保留，每个名义秒换算为 60 个离散采样点。

正式研究实验不会只比较一次运行结果。运行器已经提供独立预热和可复现的顺序轮换；正式结论仍需重复运行并报告中位数、P95 和置信区间，同时把准备、调度、合并、同步、策略选择与图形上传成本计入对应结果。

现有实验口径和历史结果见[实验与基准测试](docs/parallel-roam/05-experiments-and-benchmarks.md)。

## 项目结构

```text
.
├── assets/                  高度图、字体和着色器
├── benchmark-output/        本地运行时基准测试与实验输出
├── cmake/                   CMake 依赖和编译配置
├── docs/
│   ├── parallel-roam/       研究计划、实验、架构和开发规范
│   └── source_analysis/     ROAM 论文、参考实现和源码分析
├── scripts/                 构建、运行、测试与报告脚本
├── src/
│   ├── algorithms/          Classic 与 DOD 算法
│   ├── app/                 主循环、相机和运行时基准测试
│   ├── benchmark/           无窗口基准测试与探针
│   ├── gui/                 ImGui 控制和统计面板
│   ├── render/              OpenGL/D3D12 后端和地形渲染器
│   └── terrain/             高度图与 CPU 网格数据
├── tests/                   CTest 单元、性质与结构测试
├── third_party/             固定第三方源码和参考项目
├── CMakeLists.txt
└── CMakePresets.json
```

## 文档索引

| 文档 | 内容 |
| --- | --- |
| [当前研究定义](docs/plans/formal_experiment/cpu_roam_research_definition.md) | CPU 多阶段执行、并行优化、候选贡献、已有工作及待完成验证 |
| [P5 结论](docs/reviews/formal_experiment/prep_05_cpu_crossover_pilot_gate_review.md) | 已测五阶段的交叉、单侧优势、回退和证据边界 |
| [CPU-CBT 归档](obsolete/cpu_cbt/README.md) | 已关闭原型、历史实现、未决性能问题和恢复说明 |
| [研究计划](docs/parallel-roam/18-workload-aware-adaptive-execution-research-plan.md) | 研究问题、贡献边界、H1-H4、继续和停止条件 |
| [问题定义](docs/parallel-roam/19-workload-aware-problem-definition.md) | 阶段边界、合法实现、正确性约束和时间预算 |
| [策略定义](docs/parallel-roam/20-workload-aware-strategy-definition.md) | 固定基线、离线参考、在线策略和回退语义 |
| [实验设计](docs/parallel-roam/21-workload-aware-experiment-design.md) | 工作负载、特征、统计方法、泛化与消融实验 |
| [源码重构规划](docs/parallel-roam/22-workload-aware-source-refactoring-plan.md) | 当前真实语义、最小改造顺序和可选前沿维护 |
| [Pass 与 Crossover 实验说明](docs/parallel-roam/23-pass-crossover-experiment-specification.md) | 六个阶段、合法策略、反转机制、固定实验规模和统计判定 |
| [正式实验准备与执行计划](docs/parallel-roam/24-formal-experiment-preparation-plan.md) | 高度图与 18 个场景、相机轨迹、正式入口参数、实验轮次和实施清单 |
| [实验与基准测试](docs/parallel-roam/05-experiments-and-benchmarks.md) | 指标、字段定义、实验流程和现有结果 |
| [开发规范](docs/standards/development_guidelines.md) | 代码结构、中文术语、注释和按影响选择验证的规则 |
| [依赖配置说明](docs/parallel-roam/10-dependency-setup.md) | OpenGL/D3D12 依赖、固定版本和构建方式 |

## 可复现实验要求

正式性能与正确性结论至少应固定并记录：

- Git commit、构建配置和图形后端
- CPU、GPU、驱动和操作系统版本
- 高度图、地形尺寸、高度缩放和最大深度
- 细分/合并像素阈值与三角形预算
- 可绘制区域分辨率、FOV、相机路径和 VSync 状态
- 线程数量、亲和性、预热、重复次数和随机种子
- 每个阶段请求的实现、实际采用的实现和回退原因
- 候选数量、活动节点数量、脏槽位、脏区间和负载不均衡特征
- 准备、调度、计算、合并、同步和最终处理耗时
- 预算利用率、拓扑错误和结果证据；需要独立质量比较时，另外固定评价能力与指标
- 若研究在线策略，另外记录选择开销、时间预算超限及相对参考的额外耗时

固定策略选择与最终评价应分离；若需要训练，按完整地形或完整相机轨迹划分，不能把同一轨迹的相邻帧随机分到两侧。

## 当前限制

- 冻结拓扑配对会深复制节点池、活动索引、长期队列和增量网格状态，开销较高，因此只在显式证据模式中运行；普通交互和正式性能路径默认关闭
- DOD 的并行拓扑是并行辅助实现，候选筛选、结果合并和最终收敛仍包含串行步骤，不能描述为完全并行拓扑
- 当前并行拓扑只处理经过保守筛选的安全候选，预算、强制闭合和最终收敛仍是有序流程
- 阶段 3 的六点配对回归中，细分候选均因需要强制闭合或跨越分块而进入串行尾部；合并阶段实际使用最多 8 个线程提前提交，不能据此声称细分拓扑已经获得并行收益
- Classic 在统一优化循环前刷新两条评分队列，DOD 会在合并后刷新细分队列，因此同帧评分条目数量可以不同；评分条目、节点池和网格更新数量用于解释实现成本，不属于跨实现结果等价条件
- 活动队列成员采用局部维护，但活动成员评分仍会整批刷新；不能把当前评分阶段描述为完整增量算法
- 增量网格在变化稀疏时收益明显，但已有压力实验表明高变化率下可能输给全量路径，需要正式建立胜负反转边界
- 当前场景和 DEM 多样性不足，旧性能数据也需要在统一误差口径下重新采集
- D3D12 当前只是 CPU 网格的图形后端，不属于 GPU ROAM 实现
- 在线模型、贪心阶段参考和整帧离线参考均尚未实现，因此当前不能宣称自适应策略已经获得端到端收益

## 路线图

1. 已完成阶段 0：统一 `PassTrace`、结果证据、CSV 输出和固定轨迹确定性重放
2. 已完成阶段 1：补齐评分、拓扑、网格提交和 CPU 上传的显式策略对照与固定串行路径
3. 已完成阶段 2：固定 `Q_s/Q_m` 的全量评分语义，并拆分评分、建堆、候选快照和局部成员维护成本
4. 已完成阶段 3：建立冻结候选快照和拓扑阶段配对，使串行与并行辅助实现能够在同一输入上比较
5. 已完成阶段 4：统一 Classic/DOD 的结果检查、阶段映射、差异记录和跨实现契约回归
6. 已完成 PREP-01～05；保留原交叉门槛未满足及有限网格双向信号
7. 已关闭排序松弛 GATE-03 和 CPU-CBT 原型，保留历史证据与共享能力
8. 按当前研究定义审计贡献和 Classic 基线竞争力，冻结最小完整更新对照
9. 复用阶段配对与必要消融，验证 CPU 并行优化、固定配置收益和适用范围
10. 根据证据再判断是否需要局部动态选择；不将在线模型或新的拓扑算法设为前置任务

## 引用与参考

- ROAM 1997：*Real-time Optimally Adapting Meshes*
- CBT 2020：*Concurrent Binary Trees*
- CBT 2024：*Large-scale Adaptive Mesh Refinement on the GPU*
- Tile-based LOD：地形分块和局部细节层次选择的相关研究
- Temporal coherence：利用连续帧状态预测执行代价的相关研究
- SPAA 2024：可预测并行算法选择与性能建模的相关研究
- `third_party/LibGenROAM010206`：ROAM 参考源码快照

## 许可证

项目自有代码采用 [MIT License](LICENSE)。`third_party/` 中的依赖和参考项目保留各自许可证与使用条件。
