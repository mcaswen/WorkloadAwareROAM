#include "tools/PerformanceTimer.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

namespace
{
bool Check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
        return false;
    }
    return true;
}
} // namespace

int main()
{
    using ParallelRoam::Tools::PerformanceTimer;

    bool passed = true;
    PerformanceTimer directTimer;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    passed &= Check(directTimer.IsRunning(), "timer must start in the running state");
    passed &= Check(directTimer.ElapsedMilliseconds() > 0.0F, "running timer must report elapsed time");

    const float stoppedMilliseconds = directTimer.Stop();
    passed &= Check(!directTimer.IsRunning(), "Stop must end the timer");
    passed &= Check(
        directTimer.Stop() == stoppedMilliseconds,
        "repeated Stop calls must return the same sample");

    PerformanceTimer restartTimer;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const float firstRestartSample = restartTimer.Restart();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const float secondRestartSample = restartTimer.Stop();
    passed &= Check(firstRestartSample > 0.0F, "Restart must return the completed sample");
    passed &= Check(secondRestartSample > 0.0F, "Restart must begin a new sample");

    float accumulatedMilliseconds = 0.0F;
    float explicitlyStoppedMilliseconds = 0.0F;
    {
        PerformanceTimer accumulatedTimer(accumulatedMilliseconds);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        explicitlyStoppedMilliseconds = accumulatedTimer.Stop();
        passed &= Check(
            accumulatedMilliseconds == explicitlyStoppedMilliseconds,
            "explicit Stop must update the bound accumulator");
    }
    passed &= Check(
        accumulatedMilliseconds == explicitlyStoppedMilliseconds,
        "destruction after Stop must not accumulate the sample twice");

    const float beforeAutomaticSample = accumulatedMilliseconds;
    {
        PerformanceTimer automaticTimer(accumulatedMilliseconds);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    passed &= Check(
        accumulatedMilliseconds > beforeAutomaticSample,
        "destruction must automatically update the bound accumulator");

    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
