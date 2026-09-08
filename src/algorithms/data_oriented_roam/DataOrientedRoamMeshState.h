#pragma once

#include "algorithms/data_oriented_roam/DataOrientedRoamTypes.h"

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
/// <summary>
/// 区分槽位重放操作且记录顺序必须与拓扑提交顺序一致
/// </summary>
enum class DataOrientedRoamMeshTopologyEditType
{
    Split,
    Merge,
};

/// <summary>
/// 只引用父节点以便最终网格规划读取仍保留的子节点关系
/// </summary>
struct DataOrientedRoamMeshTopologyEdit
{
    DataOrientedRoamMeshTopologyEditType Type{DataOrientedRoamMeshTopologyEditType::Split};
    DataOrientedRoamNodeIndex Node{InvalidDataOrientedRoamNodeIndex};
};

/// <summary>
/// 保存主线程规划所需的跨帧槽位和更新记录且不携带几何数组
/// </summary>
struct DataOrientedRoamMeshMetadata
{
    // 反向索引与绘制顺序必须同步维护以保证尾槽填洞后所有权一致
    std::vector<DataOrientedRoamPosition> NodeSlots;
    std::vector<DataOrientedRoamNodeIndex> SlotOwners;
    std::vector<std::uint64_t> SlotDirtyGenerations;
    std::vector<DataOrientedRoamPosition> DirtySlots;
    std::vector<DataOrientedRoamMeshUpdateRange> UpdateRanges;
    std::vector<DataOrientedRoamNodeIndex> DebugTransitionLeaves;
    std::vector<DataOrientedRoamMeshTopologyEdit> TopologyEdits;
    std::uint64_t Generation{0U};
    bool RequiresFullUpload{true};
    bool NeedsInitialization{true};
    bool TracksTopologyEdits{false};
};

/// <summary>
/// 状态拥有元数据与真实几何而探测只复制元数据成员
/// </summary>
struct DataOrientedRoamIncrementalMesh
{
    DataOrientedRoamMeshMetadata Metadata;
    Terrain::TerrainMeshData Data;
};
}
