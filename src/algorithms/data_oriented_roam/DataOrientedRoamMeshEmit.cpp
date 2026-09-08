#include "algorithms/data_oriented_roam/DataOrientedRoamMeshEmit.h"

#include "algorithms/RoamGeometry.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamParallel.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamScoring.h"

#include <algorithm>
#include <array>
#include <cstddef>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
namespace
{
constexpr std::size_t VerticesPerTriangle = 3U;

/// <summary>
/// 根据网格提交策略和待写槽位数量选择实际线程数量
/// 串行脏数据与串行全量策略都明确限制为一个线程
/// </summary>
std::size_t ResolveEmitWorkerCount(
    std::size_t triangleCount,
    TerrainLodMeshEmitAction action,
    std::size_t requestedWorkerCount,
    std::size_t minimumParallelTriangleCount)
{
    const bool serial = action == TerrainLodMeshEmitAction::SerialDirty ||
        action == TerrainLodMeshEmitAction::SerialFull;
    return ResolveDataOrientedRoamWorkerCount(
        triangleCount, serial ? 1U : requestedWorkerCount, minimumParallelTriangleCount);
}

/// <summary>
/// 将一个活动叶节点写入指定网格槽位
/// 槽位所有者决定几何内容，线程完成顺序不会改变输出位置
/// </summary>
void WriteDomainTriangle(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node,
    Terrain::TerrainMeshData& meshData,
    std::size_t triangleIndex)
{
    const auto baseIndex = static_cast<std::uint32_t>(triangleIndex * VerticesPerTriangle);
    const TriangleDomain& domain = state.Nodes.DomainAt(node);
    const std::array<glm::vec2, VerticesPerTriangle> uvs{domain.A, domain.B, domain.C};
    const glm::vec3 debugColor = DebugColorForLeaf(state, node);
    const float debugHighlight = DebugHighlightForLeaf(state, node);

    for (std::size_t vertexOffset = 0U; vertexOffset < uvs.size(); ++vertexOffset)
    {
        const glm::vec2& uv = uvs[vertexOffset];
        Terrain::TerrainMeshVertex vertex{};
        const Roam::TerrainWorldSample terrainSample =
            Roam::SampleTerrainWorld(*state.HeightMap, uv, state.TerrainSize, state.HeightScale);
        vertex.Position = terrainSample.Position;
        vertex.Normal = Roam::SampleHeightGradientNormal(
            *state.HeightMap,
            uv,
            state.TerrainSize,
            state.HeightScale);
        vertex.TexCoord = uv;
        vertex.Height = terrainSample.Height;
        vertex.DebugColor = debugColor;
        vertex.DebugHighlight = debugHighlight;
        meshData.Vertices[static_cast<std::size_t>(baseIndex) + vertexOffset] = vertex;
    }

    const glm::vec3 edge0 =
        meshData.Vertices[baseIndex + 1U].Position - meshData.Vertices[baseIndex].Position;
    const glm::vec3 edge1 =
        meshData.Vertices[baseIndex + 2U].Position - meshData.Vertices[baseIndex].Position;
    const bool pointsTowardPositiveY = glm::cross(edge0, edge1).y >= 0.0F;

    meshData.Indices[baseIndex] = baseIndex;
    meshData.Indices[baseIndex + 1U] =
        baseIndex + (pointsTowardPositiveY ? 1U : 2U);
    meshData.Indices[baseIndex + 2U] =
        baseIndex + (pointsTowardPositiveY ? 2U : 1U);
}

void MarkMeshSlotDirty(DataOrientedRoamState& state, std::size_t slot)
{
    DataOrientedRoamIncrementalMesh& mesh = state.IncrementalMesh;
    if (slot >= mesh.SlotDirtyGenerations.size())
    {
        return;
    }

    // 使用网格版本号去重后，同一槽位即使经历连续细分和合并也只会记录一次
    // 最终只为拓扑稳定后仍占用这些槽位的叶节点生成并上传数据
    if (mesh.SlotDirtyGenerations[slot] != mesh.Generation)
    {
        mesh.SlotDirtyGenerations[slot] = mesh.Generation;
        mesh.DirtySlots.push_back(static_cast<DataOrientedRoamPosition>(slot));
    }
}

void NormalizeDirtyMeshSlots(DataOrientedRoamIncrementalMesh& mesh)
{
    // 拓扑修改顺序不决定输出顺序，因此先按槽位排序并移除已经缩掉的尾部槽位
    // 后续顶点写入和上传区间生成可以共用同一连续访问顺序
    std::sort(mesh.DirtySlots.begin(), mesh.DirtySlots.end());
    mesh.DirtySlots.erase(
        std::remove_if(
            mesh.DirtySlots.begin(),
            mesh.DirtySlots.end(),
            [&mesh](DataOrientedRoamPosition slot) { return slot >= mesh.SlotOwners.size(); }),
        mesh.DirtySlots.end());
    mesh.DirtySlots.erase(
        std::unique(mesh.DirtySlots.begin(), mesh.DirtySlots.end()),
        mesh.DirtySlots.end());
}

void ResizeMeshForSlotCount(DataOrientedRoamIncrementalMesh& mesh)
{
    const std::size_t elementCount = mesh.SlotOwners.size() * VerticesPerTriangle;
    mesh.SlotDirtyGenerations.resize(mesh.SlotOwners.size(), 0U);
    mesh.Data.Vertices.resize(elementCount);
    mesh.Data.Indices.resize(elementCount);
}

void InitializeIncrementalMesh(DataOrientedRoamState& state)
{
    DataOrientedRoamIncrementalMesh& mesh = state.IncrementalMesh;
    // 首次初始化直接使用最终活动叶集合，无需重放从根节点到当前深度的历史细分
    // 后续更新保留未变化叶节点的槽位，只重写发生变化的部分
    mesh.Data = {};
    mesh.Data.GridWidth = state.HeightMap != nullptr ? state.HeightMap->Width() : 0;
    mesh.Data.GridHeight = state.HeightMap != nullptr ? state.HeightMap->Height() : 0;
    mesh.Data.TerrainSize = state.TerrainSize;
    mesh.Data.HeightScale = state.HeightScale;
    mesh.Data.Vertices.reserve(state.Settings.TriangleBudget * VerticesPerTriangle);
    mesh.Data.Indices.reserve(state.Settings.TriangleBudget * VerticesPerTriangle);

    mesh.NodeSlots.assign(state.Nodes.size(), InvalidDataOrientedRoamPosition);
    mesh.SlotOwners = state.ActiveLeafNodes;
    mesh.SlotDirtyGenerations.assign(mesh.SlotOwners.size(), 0U);
    mesh.DirtySlots.clear();
    mesh.UpdateRanges.clear();
    mesh.DebugTransitionLeaves.clear();
    mesh.TopologyEdits.clear();
    ResizeMeshForSlotCount(mesh);

    for (std::size_t slot = 0U; slot < mesh.SlotOwners.size(); ++slot)
    {
        const DataOrientedRoamNodeIndex node = mesh.SlotOwners[slot];
        if (state.IsValidNode(node))
        {
            mesh.NodeSlots[node] = static_cast<DataOrientedRoamPosition>(slot);
        }
        MarkMeshSlotDirty(state, slot);
    }

    mesh.RequiresFullUpload = true;
    mesh.NeedsInitialization = false;
}

bool AppendMeshLeaf(DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    DataOrientedRoamIncrementalMesh& mesh = state.IncrementalMesh;
    if (!state.IsValidNode(node) || node >= mesh.NodeSlots.size() ||
        mesh.NodeSlots[node] != InvalidDataOrientedRoamPosition ||
        mesh.SlotOwners.size() >= InvalidDataOrientedRoamPosition)
    {
        return false;
    }

    // 新叶节点只追加到稠密数组尾部，不改变已有叶节点的槽位
    // 渲染缓冲区容量足够时只需上传新增范围
    const std::size_t slot = mesh.SlotOwners.size();
    mesh.NodeSlots[node] = static_cast<DataOrientedRoamPosition>(slot);
    mesh.SlotOwners.push_back(node);
    ResizeMeshForSlotCount(mesh);
    MarkMeshSlotDirty(state, slot);
    return true;
}

bool RemoveMeshLeaf(DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    DataOrientedRoamIncrementalMesh& mesh = state.IncrementalMesh;
    if (!state.IsValidNode(node) || node >= mesh.NodeSlots.size())
    {
        return false;
    }

    const std::size_t removedSlot = mesh.NodeSlots[node];
    if (removedSlot == InvalidDataOrientedRoamPosition ||
        removedSlot >= mesh.SlotOwners.size() ||
        mesh.SlotOwners[removedSlot] != node)
    {
        return false;
    }

    const std::size_t lastSlot = mesh.SlotOwners.size() - 1U;
    // 用末尾槽位填补空洞以保持绘制范围连续，此处无需复制旧顶点
    // 被移动节点的新槽位已经标记为需要重写，稍后会统一生成正确数据
    if (removedSlot != lastSlot)
    {
        const DataOrientedRoamNodeIndex movedNode = mesh.SlotOwners[lastSlot];
        mesh.SlotOwners[removedSlot] = movedNode;
        mesh.NodeSlots[movedNode] = static_cast<DataOrientedRoamPosition>(removedSlot);
        MarkMeshSlotDirty(state, removedSlot);
    }

    mesh.NodeSlots[node] = InvalidDataOrientedRoamPosition;
    mesh.SlotOwners.pop_back();
    ResizeMeshForSlotCount(mesh);
    return true;
}

bool ReplaceMeshLeafWithChildren(DataOrientedRoamState& state, DataOrientedRoamNodeIndex parent)
{
    DataOrientedRoamIncrementalMesh& mesh = state.IncrementalMesh;
    if (!state.IsValidNode(parent) || parent >= mesh.NodeSlots.size())
    {
        return false;
    }

    const DataOrientedRoamNodeIndex leftChild = state.Nodes.LeftChildAt(parent);
    const DataOrientedRoamNodeIndex rightChild = state.Nodes.RightChildAt(parent);
    const std::size_t parentSlot = mesh.NodeSlots[parent];
    if (!state.IsValidNode(leftChild) || !state.IsValidNode(rightChild) ||
        leftChild >= mesh.NodeSlots.size() || rightChild >= mesh.NodeSlots.size() ||
        parentSlot == InvalidDataOrientedRoamPosition ||
        parentSlot >= mesh.SlotOwners.size() || mesh.SlotOwners[parentSlot] != parent ||
        mesh.NodeSlots[leftChild] != InvalidDataOrientedRoamPosition ||
        mesh.NodeSlots[rightChild] != InvalidDataOrientedRoamPosition)
    {
        return false;
    }

    // 与 Classic 相同，左子节点继承父节点槽位，右子节点追加到尾部
    // DOD 通过 NodeSlots 数组维护节点到槽位的反向关系
    mesh.NodeSlots[parent] = InvalidDataOrientedRoamPosition;
    mesh.NodeSlots[leftChild] = static_cast<DataOrientedRoamPosition>(parentSlot);
    mesh.SlotOwners[parentSlot] = leftChild;
    MarkMeshSlotDirty(state, parentSlot);
    return AppendMeshLeaf(state, rightChild);
}

bool ReplaceMeshChildrenWithLeaf(DataOrientedRoamState& state, DataOrientedRoamNodeIndex parent)
{
    DataOrientedRoamIncrementalMesh& mesh = state.IncrementalMesh;
    if (!state.IsValidNode(parent) || parent >= mesh.NodeSlots.size())
    {
        return false;
    }

    DataOrientedRoamNodeIndex retainedChild = state.Nodes.LeftChildAt(parent);
    DataOrientedRoamNodeIndex removedChild = state.Nodes.RightChildAt(parent);
    if (!state.IsValidNode(retainedChild) || !state.IsValidNode(removedChild) ||
        retainedChild >= mesh.NodeSlots.size() || removedChild >= mesh.NodeSlots.size() ||
        mesh.NodeSlots[parent] != InvalidDataOrientedRoamPosition ||
        mesh.NodeSlots[retainedChild] == InvalidDataOrientedRoamPosition ||
        mesh.NodeSlots[removedChild] == InvalidDataOrientedRoamPosition ||
        mesh.SlotOwners.empty())
    {
        return false;
    }

    // 保留的子节点不能位于末尾槽位，否则删除另一子节点时可能发生填洞移动
    // 该移动会覆盖刚写入槽位的父节点
    const std::size_t lastSlot = mesh.SlotOwners.size() - 1U;
    if (mesh.NodeSlots[retainedChild] == lastSlot)
    {
        std::swap(retainedChild, removedChild);
    }

    const std::size_t parentSlot = mesh.NodeSlots[retainedChild];
    if (parentSlot >= mesh.SlotOwners.size() || mesh.SlotOwners[parentSlot] != retainedChild)
    {
        return false;
    }

    mesh.NodeSlots[retainedChild] = InvalidDataOrientedRoamPosition;
    mesh.NodeSlots[parent] = static_cast<DataOrientedRoamPosition>(parentSlot);
    mesh.SlotOwners[parentSlot] = parent;
    MarkMeshSlotDirty(state, parentSlot);
    return RemoveMeshLeaf(state, removedChild);
}

void EmitDirtySlotRange(DataOrientedRoamState& state, std::size_t begin, std::size_t end)
{
    DataOrientedRoamIncrementalMesh& mesh = state.IncrementalMesh;
    for (std::size_t index = begin; index < end; ++index)
    {
        const std::size_t slot = mesh.DirtySlots[index];
        if (slot >= mesh.SlotOwners.size())
        {
            continue;
        }

        const DataOrientedRoamNodeIndex node = mesh.SlotOwners[slot];
        if (state.IsLeaf(node))
        {
            WriteDomainTriangle(state, node, mesh.Data, slot);
        }
    }
}

void EmitDirtyMeshSlots(DataOrientedRoamState& state)
{
    const std::size_t dirtyCount = state.IncrementalMesh.DirtySlots.size();
    state.Stats.EmitWorkerCount = ResolveEmitWorkerCount(
        dirtyCount,
        state.Settings.PassPolicy.MeshEmit,
        state.Settings.PassPolicy.MeshEmitWorkerCount,
        state.Settings.PassPolicy.MeshEmitMinParallelTriangleCount);
    const std::size_t workerCount = state.Stats.EmitWorkerCount;
    if (workerCount == 0U)
    {
        return;
    }

    if (workerCount == 1U)
    {
        EmitDirtySlotRange(state, 0U, dirtyCount);
        return;
    }

    const std::size_t chunkSize = (dirtyCount + workerCount - 1U) / workerCount;
    RunDataOrientedRoamWorkers(state, workerCount, [&](std::size_t workerIndex) {
        const std::size_t begin = workerIndex * chunkSize;
        const std::size_t end = std::min(begin + chunkSize, dirtyCount);
        if (begin < end)
        {
            EmitDirtySlotRange(state, begin, end);
        }
    });
}

/// <summary>
/// 按当前槽位所有者顺序重写完整网格
/// 该对照只改变数据写入范围，不重建拓扑或槽位映射
/// </summary>
void EmitFullMeshSerial(DataOrientedRoamState& state)
{
    DataOrientedRoamIncrementalMesh& mesh = state.IncrementalMesh;
    state.Stats.EmitWorkerCount = mesh.SlotOwners.empty() ? 0U : 1U;
    for (std::size_t slot = 0U; slot < mesh.SlotOwners.size(); ++slot)
    {
        const DataOrientedRoamNodeIndex node = mesh.SlotOwners[slot];
        if (state.IsLeaf(node))
        {
            WriteDomainTriangle(state, node, mesh.Data, slot);
        }
    }
    mesh.RequiresFullUpload = true;
}
} // 匿名命名空间

