#include "algorithms/data_oriented_roam/DataOrientedRoamMeshPlan.h"
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
/// 按待写槽位数和并行下限确定任务数量；两种串行策略都请求单线程，空工作返回 0
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
/// 将指定节点的三角形写入既定槽位，顶点和索引不会追加到数组末尾
/// 调用方为各任务分配不同槽位，因此线程完成顺序不影响网格排列
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

void EmitDirtySlotRange(DataOrientedRoamState& state, std::size_t begin, std::size_t end)
{
    DataOrientedRoamIncrementalMesh& mesh = state.IncrementalMesh;
    for (std::size_t index = begin; index < end; ++index)
    {
        const std::size_t slot = mesh.Metadata.DirtySlots[index];
        if (slot >= mesh.Metadata.SlotOwners.size())
        {
            continue;
        }

        const DataOrientedRoamNodeIndex node = mesh.Metadata.SlotOwners[slot];
        if (state.IsLeaf(node))
        {
            WriteDomainTriangle(state, node, mesh.Data, slot);
        }
    }
}

void EmitDirtyMeshSlots(DataOrientedRoamState& state)
{
    // 并行下限按实际待写脏槽位数判断，活动网格规模不能替代本次工作量
    const std::size_t dirtyCount = state.IncrementalMesh.Metadata.DirtySlots.size();
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
/// 按现有槽位顺序串行重写全部三角形，并标记整份网格需要上传
/// 复用当前拓扑和槽位映射，脏槽位集合为空时也执行全量写入
/// </summary>
void EmitFullMeshSerial(DataOrientedRoamState& state)
{
    DataOrientedRoamIncrementalMesh& mesh = state.IncrementalMesh;
    state.Stats.EmitWorkerCount = mesh.Metadata.SlotOwners.empty() ? 0U : 1U;
    for (std::size_t slot = 0U; slot < mesh.Metadata.SlotOwners.size(); ++slot)
    {
        const DataOrientedRoamNodeIndex node = mesh.Metadata.SlotOwners[slot];
        if (state.IsLeaf(node))
        {
            WriteDomainTriangle(state, node, mesh.Data, slot);
        }
    }
    mesh.Metadata.RequiresFullUpload = true;
}
} // 匿名命名空间

void BeginIncrementalMeshUpdate(
    DataOrientedRoamState& state,
    bool resetTopology)
{
    BeginDataOrientedRoamMeshPlan(state.IncrementalMesh.Metadata, resetTopology);
}

void ResetIncrementalMeshStorage(DataOrientedRoamState& state)
{
    const std::uint64_t generation = state.IncrementalMesh.Metadata.Generation;
    state.IncrementalMesh = {};
    state.IncrementalMesh.Metadata.Generation = generation;
}

void RecordMeshSplit(DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    if (state.IncrementalMesh.Metadata.TracksTopologyEdits)
    {
        state.IncrementalMesh.Metadata.TopologyEdits.push_back(
            DataOrientedRoamMeshTopologyEdit{DataOrientedRoamMeshTopologyEditType::Split, node});
    }
}

void RecordMeshMerge(DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    if (state.IncrementalMesh.Metadata.TracksTopologyEdits)
    {
        state.IncrementalMesh.Metadata.TopologyEdits.push_back(
            DataOrientedRoamMeshTopologyEdit{DataOrientedRoamMeshTopologyEditType::Merge, node});
    }
}

void ApplyIncrementalMeshUpdates(DataOrientedRoamState& state)
{
    DataOrientedRoamIncrementalMesh& mesh = state.IncrementalMesh;
    const DataOrientedRoamMeshPlanInput input{state.Nodes, state.ActiveLeafNodes, state.BuildSequence};
    const bool reinitialized = ApplyDataOrientedRoamMeshPlan(input, mesh.Metadata);
    // 几何容量仍按预算预留且正常增量帧只调整最终有效长度
    if (reinitialized)
    {
        mesh.Data = {};
        mesh.Data.GridWidth = state.HeightMap != nullptr ? state.HeightMap->Width() : 0;
        mesh.Data.GridHeight = state.HeightMap != nullptr ? state.HeightMap->Height() : 0;
        mesh.Data.TerrainSize = state.TerrainSize;
        mesh.Data.HeightScale = state.HeightScale;
        mesh.Data.Vertices.reserve(state.Settings.TriangleBudget * VerticesPerTriangle);
        mesh.Data.Indices.reserve(state.Settings.TriangleBudget * VerticesPerTriangle);
    }
    const std::size_t elementCount = mesh.Metadata.SlotOwners.size() * VerticesPerTriangle;
    mesh.Data.Vertices.resize(elementCount);
    mesh.Data.Indices.resize(elementCount);
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
    FinalizeDataOrientedRoamMeshPlan(
        {state.Nodes, state.ActiveLeafNodes, state.BuildSequence}, mesh.Metadata);
    const std::size_t updatedTriangleCount = mesh.Metadata.RequiresFullUpload
        ? mesh.Metadata.SlotOwners.size()
        : mesh.Metadata.DirtySlots.size();
    state.Stats.MeshFullRebuildCount = mesh.Metadata.RequiresFullUpload ? 1U : 0U;
    state.Stats.MeshUpdatedTriangleCount = updatedTriangleCount;
    state.Stats.MeshReusedTriangleCount = mesh.Metadata.SlotOwners.size() - updatedTriangleCount;
    state.Stats.MeshDirtyRangeCount = mesh.Metadata.UpdateRanges.size();
}
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
