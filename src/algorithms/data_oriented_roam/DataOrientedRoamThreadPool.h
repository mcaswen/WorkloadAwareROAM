#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
/// <summary>
/// DOD ROAM 内部复用的轻量线程池，避免每个 pass 重复创建 worker
/// 由 Pipeline 创建并跨帧复用；EnsureWorkerCount 扩容，Shutdown 或析构时 join 并释放 worker
/// DataOrientedRoamPipeline 持有它；各 pass 只提交临时 task，不拥有线程
/// </summary>
class DataOrientedRoamThreadPool
{
public:
    DataOrientedRoamThreadPool() = default;
    ~DataOrientedRoamThreadPool();

    DataOrientedRoamThreadPool(const DataOrientedRoamThreadPool&) = delete;
    DataOrientedRoamThreadPool& operator=(const DataOrientedRoamThreadPool&) = delete;

    // worker 数只增不减，避免帧间反复创建和销毁线程
    void EnsureWorkerCount(std::size_t workerCount);
    // 调用方按 workerIndex 自行切 chunk，线程池只负责复用 worker
    void ParallelFor(std::size_t workerCount, const std::function<void(std::size_t workerIndex)>& task);
    // Shutdown 会等待已入队任务执行完成后再 join worker
    void Shutdown();

    // WorkerCount 反映当前已创建 worker 数，不等同于本帧实际并行宽度
    [[nodiscard]] std::size_t WorkerCount() const;

private:
    using Task = std::function<void()>;

    void WorkerLoop();

    // mutex 同时保护 worker 集合、任务队列和停止状态
    mutable std::mutex _mutex;
    // taskAvailable 唤醒正在等待新任务的 worker
    std::condition_variable _taskAvailable;
    // taskFinished 唤醒等待整批任务完成的提交线程
    std::condition_variable _taskFinished;
    std::vector<std::thread> _workers;
    std::queue<Task> _tasks;
    // activeTaskCount 记录已经出队但尚未执行完的任务
    std::size_t _activeTaskCount{0};
    // stopping 置位后不再接受新 worker
    bool _stopping{false};
};
} // namespace ParallelRoam::Algorithms::DataOrientedRoam
