#pragma once

#include "algorithms/ITerrainLodAlgorithm.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamPipeline.h"

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
/// <summary>
/// 将 SoA Data-Oriented ROAM 接入三版本共享的 Terrain LOD 算法接口
/// 由算法工厂创建并长期持有；每次 Build 转发输入到 MeshBuilder，Stats 在调用后可读取
/// </summary>
class DataOrientedRoamTerrainLodAlgorithm final : public ITerrainLodAlgorithm
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
    [[nodiscard]] static DataOrientedRoamSettings ToDataOrientedSettings(const TerrainLodSettings& settings);
    [[nodiscard]] static TerrainLodStats ToTerrainLodStats(const DataOrientedRoamStats& stats);

    DataOrientedRoamPipeline _pipeline;
    TerrainLodStats _stats;
};
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
