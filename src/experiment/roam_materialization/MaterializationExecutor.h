#pragma once

#include "experiment/roam_materialization/MaterializationTypes.h"
#include "tools/CpuTaskExecutor.h"

namespace ParallelRoam::Experiment::RoamMaterialization
{
/// <summary>
/// 由实验入口拥有线程池，核心只在同步事务期间借用执行回调
/// 提交失败会排空并停止线程池，不能继续使用该适配器
/// </summary>
class MaterializationExecutor
{
public:
    explicit MaterializationExecutor(std::size_t workers);
    ~MaterializationExecutor();
    MaterializationExecutor(const MaterializationExecutor&) = delete;
    MaterializationExecutor& operator=(const MaterializationExecutor&) = delete;
    [[nodiscard]] MaterializationExecution Execution();
private:
    Tools::CpuTaskExecutor _executor;
};
}
