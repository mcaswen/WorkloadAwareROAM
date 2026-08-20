#pragma once

#include "algorithms/data_oriented_roam/DataOrientedRoamTypes.h"

#include <cstddef>
#include <memory>

namespace ParallelRoam::Algorithms
{
struct TerrainLodViewInput;
}

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
struct DataOrientedRoamState;
class DataOrientedRoamThreadPool;

/// <summary>
/// Data-Oriented CPU ROAM 在 SoA 节点池上维护持久拓扑并生成 CPU Mesh
/// 由 DataOrientedRoamTerrainLodAlgorithm 持有；内部 state 和线程池跨帧复用
/// Build 会修改它；调用方只读取输出 mesh、State 和 Stats
/// </summary>
class DataOrientedRoamPipeline
{
public:
    DataOrientedRoamPipeline();
    ~DataOrientedRoamPipeline();

    DataOrientedRoamPipeline(const DataOrientedRoamPipeline&) = delete;
    DataOrientedRoamPipeline& operator=(const DataOrientedRoamPipeline&) = delete;
    DataOrientedRoamPipeline(DataOrientedRoamPipeline&&) noexcept;
    DataOrientedRoamPipeline& operator=(DataOrientedRoamPipeline&&) noexcept;

    [[nodiscard]] const Terrain::TerrainMeshData& Build(
        const Terrain::HeightMap& heightMap,
        float terrainSize,
        float heightScale,
        const TerrainLodViewInput& view,
        const DataOrientedRoamSettings& settings);

    [[nodiscard]] const DataOrientedRoamStats& Stats() const;
    [[nodiscard]] const DataOrientedRoamState& State() const;
    [[nodiscard]] const std::vector<DataOrientedRoamMeshUpdateRange>& MeshUpdateRanges() const;
    [[nodiscard]] bool MeshRequiresFullUpload() const;
    [[nodiscard]] std::uint64_t MeshGeneration() const;

private:
    void BuildInternal(
        const Terrain::HeightMap& heightMap,
        float terrainSize,
        float heightScale,
        const TerrainLodViewInput& view,
        const DataOrientedRoamSettings& settings);

    std::unique_ptr<DataOrientedRoamState> _state;
    std::unique_ptr<DataOrientedRoamThreadPool> _threadPool;
};
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
