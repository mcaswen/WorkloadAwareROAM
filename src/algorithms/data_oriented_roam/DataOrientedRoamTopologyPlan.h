#pragma once

#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
/// <summary>
/// 独立保存已排序候选、安全分块和规划期间的分类数量
/// 这些数量描述执行前的计划，不代表最终成功提交数量
/// </summary>
template<class Candidate>
struct DataOrientedRoamTopologyPlan
{
    std::vector<Candidate> Candidates;
    std::vector<std::vector<Candidate>> Chunks;
    std::size_t InteriorCandidateCount{0U};
    std::size_t BoundaryCandidateCount{0U};
    std::size_t ScheduledCandidateCount{0U};
    std::size_t NonEmptyChunkCount{0U};
};

using DataOrientedRoamSplitPlan = DataOrientedRoamTopologyPlan<DataOrientedRoamSplitCandidate>;
using DataOrientedRoamMergePlan = DataOrientedRoamTopologyPlan<DataOrientedRoamMergeCandidate>;

/// <summary>
/// 共享安全分类规则供规划与实际提交前重新检查
/// </summary>
[[nodiscard]] DataOrientedRoamChunkId SafeInteriorSplitChunkId(
    const DataOrientedRoamState& state, DataOrientedRoamNodeIndex node);
[[nodiscard]] DataOrientedRoamChunkId SafeInteriorMergeChunkId(
    const DataOrientedRoamState& state, DataOrientedRoamNodeIndex node, bool validateMergeScore);

/// <summary>
/// 对已评分候选排序，按安全分类和当前预算生成分块计划
/// 只读取源状态，不消耗预算或累加生产统计
/// </summary>
[[nodiscard]] DataOrientedRoamSplitPlan PlanDataOrientedRoamSplitTopology(
    const DataOrientedRoamState& state, const std::vector<DataOrientedRoamSplitCandidate>& candidates);
[[nodiscard]] DataOrientedRoamMergePlan PlanDataOrientedRoamMergeTopology(
    const DataOrientedRoamState& state, const std::vector<DataOrientedRoamMergeCandidate>& candidates);

/// <summary>
/// 从真实长期队列生成只读快照以取得串行路径未记录的候选工作量
/// </summary>
[[nodiscard]] DataOrientedRoamSplitPlan PlanDataOrientedRoamSplitTopology(const DataOrientedRoamState& state);
[[nodiscard]] DataOrientedRoamMergePlan PlanDataOrientedRoamMergeTopology(const DataOrientedRoamState& state);
}
