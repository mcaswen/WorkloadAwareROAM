#pragma once

#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
/// <summary>
/// 提供 DOD 节点创建、拓扑重置和活动状态汇总
/// 所有操作都作用于调用方传入的 DataOrientedRoamState
/// </summary>
// PathId 在状态清空前始终对应同一个拓扑位置，用于下一帧的迟滞判断
[[nodiscard]] std::uint64_t LeftChildPathId(std::uint64_t parentPathId);
[[nodiscard]] std::uint64_t RightChildPathId(std::uint64_t parentPathId);

/// <summary>
/// 三个顶点位于同一分块时返回该分块编号，否则返回无效编号并交给主线程顺序处理
/// </summary>
[[nodiscard]] DataOrientedRoamChunkId ComputeInteriorChunkId(const TriangleDomain& domain);

// AddNode 向节点池的每个数组追加一个字段值，并返回之后不会变化的节点下标
[[nodiscard]] DataOrientedRoamNodeIndex AddNode(
    DataOrientedRoamState& state,
    const TriangleDomain& domain,
    DataOrientedRoamNodeIndex parent,
    int depth,
    std::uint64_t pathId,
    std::uint8_t varianceTreeIndex,
    std::size_t varianceIndex);

// ReserveNodePool 只预留节点容量，ResetTopology 才会清空并重建两个根节点
void ReserveNodePool(DataOrientedRoamState& state);
void ResetTopology(DataOrientedRoamState& state);

// NeedsTopologyReset 比较新旧输入是否兼容，不修改现有状态
[[nodiscard]] bool NeedsTopologyReset(
    const DataOrientedRoamState& state,
    const Terrain::HeightMap& heightMap,
    float terrainSize,
    float heightScale,
    const DataOrientedRoamSettings& settings);

/// <summary>
/// 从两个根节点收集最终活动叶集合，排除节点池中等待复用的历史节点
/// </summary>
void CollectLeafNodes(
    const DataOrientedRoamState& state,
    std::vector<DataOrientedRoamNodeIndex>& leafNodes);

/// <summary>
/// 根据最终活动拓扑重新记录仍处于细分状态的路径，供下一帧迟滞判断使用
/// </summary>
void CollectActiveSplitPaths(DataOrientedRoamState& state);

/// <summary>
/// 根据最终叶集合汇总活动数量、调试分类和实际最大深度
/// </summary>
void AccumulateLeafStats(
    DataOrientedRoamState& state,
    const std::vector<DataOrientedRoamNodeIndex>& leafNodes);
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
