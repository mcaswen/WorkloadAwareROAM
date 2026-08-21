#pragma once

#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
/// <summary>
/// 评估节点是否能作为合并候选，并返回整个菱形的合并分数
/// 函数只读取当前状态，不持有节点或队列资源
/// </summary>
[[nodiscard]] DataOrientedRoamMergeCandidateEvaluation EvaluateMergeCandidate(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node,
    float maximumScore);

// CanMergeNode 只检查菱形结构和误差条件，实际合并由拓扑阶段完成
[[nodiscard]] bool CanMergeNode(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node);
[[nodiscard]] bool CanMergeNode(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node,
    float maximumScore);
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
