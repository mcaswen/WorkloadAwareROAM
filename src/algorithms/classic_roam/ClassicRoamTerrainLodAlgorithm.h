#pragma once

#include "algorithms/ITerrainLodAlgorithm.h"
#include "algorithms/classic_roam/ClassicRoamMeshBuilder.h"

namespace ParallelRoam::Algorithms::ClassicRoam
{
/// <summary>
/// 将现有 ClassicRoamMeshBuilder 适配到三版本共享的 Terrain LOD 算法接口
/// 由算法工厂创建并长期持有；内部 builder 跨帧复用，Reset 时清空持久拓扑
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
