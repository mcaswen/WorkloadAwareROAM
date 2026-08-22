# 面向动态不规则拓扑更新的 Workload-Aware ROAM：Pass 级 Execution Crossover 建模与自适应执行

本项目是一个面向高度图地形的 ROAM 研究与实验平台。项目以 ROAM 1997 的核心算法为基础，实现了 Classic CPU ROAM 和 Data-Oriented CPU ROAM（DOD）两条可对照路径，并在此基础上研究：面对持续变化的网格规模、活动候选、脏数据分布和线程负载，各处理阶段应在什么条件下选择串行、并行、增量或全量实现。

**研究状态：阶段 0 的观测与确定性重放基线已完成，阶段 1 的显式策略控制尚未开始。**

Classic 和 DOD 基线、统一误差口径、固定三角形预算、拓扑验证、增量网格提交、OpenGL/D3D12 双图形后端、无窗口基准测试和应用级运行时基准测试均已可运行。当前已经接入统一的 `PassTrace`、输入与结果哈希、队列完整性证据和固定轨迹确定性重放；按阶段独立控制策略、冻结阶段输入配对、离线最优参考和在线选择策略尚未实现。

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

在不改变 ROAM 评分、候选资格、预算限制和最终拓扑语义的前提下，能否根据每个阶段开始前可获得的低成本特征，选择当帧更合适的合法实现，从而减少阶段耗时、整帧耗时和时间预算超限次数？

**本项目的研究目标是：**

建立一套可重放、可验证、可解释的阶段级实验方法，识别串行与并行、增量与全量实现之间稳定的胜负反转边界，并设计一个决策开销足够低、能够泛化到未参与训练的地形和相机轨迹的在线选择策略。

若 `p` 表示处理阶段，`a` 表示该阶段的一种合法实现，`x` 表示冻结的输入状态，则阶段的最佳实现可以写为：

```text
a*(p, x) = argmin_a T(p, a, x)
```

其中 `T(p, a, x)` 必须包含该实现引入的完整成本，而不只是并行循环本身：

```text
T = prepare + schedule + compute + merge + synchronize + finalize
```

在线策略的主要评价量是它相对离线参考的额外耗时：

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

## 研究假设

项目围绕以下可证伪假设展开：

| 假设 | 验证目标 |
| --- | --- |
| H1 胜负反转 | 至少三个代表性阶段在可复现的工作负载区间内出现稳定的实现胜负反转；若不存在，则收缩研究范围 |
| H2 可预测性 | 使用决策前可获得的结构与负载特征，预测结果显著优于单一候选数量阈值 |
| H3 端到端收益 | 计入特征提取和策略选择开销后，在线策略优于固定串行、最大安全并行和最佳静态组合 |
| H4 正确性与泛化 | 在未参与训练的地形和相机轨迹上保持预算、拓扑与结果等价，并维持可接受的相对损失 |

H1 是继续条件。只有确认多个阶段确实存在稳定的胜负反转，才进入预测模型和在线策略研究；如果只有一个阶段具有明确边界，则将论文主题收缩为该阶段的专门优化问题。

详细的继续条件、停止条件和实验设计见[研究计划](docs/parallel-roam/18-workload-aware-adaptive-execution-research-plan.md)、[问题定义](docs/parallel-roam/19-workload-aware-problem-definition.md)和[实验设计](docs/parallel-roam/21-workload-aware-experiment-design.md)。

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
| Data-Oriented CPU ROAM | SoA 节点池、索引邻接、持久 `Q_s/Q_m`、批量评分和条件并行 | 与 Classic 共用误差、阈值、预算和验证口径；支持并行评分、安全候选的并行辅助处理、活动索引和增量网格提交 | 当前并行选择分散在内部阈值与开关中，尚不能独立重放每个阶段的全部合法实现 |
| 阶段级策略变体 | 计划在 DOD 内建立的受控实验路径 | 已记录请求值、实际值、线程数量、规模、回退原因和耗时，并支持固定轨迹哈希重放 | 尚未完成按阶段控制和同一冻结输入上的策略配对 |
| 工作负载感知在线策略 | 计划中的研究变体 | 特征范围、固定基线、离线参考和验收指标已经定义 | 尚未训练、实现或验证，不应视为当前功能 |

### 各阶段的真实语义

| 阶段 | 当前语义 | 尚需补齐的实验能力 |
| --- | --- | --- |
| 合并评分 | `Q_m` 成员局部维护，活动成员的评分整批刷新；评分计算可并行，堆重建串行 | 显式区分并重放 `SerialRefresh` 与 `ParallelRefresh` |
| 细分扫描与评分 | `Q_s` 成员局部维护，活动成员的评分整批刷新；评分可并行，堆重建、快照和分块准备串行 | 把评分刷新与候选快照/分块开关解耦 |
| 合并拓扑 | 安全的内部候选可以并行辅助处理；结果合并、活动索引、`Q_m` 更新和最终收敛由主线程完成 | 增加显式策略，并在相同冻结输入上比较串行立即处理与并行辅助处理 |
| 细分拓扑 | 已有可复用子节点的安全候选可以并行辅助处理；预算、强制细分链和最终收敛由主线程完成 | 增加显式策略，并为不同合法顺序建立规范化等价判断 |
| 网格提交 | 拓扑编辑重放、槽位和区间维护串行；脏槽位写入超过阈值时可并行 | 增加全量对照和规范化网格哈希 |
| CPU 上传 | 首次建立或资源变化时全量上传，后续可按脏区间上传；图形 API 调用留在渲染线程 | 建立可控的 `DirtyRange` 与 `FullBuffer` 对照 |

