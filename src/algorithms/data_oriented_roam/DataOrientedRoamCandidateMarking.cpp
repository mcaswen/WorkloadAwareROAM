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

    // 实际合并前只重新计算当前父节点，整帧分数刷新和成员查找由跨帧保留的 Q_m 负责
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

    // 对侧父节点必须与当前节点互为底边邻居，且两侧子节点在提交时仍全部是叶节点
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

    // PairScore 衡量合并整个菱形的画质损失，因此取两侧父节点分数的较大值
    return DataOrientedRoamMergeCandidateEvaluation{
        true,
        score,
        std::max(score, baseNeighborScore)};
}
} // 匿名命名空间

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
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
