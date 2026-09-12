#pragma once

#include "experiment/roam_materialization/MaterializationState.h"

namespace ParallelRoam::Experiment::RoamMaterialization
{
/// <summary>
/// 独立逐菱形参考只改动本次操作的叶和外部接口，不调用批量补丁
/// </summary>
class MaterializationReference
{
public:
    static void Apply(MaterializationState& state, const CertifiedTarget& target, WorkCounters* work = nullptr);
    // 续接接口遵守队列抑制和预算；缺少前置时返回 false，不隐式更换目标
    [[nodiscard]] static bool TrySplit(MaterializationState& state, NodeId root, WorkCounters* work = nullptr);
    [[nodiscard]] static bool TryMerge(MaterializationState& state, NodeId group, WorkCounters* work = nullptr);

private:
    static void EditGroup(MaterializationState& state, NodeId group, bool split);
};
}
