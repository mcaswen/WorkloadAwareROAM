#pragma once

#include "algorithms/data_oriented_roam/materialization/NativePlanningQueues.h"

namespace ParallelRoam::Algorithms::DataOrientedRoam::Materialization
{
/// <summary>
/// 外部审计可在同步调用内读取工作区，不能保存引用或用它作为物化输入
/// 全量诊断回调只用于验证，普通计时入口不执行这些回调
/// </summary>
struct NativePlanningAudit
{
    DecisionTraceSink Decisions;
    void* Context{nullptr};
    void (*AfterStep)(void*, const NativePlanningView&, const NativePlanningQueues&){nullptr};
    void (*Finished)(void*, const NativePlanningView&, const NativePlanningQueues&, const NativeTargetPlan&){nullptr};
    bool CollectReadCoverage{false};
};

/// <summary>
/// 从已评分、共形且预算合法的来源独立规划严格细分目标，调用期间来源必须保持不变
/// 当前支持冻结的关闭评分镜像配置；函数返回前销毁临时邻接与堆，结果不包含生产状态
/// </summary>
[[nodiscard]] NativeTargetPlan BuildNativeSplitTarget(const DataOrientedRoamState& source,
    const NativePlanningAudit& audit = {});
}