现有的 `EnableParallelSplit` 只影响细分候选快照、分块和预处理，不控制 `Q_s` 的并行评分，也不等价于“整个细分阶段串行或并行”。后续重构会保留这一事实边界，避免用一个混合开关代表完整阶段策略。

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

截至 2026-08-22，`RelWithDebInfo` 配置下现有 12 项 CTest 均通过。

## 基准测试

### 无窗口算法回归

```powershell
.\build\relwithdebinfo-fetch\bin\ParallelROAM.exe `
  --benchmark `
  --algorithm all `
  --profile standard
```

可选算法为 `classic|dod|all`，可选配置为 `smoke|budget-reentry|budget-saturation|incremental-emit|pass-trace-replay|standard`。其中 `incremental-emit` 使用重复视点区分首次建立、一次属性变化和随后无脏区间复用；`pass-trace-replay` 会在重置后重复固定轨迹，并逐帧比较输入、拓扑、活动叶和网格哈希。

需要保存逐帧数据时，可增加：

```powershell
--csv benchmark-output\standard.csv
```

### 应用级运行时实验

```powershell
.\build\relwithdebinfo-fetch\bin\ParallelROAM.exe --runtime-benchmark
```

运行时基准测试会让可用算法依次经过相同的离散相机采样点，并在 `benchmark-output/` 生成：

- `runtime-benchmark-<timestamp>.md`：中文汇总和阶段对比
- `runtime-benchmark-<timestamp>.csv`：逐帧原始数据

默认路径包含 600 个采样点，预算饱和压力路径包含 64 个采样点。每种算法按相同 `sampleIndex` 执行，算法快慢不会改变采样密度。可以使用以下参数覆盖实验配置：

- `--runtime-benchmark-heightmap`
- `--runtime-benchmark-path default|budget-saturation`
- `--runtime-benchmark-terrain-size`
- `--runtime-benchmark-height-scale`
- `--runtime-benchmark-max-depth`
- `--runtime-benchmark-split-pixels`
- `--runtime-benchmark-merge-pixels`
- `--runtime-benchmark-samples`
- `--runtime-benchmark-label`

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

正式研究实验不会只比较一次运行结果。每个实现需要独立预热，成对轮换或随机化执行顺序，重复运行并报告中位数、P95 和置信区间，同时把准备、调度、合并、同步、策略选择与图形上传成本计入对应结果。

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
| [研究计划](docs/parallel-roam/18-workload-aware-adaptive-execution-research-plan.md) | 研究问题、贡献边界、H1-H4、继续和停止条件 |
| [问题定义](docs/parallel-roam/19-workload-aware-problem-definition.md) | 阶段边界、合法实现、正确性约束和时间预算 |
| [策略定义](docs/parallel-roam/20-workload-aware-strategy-definition.md) | 固定基线、离线参考、在线策略和回退语义 |
| [实验设计](docs/parallel-roam/21-workload-aware-experiment-design.md) | 工作负载、特征、统计方法、泛化与消融实验 |
| [源码重构规划](docs/parallel-roam/22-workload-aware-source-refactoring-plan.md) | 当前真实语义、最小改造顺序和可选前沿维护 |
| [实验与基准测试](docs/parallel-roam/05-experiments-and-benchmarks.md) | 指标、字段定义、实验流程和现有结果 |
| [开发规范](docs/parallel-roam/09-development-guidelines.md) | 代码结构、中文术语和注释规则 |
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
- 最大/P95/平均屏幕空间误差、预算利用率、拓扑错误和结果哈希
- 在线策略开销、时间预算超限次数和相对离线参考的额外耗时

训练集和测试集必须按完整地形或完整相机轨迹划分，不能把同一轨迹的相邻帧随机分到两侧。

## 当前限制

- 阶段级策略控制和同一冻结输入上的策略配对尚未实现；当前重放入口通过重置后确定性重建固定轨迹
- 当前 DOD 混合使用串行和并行步骤，现有开关不能代表整个阶段的完整策略
- 当前并行拓扑只处理经过保守筛选的安全候选，预算、强制闭合和最终收敛仍是有序流程
- 活动队列成员采用局部维护，但活动成员评分仍会整批刷新；不能把当前评分阶段描述为完整增量算法
- 增量网格在变化稀疏时收益明显，但已有压力实验表明高变化率下可能输给全量路径，需要正式建立胜负反转边界
- 当前场景和 DEM 多样性不足，旧性能数据也需要在统一误差口径下重新采集
- D3D12 当前只是 CPU 网格的图形后端，不属于 GPU ROAM 实现
- 在线模型、贪心阶段参考和整帧离线参考均尚未实现，因此当前不能宣称自适应策略已经获得端到端收益

## 路线图

1. 已完成阶段 0：统一 `PassTrace`、结果证据、CSV 输出和固定轨迹确定性重放
2. 建立冻结阶段输入和规范化结果，使同一阶段的合法实现能够成对比较
3. 以最小改造补齐评分、拓扑、网格提交和 CPU 上传的显式策略对照
4. 运行胜负反转实验；若 H1 不成立，立即收缩或更换研究问题
5. 建立贪心阶段参考和选定帧的整帧离线参考，测量阶段之间的耦合
6. 训练并实现低成本在线选择策略，与固定串行、最大安全并行和最佳静态组合比较
7. 在未参与训练的地形、轨迹、线程配置和时间预算上验证泛化与正确性
8. 仅当主研究完成后确认活动索引或持久队列局部维护成为稳定瓶颈，再把 `FrontierMaintenance` 作为可选实现加入；默认保留当前立即增量维护，不让该实验阻塞主线

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
