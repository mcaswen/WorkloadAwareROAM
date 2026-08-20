#include "tools/PerformanceTimer.h"

namespace ParallelRoam::Tools
{
PerformanceTimer::PerformanceTimer() noexcept
    : _start(Clock::now())
{
}

PerformanceTimer::PerformanceTimer(float& accumulatedMilliseconds) noexcept
    : _start(Clock::now()),
      _accumulatedMilliseconds(&accumulatedMilliseconds)
{
}

PerformanceTimer::~PerformanceTimer() noexcept
{
    Stop();
}

float PerformanceTimer::Stop() noexcept
{
    if (!_running)
    {
        return _stoppedMilliseconds;
    }

    _stoppedMilliseconds = ToMilliseconds(_start, Clock::now());
    _running = false;
    if (_accumulatedMilliseconds != nullptr)
    {
        *_accumulatedMilliseconds += _stoppedMilliseconds;
    }
    return _stoppedMilliseconds;
}

float PerformanceTimer::Restart() noexcept
{
    const float elapsedMilliseconds = Stop();
    _start = Clock::now();
    _stoppedMilliseconds = 0.0F;
    _running = true;
    return elapsedMilliseconds;
}

float PerformanceTimer::ElapsedMilliseconds() const noexcept
{
    if (!_running)
    {
        return _stoppedMilliseconds;
    }
    return ToMilliseconds(_start, Clock::now());
}

PerformanceTimer::TimePoint PerformanceTimer::Now() noexcept
{
    return Clock::now();
}

float PerformanceTimer::ElapsedMilliseconds(TimePoint start, TimePoint end) noexcept
{
    return ToMilliseconds(start, end);
}

bool PerformanceTimer::IsRunning() const noexcept
{
    return _running;
}

float PerformanceTimer::ToMilliseconds(
    Clock::time_point start,
    Clock::time_point end) noexcept
{
    return std::chrono::duration<float, std::milli>(end - start).count();
}
} // namespace ParallelRoam::Tools
