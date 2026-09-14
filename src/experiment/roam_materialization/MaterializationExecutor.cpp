#include "experiment/roam_materialization/MaterializationExecutor.h"

namespace ParallelRoam::Experiment::RoamMaterialization
{
MaterializationExecutor::MaterializationExecutor(std::size_t workers) : _executor(workers) {}
MaterializationExecutor::~MaterializationExecutor() = default;

MaterializationExecution MaterializationExecutor::Execution()
{
    // 研究层只转换回调类型；任务寿命和异常排空由公共执行器保证
    return {_executor.WorkerCount(), [this](std::size_t chunks, const auto& task) {
        _executor.Dispatch(chunks, task);
    }};
}
}
