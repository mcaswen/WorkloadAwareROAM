#include <iostream>

#if defined(PARALLEL_ROAM_BUILD_FULL_APP)
#include "app/Application.h"
#include "benchmark/RoamProbe.h"
#include "benchmark/TerrainLodBenchmark.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <exception>
#include <limits>
#include <string>
#include <string_view>
#endif

#if defined(PARALLEL_ROAM_HAS_SDL2)
#include <SDL.h>
#endif

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

#if defined(PARALLEL_ROAM_BUILD_FULL_APP)
    // full app 路径支持无窗口/短窗口入口
    // 参数分流必须早于 Application 初始化
    int maxFrameCount = -1;
    bool fixedFrameSmokeTest = false;
    bool automaticRuntimeBenchmark = false;
    ParallelRoam::App::RuntimeBenchmarkOverrides runtimeBenchmarkOverrides{};
    bool hasRuntimeBenchmarkOverrides = false;
    std::string parseError;

    auto requireValue = [&](int& index, std::string_view option) -> const char* {
        if (index + 1 >= argc)
        {
            parseError = "Missing value for " + std::string{option};
            return nullptr;
        }
        ++index;
        return argv[index];
    };

    auto parseFloatOption = [&](int& index, std::string_view option, float& output) -> bool {
        const char* value = requireValue(index, option);
        if (value == nullptr)
        {
            return false;
        }

        try
        {
            output = std::stof(std::string{value});
        }
        catch (const std::exception&)
        {
            parseError = "Invalid float for " + std::string{option} + ": " + value;
            return false;
        }
        return true;
    };

    auto parseIntOption = [&](int& index, std::string_view option, int& output) -> bool {
        const char* value = requireValue(index, option);
        if (value == nullptr)
        {
            return false;
        }

        try
        {
            output = std::stoi(std::string{value});
        }
        catch (const std::exception&)
        {
            parseError = "Invalid integer for " + std::string{option} + ": " + value;
            return false;
        }
        return true;
    };

    auto parseHeightMapOption = [&](int& index, std::string_view option, int& output) -> bool {
        const char* value = requireValue(index, option);
        if (value == nullptr)
        {
            return false;
        }

        const std::string_view heightMapValue{value};
        if (heightMapValue == "0" || heightMapValue == "test" || heightMapValue == "test129")
        {
            output = 0;
            return true;
        }

        if (heightMapValue == "1" || heightMapValue == "peking" || heightMapValue == "peking513")
        {
            output = 1;
            return true;
        }

        parseError = "Invalid height map for " + std::string{option} + ": " + value;
        return false;
    };
    auto parsePassPolicyOption = [&parseError](
        std::string_view value,
        ParallelRoam::Algorithms::TerrainLodPassPolicy& output) -> bool {
        if (value == "default")
        {
            output = {};
            return true;
        }
        if (value == "serial-incremental" || value == "serial")
        {
            output = ParallelRoam::Algorithms::MakeTerrainLodSerialIncrementalPolicy();
            return true;
        }
        if (value == "maximum-parallel-incremental" ||
            value == "maximum-parallel" || value == "parallel")
        {
            output = ParallelRoam::Algorithms::MakeTerrainLodMaximumSafeParallelIncrementalPolicy();
            return true;
        }
        if (value == "serial-full")
        {
            output = ParallelRoam::Algorithms::MakeTerrainLodSerialFullOutputPolicy();
            return true;
        }
        if (value == "maximum-parallel-full" || value == "parallel-full")
        {
            output = ParallelRoam::Algorithms::MakeTerrainLodMaximumSafeParallelFullOutputPolicy();
            return true;
        }
        parseError = "Invalid runtime benchmark policy: " + std::string{value};
        return false;
    };
    auto parseParallelTopologyPhase = [&parseError](
        std::string_view value,
        ParallelRoam::Algorithms::TerrainLodParallelTopologyPhase& output) -> bool {
        if (value == "both")
        {
            output = ParallelRoam::Algorithms::TerrainLodParallelTopologyPhase::Both;
            return true;
        }
        if (value == "split")
        {
            output = ParallelRoam::Algorithms::TerrainLodParallelTopologyPhase::SplitOnly;
            return true;
        }
        if (value == "merge")
        {
            output = ParallelRoam::Algorithms::TerrainLodParallelTopologyPhase::MergeOnly;
            return true;
        }
        parseError = "Invalid parallel topology phase: " + std::string{value};
        return false;
    };
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view argument{argv[index]};
        // smoke test 用固定帧数退出，避免自动化验证卡在窗口循环里
        if (argument == "--smoke-test")
        {
            fixedFrameSmokeTest = true;
            maxFrameCount = 3;
        }

        if (argument == "--dx12-smoke-test")
        {
#if defined(PARALLEL_ROAM_GRAPHICS_API_D3D12)
            fixedFrameSmokeTest = true;
            maxFrameCount = 32;
#else
            parseError = "--dx12-smoke-test requires PARALLEL_ROAM_GRAPHICS_API=D3D12";
            break;
#endif
        }

        if (argument == "--runtime-benchmark")
        {
            automaticRuntimeBenchmark = true;
            continue;
        }

        if (argument == "--runtime-benchmark-path")
        {
            const char* value = requireValue(index, argument);
            if (value == nullptr)
            {
                break;
            }
            const std::string_view pathValue{value};
            if (pathValue == "default")
            {
                runtimeBenchmarkOverrides.Path =
                    ParallelRoam::Gui::TerrainPanelState::RuntimeBenchmarkPath::Default;
            }
            else if (pathValue == "budget-saturation" || pathValue == "stress")
            {
                runtimeBenchmarkOverrides.Path =
                    ParallelRoam::Gui::TerrainPanelState::RuntimeBenchmarkPath::BudgetSaturation;
            }
            else
            {
                parseError = "Invalid runtime benchmark path: " + std::string{pathValue};
                break;
            }
            runtimeBenchmarkOverrides.HasPath = true;
            hasRuntimeBenchmarkOverrides = true;
            continue;
        }

        if (argument == "--runtime-benchmark-policy")
        {
            const char* value = requireValue(index, argument);
            if (value == nullptr ||
                !parsePassPolicyOption(value, runtimeBenchmarkOverrides.PassPolicy))
            {
                break;
            }
            runtimeBenchmarkOverrides.HasPassPolicy = true;
            hasRuntimeBenchmarkOverrides = true;
            continue;
        }

        if (argument == "--runtime-benchmark-split-topology-min-candidates")
        {
            int value = 0;
            if (!parseIntOption(index, argument, value))
            {
                break;
            }
            if (value < 0)
            {
                parseError = std::string{argument} + " must be non-negative";
                break;
            }
            runtimeBenchmarkOverrides.HasSplitTopologyMinParallelCandidateCount = true;
            runtimeBenchmarkOverrides.SplitTopologyMinParallelCandidateCount =
                static_cast<std::size_t>(value);
            hasRuntimeBenchmarkOverrides = true;
            continue;
        }

        if (argument == "--runtime-benchmark-merge-topology-min-candidates")
        {
            int value = 0;
            if (!parseIntOption(index, argument, value))
            {
                break;
            }
            if (value < 0)
            {
                parseError = std::string{argument} + " must be non-negative";
                break;
            }
            runtimeBenchmarkOverrides.HasMergeTopologyMinParallelCandidateCount = true;
            runtimeBenchmarkOverrides.MergeTopologyMinParallelCandidateCount =
                static_cast<std::size_t>(value);
            hasRuntimeBenchmarkOverrides = true;
            continue;
        }

        if (argument == "--runtime-benchmark-parallel-topology-target-build")
        {
            int value = 0;
            if (!parseIntOption(index, argument, value))
            {
                break;
            }
            if (value < 0)
            {
                parseError = std::string{argument} + " must be non-negative";
                break;
            }
            runtimeBenchmarkOverrides.HasParallelTopologyTargetBuild = true;
            runtimeBenchmarkOverrides.ParallelTopologyTargetBuild = static_cast<std::size_t>(value);
            hasRuntimeBenchmarkOverrides = true;
            continue;
        }

        if (argument == "--runtime-benchmark-parallel-topology-phase")
        {
            const char* value = requireValue(index, argument);
            if (value == nullptr ||
                !parseParallelTopologyPhase(value, runtimeBenchmarkOverrides.ParallelTopologyPhase))
            {
                break;
            }
            runtimeBenchmarkOverrides.HasParallelTopologyPhase = true;
            hasRuntimeBenchmarkOverrides = true;
            continue;
        }

        if (argument == "--runtime-benchmark-heightmap")
        {
            int value = 0;
            if (!parseHeightMapOption(index, argument, value))
            {
                break;
            }
            runtimeBenchmarkOverrides.HasHeightMapIndex = true;
            runtimeBenchmarkOverrides.HeightMapIndex = value;
            hasRuntimeBenchmarkOverrides = true;
            continue;
        }

        if (argument == "--runtime-benchmark-terrain-size")
        {
            float value = 0.0F;
            if (!parseFloatOption(index, argument, value))
            {
                break;
            }
            runtimeBenchmarkOverrides.HasTerrainSize = true;
            runtimeBenchmarkOverrides.TerrainSize = value;
            hasRuntimeBenchmarkOverrides = true;
            continue;
        }

        if (argument == "--runtime-benchmark-height-scale")
        {
            float value = 0.0F;
            if (!parseFloatOption(index, argument, value))
            {
                break;
            }
            runtimeBenchmarkOverrides.HasHeightScale = true;
            runtimeBenchmarkOverrides.HeightScale = value;
            hasRuntimeBenchmarkOverrides = true;
            continue;
        }

        if (argument == "--runtime-benchmark-depth" || argument == "--runtime-benchmark-max-depth")
        {
            int value = 0;
            if (!parseIntOption(index, argument, value))
            {
                break;
            }
            runtimeBenchmarkOverrides.HasMaxDepth = true;
            runtimeBenchmarkOverrides.MaxDepth = value;
            hasRuntimeBenchmarkOverrides = true;
            continue;
        }

        if (argument == "--runtime-benchmark-split-pixels" ||
            argument == "--runtime-benchmark-split-threshold")
        {
            float value = 0.0F;
            if (!parseFloatOption(index, argument, value))
            {
                break;
            }
            runtimeBenchmarkOverrides.HasScreenSpaceSplitThresholdPixels = true;
            runtimeBenchmarkOverrides.ScreenSpaceSplitThresholdPixels = value;
            hasRuntimeBenchmarkOverrides = true;
            continue;
        }

        if (argument == "--runtime-benchmark-merge-pixels" ||
            argument == "--runtime-benchmark-merge-threshold")
        {
            float value = 0.0F;
            if (!parseFloatOption(index, argument, value))
            {
                break;
            }
            runtimeBenchmarkOverrides.HasScreenSpaceMergeThresholdPixels = true;
            runtimeBenchmarkOverrides.ScreenSpaceMergeThresholdPixels = value;
            hasRuntimeBenchmarkOverrides = true;
            continue;
        }

        if (argument == "--runtime-benchmark-distance-scale")
        {
            parseError = "--runtime-benchmark-distance-scale was removed; ROAM now uses pixel screen-space error";
            break;
        }

        if (argument == "--runtime-benchmark-samples")
        {
            int value = 0;
            if (!parseIntOption(index, argument, value))
            {
                break;
            }
            runtimeBenchmarkOverrides.HasSampleCount = true;
            runtimeBenchmarkOverrides.SampleCount = static_cast<std::size_t>(std::max(value, 2));
            hasRuntimeBenchmarkOverrides = true;
            continue;
        }

        if (argument == "--runtime-benchmark-warmup-samples")
        {
            int value = 0;
            if (!parseIntOption(index, argument, value))
            {
                break;
            }
            if (value < 0)
            {
                parseError = std::string{argument} + " must be non-negative";
                break;
            }
            runtimeBenchmarkOverrides.HasWarmupSampleCount = true;
            runtimeBenchmarkOverrides.WarmupSampleCount = static_cast<std::size_t>(value);
            hasRuntimeBenchmarkOverrides = true;
            continue;
        }

        if (argument == "--runtime-benchmark-order-rotation")
        {
            int value = 0;
            if (!parseIntOption(index, argument, value))
            {
                break;
            }
            if (value < 0)
            {
                parseError = std::string{argument} + " must be non-negative";
                break;
            }
            runtimeBenchmarkOverrides.HasAlgorithmOrderRotation = true;
            runtimeBenchmarkOverrides.AlgorithmOrderRotation = static_cast<std::size_t>(value);
            hasRuntimeBenchmarkOverrides = true;
            continue;
        }

        if (argument == "--runtime-benchmark-duration")
        {
            float value = 0.0F;
            if (!parseFloatOption(index, argument, value))
            {
                break;
            }
            if (!std::isfinite(value) || value <= 0.0F)
            {
                parseError = std::string{argument} + " must be a finite positive number";
                break;
            }
            // 旧参数仅保留兼容性：每个名义秒换算为 60 个离散采样点
            const double convertedSampleCount = static_cast<double>(value) * 60.0;
            if (convertedSampleCount > static_cast<double>(std::numeric_limits<int>::max()))
            {
                parseError = std::string{argument} + " produces too many sample points";
                break;
            }
            runtimeBenchmarkOverrides.HasSampleCount = true;
            runtimeBenchmarkOverrides.SampleCount = std::max(
                static_cast<std::size_t>(convertedSampleCount),
                static_cast<std::size_t>(2));
            hasRuntimeBenchmarkOverrides = true;
            continue;
        }

        if (argument == "--runtime-benchmark-label")
        {
            const char* value = requireValue(index, argument);
            if (value == nullptr)
            {
                break;
            }
            runtimeBenchmarkOverrides.Label = value;
            hasRuntimeBenchmarkOverrides = true;
            continue;
        }

        // ROAM 探针不启动窗口，用于快速确认算法层 LOD 是否随相机变化
        if (argument == "--roam-probe")
        {
            // --roam-probe 保留旧探针命令
            // probe 也必须早于 Application 初始化
            // 它只验证算法层
            return ParallelRoam::Benchmark::RunRoamProbe();
        }

        if (argument == "--benchmark")
        {
            // --benchmark 走两种 CPU ROAM 共享 benchmark
            // benchmark 必须在 Application 创建前分流
            return ParallelRoam::Benchmark::RunTerrainLodBenchmarkFromCommandLine(argc, argv);
        }
    }

    if (!parseError.empty())
    {
        std::cerr << parseError << '\n';
        return 2;
    }

    if (automaticRuntimeBenchmark && fixedFrameSmokeTest)
    {
        std::cerr << "--runtime-benchmark cannot be combined with a smoke-test option.\n";
        return 2;
    }

    ParallelRoam::App::Application application;
    if (hasRuntimeBenchmarkOverrides)
    {
        application.ConfigureRuntimeBenchmark(runtimeBenchmarkOverrides);
    }
    if (automaticRuntimeBenchmark)
    {
        application.EnableAutomaticRuntimeBenchmark();
    }

    if (!application.Initialize())
    {
        return 1;
    }

    return application.Run(maxFrameCount);
