#pragma once

#include "experiment/roam_materialization/MaterializationState.h"

namespace ParallelRoam::Experiment::RoamMaterialization
{
/// <summary>
/// 直接枚举变化支持集并连接最终边，不调用逐菱形参考或生产递归细分
/// </summary>
class MaterializationPatch
{
public:
    static void Apply(MaterializationState& state, const CertifiedTarget& target, WorkCounters* work = nullptr,
        const MaterializationExecution& execution = {});
};
}
