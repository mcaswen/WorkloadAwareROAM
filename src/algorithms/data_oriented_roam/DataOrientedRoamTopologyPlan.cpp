#include "algorithms/data_oriented_roam/DataOrientedRoamTopologyPlan.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamCandidateMarking.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamQueues.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamScoring.h"

#include <algorithm>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
namespace
{
DataOrientedRoamChunkId InteriorChunkIdForNode(const DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    if (!state.IsValidNode(node))
    {
        // 无效节点不能分配给任何分块
        return InvalidDataOrientedRoamChunkId;
    }

    // 节点创建时已经记录整个三角形所在的单一分块编号
    return state.Nodes.InteriorChunkIdAt(node);
}

bool NodeBelongsToChunk(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node,
    DataOrientedRoamChunkId chunkId)
{
    if (!state.IsValidNode(node))
    {
        // 无效邻居不会被写入，因此不影响不同分块同时修改拓扑
        return true;
    }

    return InteriorChunkIdForNode(state, node) == chunkId;
}

bool HasReusableChildren(const DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    // 当前并行细分不扩展节点池，只处理已有可复用子节点的候选
    return state.IsValidNode(node) &&
           state.IsValidNode(state.Nodes.LeftChildAt(node)) &&
           state.IsValidNode(state.Nodes.RightChildAt(node));
}

bool SplitWouldNeedForcedNeighbor(const DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    if (!state.Settings.EnableLocalConstraints)
    {
        // 关闭局部约束时不会递归细分底边邻居
        return false;
    }

    const DataOrientedRoamNodeIndex baseNeighbor = state.Nodes.BaseNeighborAt(node);
    if (!state.IsValidNode(baseNeighbor))
    {
        // 地形外边界没有对侧三角形，可以独立细分
        return false;
    }

    if (state.IsLeaf(baseNeighbor))
    {
        // 底边邻居仍为叶节点时需要连锁细分，必须交给主线程处理
        return true;
    }

    // 尚未形成底边互指的邻接链需要递归修复，也必须交给主线程处理
    return state.Nodes.BaseNeighborAt(baseNeighbor) != node;
}

}

DataOrientedRoamChunkId SafeInteriorSplitChunkId(const DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    if (!state.IsValidNode(node) ||
        !state.IsLeaf(node) ||
        state.Nodes.DepthAt(node) >= state.Settings.MaxDepth ||
        !HasReusableChildren(state, node) ||
        SplitWouldNeedForcedNeighbor(state, node))
    {
        // 任一安全条件不满足时都保留在原有串行细分队列中
        return InvalidDataOrientedRoamChunkId;
    }

    const DataOrientedRoamChunkId chunkId = InteriorChunkIdForNode(state, node);
    if (chunkId == InvalidDataOrientedRoamChunkId)
    {
        // 跨分块三角形可能与其他任务修改同一邻居，因此必须交给主线程处理
        return InvalidDataOrientedRoamChunkId;
    }

    const DataOrientedRoamNodeIndex leftChild = state.Nodes.LeftChildAt(node);
    const DataOrientedRoamNodeIndex rightChild = state.Nodes.RightChildAt(node);
    const DataOrientedRoamNodeIndex baseNeighbor = state.Nodes.BaseNeighborAt(node);
    // 细分会修改父节点、两个子节点以及左右外侧邻居
    if (!NodeBelongsToChunk(state, leftChild, chunkId) ||
        !NodeBelongsToChunk(state, rightChild, chunkId) ||
        !NodeBelongsToChunk(state, state.Nodes.LeftNeighborAt(node), chunkId) ||
        !NodeBelongsToChunk(state, state.Nodes.RightNeighborAt(node), chunkId))
    {
        return InvalidDataOrientedRoamChunkId;
    }

    if (state.IsValidNode(baseNeighbor) && !state.IsLeaf(baseNeighbor))
    {
        // 菱形对侧已细分时还会修改对侧子节点的邻接关系
        if (!NodeBelongsToChunk(state, baseNeighbor, chunkId) ||
            !NodeBelongsToChunk(state, state.Nodes.LeftChildAt(baseNeighbor), chunkId) ||
            !NodeBelongsToChunk(state, state.Nodes.RightChildAt(baseNeighbor), chunkId))
        {
            return InvalidDataOrientedRoamChunkId;
        }
    }

    return chunkId;
}

