#pragma once

#include "tools/PerformanceTimer.h"

#include <algorithm>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/resource.h>
#include <sys/time.h>
#endif

#include <ctime>

namespace ParallelRoam::Algorithms
{
namespace Detail
{
[[nodiscard]] inline double CaptureProcessCpuMilliseconds()
{
#if defined(_WIN32)
    FILETIME creationTime{};
    FILETIME exitTime{};
    FILETIME kernelTime{};
    FILETIME userTime{};
    if (GetProcessTimes(GetCurrentProcess(), &creationTime, &exitTime, &kernelTime, &userTime) == 0)
    {
        return -1.0;
    }

    ULARGE_INTEGER kernelTicks{};
    kernelTicks.LowPart = kernelTime.dwLowDateTime;
    kernelTicks.HighPart = kernelTime.dwHighDateTime;

    ULARGE_INTEGER userTicks{};
    userTicks.LowPart = userTime.dwLowDateTime;
    userTicks.HighPart = userTime.dwHighDateTime;

    // FILETIME 使用 100ns tick  kernel + user 是进程所有线程累计 CPU 时间
    return static_cast<double>(kernelTicks.QuadPart + userTicks.QuadPart) / 10'000.0;
#else
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0)
    {
        return -1.0;
    }

    const double userMilliseconds =
        static_cast<double>(usage.ru_utime.tv_sec) * 1000.0 +
        static_cast<double>(usage.ru_utime.tv_usec) / 1000.0;
    const double kernelMilliseconds =
        static_cast<double>(usage.ru_stime.tv_sec) * 1000.0 +
        static_cast<double>(usage.ru_stime.tv_usec) / 1000.0;
    return userMilliseconds + kernelMilliseconds;
#endif
}
} // namespace Detail

/// <summary>
/// 捕获一次 LOD build 前后的墙钟时间和进程 CPU 时间
/// </summary>
struct TerrainLodCpuSample
{
    // WallTime 用于计算本次 build 的真实等待时间
    Tools::PerformanceTimer::TimePoint WallTime;
    // ProcessCpuMilliseconds 会累加同进程内多个 worker 的 CPU 时间
    double ProcessCpuMilliseconds{-1.0};
};

[[nodiscard]] inline TerrainLodCpuSample CaptureTerrainLodCpuSample()
{
    return TerrainLodCpuSample{
        Tools::PerformanceTimer::Now(),
        Detail::CaptureProcessCpuMilliseconds(),
    };
}

[[nodiscard]] inline float ComputeCpuUtilizationPercent(
    const TerrainLodCpuSample& start,
    const TerrainLodCpuSample& end)
{
    const double wallMilliseconds = static_cast<double>(
        Tools::PerformanceTimer::ElapsedMilliseconds(start.WallTime, end.WallTime));
    if (wallMilliseconds <= 0.0 ||
        start.ProcessCpuMilliseconds < 0.0 ||
        end.ProcessCpuMilliseconds < 0.0)
    {
        return 0.0F;
    }

    // 100% 表示一个逻辑核心满载  多线程 build 可以超过 100%
    const double cpuMilliseconds = end.ProcessCpuMilliseconds - start.ProcessCpuMilliseconds;
    return static_cast<float>(std::max(0.0, cpuMilliseconds / wallMilliseconds * 100.0));
}
} // 命名空间 ParallelRoam::Algorithms
