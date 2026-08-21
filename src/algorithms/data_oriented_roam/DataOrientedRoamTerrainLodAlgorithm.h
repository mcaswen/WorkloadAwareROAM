#pragma once

#include "algorithms/ITerrainLodAlgorithm.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamPipeline.h"

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
/// <summary>
/// 将 SoA 形式的 DOD ROAM 接入项目统一的地形 LOD 接口
/// 算法对象跨帧保留流水线，每次更新后都可以读取公共统计结果
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
