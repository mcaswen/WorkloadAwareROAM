#pragma once

#include "experiment/roam_materialization/MaterializationState.h"

namespace ParallelRoam::Experiment::RoamMaterialization
{
/// <summary>
/// 独立全量检查只供认证和计时外取证，不修复被测状态
/// </summary>
class MaterializationValidation
{
public:
    [[nodiscard]] static TargetRequest Difference(const MaterializationState& state, EventSet target);
    [[nodiscard]] static CertifiedTarget Certify(const MaterializationState& state, const TargetRequest& request);
    static void Validate(const MaterializationState& state);
    static void Compare(const MaterializationState& a, const MaterializationState& b);
    [[nodiscard]] static EventSet EnumerateLeaves(const EventSet& events);
    static void ValidateClosed(const EventSet& events, int maxDepth, std::size_t budget);
};
}
