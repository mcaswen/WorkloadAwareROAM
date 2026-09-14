#include "tools/CpuTaskExecutor.h"
#include "tools/CpuThreadPool.h"

#include <exception>
#include <stdexcept>
#include <vector>

namespace ParallelRoam::Tools
{
CpuTaskExecutor::CpuTaskExecutor(std::size_t workers)
    : _pool(std::make_unique<CpuThreadPool>()), _workers(workers)
{
    if (workers == 0) throw std::invalid_argument("线程数必须为正");
    _pool->EnsureWorkerCount(workers);
}

CpuTaskExecutor::~CpuTaskExecutor() = default;

void CpuTaskExecutor::Dispatch(std::size_t chunks, const std::function<void(std::size_t)>& task)
{
    if (_failed || chunks == 0 || chunks > _workers) throw std::logic_error("无效或已停止的分块执行");
    // 每块独占异常槽；其余任务仍完成，等待结束后才向调用方传播
    std::vector<std::exception_ptr> errors(chunks);
    try
    {
        _pool->ParallelFor(chunks, [&](std::size_t chunk) noexcept {
            try { task(chunk); }
            catch (...) { errors[chunk] = std::current_exception(); }
        });
    }
    catch (...)
    {
        // 入队可能只完成一部分；借用仍有效时停止并排空，不能立即离开栈帧
        _failed = true;
        _pool->Shutdown();
        throw;
    }
    for (const auto& error : errors) if (error) std::rethrow_exception(error);
}
}
