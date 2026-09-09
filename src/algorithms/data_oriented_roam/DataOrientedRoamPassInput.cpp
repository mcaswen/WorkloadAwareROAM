#include "algorithms/data_oriented_roam/DataOrientedRoamPassInput.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"

#include <algorithm>
#include <stdexcept>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
namespace
{
/// <summary>
/// 按顺序编码容器的长度与有效元素，排除预留容量和内存地址
/// </summary>
template<class Value>
void AppendSequence(std::uint64_t& hash, const std::vector<Value>& values)
{
    AppendTerrainLodHash(hash, static_cast<std::uint64_t>(values.size()));
    for (const auto& value : values)
        AppendTerrainLodHash(hash, value);
}

/// <summary>
/// 迟滞集合只表达成员关系，编码前先对路径排序
/// 同一组路径应得到相同哈希，不受插入顺序和散列表桶布局影响
/// </summary>
void AppendPaths(std::uint64_t& hash, const std::unordered_set<std::uint64_t>& paths)
{
    std::vector<std::uint64_t> ordered(paths.begin(), paths.end());
    std::sort(ordered.begin(), ordered.end());
    AppendSequence(hash, ordered);
}
}

std::uint64_t HashDataOrientedRoamPassInput(const DataOrientedRoamState& state, TerrainLodPassId passId)
{
    if (passId != TerrainLodPassId::MergeScore && passId != TerrainLodPassId::SplitScore &&
        passId != TerrainLodPassId::MergeTopology && passId != TerrainLodPassId::SplitTopology &&
        passId != TerrainLodPassId::MeshEmit)
        throw std::invalid_argument{"Expected a CPU ROAM input pass"};
    std::uint64_t hash = TerrainLodHashOffset;
    AppendTerrainLodHash(hash, DataOrientedRoamPassInputVersion);
    AppendTerrainLodHash(hash, static_cast<std::uint32_t>(passId));
    // 配置资产的字节身份由冻结清单绑定且这里不编码借用地址
    AppendTerrainLodHash(hash, state.HeightMap != nullptr);
    if (state.HeightMap != nullptr)
    {
        AppendTerrainLodHash(hash, state.HeightMap->Width());
        AppendTerrainLodHash(hash, state.HeightMap->Height());
    }
    AppendTerrainLodHash(hash, state.Settings.MaxDepth);
    AppendTerrainLodHash(hash, state.Settings.TriangleBudget);
    AppendTerrainLodHash(hash, state.Settings.SplitThreshold);
    AppendTerrainLodHash(hash, state.Settings.MergeThreshold);
    AppendTerrainLodHash(hash, state.Settings.EnableLocalConstraints);
    AppendTerrainLodHash(hash, state.Settings.MirrorSplitScoresToNodePool);
    AppendTerrainLodHash(hash, state.TerrainSize);
    AppendTerrainLodHash(hash, state.HeightScale);
    AppendTerrainLodHash(hash, state.BuildSequence);
    AppendTerrainLodHash(hash, state.TopologyMaxDepth);
    AppendTerrainLodHash(hash, state.RootA);
    AppendTerrainLodHash(hash, state.RootB);
    AppendTerrainLodHash(hash, state.DrawableWidth);
    AppendTerrainLodHash(hash, state.DrawableHeight);
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            AppendTerrainLodHash(hash, state.ViewProjection[column][row]);
    for (const auto& plane : state.FrustumPlanes)
        for (int component = 0; component < 4; ++component)
            AppendTerrainLodHash(hash, plane[component]);
    // 原子预算在同步边界读取且不通过加载修改其数值
    AppendTerrainLodHash(hash, state.RemainingSerialSplitBudget);
    AppendTerrainLodHash(hash, state.RemainingParallelSplitBudget.load(std::memory_order_relaxed));
    AppendPaths(hash, state.PreviousSplitPaths);
    AppendPaths(hash, state.CurrentSplitPaths);
    AppendTerrainLodHash(hash, state.VarianceTreeMaxDepth);
    for (const auto& tree : state.VarianceTrees)
        AppendSequence(hash, tree);
    // 节点采用显式字段编码以避免结构填充并保留历史可复用子节点
    AppendTerrainLodHash(hash, static_cast<std::uint64_t>(state.Nodes.size()));
    for (const auto& domain : state.Nodes.Domains)
        for (const auto uv : {domain.A, domain.B, domain.C})
        {
            AppendTerrainLodHash(hash, uv.x);
            AppendTerrainLodHash(hash, uv.y);
        }
    AppendSequence(hash, state.Nodes.Parents);
    AppendSequence(hash, state.Nodes.LeftChildren);
    AppendSequence(hash, state.Nodes.RightChildren);
    AppendSequence(hash, state.Nodes.BaseNeighbors);
    AppendSequence(hash, state.Nodes.LeftNeighbors);
    AppendSequence(hash, state.Nodes.RightNeighbors);
    AppendSequence(hash, state.Nodes.InteriorChunkIds);
    AppendSequence(hash, state.Nodes.GeometricErrors);
    AppendSequence(hash, state.Nodes.ScreenErrors);
    AppendSequence(hash, state.Nodes.VarianceIndices);
    AppendSequence(hash, state.Nodes.PathIds);
    AppendSequence(hash, state.Nodes.CreatedBuildIds);
    AppendSequence(hash, state.Nodes.ActivatedBuildIds);
    AppendSequence(hash, state.Nodes.SplitBuildIds);
    AppendSequence(hash, state.Nodes.MergeBuildIds);
    AppendSequence(hash, state.Nodes.Depths);
    AppendSequence(hash, state.Nodes.VarianceTreeIndices);
    AppendSequence(hash, state.Nodes.ActivatedByForcedSplits);
    AppendSequence(hash, state.Nodes.IsSplits);
    AppendSequence(hash, state.ActiveInternalNodes);
    AppendSequence(hash, state.ActiveLeafNodes);
    // 长期队列保留堆顺序与成员反向位置以识别同分候选的不同输入
    AppendTerrainLodHash(hash, static_cast<std::uint64_t>(state.NodeMembership.size()));
    for (const auto& member : state.NodeMembership)
    {
        AppendTerrainLodHash(hash, member.ActiveInternalPosition);
        AppendTerrainLodHash(hash, member.ActiveLeafPosition);
        AppendTerrainLodHash(hash, member.SplitQueuePosition);
        AppendTerrainLodHash(hash, member.MergeQueuePosition);
        AppendTerrainLodHash(hash, member.MergeQueueRepresentative);
        AppendTerrainLodHash(hash, member.MergeQueuePartner);
    }
    const auto appendQueue = [&hash](const auto& queue) {
        AppendTerrainLodHash(hash, static_cast<std::uint64_t>(queue.size()));
        for (const auto& entry : queue)
        {
            AppendTerrainLodHash(hash, entry.Node);
            AppendTerrainLodHash(hash, entry.Score);
        }
    };
    appendQueue(state.SplitQueue);
    appendQueue(state.MergeQueue);
    AppendSequence(hash, state.SplitQueueBlockedBuildIds);
    // 槽位有序修改和调试过渡参与下一次网格更新而非仅用于展示
    const auto& mesh = state.IncrementalMesh.Metadata;
    AppendSequence(hash, mesh.NodeSlots);
    AppendSequence(hash, mesh.SlotOwners);
    AppendSequence(hash, mesh.SlotDirtyGenerations);
    AppendSequence(hash, mesh.DirtySlots);
    AppendSequence(hash, mesh.DebugTransitionLeaves);
    AppendTerrainLodHash(hash, mesh.Generation);
    AppendTerrainLodHash(hash, mesh.RequiresFullUpload);
    AppendTerrainLodHash(hash, mesh.NeedsInitialization);
    AppendTerrainLodHash(hash, mesh.TracksTopologyEdits);
    AppendTerrainLodHash(hash, static_cast<std::uint64_t>(mesh.TopologyEdits.size()));
    for (const auto& edit : mesh.TopologyEdits)
    {
        AppendTerrainLodHash(hash, static_cast<std::uint32_t>(edit.Type));
        AppendTerrainLodHash(hash, edit.Node);
    }
    AppendTerrainLodHash(hash, static_cast<std::uint64_t>(mesh.UpdateRanges.size()));
    for (const auto& range : mesh.UpdateRanges)
    {
        AppendTerrainLodHash(hash, range.FirstTriangle);
        AppendTerrainLodHash(hash, range.TriangleCount);
    }
    // 增量提交保留旧几何的未写部分所以有效内容也属于阶段输入
    AppendTerrainLodHash(hash, HashTerrainLodMesh(state.IncrementalMesh.Data));
    return hash;
}
}
