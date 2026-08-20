#pragma once

#include <chrono>

namespace ParallelRoam::Tools
{
/// <summary>
/// 记录一个代码阶段的墙钟耗时，可在析构时自动累加到指定统计字段。
/// </summary>
class PerformanceTimer final
{
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    PerformanceTimer() noexcept;
    explicit PerformanceTimer(float& accumulatedMilliseconds) noexcept;
    ~PerformanceTimer() noexcept;

    PerformanceTimer(const PerformanceTimer&) = delete;
    PerformanceTimer& operator=(const PerformanceTimer&) = delete;
    PerformanceTimer(PerformanceTimer&&) = delete;
    PerformanceTimer& operator=(PerformanceTimer&&) = delete;

    /// <summary>
    /// 结束当前计时并返回毫秒数；重复调用只返回首次停止时的结果。
    /// </summary>
    float Stop() noexcept;

    /// <summary>
    /// 结束当前计时，返回本段耗时，并立即开始下一段计时。
    /// </summary>
    float Restart() noexcept;

    /// <summary>
    /// 返回从构造到当前时刻或首次 Stop() 的毫秒数，不改变计时状态。
    /// </summary>
    [[nodiscard]] float ElapsedMilliseconds() const noexcept;

    [[nodiscard]] static TimePoint Now() noexcept;
    [[nodiscard]] static float ElapsedMilliseconds(TimePoint start, TimePoint end) noexcept;

    [[nodiscard]] bool IsRunning() const noexcept;

private:
    [[nodiscard]] static float ToMilliseconds(
        Clock::time_point start,
        Clock::time_point end) noexcept;

    Clock::time_point _start;
    float* _accumulatedMilliseconds{nullptr};
    float _stoppedMilliseconds{0.0F};
    bool _running{true};
};
} // namespace ParallelRoam::Tools
