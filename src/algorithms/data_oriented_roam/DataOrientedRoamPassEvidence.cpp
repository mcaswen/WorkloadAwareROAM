#include "algorithms/data_oriented_roam/DataOrientedRoamPassEvidence.h"

#include "algorithms/data_oriented_roam/DataOrientedRoamMeshPlan.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamQueues.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamValidation.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
namespace
{
std::uint64_t HashCurrentTopology(const DataOrientedRoamState& state)
{
    // 保留旧诊断的路径集合编码，合法节点分配顺序不影响结果
    std::vector<std::uint64_t> paths;
    paths.reserve(state.ActiveInternalNodes.size());
    for (DataOrientedRoamNodeIndex node = 0U; node < state.Nodes.size(); ++node)
        if (state.Nodes.IsSplitAt(node))
            paths.push_back(state.Nodes.PathIdAt(node));
    return HashTerrainLodPathIds(std::move(paths));
}

std::uint64_t HashActiveLeaves(const DataOrientedRoamState& state)
{
    std::vector<std::uint64_t> paths;
    paths.reserve(state.ActiveLeafNodes.size());
    for (const auto node : state.ActiveLeafNodes)
        paths.push_back(state.Nodes.PathIdAt(node));
    return HashTerrainLodPathIds(std::move(paths));
}

std::uint64_t HashQueueMembership(const DataOrientedRoamState& state)
{
    // 队列结果比较成员，堆顺序及反向位置另由不变量检查覆盖
    std::vector<std::uint64_t> splitPaths;
    std::vector<std::uint64_t> mergePaths;
    for (const auto& entry : state.SplitQueue)
        splitPaths.push_back(state.Nodes.PathIdAt(entry.Node));
    for (const auto& entry : state.MergeQueue)
        mergePaths.push_back(state.Nodes.PathIdAt(entry.Node));
    std::sort(splitPaths.begin(), splitPaths.end());
    std::sort(mergePaths.begin(), mergePaths.end());
    std::uint64_t hash = TerrainLodHashOffset;
    AppendTerrainLodHash(hash, splitPaths.size());
    for (const auto path : splitPaths) AppendTerrainLodHash(hash, path);
    AppendTerrainLodHash(hash, mergePaths.size());
    for (const auto path : mergePaths) AppendTerrainLodHash(hash, path);
    return hash;
}

std::uint64_t HashMeshEdits(const DataOrientedRoamState& state)
{
    // 保留全部前序修改，规范化仅忽略独立事务之间的提交顺序
    std::vector<std::pair<std::uint8_t, std::uint64_t>> edits;
    for (const auto& edit : state.IncrementalMesh.Metadata.TopologyEdits)
        edits.emplace_back(static_cast<std::uint8_t>(edit.Type), state.Nodes.PathIdAt(edit.Node));
    std::sort(edits.begin(), edits.end());
    std::uint64_t hash = TerrainLodHashOffset;
    AppendTerrainLodHash(hash, edits.size());
    for (const auto& [type, path] : edits)
    {
        AppendTerrainLodHash(hash, type);
        AppendTerrainLodHash(hash, path);
    }
    return hash;
}

DataOrientedRoamPassEvidence CaptureScore(const DataOrientedRoamState& state, TerrainLodPassId passId)
{
    DataOrientedRoamPassEvidence evidence;
    evidence.ValidationPerformed = true;
    evidence.Correct = CountPersistentQueueInvariantViolations(state) == 0U;
    // 逐路径保留分数位模式，不能只比较队列长度或堆顶
    std::vector<std::pair<std::uint64_t, float>> entries;
    const auto append = [&](const auto& queue) {
        for (const auto& entry : queue)
        {
            if (!state.IsValidNode(entry.Node) || !std::isfinite(entry.Score))
            {
                evidence.Correct = false;
                continue;
            }
            entries.emplace_back(state.Nodes.PathIdAt(entry.Node), entry.Score);
        }
    };
    if (passId == TerrainLodPassId::MergeScore) append(state.MergeQueue);
    else append(state.SplitQueue);
    std::sort(entries.begin(), entries.end());
    evidence.ResultHash = TerrainLodHashOffset;
    for (const auto& [path, score] : entries)
    {
        AppendTerrainLodHash(evidence.ResultHash, path);
        AppendTerrainLodHash(evidence.ResultHash, score);
    }
    return evidence;
}

bool ValidateMeshEditSequence(const DataOrientedRoamState& state)
{
    // 只在独立元数据副本上验证修改顺序；不更新真实槽位，更不生成中间阶段的几何
    auto planned = state.IncrementalMesh.Metadata;
    const bool initializationExpected = planned.NeedsInitialization;
    // 根帧允许首次初始化，已有槽位的增量序列却不能靠回退重建掩盖错误
    const bool reinitialized = ApplyDataOrientedRoamMeshPlan(
        {state.Nodes, state.ActiveLeafNodes, state.BuildSequence}, planned);
    if (reinitialized && !initializationExpected) return false;
    auto actual = planned.SlotOwners;
    auto expected = state.ActiveLeafNodes;
    std::sort(actual.begin(), actual.end());
    std::sort(expected.begin(), expected.end());
    if (actual != expected) return false;
    // 正向集合相同仍不够，反向槽位也必须指回最终对应叶节点
    for (std::size_t slot = 0U; slot < planned.SlotOwners.size(); ++slot)
    {
        const auto node = planned.SlotOwners[slot];
        if (node >= planned.NodeSlots.size() || planned.NodeSlots[node] != slot) return false;
    }
    return true;
}

DataOrientedRoamPassEvidence CaptureTopology(DataOrientedRoamState& state)
{
    // 此处不检查旧几何，网格尚未消费本帧的拓扑修改
    ValidateTopology(state);
    DataOrientedRoamPassEvidence evidence;
    evidence.ValidationPerformed = true;
    auto& topology = evidence.Topology;
    topology.TopologyHash = HashCurrentTopology(state);
    topology.ActiveLeafHash = HashActiveLeaves(state);
    topology.QueueMembershipHash = HashQueueMembership(state);
    topology.MeshEditHash = HashMeshEdits(state);
    topology.ActiveTriangleCount = state.ActiveLeafNodes.size();
    topology.BudgetViolationCount = state.ActiveLeafNodes.size() > state.Settings.TriangleBudget ? 1U : 0U;
    topology.QueueInvariantViolationCount = state.Stats.QueueInvariantViolationCount;
    topology.TjunctionCount = state.Stats.TjunctionCount;
    topology.InvalidNeighborCount = state.Stats.InvalidNeighborCount;
    topology.InvalidTopologyCount = state.Stats.InvalidTopologyCount;
    evidence.Correct = topology.BudgetViolationCount == 0U && topology.QueueInvariantViolationCount == 0U &&
        topology.TjunctionCount == 0U && topology.InvalidNeighborCount == 0U && topology.InvalidTopologyCount == 0U;
    // 集合哈希允许独立提交换序，但不能掩盖非交换修改顺序已经损坏
    evidence.Correct = evidence.Correct && ValidateMeshEditSequence(state);
    evidence.ResultHash = TerrainLodHashOffset;
    AppendTerrainLodHash(evidence.ResultHash, topology.TopologyHash);
    AppendTerrainLodHash(evidence.ResultHash, topology.ActiveLeafHash);
    AppendTerrainLodHash(evidence.ResultHash, topology.QueueMembershipHash);
    AppendTerrainLodHash(evidence.ResultHash, topology.MeshEditHash);
    AppendTerrainLodHash(evidence.ResultHash, topology.ActiveTriangleCount);
    return evidence;
}

DataOrientedRoamPassEvidence CaptureMesh(DataOrientedRoamState& state)
{
    ValidateIncrementalMesh(state);
    DataOrientedRoamPassEvidence evidence;
    evidence.ValidationPerformed = true;
    evidence.Correct = state.Stats.InvalidTopologyCount == 0U;
    const auto& mesh = state.IncrementalMesh.Data;
    // 结构检查通过后再读取规范化几何，错误长度不能进入散列器
    if (!evidence.Correct) return evidence;
    for (const auto& vertex : mesh.Vertices)
    {
        for (int axis = 0; axis < 3; ++axis)
            evidence.Correct = evidence.Correct && std::isfinite(vertex.Position[axis]) &&
                std::isfinite(vertex.Normal[axis]) && std::isfinite(vertex.DebugColor[axis]);
        evidence.Correct = evidence.Correct && std::isfinite(vertex.TexCoord.x) &&
            std::isfinite(vertex.TexCoord.y) && std::isfinite(vertex.Height) && std::isfinite(vertex.DebugHighlight);
    }
    for (std::size_t index = 0U; index < mesh.Indices.size(); index += 3U)
        evidence.Correct = evidence.Correct && mesh.Indices[index] != mesh.Indices[index + 1U] &&
            mesh.Indices[index] != mesh.Indices[index + 2U] && mesh.Indices[index + 1U] != mesh.Indices[index + 2U];
    // 槽位映射可以不同，全部有效属性、索引和绘制规模仍须一致
    std::vector<std::uint64_t> paths;
    paths.reserve(state.IncrementalMesh.Metadata.SlotOwners.size());
    for (const auto node : state.IncrementalMesh.Metadata.SlotOwners)
        paths.push_back(state.Nodes.PathIdAt(node));
    evidence.ResultHash = HashTerrainLodNormalizedMesh(mesh, paths);
    // 公共网格哈希刻意忽略调试属性，本协议还必须核对这些有效顶点字段
    std::vector<std::pair<std::uint64_t, std::size_t>> orderedSlots;
    orderedSlots.reserve(paths.size());
    for (std::size_t slot = 0U; slot < paths.size(); ++slot)
        orderedSlots.emplace_back(paths[slot], slot);
    std::sort(orderedSlots.begin(), orderedSlots.end());
    for (const auto& [path, slot] : orderedSlots)
    {
        AppendTerrainLodHash(evidence.ResultHash, path);
        for (std::size_t offset = 0U; offset < 3U; ++offset)
        {
            const auto& vertex = mesh.Vertices[slot * 3U + offset];
            AppendTerrainLodHash(evidence.ResultHash, vertex.DebugColor.x);
            AppendTerrainLodHash(evidence.ResultHash, vertex.DebugColor.y);
            AppendTerrainLodHash(evidence.ResultHash, vertex.DebugColor.z);
            AppendTerrainLodHash(evidence.ResultHash, vertex.DebugHighlight);
        }
    }
    return evidence;
}
}

DataOrientedRoamPassEvidence CaptureDataOrientedRoamPassEvidence(
    DataOrientedRoamState& state, TerrainLodPassId passId)
{
    switch (passId)
    {
    case TerrainLodPassId::MergeScore:
    case TerrainLodPassId::SplitScore: return CaptureScore(state, passId);
    case TerrainLodPassId::MergeTopology:
    case TerrainLodPassId::SplitTopology: return CaptureTopology(state);
    case TerrainLodPassId::MeshEmit: return CaptureMesh(state);
    default: throw std::invalid_argument{"Expected a CPU ROAM evidence pass"};
    }
}
}
