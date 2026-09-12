#pragma once

#include "algorithms/data_oriented_roam/materialization/NativePlanningQueues.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamQueues.h"

#include <map>
#include <set>

namespace ParallelRoam::Algorithms::DataOrientedRoam::Materialization
{
/// <summary>
/// 在私有视图中推进一次请求的强制前置与候选演化，不选择全局根或写生产状态
/// Trace 只控制诊断事件，逻辑变更和普通工作计数在两种实例中一致
/// </summary>
template<bool Trace>
class NativeRefinementSimulation
{
public:
    NativeRefinementSimulation(NativePlanningView& view, NativePlanningQueues& queues, DecisionTraceCursor* trace)
        : _view(view), _queues(queues), _trace(trace) {}
    // 控制器独占模拟器，不能复制局部计数后继续改写同一个借用工作区
    NativeRefinementSimulation(const NativeRefinementSimulation&) = delete;
    NativeRefinementSimulation& operator=(const NativeRefinementSimulation&) = delete;
    bool Split(DataOrientedRoamNodeIndex node, DataOrientedRoamNodeIndex forcedFrom = InvalidDataOrientedRoamNodeIndex);
    bool Merge(DataOrientedRoamNodeIndex node);
    void Block(DataOrientedRoamNodeIndex node);
    void RemoveFailedMerge(DataOrientedRoamNodeIndex node);
    [[nodiscard]] bool WantsSplit(DataOrientedRoamNodeIndex node, float score) const;
    [[nodiscard]] bool MissingFailedMerge(std::uint64_t path);
    [[nodiscard]] const std::set<std::uint64_t>& FailedMergeRemovals() const { return _failedMergeRemovals; }
    [[nodiscard]] std::vector<NativeScoreEvaluation> Evaluations() const;
    NativePlanningWork Work;

private:
    using Node = DataOrientedRoamNodeIndex;
    using Field = NativePlanningField;
    [[nodiscard]] bool Leaf(Node node) const;
    [[nodiscard]] bool Active(Node node, NativePlanningActivity activity) const;
    [[nodiscard]] Node Relation(Node node, Field field) const { return _view.Relation(node, field); }
    [[nodiscard]] float Score(Node node);
    [[nodiscard]] float SplitScore(Node node);
    [[nodiscard]] Node MergeRepresentative(Node node);
    [[nodiscard]] bool CanMerge(Node node);
    void AppendNeighborhood(Node node, DataOrientedRoamNeighborhood& nodes);
    void Invalidate(const DataOrientedRoamNeighborhood& nodes);
    void Refresh(const DataOrientedRoamNeighborhood& nodes);
    void Replace(Node neighbor, Node oldNode, Node newNode);
    void CommitSplit(Node node, Node base, bool forced);
    void CommitMerge(Node node);
    void ReleaseBudget();

    NativePlanningView& _view;
    NativePlanningQueues& _queues;
    DecisionTraceCursor* _trace;
    std::map<Node, float> _scores;
    std::set<std::uint64_t> _failedMergeRemovals;
};
extern template class NativeRefinementSimulation<false>;
extern template class NativeRefinementSimulation<true>;
}
