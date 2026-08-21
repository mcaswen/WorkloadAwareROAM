# Parallel ROAM Classic/DOD Baseline

本项目是一个面向高度图地形的自适应网格细分研究基线，提供两条共享输入、共享误差口径和共享验证条件的 CPU ROAM 实现：

- Classic CPU ROAM：以指针式节点和 AoS 状态为主的经典实现；
- Data-Oriented CPU ROAM：以稳定索引、SoA 状态、数据局部性和受控并行为主的数据导向实现。

仓库保留 OpenGL 与 D3D12 双渲染后端、vendored 依赖、自动测试、固定相机路径和阶段化 benchmark，便于从同一可复现基线开展新的研究问题。

## 研究边界

Classic 和 DOD 必须使用相同的：

- 高度图、相机路径、视锥输入和屏幕空间误差定义；
- split/merge 像素阈值、最大深度和活动三角形预算；
- 裂缝约束、拓扑验证、增量 mesh 输出契约；
- benchmark 采样点、统计字段和报告生成方式。

DOD 可以采用由稳定索引、SoA、批处理、连续内存和数据并行直接带来的实现差异。若某项优化同样适用于普通 AoS/指针实现，则必须在 Classic 中实现等价逻辑，或明确证明它属于数据导向布局才能获得的优势区间，不能仅因实现位置不同而把通用优化记为 DOD 收益。

## 当前能力

| 能力 | Classic | Data-Oriented |
|---|---:|---:|
| 屏幕空间误差 split/merge | 是 | 是 |
| 固定活动三角形预算 | 是 | 是 |
| diamond/局部裂缝约束 | 是 | 是 |
| 持久 split/merge queue | 是 | 是 |
| 增量 CPU mesh emit | 是 | 是 |
| 拓扑验证 | 是 | 是 |
| OpenGL/D3D12 CPU mesh 渲染 | 是 | 是 |
| 统一 runtime benchmark | 是 | 是 |
| SoA 和稳定索引 | 否 | 是 |
| chunk 并行拓扑提交 | 否 | 是 |

## 构建环境

- Windows 10/11；
- Visual Studio 2022 C++ 工具链；
- 系统中需要安装 CMake 3.24 或更高版本，并确保 `cmake`、`ctest` 在 PATH 中可用；
- SDL2、GLM、GLAD、Dear ImGui 和 stb 已保存在 third_party；
- D3D12 使用固定版本的 Agility SDK 与 DXC，由脚本下载到被忽略的本地目录。

### OpenGL RelWithDebInfo

    cmake --preset relwithdebinfo-fetch
    cmake --build --preset relwithdebinfo-fetch --parallel

### D3D12 RelWithDebInfo

首次配置 D3D12 前运行：

    powershell -ExecutionPolicy Bypass -File scripts/setup_dx12_dependencies.ps1
    cmake --preset relwithdebinfo-d3d12-fetch
    cmake --build --preset relwithdebinfo-d3d12-fetch --parallel

固定依赖损坏或版本不匹配时可重新安装：

    powershell -ExecutionPolicy Bypass -File scripts/setup_dx12_dependencies.ps1 -Force

## 运行

### 交互应用

    .\build\relwithdebinfo-fetch\bin\ParallelROAM.exe

D3D12 版本：

    .\build\relwithdebinfo-d3d12-fetch\bin\ParallelROAM.exe

### Headless benchmark

    .\build\relwithdebinfo-fetch\bin\ParallelROAM.exe --benchmark --algorithm all --profile smoke --csv benchmark-output\smoke.csv

--algorithm 支持 classic、dod 和 all。常用 profile 包括：

- smoke：快速正确性与基本性能验证；
- budget-reentry：预算下降后再上升的拓扑重入；
- incremental-emit：增量 mesh 输出；
- 其他实验 profile 以 src/benchmark/TerrainLodBenchmark.cpp 为准。

### Runtime benchmark

    .\build\relwithdebinfo-fetch\bin\ParallelROAM.exe --runtime-benchmark

该入口按相同离散相机采样点依次运行 Classic 和 DOD，并输出 Markdown 与逐点 CSV。可通过 runtime benchmark 参数覆盖高度图、采样数、最大深度和 split/merge 阈值。

## 测试

    ctest --test-dir build\relwithdebinfo-fetch -C RelWithDebInfo --output-on-failure

当前测试覆盖：

- C++ 注释覆盖率；
- performance timer；
- nested wedgie 与屏幕投影；
- terrain LOD view；
- Classic/DOD 预算重入；
- DOD split queue view；
- Classic/DOD 增量 mesh emit。

## 目录

    assets/                     高度图、纹理和双后端 shader
    benchmark-output/           可复现实验输入与历史输出
    cmake/                      构建选项、依赖和固定 D3D12 runtime
    docs/parallel-roam/         算法、实验和工程说明
    scripts/                    构建、依赖、报告与图表脚本
    src/algorithms/classic_roam Classic CPU ROAM
    src/algorithms/data_oriented_roam
                                Data-Oriented CPU ROAM
    src/app/                    应用与 runtime benchmark
    src/benchmark/              headless benchmark 和 probe
    src/render/                 OpenGL/D3D12 渲染后端
    tests/                      CTest 与算法回归入口
    third_party/                固定第三方源码
    tools/                      项目检查工具

## 核心文档

| 文档 | 内容 |
|---|---|
| [项目规划](Parallel_ROAM_Project_Plan.md) | 基线状态、研究约束和验收门槛 |
| [里程碑](docs/parallel-roam/04-milestones.md) | Classic/DOD 实现阶段与历史结果 |
| [实验与 benchmark](docs/parallel-roam/05-experiments-and-benchmarks.md) | 指标、场景和报告口径 |
| [开发规范](docs/parallel-roam/09-development-guidelines.md) | 模块边界、命名和验证规则 |
| [依赖与构建](docs/parallel-roam/10-dependency-setup.md) | 双后端可复现构建 |
| [问题修复记录](docs/parallel-roam/11-bug-fix-log.md) | 关键正确性与性能问题 |
| [Classic/DOD 优化计划](docs/parallel-roam/17-dod-classic-optimization-plan.md) | 数据导向优化阶段与公平性约束 |
| [Workload-Aware 自适应执行研究计划](docs/parallel-roam/18-workload-aware-adaptive-execution-research-plan.md) | 研究问题、假设和论文结果结构 |
| [Workload-Aware 具体问题定义](docs/parallel-roam/19-workload-aware-problem-definition.md) | pass、二维 workload matrix 和 feature 定义 |
| [Workload-Aware 策略定义](docs/parallel-roam/20-workload-aware-strategy-definition.md) | 策略接口、基线、oracle 和消融 |
| [Workload-Aware 实验设计](docs/parallel-roam/21-workload-aware-experiment-design.md) | 两层实验、规模、指标和验证标准 |

## 可复现性要求

正式对比报告必须记录：

- Git 提交、构建类型、编译器和图形后端；
- 高度图、相机采样数、分辨率和算法参数；
- CPU、GPU、驱动和 D3D12 runtime/OpenGL 版本；
- 每组测试的 warmup、样本量和统计方法；
- 拓扑非法数、预算违规数和最终三角形数。

性能结论只在正确性、预算和拓扑检查全部通过后成立。

## License

项目自有代码采用 [MIT License](LICENSE)。third_party 中的依赖保留各自许可证。
