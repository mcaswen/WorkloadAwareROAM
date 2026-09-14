#include "tools/CpuTaskExecutor.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamThreadPool.h"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <type_traits>

namespace
{
// 只在派发者线程注入一次分配失败，后台任务不参与故障计数
thread_local int allocationCountdown = -1;
void Require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
}

void* operator new(std::size_t size)
{
    if (allocationCountdown >= 0 && allocationCountdown-- == 0)
    {
        allocationCountdown = -1;
        throw std::bad_alloc();
    }
    if (void* memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc();
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

int main()
{
    using ParallelRoam::Tools::CpuTaskExecutor;
    static_assert(std::is_same_v<ParallelRoam::Tools::CpuThreadPool,
        ParallelRoam::Algorithms::DataOrientedRoam::DataOrientedRoamThreadPool>);
    try
    {
        for (std::size_t workers : {1U, 4U})
        {
            CpuTaskExecutor executor(workers);
            std::atomic<std::size_t> completed{};
            bool failed = false;
            try
            {
                executor.Dispatch(workers, [&](std::size_t i) {
                    ++completed;
                    if (i == 0) throw std::runtime_error("task failure");
                });
            }
            catch (const std::runtime_error&) { failed = true; }
            Require(failed && completed == workers, "任务异常提前离开排空边界");
            executor.Dispatch(workers, [&](std::size_t) { ++completed; });
            Require(completed == 2 * workers, "任务异常后无法复用");
            for (auto invalid : {std::size_t{0}, workers + 1})
            {
                failed = false;
                try { executor.Dispatch(invalid, [](std::size_t) {}); }
                catch (const std::logic_error&) { failed = true; }
                Require(failed, "非法分块未拒绝");
            }
        }

        bool observedPartialEnqueue = false;
        // 扫过少量分配点，既覆盖异常数组准备，也覆盖闭包与队列部分入队
        for (int failAt = 0; failAt < 12; ++failAt)
        {
            CpuTaskExecutor executor(4);
            std::atomic<std::size_t> completed{};
            const std::function<void(std::size_t)> task = [&](std::size_t) { ++completed; };
            bool failed = false;
            allocationCountdown = failAt;
            try { executor.Dispatch(4, task); }
            catch (const std::bad_alloc&) { failed = true; }
            allocationCountdown = -1;
            if (failed && completed > 0 && completed < 4)
            {
                observedPartialEnqueue = true;
                const auto finished = completed.load();
                bool stopped = false;
                try { executor.Dispatch(4, task); }
                catch (const std::logic_error&) { stopped = true; }
                Require(stopped && completed == finished, "部分入队失败后仍执行借用任务");
            }
        }
        Require(observedPartialEnqueue, "未覆盖实际部分入队失败");
        std::cout << "同步、异常排空、部分入队与旧类型兼容核查完成\n";
    }
    catch (const std::exception& error)
    {
        allocationCountdown = -1;
        std::cerr << error.what() << '\n';
        return 1;
    }
}
