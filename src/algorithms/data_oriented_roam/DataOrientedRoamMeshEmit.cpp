#include "algorithms/data_oriented_roam/DataOrientedRoamMeshEmit.h"

#include "algorithms/RoamGeometry.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamParallel.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamScoring.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <thread>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
namespace
{
constexpr std::size_t VerticesPerTriangle = 3U;
constexpr std::size_t MaxAutoEmitWorkerCount = 8U;
constexpr std::size_t MinParallelEmitTriangleCount = 256U;

std::size_t ResolveEmitWorkerCount(const DataOrientedRoamState& state, std::size_t triangleCount)
{
    if (triangleCount == 0U)
    {
        return 0U;
    }

    if (state.Settings.ErrorEvaluationWorkerCount == 1U ||
        triangleCount < MinParallelEmitTriangleCount)
    {
        return 1U;
    }

    std::size_t requestedWorkerCount = state.Settings.ErrorEvaluationWorkerCount;
    if (requestedWorkerCount == 0U)
    {
        const unsigned int hardwareWorkerCount = std::thread::hardware_concurrency();
        requestedWorkerCount = hardwareWorkerCount == 0U
            ? 1U
            : static_cast<std::size_t>(hardwareWorkerCount);
        requestedWorkerCount = std::min(requestedWorkerCount, MaxAutoEmitWorkerCount);
    }

    return std::clamp(requestedWorkerCount, std::size_t{1U}, triangleCount);
}

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

    // generation 去重允许同一 slot 在一次 replay 中被连续 split/merge 覆盖，
    // 最终只为拓扑稳定后的 owner 生成和上传一次数据。
    if (mesh.SlotDirtyGenerations[slot] != mesh.Generation)
    {
        mesh.SlotDirtyGenerations[slot] = mesh.Generation;
        mesh.DirtySlots.push_back(static_cast<DataOrientedRoamPosition>(slot));
    }
}

void NormalizeDirtyMeshSlots(DataOrientedRoamIncrementalMesh& mesh)
{
    // topology edit 顺序不承载输出顺序；提前按 slot 排序既清理已被尾部
    // shrink 删除的槽位，也让后续顶点写入和 range 生成共享同一连续访问序列。
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
    // 初始化直接采用最终 active cut；首帧无需重放从 root 到当前深度的历史 split。
    // 后续 Build 才依赖稳定 slot 继承增量更新。
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

    // 追加只增长稠密尾部，不改变已有 leaf 的 slot。
    // renderer 容量足够时只需上传这个新增范围。
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
    // move-last 维持单一连续 draw range；这里不复制旧顶点，
    // 被移动 owner 的目标 slot 已标脏，稍后的批量 emit 会直接重建正确数据。
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

    // 与 Classic 相同，left child 继承 parent slot，right child 追加尾部。
    // 差异仅是 DOD 通过 NodeSlots 数组维护反向关系。
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

    // retained child 不能位于末槽，否则删除另一 child 时可能把 retained
    // move 到空洞并覆盖刚建立的 parent owner。
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
    // 无论 dirty 比例高低都只消费 DirtySlots；D1 不在这里选择完整 emit。
    // 较大的 dirty 批次只沿用 DOD 已有 worker 分段能力。
    const std::size_t dirtyCount = state.IncrementalMesh.DirtySlots.size();
    state.Stats.EmitWorkerCount = ResolveEmitWorkerCount(state, dirtyCount);
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
} // namespace

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
        // generation 0 保留给从未发布过的 packet。
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
        // Rebuilt 颜色只维持一个 Build；仍是最终 leaf 的旧成员需要刷新一次。
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

        // edit 保持 topology 提交顺序，因此同一 Build 的级联 split/merge
        // 可以逐步把上一代 slot cut 变换为最终 active cut。
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
            // edit 契约失配时回到当前 active cut，保证发布的 Mesh 不携带旧拓扑。
            InitializeIncrementalMesh(state);
        }
    }

    mesh.TopologyEdits.clear();
    mesh.TracksTopologyEdits = false;
    NormalizeDirtyMeshSlots(mesh);
    EmitDirtyMeshSlots(state);
}

void FinalizeIncrementalMeshUpdate(DataOrientedRoamState& state)
{
    DataOrientedRoamIncrementalMesh& mesh = state.IncrementalMesh;
    // slot 排序后只合并物理连续区间；不跨空洞扩大上传范围。
    // 首次初始化仍发布单个完整范围。
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
} // namespace ParallelRoam::Algorithms::DataOrientedRoam
