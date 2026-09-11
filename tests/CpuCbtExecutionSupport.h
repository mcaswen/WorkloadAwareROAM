#pragma once

#include "algorithms/cpu_cbt/CpuCbtBisect.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamThreadPool.h"

#include <algorithm>
#include <stdexcept>

namespace ParallelRoam::Tests
{
/// <summary>
/// 一个非空区间的实际执行证据，任务编号与线程身份分别记录
/// </summary>
struct CpuCbtRangeEvidence
{
    std::size_t Begin{0}, End{0};
    std::thread::id Thread;
};

/// <summary>
/// 两个驱动共用的固定分块适配器，持有跨轮线程池，算法库只借用同步回调
/// 线程证据仅按需收集，不通过等待或睡眠改变自然任务的参与情况
/// </summary>
class CpuCbtTestExecutor
{
public:
    explicit CpuCbtTestExecutor(std::size_t threads) : _threads(threads)
    {
        if (threads != 1U && threads != 2U && threads != 4U)
            throw std::invalid_argument("CPU CBT 执行线程数只支持 1、2、4");
        _pool.EnsureWorkerCount(threads);
    }

    [[nodiscard]] Algorithms::CpuCbt::CpuCbtRangeExecutor Executor(
        std::vector<CpuCbtRangeEvidence>* evidence = nullptr)
    {
        return [this, evidence](std::size_t count, const Algorithms::CpuCbt::CpuCbtRangeTask& task) {
            if (_failed) throw std::runtime_error("CPU CBT 不能复用派发失败的线程池");
            const auto ranges = std::min(_threads, count);
            if (evidence) evidence->assign(ranges, {});
            if (ranges == 0U) return;
            const auto run = [&](std::size_t index) {
                const auto begin = count * index / ranges, end = count * (index + 1U) / ranges;
                if (evidence) (*evidence)[index] = {begin, end, std::this_thread::get_id()};
                task(begin, end);
            };
            if (ranges == 1U) { run(0U); return; }
            try
            {
                _pool.ParallelFor(ranges, run);
            }
            catch (...)
            {
                // 入队可能部分成功，先等所有借用结束，再允许上层销毁临时输出
                _failed = true;
                _pool.Shutdown();
                throw;
            }
        };
    }

private:
    Algorithms::DataOrientedRoam::DataOrientedRoamThreadPool _pool;
    std::size_t _threads;
    bool _failed{false};
};
} // 命名空间 ParallelRoam::Tests
