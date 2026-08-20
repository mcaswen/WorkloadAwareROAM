#pragma once

#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
/// <summary>
/// DOD state 的创建、重置和活动拓扑快照接口
/// 只在 builder 初始化、输入变化或 Build 收尾阶段调用；所有修改都落到调用方提供的 state
/// </summary>
// PathId 在整个 state 生命周期内保持稳定，用于跨帧 split/merge hysteresis
[[nodiscard]] std::uint64_t LeftChildPathId(std::uint64_t parentPathId);
[[nodiscard]] std::uint64_t RightChildPathId(std::uint64_t parentPathId);

[[nodiscard]] DataOrientedRoamChunkId ComputeInteriorChunkId(const TriangleDomain& domain);

// AddNode 追加 node pool 元素；返回值只在当前 state 生命周期内有效
[[nodiscard]] DataOrientedRoamNodeIndex AddNode(
    DataOrientedRoamState& state,
    const TriangleDomain& domain,
    DataOrientedRoamNodeIndex parent,
    int depth,
    std::uint64_t pathId,
    std::uint8_t varianceTreeIndex,
    std::size_t varianceIndex);

// ReserveNodePool 只调整容量，不改变活动拓扑；ResetTopology 会重建两棵根树
void ReserveNodePool(DataOrientedRoamState& state);
void ResetTopology(DataOrientedRoamState& state);

// NeedsTopologyReset 只读比较输入；不修改 state
[[nodiscard]] bool NeedsTopologyReset(
    const DataOrientedRoamState& state,
    const Terrain::HeightMap& heightMap,
    float terrainSize,
    float heightScale,
    const DataOrientedRoamSettings& settings);

// 这些快照函数由 validator、mesh emit 和统计阶段读取活动拓扑
void CollectLeafNodes(
    const DataOrientedRoamState& state,
    std::vector<DataOrientedRoamNodeIndex>& leafNodes);

void CollectActiveSplitPaths(DataOrientedRoamState& state);

void AccumulateLeafStats(
    DataOrientedRoamState& state,
    const std::vector<DataOrientedRoamNodeIndex>& leafNodes);
} // namespace ParallelRoam::Algorithms::DataOrientedRoam
