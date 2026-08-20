#pragma once

#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
/// <summary>
/// merge 候选标记接口
/// 在每帧 queue refresh 或 merge 前读取 state；只返回候选评估，不持有节点和队列所有权
/// </summary>
[[nodiscard]] DataOrientedRoamMergeCandidateEvaluation EvaluateMergeCandidate(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node,
    float maximumScore);

// CanMergeNode 只检查 diamond 的当前拓扑条件；真正修改由 topology pass 完成
[[nodiscard]] bool CanMergeNode(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node);
[[nodiscard]] bool CanMergeNode(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node,
    float maximumScore);
} // namespace ParallelRoam::Algorithms::DataOrientedRoam