void BeginIncrementalMeshUpdate(
    DataOrientedRoamState& state,
    bool resetTopology)
{
    DataOrientedRoamIncrementalMesh& mesh = state.IncrementalMesh;
    mesh.DirtySlots.clear();
    mesh.UpdateRanges.clear();
    mesh.TopologyEdits.clear();
    mesh.RequiresFullUpload = false;
    mesh.TracksTopologyEdits = false;

    ++mesh.Generation;
    if (mesh.Generation == 0U)
    {
        // 版本号 0 表示网格尚未生成，首次更新必须从 1 开始
        ++mesh.Generation;
        std::fill(mesh.SlotDirtyGenerations.begin(), mesh.SlotDirtyGenerations.end(), 0U);
    }

    if (resetTopology)
    {
        mesh.NeedsInitialization = true;
    }
    mesh.TracksTopologyEdits = !mesh.NeedsInitialization;
}

void ResetIncrementalMeshStorage(DataOrientedRoamState& state)
{
    const std::uint64_t generation = state.IncrementalMesh.Generation;
    state.IncrementalMesh = {};
    state.IncrementalMesh.Generation = generation;
}

void RecordMeshSplit(DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    if (state.IncrementalMesh.TracksTopologyEdits)
    {
        state.IncrementalMesh.TopologyEdits.push_back(
            DataOrientedRoamMeshTopologyEdit{DataOrientedRoamMeshTopologyEditType::Split, node});
    }
}

