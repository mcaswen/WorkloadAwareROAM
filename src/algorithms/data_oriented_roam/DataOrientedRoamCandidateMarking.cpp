#include "algorithms/data_oriented_roam/DataOrientedRoamCandidateMarking.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamScoring.h"

#include <algorithm>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
namespace
{
DataOrientedRoamMergeCandidateEvaluation EvaluateMergeCandidateImpl(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node,
    float maximumScore)
{
    if (!state.IsValidNode(node) || state.IsLeaf(node))
    {
        return {};
    }

    const DataOrientedRoamNodeIndex leftChild = state.Nodes.LeftChildAt(node);
    const DataOrientedRoamNodeIndex rightChild = state.Nodes.RightChildAt(node);
    if (!state.IsValidNode(leftChild) || !state.IsValidNode(rightChild) ||
        !state.IsLeaf(leftChild) || !state.IsLeaf(rightChild))
    {
        return {};
    }

    // 提交时校验只重新计算当前选中的 parent
    // 持久 Q_m 负责整帧评分和拓扑成员发现
    const float score = ComputeScreenErrorScore(state, node);
    if (score > maximumScore)
    {
        return {};
    }

    const DataOrientedRoamNodeIndex baseNeighbor = state.Nodes.BaseNeighborAt(node);
    if (!state.IsValidNode(baseNeighbor) || state.IsLeaf(baseNeighbor))
    {
        return DataOrientedRoamMergeCandidateEvaluation{true, score, score};
    }

    // 对侧 internal parent 必须组成互指 diamond，且提交拓扑事务时其 child 仍然都是 leaf
    const DataOrientedRoamNodeIndex baseLeftChild = state.Nodes.LeftChildAt(baseNeighbor);
    const DataOrientedRoamNodeIndex baseRightChild = state.Nodes.RightChildAt(baseNeighbor);
    if (state.Nodes.BaseNeighborAt(baseNeighbor) != node ||
        !state.IsValidNode(baseLeftChild) ||
        !state.IsValidNode(baseRightChild) ||
        !state.IsLeaf(baseLeftChild) ||
        !state.IsLeaf(baseRightChild))
    {
        return {};
    }

    const float baseNeighborScore = ComputeScreenErrorScore(state, baseNeighbor);
    if (baseNeighborScore > maximumScore)
    {
        return {};
    }

    // PairScore 表示完整 diamond 事务的误差损失，因此使用持久 Q_m 相同的 max(parent priority) 键
    return DataOrientedRoamMergeCandidateEvaluation{
        true,
        score,
        std::max(score, baseNeighborScore)};
}
} // namespace

DataOrientedRoamMergeCandidateEvaluation EvaluateMergeCandidate(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node,
    float maximumScore)
{
    return EvaluateMergeCandidateImpl(state, node, maximumScore);
}

bool CanMergeNode(const DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    return CanMergeNode(state, node, state.Settings.MergeThreshold);
}

bool CanMergeNode(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node,
    float maximumScore)
{
    return EvaluateMergeCandidate(state, node, maximumScore).Eligible;
}
} // namespace ParallelRoam::Algorithms::DataOrientedRoam