namespace
{
bool HasMergeReadyChildren(const DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    if (!state.IsValidNode(node) || state.IsLeaf(node))
    {
        // 合并只作用于当前活动内部节点
        return false;
    }

    const DataOrientedRoamNodeIndex leftChild = state.Nodes.LeftChildAt(node);
    const DataOrientedRoamNodeIndex rightChild = state.Nodes.RightChildAt(node);
    // 按分块归类时只检查拓扑形状，真正修改前还会重新确认屏幕误差
    return state.IsValidNode(leftChild) &&
           state.IsValidNode(rightChild) &&
           state.IsLeaf(leftChild) &&
           state.IsLeaf(rightChild);
}

bool HasMergeReadyDiamond(const DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    const DataOrientedRoamNodeIndex baseNeighbor = state.Nodes.BaseNeighborAt(node);
    if (!state.IsValidNode(baseNeighbor) || state.IsLeaf(baseNeighbor))
    {
        // 没有对侧内部菱形时可以单独合并当前父节点
        return true;
    }

    // 对侧菱形也必须由两个可合并叶节点组成
    return state.Nodes.BaseNeighborAt(baseNeighbor) == node && HasMergeReadyChildren(state, baseNeighbor);
}

}

DataOrientedRoamChunkId SafeInteriorMergeChunkId(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node,
    bool validateMergeScore)
{
    if (!HasMergeReadyChildren(state, node) || !HasMergeReadyDiamond(state, node))
    {
        // 合并所需的拓扑条件不满足时，不加入任何待处理队列
        return InvalidDataOrientedRoamChunkId;
    }

    if (validateMergeScore && !CanMergeNode(state, node))
    {
        // 线程真正修改拓扑前再次检查整个菱形的合并分数
        return InvalidDataOrientedRoamChunkId;
    }

    const DataOrientedRoamChunkId chunkId = InteriorChunkIdForNode(state, node);
    if (chunkId == InvalidDataOrientedRoamChunkId)
    {
        // 父节点自身跨越分块时不能并行合并
        return InvalidDataOrientedRoamChunkId;
    }

    const DataOrientedRoamNodeIndex leftChild = state.Nodes.LeftChildAt(node);
    const DataOrientedRoamNodeIndex rightChild = state.Nodes.RightChildAt(node);
    // MergeSingleNode 会修改两个子节点底边邻居指向的外侧节点
    if (!NodeBelongsToChunk(state, leftChild, chunkId) ||
        !NodeBelongsToChunk(state, rightChild, chunkId) ||
        !NodeBelongsToChunk(state, state.Nodes.BaseNeighborAt(leftChild), chunkId) ||
        !NodeBelongsToChunk(state, state.Nodes.BaseNeighborAt(rightChild), chunkId))
    {
        return InvalidDataOrientedRoamChunkId;
    }

    const DataOrientedRoamNodeIndex baseNeighbor = state.Nodes.BaseNeighborAt(node);
    if (state.IsValidNode(baseNeighbor) && !state.IsLeaf(baseNeighbor))
    {
        // 合并完整菱形时还会同时修改底边邻居一侧
        if (!NodeBelongsToChunk(state, baseNeighbor, chunkId) ||
            !NodeBelongsToChunk(state, state.Nodes.LeftChildAt(baseNeighbor), chunkId) ||
            !NodeBelongsToChunk(state, state.Nodes.RightChildAt(baseNeighbor), chunkId) ||
            !NodeBelongsToChunk(
                state,
                state.Nodes.BaseNeighborAt(state.Nodes.LeftChildAt(baseNeighbor)),
                chunkId) ||
            !NodeBelongsToChunk(
                state,
                state.Nodes.BaseNeighborAt(state.Nodes.RightChildAt(baseNeighbor)),
                chunkId))
        {
            return InvalidDataOrientedRoamChunkId;
        }
    }

    return chunkId;
}