void RecordMeshMerge(DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    if (state.IncrementalMesh.TracksTopologyEdits)
    {
        state.IncrementalMesh.TopologyEdits.push_back(
            DataOrientedRoamMeshTopologyEdit{DataOrientedRoamMeshTopologyEditType::Merge, node});
    }
}

void ApplyIncrementalMeshUpdates(DataOrientedRoamState& state)
{
    DataOrientedRoamIncrementalMesh& mesh = state.IncrementalMesh;
    if (mesh.NeedsInitialization)
    {
        InitializeIncrementalMesh(state);
    }
    else
    {
        // 用于标出本轮重建节点的颜色只显示一次更新，之后仍活动的叶节点需要刷新调试属性
        for (DataOrientedRoamNodeIndex node : mesh.DebugTransitionLeaves)
        {
            if (state.IsLeaf(node) && node < mesh.NodeSlots.size())
            {
                const std::size_t slot = mesh.NodeSlots[node];
                if (slot < mesh.SlotOwners.size() && mesh.SlotOwners[slot] == node)
                {
                    MarkMeshSlotDirty(state, slot);
                }
            }
        }
        mesh.DebugTransitionLeaves.clear();

        // 按记录顺序应用拓扑修改，使同一次更新中的连续细分和合并能够逐步
        // 将上一版槽位集合转换为最终活动叶集合
        bool replaySucceeded = true;
        for (const DataOrientedRoamMeshTopologyEdit& edit : mesh.TopologyEdits)
        {
            replaySucceeded = edit.Type == DataOrientedRoamMeshTopologyEditType::Split
                ? ReplaceMeshLeafWithChildren(state, edit.Node)
                : ReplaceMeshChildrenWithLeaf(state, edit.Node);
            if (!replaySucceeded)
            {
                break;
            }
        }

        if (!replaySucceeded)
        {
            // 拓扑记录与槽位状态不一致时按当前活动叶集合完整重建，避免输出旧拓扑对应的网格
            InitializeIncrementalMesh(state);
        }
    }

    mesh.TopologyEdits.clear();
    mesh.TracksTopologyEdits = false;
    NormalizeDirtyMeshSlots(mesh);
    if (state.Settings.PassPolicy.MeshEmit == TerrainLodMeshEmitAction::SerialFull)
    {
        EmitFullMeshSerial(state);
    }
    else
    {
        EmitDirtyMeshSlots(state);
    }
}

