#pragma once

#include "experiment/roam_materialization/MaterializationState.h"

namespace ParallelRoam::Algorithms::DataOrientedRoam { struct DataOrientedRoamState; }

namespace ParallelRoam::Experiment::RoamMaterialization
{
/// <summary>
/// 来源恢复、静态环境复制和目标发现分别记账，不能将免费目标的应用时间称为完整更新
/// </summary>
struct DodTargetTask
{
    std::unique_ptr<MaterializationState> Initial;
    std::shared_ptr<const FrozenEnvironment> Environment;
    TargetRequest Request;
    std::uint64_t SourceHash{0};
    std::size_t SourceRecords{0}, StaticBytes{0};
    double EnvironmentMs{0}, ImportMs{0}, DiscoveryCopyMs{0}, DiscoveryMs{0}, DifferenceMs{0};
};

/// <summary>
/// 唯一解释生产布局的适配器；来源只读，所有返回数据独立拥有其生命周期
/// 导入允许全量规范化，但遇到来源语义不对应时拒绝，不能补齐一个不同的状态
/// </summary>
class MaterializationDodBridge
{
public:
    [[nodiscard]] static DodTargetTask Capture(
        const Algorithms::DataOrientedRoam::DataOrientedRoamState& source);
private:
    [[nodiscard]] static std::unique_ptr<MaterializationState> Import(
        const Algorithms::DataOrientedRoam::DataOrientedRoamState& source,
        std::shared_ptr<const FrozenEnvironment> environment);
};
}
