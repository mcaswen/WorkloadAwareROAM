#include "algorithms/data_oriented_roam/DataOrientedRoamThreadPool.h"

#include <utility>

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

    // 扩容时只补缺口，已创建 worker 会跨帧保留
    while (_workers.size() < workerCount)
    {
        // worker 只从队列取闭包执行，不持有任何算法状态
        _workers.emplace_back([this]() {
            WorkerLoop();
        });
    }
}

void DataOrientedRoamThreadPool::ParallelFor(
    std::size_t workerCount,
    const std::function<void(std::size_t workerIndex)>& task)
{
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
        std::lock_guard<std::mutex> lock{_mutex};
        // 任务先全部入队，再统一通知 worker 竞争领取
        for (std::size_t workerIndex = 0U; workerIndex < workerCount; ++workerIndex)
        {
            _tasks.push([task, workerIndex]() {
                task(workerIndex);
            });
        }
    }

    _taskAvailable.notify_all();

    std::unique_lock<std::mutex> lock{_mutex};
    // 返回前必须等待队列和正在执行的任务全部清空
    _taskFinished.wait(lock, [this]() {
        return _tasks.empty() && _activeTaskCount == 0U;
    });
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

    // worker 会处理完已出队任务，再在队列清空后退出循环
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
    while (true)
    {
        Task task;
        {
            std::unique_lock<std::mutex> lock{_mutex};
            // wait predicate 同时观察停止信号和任务队列
            _taskAvailable.wait(lock, [this]() {
                return _stopping || !_tasks.empty();
            });

            // 队列清空后才响应停止，避免丢掉已提交任务
            if (_stopping && _tasks.empty())
            {
                return;
            }

            task = std::move(_tasks.front());
            _tasks.pop();
            ++_activeTaskCount;
        }

        // 任务在锁外执行，避免长任务阻塞其他 worker 取任务
        task();

        {
            std::lock_guard<std::mutex> lock{_mutex};
            // task 完成后再更新计数，等待线程才能看见批次边界
            _activeTaskCount = _activeTaskCount == 0U ? 0U : _activeTaskCount - 1U;
            if (_tasks.empty() && _activeTaskCount == 0U)
            {
                _taskFinished.notify_all();
            }
        }
    }
}
} // namespace ParallelRoam::Algorithms::DataOrientedRoam
