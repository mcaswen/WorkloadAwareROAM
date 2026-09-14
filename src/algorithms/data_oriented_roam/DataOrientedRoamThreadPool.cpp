#include "algorithms/data_oriented_roam/DataOrientedRoamThreadPool.h"
#include "profiling/CpuProfiling.h"

#include <utility>
#if defined(TRACY_ENABLE) && defined(__linux__)
#include <sys/syscall.h>
#include <unistd.h>
#include <string>
#endif

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
DataOrientedRoamThreadPool::~DataOrientedRoamThreadPool()
{
    Shutdown();
}

void DataOrientedRoamThreadPool::EnsureWorkerCount(std::size_t workerCount)
{
    if (workerCount <= 1U)
    {
        return;
    }

    std::lock_guard<std::mutex> lock{_mutex};
    if (_stopping)
    {
        return;
    }

    // 扩容时只创建缺少的线程，已有线程继续跨帧复用
    while (_workers.size() < workerCount)
    {
        // 工作线程只执行任务队列中的任务，不直接保存算法状态
        _workers.emplace_back([this]() {
            WorkerLoop();
        });
    }
}

void DataOrientedRoamThreadPool::ParallelFor(
    std::size_t workerCount,
    const std::function<void(std::size_t workerIndex)>& task)
{
    ROAM_CPU_ZONE("pool.parallel_for");
    if (workerCount == 0U)
    {
        return;
    }

    if (workerCount == 1U)
    {
        task(0U);
        return;
    }

    EnsureWorkerCount(workerCount);
    {
        ROAM_CPU_ZONE("pool.enqueue");
        std::lock_guard<std::mutex> lock{_mutex};
        // 先将整批任务入队，再统一唤醒工作线程，避免重复通知
        for (std::size_t workerIndex = 0U; workerIndex < workerCount; ++workerIndex)
        {
            _tasks.push([task, workerIndex]() {
                task(workerIndex);
            });
        }
    }

    _taskAvailable.notify_all();

    {
        ROAM_CPU_ZONE("pool.wait");
        std::unique_lock<std::mutex> lock{_mutex};
        // 返回前必须等待队列和正在执行的任务全部清空
        _taskFinished.wait(lock, [this]() {
            return _tasks.empty() && _activeTaskCount == 0U;
        });
    }
}

void DataOrientedRoamThreadPool::Shutdown()
{
    {
        std::lock_guard<std::mutex> lock{_mutex};
        if (_stopping)
        {
            return;
        }

        _stopping = true;
    }

    // 工作线程会处理完已提交任务，并在队列清空后退出
    _taskAvailable.notify_all();
    for (std::thread& worker : _workers)
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }

    _workers.clear();
}

std::size_t DataOrientedRoamThreadPool::WorkerCount() const
{
    std::lock_guard<std::mutex> lock{_mutex};
    return _workers.size();
}

void DataOrientedRoamThreadPool::WorkerLoop()
{
#if defined(TRACY_ENABLE) && defined(__linux__)
    // 名称使用实际 OS 线程身份，任务分块索引不能冒充线程编号
    const auto name=std::string("pool.tid.")+std::to_string(syscall(SYS_gettid));
    ROAM_CPU_THREAD(name.c_str());
#else
    ROAM_CPU_THREAD("pool.worker");
#endif
    while (true)
    {
        Task task;
        {
            std::unique_lock<std::mutex> lock{_mutex};
            // 等待条件同时检查停止信号和新任务，避免无任务时空转
            {
                ROAM_CPU_ZONE("pool.sleep");
                _taskAvailable.wait(lock, [this]() {
                    return _stopping || !_tasks.empty();
                });
            }

            // 只有任务队列清空后才响应停止，避免丢失已提交任务
            if (_stopping && _tasks.empty())
            {
                return;
            }

            task = std::move(_tasks.front());
            _tasks.pop();
            ++_activeTaskCount;
        }

        // 在互斥锁外执行任务，避免长任务阻塞其他线程领取工作
        {
            ROAM_CPU_ZONE("pool.task");
            task();
        }

        {
            ROAM_CPU_ZONE("pool.complete");
            std::lock_guard<std::mutex> lock{_mutex};
            // 任务完成后再减少活动数量，使等待方准确识别整批任务结束
            _activeTaskCount = _activeTaskCount == 0U ? 0U : _activeTaskCount - 1U;
            if (_tasks.empty() && _activeTaskCount == 0U)
            {
                _taskFinished.notify_all();
            }
        }
    }
}
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
