#pragma once

#include "algorithms/ITerrainLodAlgorithm.h"
#include "algorithms/classic_roam/ClassicRoamMeshBuilder.h"

namespace ParallelRoam::Algorithms::ClassicRoam
{
/// <summary>
/// 将 ClassicRoamMeshBuilder 接入项目统一的地形 LOD 接口
/// 算法对象跨帧保留网格生成器中的拓扑状态，调用 Reset 时再全部清空
/// </summary>
class ClassicRoamTerrainLodAlgorithm final : public ITerrainLodAlgorithm
{
public:
    [[nodiscard]] TerrainLodAlgorithmInfo Info() const override;
    [[nodiscard]] TerrainLodAlgorithmCapabilities Capabilities() const override;

    [[nodiscard]] bool BuildRenderData(
        const TerrainLodBuildInput& input,
        TerrainLodRenderPacket& outPacket,
        std::string* errorMessage) override;

    [[nodiscard]] const TerrainLodStats& Stats() const override;
    void Reset() override;

private:
    [[nodiscard]] static ClassicRoamSettings ToClassicSettings(const TerrainLodSettings& settings);
    [[nodiscard]] static TerrainLodStats ToTerrainLodStats(const ClassicRoamStats& stats);

    ClassicRoamMeshBuilder _builder;
    TerrainLodStats _stats;
};
} // 命名空间 ParallelRoam::Algorithms::ClassicRoam
