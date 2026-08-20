# 依赖、构建与验证

> 当前基线保留 OpenGL/D3D12 双后端和 vendored 依赖，使用本机 CMake。两种后端都只消费 Classic/DOD 的 CPU mesh 输出。

## 1. 固定工具与依赖

仓库内包含：

- third_party/SDL2；
- third_party/glm；
- third_party/glad；
- third_party/imgui；
- third_party/stb。

CMake 会优先使用系统包或项目内固定源码。带 -fetch 的 preset 允许在缺少本地依赖时使用 FetchContent。

构建机需要安装 CMake 3.24 或更高版本，并确保 `cmake` 和 `ctest` 在 PATH 中可用。

## 2. D3D12 固定依赖

D3D12 构建使用固定版本的 Agility SDK 和 DXC。首次配置前运行：

    powershell -ExecutionPolicy Bypass -File scripts/setup_dx12_dependencies.ps1

脚本会验证 NuGet 包和关键文件的 SHA-256，并安装到：

    third_party/microsoft/d3d12-agility-sdk/1.614.1
    third_party/microsoft/dxc/1.7.2308.12

这些下载目录不进入 Git。需要强制恢复时运行：

    powershell -ExecutionPolicy Bypass -File scripts/setup_dx12_dependencies.ps1 -Force

构建系统会验证 D3D12Core.dll、d3d12SDKLayers.dll、dxc.exe、dxcompiler.dll 和 dxil.dll 的固定哈希，并在构建后把运行时和已编译 shader 复制到可执行文件目录。

## 3. CMake presets

| Preset | 后端 | 构建类型 | 依赖策略 |
|---|---|---|---|
| debug-fetch | OpenGL | Debug | 本地优先，允许 FetchContent |
| relwithdebinfo-fetch | OpenGL | RelWithDebInfo | 本地优先，允许 FetchContent |
| release-fetch | OpenGL | Release | 本地优先，允许 FetchContent |
| debug-d3d12-fetch | D3D12 | Debug | 固定 D3D12 runtime |
| relwithdebinfo-d3d12-fetch | D3D12 | RelWithDebInfo | 固定 D3D12 runtime |
| release-d3d12-fetch | D3D12 | Release | 固定 D3D12 runtime |

## 4. OpenGL 构建

    cmake --preset relwithdebinfo-fetch
    cmake --build --preset relwithdebinfo-fetch --parallel

运行：

    .\build\relwithdebinfo-fetch\bin\ParallelROAM.exe

OpenGL terrain renderer 使用 4.1 core context。

## 5. D3D12 构建

    cmake --preset relwithdebinfo-d3d12-fetch
    cmake --build --preset relwithdebinfo-d3d12-fetch --parallel

运行：

    .\build\relwithdebinfo-d3d12-fetch\bin\ParallelROAM.exe

D3D12 后端最低使用 Feature Level 12_0，地形 shader 由固定 DXC 编译。

## 6. 自动测试

    ctest --test-dir build\relwithdebinfo-fetch -C RelWithDebInfo --output-on-failure

CTest 覆盖注释率、计时器、nested wedgie、屏幕投影、视锥输入、Classic/DOD 预算重入、DOD split queue view 和两条算法的增量 mesh emit。

## 7. Headless benchmark

快速验证：

    .\build\relwithdebinfo-fetch\bin\ParallelROAM.exe --benchmark --algorithm all --profile smoke --csv build\classic-dod-smoke.csv

预算重入：

    .\build\relwithdebinfo-fetch\bin\ParallelROAM.exe --benchmark --algorithm all --profile budget-reentry

增量输出：

    .\build\relwithdebinfo-fetch\bin\ParallelROAM.exe --benchmark --algorithm all --profile incremental-emit

所有 profile 必须同时满足活动三角形预算、邻接、T-junction 和非法拓扑断言。

## 8. Runtime benchmark

    .\build\relwithdebinfo-fetch\bin\ParallelROAM.exe --runtime-benchmark

默认相机路径使用固定离散采样点，Classic 和 DOD 顺序运行相同输入。输出位于 benchmark-output，包含汇总 Markdown 和逐点 CSV。

常用覆盖参数：

    --runtime-benchmark-heightmap peking
    --runtime-benchmark-samples 600
    --runtime-benchmark-depth 14
    --runtime-benchmark-split-pixels 4.0
    --runtime-benchmark-merge-pixels 2.0
    --runtime-benchmark-label experiment-name

## 9. 常见问题

### 找不到 CMake

安装 CMake 3.24 或更高版本，并将 CMake 的 `bin` 目录加入 PATH。然后验证：

    cmake --version
    ctest --version

### D3D12 固定依赖缺失或哈希错误

    powershell -ExecutionPolicy Bypass -File scripts/setup_dx12_dependencies.ps1 -Force

### 旧构建目录仍引用已删除目标

CMake build 通常会自动重新配置。若生成器缓存异常，使用新的 preset 构建目录重新配置，不要复用其他后端的目录。

## 10. 可复现记录

正式实验至少记录：

- Git 提交和工作区状态；
- preset、构建类型和编译器版本；
- CPU、GPU、驱动、后端和图形 runtime 版本；
- 高度图、分辨率、采样点和相机路径；
- 最大深度、split/merge 阈值、triangle budget；
- warmup、样本量和统计方式；
- 拓扑非法数和预算违规数。
