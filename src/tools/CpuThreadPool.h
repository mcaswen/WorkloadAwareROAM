#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace ParallelRoam::Tools
{
/// <summary>
/// 跨更新复用任务线程，调度层不持有算法状态
/// 调用方持有生命周期；可抛异常的任务由 CpuTaskExecutor 包装后提交
/// </summary>
class CpuThreadPool
{
public:
    CpuThreadPool() = default;
    ~CpuThreadPool();

    CpuThreadPool(const CpuThreadPool&) = delete;
    CpuThreadPool& operator=(const CpuThreadPool&) = delete;

    // 线程数量只增不减，避免帧间反复创建和销毁
    void EnsureWorkerCount(std::size_t workerCount);
    // 调用方按线程编号划分工作，线程池只负责调度指定数量的任务
    void ParallelFor(std::size_t workerCount, const std::function<void(std::size_t workerIndex)>& task);
    // Shutdown 等待已提交任务完成后再回收全部线程
    void Shutdown();

    // WorkerCount 返回当前已创建的线程数量，不代表本帧实际同时工作的线程数量
    [[nodiscard]] std::size_t WorkerCount() const;

private:
    using Task = std::function<void()>;

    void WorkerLoop();

    // 同一把互斥锁保护线程集合、任务队列和停止状态
    mutable std::mutex _mutex;
    // _taskAvailable 在新任务入队后唤醒工作线程
    std::condition_variable _taskAvailable;
    // _taskFinished 在整批任务完成后唤醒等待结果的调用线程
    std::condition_variable _taskFinished;
    std::vector<std::thread> _workers;
    std::queue<Task> _tasks;
    // _activeTaskCount 记录已经出队但尚未完成的任务数量
    std::size_t _activeTaskCount{0};
    // _stopping 置位后不再创建线程或接受新任务
    bool _stopping{false};
};
} // 命名空间 ParallelRoam::Tools