#else
    // 依赖不完整时保留 bootstrap，方便只验证 CMake 和基础链接
    std::cout << "Parallel ROAM bootstrap\n";

#if defined(PARALLEL_ROAM_GRAPHICS_API_OPENGL)
    std::cout << "Graphics API: OpenGL\n";
#elif defined(PARALLEL_ROAM_GRAPHICS_API_D3D12)
    std::cout << "Graphics API: D3D12 (backend implementation pending)\n";
#else
    std::cout << "Graphics API: unknown\n";
#endif

#if defined(PARALLEL_ROAM_HAS_OPENGL)
    std::cout << "OpenGL: linked\n";
#else
    std::cout << "OpenGL: not linked\n";
#endif

#if defined(PARALLEL_ROAM_HAS_GLM)
    std::cout << "GLM: linked\n";
#else
    std::cout << "GLM: not linked\n";
#endif

#if defined(PARALLEL_ROAM_HAS_GLAD)
    std::cout << "GLAD: linked\n";
#else
    std::cout << "GLAD: not linked\n";
#endif

#if defined(PARALLEL_ROAM_HAS_STB)
    std::cout << "stb: linked\n";
#else
    std::cout << "stb: not linked\n";
#endif

#if defined(PARALLEL_ROAM_HAS_IMGUI)
    std::cout << "Dear ImGui: linked\n";
#else
    std::cout << "Dear ImGui: not linked\n";
#endif

#if defined(PARALLEL_ROAM_HAS_SDL2)
    SDL_SetMainReady();

    // bootstrap 只初始化 timer subsystem
    // 用于确认 SDL2 链接和基础运行时可用
    if (SDL_Init(SDL_INIT_TIMER) != 0)
    {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
        return 1;
    }

    SDL_Quit();
    std::cout << "SDL2: initialized timer subsystem\n";
#else
    std::cout << "SDL2: not linked\n";
#endif

    return 0;
#endif
}
