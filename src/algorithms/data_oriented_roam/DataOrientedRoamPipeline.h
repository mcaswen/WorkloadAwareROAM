#pragma once

#include "algorithms/data_oriented_roam/DataOrientedRoamTypes.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamPassExecution.h"

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
/// 在 SoA 节点池上维护 DOD CPU ROAM 跨帧保留的拓扑，并只更新发生变化的 CPU 网格部分
/// 对象跨帧复用状态和线程池，调用方每帧更新一次并读取网格、状态和统计结果
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

    /// <summary>
    /// 使用本帧输入更新拓扑和网格，并返回渲染器可以直接读取的内部 CPU 网格
    /// </summary>
    [[nodiscard]] const Terrain::TerrainMeshData& Build(
        const Terrain::HeightMap& heightMap,
        float terrainSize,
        float heightScale,
        const TerrainLodViewInput& view,
        const DataOrientedRoamSettings& settings);

    /// <summary>
    /// 在生产执行边界同步观察各阶段的真实输入
    /// 回调不得重入流水线，也不得保留状态引用供返回后使用
    /// </summary>
    [[nodiscard]] const Terrain::TerrainMeshData& BuildWithPassObserver(
        const Terrain::HeightMap& heightMap,
        float terrainSize,
        float heightScale,
        const TerrainLodViewInput& view,
        const DataOrientedRoamSettings& settings,
        const DataOrientedRoamPassObserver& observer);

    /// <summary>
    /// 返回最近一次更新的统计和当前跨帧状态且引用仅供读取
    /// </summary>
    [[nodiscard]] const DataOrientedRoamStats& Stats() const;
    [[nodiscard]] const DataOrientedRoamState& State() const;

    /// <summary>
    /// 提供本次需要上传的连续网格范围、完整上传标记和网格版本号
    /// </summary>
    [[nodiscard]] const std::vector<DataOrientedRoamMeshUpdateRange>& MeshUpdateRanges() const;
    [[nodiscard]] bool MeshRequiresFullUpload() const;
    [[nodiscard]] std::uint64_t MeshGeneration() const;

private:
    /// <summary>
    /// 依次完成输入准备、低误差区域合并、高误差区域细分、网格更新和统计收尾
    /// </summary>
    void BuildInternal(
        const Terrain::HeightMap& heightMap,
        float terrainSize,
        float heightScale,
        const TerrainLodViewInput& view,
        const DataOrientedRoamSettings& settings,
        const DataOrientedRoamPassObserver& observer = {});

    std::unique_ptr<DataOrientedRoamState> _state;
    std::unique_ptr<DataOrientedRoamThreadPool> _threadPool;
};
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
