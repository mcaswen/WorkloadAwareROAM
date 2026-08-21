#pragma once

#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamThreadPool.h"

#include <cstddef>
#include <functional>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
/// <summary>
/// 为 DOD 各并行阶段提供统一的线程调度入口
/// 线程池由 DataOrientedRoamPipeline 跨帧持有，当前阶段只提供临时任务
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
        // 只有一个线程时直接在调用线程执行，避免小任务额外进入队列
        task(0U);
        return;
    }

    if (state.ThreadPool != nullptr)
    {
        // 调用方已经按线程编号分好工作，线程池只负责执行这些任务
        state.ThreadPool->ParallelFor(workerCount, task);
        return;
    }

    for (std::size_t workerIndex = 0U; workerIndex < workerCount; ++workerIndex)
    {
        // 测试或过渡期间没有线程池时按编号顺序执行，保证每次得到相同结果
        task(workerIndex);
    }
}
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