DataOrientedRoamSplitPlan PlanDataOrientedRoamSplitTopology(
    const DataOrientedRoamState& state,
    const std::vector<DataOrientedRoamSplitCandidate>& candidates)
{
    // 恢复串行队列的分数与路径顺序，快照遍历序号不参与优先级决策
    DataOrientedRoamSplitPlan plan;
    plan.Candidates = candidates;
    auto& sortedCandidates = plan.Candidates;
    std::sort(
        sortedCandidates.begin(),
        sortedCandidates.end(),
        [&state](const DataOrientedRoamSplitCandidate& left, const DataOrientedRoamSplitCandidate& right) {
            return SplitPriorityPrecedes(state, left.Node, left.Score, right.Node, right.Score);
        });

    const std::size_t earlyCommitBudget = state.RemainingSerialSplitBudget;
    // 空前缀无需查找停止项；串行优先合并和不安全首项都禁止提前细分
    // 仍保留后面的完整分类，不能因省去扫描而改变工作量特征
    bool prefixOpen = !sortedCandidates.empty() && earlyCommitBudget > 0U &&
        !(state.IsValidNode(TopPersistentMergeQueueNode(state)) &&
            TopPersistentMergeQueueScore(state) < state.Settings.MergeThreshold) &&
        SafeInteriorSplitChunkId(state, sortedCandidates.front().Node) != InvalidDataOrientedRoamChunkId;
    const DataOrientedRoamSplitQueueEntry* serialStop = nullptr;
    if (prefixOpen)
    {
        // 快照会过滤无需细分的条目，但串行遇到其中的队首就会结束
        // 可能形成非空前缀时，仍需查找真实停止边界，避免低分迟滞项越过它
        for (const auto& entry : state.SplitQueue)
        {
            if (!ShouldSplitWithScore(state, entry.Node, entry.Score) &&
                (serialStop == nullptr || SplitPriorityPrecedes(
                    state, entry.Node, entry.Score, serialStop->Node, serialStop->Score)))
            {
                serialStop = &entry;
            }
        }
    }

    plan.Chunks.resize(DataOrientedRoamTopologyChunkGridSize * DataOrientedRoamTopologyChunkGridSize);
    auto& chunks = plan.Chunks;
    std::size_t scheduledInteriorCount = 0U;
    for (const DataOrientedRoamSplitCandidate& candidate : sortedCandidates)
    {
        const DataOrientedRoamChunkId chunkId = SafeInteriorSplitChunkId(state, candidate.Node);
        if (chunkId == InvalidDataOrientedRoamChunkId)
        {
            // 首个不安全项关闭整个前缀，后续项仍需完成分类
            ++plan.BoundaryCandidateCount;
            prefixOpen = false;
            continue;
        }

        ++plan.InteriorCandidateCount;
        if (scheduledInteriorCount >= earlyCommitBudget ||
            (serialStop != nullptr && !SplitPriorityPrecedes(
                state, candidate.Node, candidate.Score, serialStop->Node, serialStop->Score)))
        {
            prefixOpen = false;
        }
        if (!prefixOpen)
        {
            // 安全后缀仍留在长期队列，不能重新开启提前提交
            continue;
        }

        // 只有连续前缀进入分块，同一分块内继续保留全局顺序
        chunks[chunkId].push_back(candidate);
        ++scheduledInteriorCount;
    }

    for (const auto& chunk : chunks)
    {
        plan.NonEmptyChunkCount += chunk.empty() ? 0U : 1U;
        plan.ScheduledCandidateCount += chunk.size();
    }
    return plan;
}

DataOrientedRoamMergePlan PlanDataOrientedRoamMergeTopology(
    const DataOrientedRoamState& state,
    const std::vector<DataOrientedRoamMergeCandidate>& candidates)
{
    DataOrientedRoamMergePlan plan;
    plan.Candidates = candidates;
    std::sort(plan.Candidates.begin(), plan.Candidates.end(), [&state](const auto& left, const auto& right) {
        return left.Score == right.Score ? state.Nodes.PathIdAt(left.Node) < state.Nodes.PathIdAt(right.Node)
                                        : left.Score < right.Score;
    });
    plan.Chunks.resize(DataOrientedRoamTopologyChunkGridSize * DataOrientedRoamTopologyChunkGridSize);
    auto& chunks = plan.Chunks;

    // 合并不依赖细分队列，全部安全内部候选都可以先分配到各分块
    for (const DataOrientedRoamMergeCandidate& candidate : plan.Candidates)
    {
        // 合并候选已经按分数排序，分块内继续保留该顺序
        const DataOrientedRoamChunkId chunkId = SafeInteriorMergeChunkId(state, candidate.Node, false);
        if (chunkId == InvalidDataOrientedRoamChunkId)
        {
            // 跨分块菱形仍交给主线程顺序处理
            ++plan.BoundaryCandidateCount;
            continue;
        }

        // 同一分块内的候选由同一个线程按顺序处理
        chunks[chunkId].push_back(candidate);
        ++plan.InteriorCandidateCount;
    }

    for (const auto& chunk : chunks)
    {
        plan.NonEmptyChunkCount += chunk.empty() ? 0U : 1U;
        plan.ScheduledCandidateCount += chunk.size();
    }
    return plan;
}

DataOrientedRoamSplitPlan PlanDataOrientedRoamSplitTopology(const DataOrientedRoamState& state)
{
    std::vector<DataOrientedRoamSplitCandidate> candidates;
    SnapshotPersistentSplitQueueCandidates(state, candidates);
    return PlanDataOrientedRoamSplitTopology(state, candidates);
}

DataOrientedRoamMergePlan PlanDataOrientedRoamMergeTopology(const DataOrientedRoamState& state)
{
    std::vector<DataOrientedRoamMergeCandidate> candidates;
    SnapshotPersistentMergeQueueCandidates(state, state.Settings.MergeThreshold, candidates);
    return PlanDataOrientedRoamMergeTopology(state, candidates);
}
}
