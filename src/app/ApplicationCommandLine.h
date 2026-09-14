#pragma once

#include "app/RuntimeBenchmarkConfig.h"

#include <string>

namespace ParallelRoam::App
{
/// <summary>
/// 区分交互应用和两个不创建窗口的算法入口
/// </summary>
enum class ApplicationLaunchMode
{
    Application,
    RoamProbe,
    TerrainLodBenchmark,
    TransactionalPlatformCheck,
};

/// <summary>
/// 保存应用入口完成分流和初始化所需的参数
/// </summary>
struct ApplicationCommandLineOptions
{
    // 默认进入交互应用，解析到无窗口入口后立即改写
    ApplicationLaunchMode LaunchMode{ApplicationLaunchMode::Application};

    // 负数表示持续运行，冒烟测试使用固定帧数自动退出
    int MaxFrameCount{-1};
    bool FixedFrameSmokeTest{false};

    // 自动实验仍使用正常应用主循环，只是完成后自动退出
    bool AutomaticRuntimeBenchmark{false};
    bool HasRuntimeBenchmarkOverrides{false};
    RuntimeBenchmarkOverrides RuntimeBenchmark;
};

/// <summary>
/// 同时返回可用配置和面向用户的首个解析错误
/// </summary>
struct ApplicationCommandLineParseResult
{
    ApplicationCommandLineOptions Options;
    std::string Error;

    [[nodiscard]] bool Succeeded() const
    {
        return Error.empty();
    }
};

/// <summary>
/// 解析应用入口参数，无窗口入口只做分流并保留原始参数
/// </summary>
[[nodiscard]] ApplicationCommandLineParseResult ParseApplicationCommandLine(
    int argumentCount,
    char** argumentValues);
} // 命名空间 ParallelRoam::App
