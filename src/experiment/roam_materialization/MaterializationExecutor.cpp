#include "experiment/roam_materialization/MaterializationExecutor.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamThreadPool.h"

#include <exception>
#include <stdexcept>

namespace ParallelRoam::Experiment::RoamMaterialization
{
MaterializationExecutor::MaterializationExecutor(std::size_t workers)
    : _pool(std::make_unique<Algorithms::DataOrientedRoam::DataOrientedRoamThreadPool>()), _workers(workers)
{
    if (workers == 0) throw std::invalid_argument("线程数必须为正");
    _pool->EnsureWorkerCount(workers);
}

MaterializationExecutor::~MaterializationExecutor() = default;

MaterializationExecution MaterializationExecutor::Execution()
{
    return {_workers, [this](std::size_t chunks, const auto& task) { Dispatch(chunks, task); }};
}

void MaterializationExecutor::Dispatch(std::size_t chunks, const std::function<void(std::size_t)>& task)
{
    if (_failed || chunks == 0 || chunks > _workers) throw std::logic_error("无效或已停止的分块执行");
    // 防止任务异常穿透原线程池；即使调用方不是补丁，也要等待后再传播
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
        // 部分入队也可能失败；闭包仍存活时排空，不能带着悬空引用返回
        _failed = true;
        _pool->Shutdown();
        throw;
    }
    for (const auto& error : errors) if (error) std::rethrow_exception(error);
}
}
