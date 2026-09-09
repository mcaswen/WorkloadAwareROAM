#pragma once

#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamThreadPool.h"

#include <algorithm>
#include <cstddef>
#include <thread>
#include <functional>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
/// <summary>
/// 根据工作量、线程请求和并行下限确定评分或网格写入的任务数量
/// 本函数不解释执行策略，调用方选择串行时应传入线程请求 1
/// </summary>
[[nodiscard]] inline std::size_t ResolveDataOrientedRoamWorkerCount(
    std::size_t workItemCount,
    std::size_t requestedWorkerCount,
    std::size_t minimumParallelWorkItemCount)
{
    // 先排除空工作，避免串行动作归一为请求 1 后产生空任务
    if (workItemCount == 0U)
    {
        return 0U;
    }
    if (requestedWorkerCount == 1U || workItemCount < minimumParallelWorkItemCount)
    {
        return 1U;
    }
    if (requestedWorkerCount == 0U)
    {
        // 自动模式保留保守上限，显式请求只由实际工作量限制
        const auto hardwareCount = std::thread::hardware_concurrency();
        requestedWorkerCount = hardwareCount == 0U ? 1U : static_cast<std::size_t>(hardwareCount);
        requestedWorkerCount = std::min(requestedWorkerCount, std::size_t{8U});
    }
    // 下限为 0 只解除数量门槛，单条工作仍不能派发多个任务
    return std::clamp(requestedWorkerCount, std::size_t{1U}, workItemCount);
}

/// <summary>
/// 同步执行本阶段的全部任务，返回时所有回调都已结束
/// 线程池由流水线持有，回调引用的数据必须在本次调用期间保持有效
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

    // 无线程池时只在调用线程遍历任务编号，任务数量不代表实际系统线程数量
    for (std::size_t workerIndex = 0U; workerIndex < workerCount; ++workerIndex)
    {
        task(workerIndex);
    }
}
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
