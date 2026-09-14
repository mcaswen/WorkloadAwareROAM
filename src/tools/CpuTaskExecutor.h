#pragma once

#include <cstddef>
#include <functional>
#include <memory>

namespace ParallelRoam::Tools
{
class CpuThreadPool;

/// <summary>
/// 拥有固定线程请求的同步执行器，返回前排空所有已提交任务
/// 任务异常在等待后传播；部分入队失败会停止实例，防止借用回调悬空
/// </summary>
class CpuTaskExecutor
{
public:
    explicit CpuTaskExecutor(std::size_t workers);
    ~CpuTaskExecutor();
    CpuTaskExecutor(const CpuTaskExecutor&) = delete;
    CpuTaskExecutor& operator=(const CpuTaskExecutor&) = delete;

    /// <summary>
    /// 分块数必须处于 [1, WorkerCount()]；回调只在本次同步调用内被借用
    /// 实例不支持重入或多个调用者并发派发
    /// </summary>
    void Dispatch(std::size_t chunks, const std::function<void(std::size_t)>& task);
    [[nodiscard]] std::size_t WorkerCount() const { return _workers; }
    [[nodiscard]] bool IsStopped() const { return _failed; }

private:
    std::unique_ptr<CpuThreadPool> _pool;
    std::size_t _workers;
    bool _failed{false};
};
}