void FinalizeIncrementalMeshUpdate(DataOrientedRoamState& state)
{
    DataOrientedRoamIncrementalMesh& mesh = state.IncrementalMesh;
    // 槽位排序后只合并下标连续的区间，不跨越空洞扩大上传范围
    // 首次初始化时仍将整个网格作为一个连续区间上传
    mesh.UpdateRanges.clear();
    if (mesh.RequiresFullUpload && !mesh.SlotOwners.empty())
    {
        mesh.UpdateRanges.push_back(DataOrientedRoamMeshUpdateRange{0U, mesh.SlotOwners.size()});
    }
    else
    {
        for (DataOrientedRoamPosition slot : mesh.DirtySlots)
        {
            if (mesh.UpdateRanges.empty() ||
                mesh.UpdateRanges.back().FirstTriangle + mesh.UpdateRanges.back().TriangleCount != slot)
            {
                mesh.UpdateRanges.push_back(DataOrientedRoamMeshUpdateRange{slot, 1U});
            }
            else
            {
                ++mesh.UpdateRanges.back().TriangleCount;
            }
        }
    }

    const std::size_t updatedTriangleCount = mesh.RequiresFullUpload
        ? mesh.SlotOwners.size()
        : mesh.DirtySlots.size();
    state.Stats.MeshFullRebuildCount = mesh.RequiresFullUpload ? 1U : 0U;
    state.Stats.MeshUpdatedTriangleCount = updatedTriangleCount;
    state.Stats.MeshReusedTriangleCount = mesh.SlotOwners.size() - updatedTriangleCount;
    state.Stats.MeshDirtyRangeCount = mesh.UpdateRanges.size();

    const auto appendTransition = [&state, &mesh](DataOrientedRoamNodeIndex node) {
        if (state.IsValidNode(node) &&
            (state.Nodes.ActivatedBuildIdAt(node) == state.BuildSequence ||
             state.Nodes.MergeBuildIdAt(node) == state.BuildSequence))
        {
            mesh.DebugTransitionLeaves.push_back(node);
        }
    };
    mesh.DebugTransitionLeaves.clear();
    if (mesh.RequiresFullUpload)
    {
        for (DataOrientedRoamNodeIndex node : mesh.SlotOwners)
        {
            appendTransition(node);
        }
    }
    else
    {
        for (DataOrientedRoamPosition slot : mesh.DirtySlots)
        {
            appendTransition(mesh.SlotOwners[slot]);
        }
    }
}
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
