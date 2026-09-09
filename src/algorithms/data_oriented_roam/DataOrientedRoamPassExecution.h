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
/// 同步借用当前阶段的只读输入
/// 必须在回调返回前完成读取，不得保留状态引用供后续使用
/// </summary>
using DataOrientedRoamPassObserver =
    std::function<void(const DataOrientedRoamState&, TerrainLodPassId)>;

/// <summary>
/// 为生产与回放统一准备本帧状态
/// 高度图无效时清空网格并返回 false，调用方应停止后续阶段
/// </summary>
[[nodiscard]] bool PrepareDataOrientedRoamFrame(
    DataOrientedRoamState& state,
    const Terrain::HeightMap& heightMap,
    float terrainSize,
    float heightScale,
    const TerrainLodViewInput& view,
    const DataOrientedRoamSettings& settings);

/// <summary>
/// 按当前策略执行指定的单个阶段
/// 拓扑阶段要求输入队列已完成对应评分，入口不会重新评分
/// </summary>
void ExecuteDataOrientedRoamPass(DataOrientedRoamState& state, TerrainLodPassId passId);

/// <summary>
/// 按固定顺序执行五个阶段，每个阶段开始前同步调用观察器
/// 观察器返回后才开始阶段计时，采集成本不计入阶段耗时
/// </summary>
void ExecuteDataOrientedRoamCpuPasses(
    DataOrientedRoamState& state,
    const DataOrientedRoamPassObserver& observer = {});
}
