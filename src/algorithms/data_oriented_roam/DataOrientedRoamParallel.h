#pragma once

#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamThreadPool.h"

#include <cstddef>
#include <functional>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
/// <summary>
/// DOD pass 的 worker 调度适配层
/// 只在单次 Build 的并行阶段调用；线程池由 Pipeline 持有，task 由当前 pass 临时提供
/// </summary>
inline void RunDataOrientedRoamWorkers(
    DataOrientedRoamState& state,
    std::size_t workerCount,
    const std::function<void(std::size_t workerIndex)>& task)
{
    if (workerCount == 0U)
    {
        return;
    }

    if (workerCount == 1U)
    {
        // 单 worker 直接在调用线程执行，避免小任务进入队列
        task(0U);
        return;
    }

    if (state.ThreadPool != nullptr)
    {
        // 线程池由 builder 持有，pass 只提交按 workerIndex 切好的任务
        state.ThreadPool->ParallelFor(workerCount, task);
        return;
    }

    for (std::size_t workerIndex = 0U; workerIndex < workerCount; ++workerIndex)
    {
        // 测试或迁移期间没有线程池时，保持确定性的串行回退
        task(workerIndex);
    }
}
} // namespace ParallelRoam::Algorithms::DataOrientedRoam
