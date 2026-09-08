#pragma once

#include "algorithms/data_oriented_roam/DataOrientedRoamTypes.h"

#include <functional>

namespace ParallelRoam::Algorithms
{
struct TerrainLodViewInput;
}

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
struct DataOrientedRoamState;

/// <summary>
/// 回调只借用当前阶段输入且必须在返回前完成读取
/// </summary>
using DataOrientedRoamPassObserver =
    std::function<void(const DataOrientedRoamState&, TerrainLodPassId)>;

/// <summary>
/// 生产与回放共用帧准备规则并以返回值表示高度图是否可执行
/// </summary>
[[nodiscard]] bool PrepareDataOrientedRoamFrame(
    DataOrientedRoamState& state,
    const Terrain::HeightMap& heightMap,
    float terrainSize,
    float heightScale,
    const TerrainLodViewInput& view,
    const DataOrientedRoamSettings& settings);

/// <summary>
/// 执行当前策略指定的单个阶段且拓扑输入必须已完成对应评分
/// </summary>
void ExecuteDataOrientedRoamPass(DataOrientedRoamState& state, TerrainLodPassId passId);

/// <summary>
/// 按固定顺序同步观察并执行五阶段且观察成本不进入阶段包络
/// </summary>
void ExecuteDataOrientedRoamCpuPasses(
    DataOrientedRoamState& state,
    const DataOrientedRoamPassObserver& observer = {});
}
