#pragma once

#include "algorithms/data_oriented_roam/DataOrientedRoamPassExperiment.h"
#include "tools/PerformanceTimer.h"

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
/// <summary>
/// 只改变指定阶段的动作和线程请求，保留工作量门槛及来源算法状态
/// 非法阶段或动作抛出异常，不能静默落入默认策略
/// </summary>
void ConfigureDataOrientedRoamPassAction(DataOrientedRoamState& state, TerrainLodPassId passId,
    TerrainLodPassAction action, std::size_t parallelWorkerCount);

/// <summary>
/// 在任何策略计时之前统一扩容并唤醒借用线程池，返回独立的准备耗时
/// 只准备该阶段可能使用的线程上限，不修改来源算法状态
/// </summary>
[[nodiscard]] float PrepareDataOrientedRoamPassWorkers(const DataOrientedRoamState& source,
    TerrainLodPassId passId, std::size_t parallelWorkerCount);

/// <summary>
/// 复制阶段输入、准备执行资源并连续计时一次生产调用，停表后才采集结果证据
/// 线程池与高度图由来源持有，本函数同步结束全部任务
/// </summary>
[[nodiscard]] DataOrientedRoamPassExperimentSample MeasureDataOrientedRoamPass(
    const DataOrientedRoamState& source, TerrainLodPassId passId, TerrainLodPassAction action,
    const DataOrientedRoamPassExperimentConfig& config);

namespace Detail
{
// 用静态编排固定准备、执行和取证边界，测试可替换时钟而不在生产路径添加虚调用
template<class Timer = Tools::PerformanceTimer, class Prepare, class Execute, class Capture>
auto MeasurePreparedCpuPass(Prepare&& prepare, Execute&& execute, Capture&& capture)
{
    auto state = prepare();
    Timer timer;
    execute(*state);
    const auto elapsed = timer.Stop();
    return capture(*state, elapsed);
}
}
}
