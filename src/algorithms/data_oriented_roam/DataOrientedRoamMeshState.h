#pragma once

#include "algorithms/data_oriented_roam/DataOrientedRoamTypes.h"

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
/// <summary>
/// 区分需要重放到网格槽位的细分与合并操作
/// 修改记录必须保持拓扑提交顺序，保证槽位重放结果一致
/// </summary>
enum class DataOrientedRoamMeshTopologyEditType
{
    Split,
    Merge,
};

/// <summary>
/// 保存一次拓扑修改的类型与父节点索引
/// 网格规划通过父节点读取仍保留的子节点关系，无需复制整份拓扑
/// </summary>
struct DataOrientedRoamMeshTopologyEdit
{
    DataOrientedRoamMeshTopologyEditType Type{DataOrientedRoamMeshTopologyEditType::Split};
    DataOrientedRoamNodeIndex Node{InvalidDataOrientedRoamNodeIndex};
};

/// <summary>
/// 保存主线程网格规划所需的跨帧槽位、脏标记和拓扑修改记录
/// 不携带顶点或索引数组，允许探测独立复制并重放元数据
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
/// 同时拥有增量网格的元数据与真实几何
/// 工作量探测只复制 Metadata，几何写入由网格提交阶段负责
/// </summary>
struct DataOrientedRoamIncrementalMesh
{
    DataOrientedRoamMeshMetadata Metadata;
    Terrain::TerrainMeshData Data;
};
}
