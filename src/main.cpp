#include <iostream>

#if defined(PARALLEL_ROAM_BUILD_FULL_APP)
#include "app/Application.h"
#include "app/ApplicationCommandLine.h"
#include "benchmark/RoamProbe.h"
#include "benchmark/TerrainLodBenchmark.h"
#include "benchmark/TransactionalPlatformProbe.h"
#endif

#if defined(PARALLEL_ROAM_HAS_SDL2)
#include <SDL.h>
#endif

int main(int argc, char** argv)
{
#if defined(PARALLEL_ROAM_BUILD_FULL_APP)
    // 在创建窗口和图形资源前完成参数校验，失败时不会留下半初始化状态
    const ParallelRoam::App::ApplicationCommandLineParseResult commandLine =
        ParallelRoam::App::ParseApplicationCommandLine(argc, argv);
    if (!commandLine.Succeeded())
    {
        std::cerr << commandLine.Error << '\n';
        return 2;
    }

    // 探针和无窗口实验直接使用各自入口，不需要初始化图形应用
    switch (commandLine.Options.LaunchMode)
    {
    case ParallelRoam::App::ApplicationLaunchMode::RoamProbe:
        return ParallelRoam::Benchmark::RunRoamProbe();
    case ParallelRoam::App::ApplicationLaunchMode::TerrainLodBenchmark:
        return ParallelRoam::Benchmark::RunTerrainLodBenchmarkFromCommandLine(argc, argv);
    case ParallelRoam::App::ApplicationLaunchMode::Application:
        break;
    case ParallelRoam::App::ApplicationLaunchMode::TransactionalPlatformCheck:
        return ParallelRoam::Benchmark::RunTransactionalPlatformCheck();
    }

    ParallelRoam::App::Application application;
    // 实验参数必须在初始化前写入，首帧才能使用最终配置
    if (commandLine.Options.HasRuntimeBenchmarkOverrides)
    {
        application.ConfigureRuntimeBenchmark(commandLine.Options.RuntimeBenchmark);
    }
    if (commandLine.Options.AutomaticRuntimeBenchmark)
    {
        application.EnableAutomaticRuntimeBenchmark();
    }
    if (!application.Initialize())
    {
        return 1;
    }
    return application.Run(commandLine.Options.MaxFrameCount);
#else
    (void)argc;
    (void)argv;

    // 依赖不完整时保留引导程序，方便只验证 CMake 和基础链接
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

    // 引导程序只初始化计时器子系统，用于确认 SDL2 链接和基础运行时可用
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
