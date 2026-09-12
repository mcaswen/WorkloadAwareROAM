#pragma once

#include "algorithms/data_oriented_roam/materialization/NativePlanningView.h"

#include <array>
#include <map>
#include <set>

namespace ParallelRoam::Algorithms::DataOrientedRoam::Materialization
{
/// <summary>
/// 对入口两堆维护私有槽位和反向位置覆盖，根控制器负责决定成员资格与分数
/// 普通接口不导出堆布局，诊断读取只在工作区销毁前使用
/// </summary>
class NativePlanningQueues
{
public:
    explicit NativePlanningQueues(NativePlanningView& view, bool collectReadCoverage = false);
    NativePlanningQueues(const NativePlanningQueues&) = delete;
    NativePlanningQueues& operator=(const NativePlanningQueues&) = delete;

    [[nodiscard]] std::size_t Size(NativePlanningQueueKind kind) const;
    [[nodiscard]] NativePlanningQueueEntry At(NativePlanningQueueKind kind, std::size_t index) const;
    [[nodiscard]] NativePlanningQueueEntry Top(NativePlanningQueueKind kind) const;
    [[nodiscard]] std::size_t Position(NativePlanningQueueKind kind, DataOrientedRoamNodeIndex node) const;
    void UpsertSplit(DataOrientedRoamNodeIndex node, float score);
    bool RemoveSplit(DataOrientedRoamNodeIndex node);

    /// <summary>
    /// 调用方提供已经复核的代表与伙伴；改组时先解除双方旧关联，再写入新条目
    /// </summary>
    void UpsertMerge(DataOrientedRoamNodeIndex representative, DataOrientedRoamNodeIndex partner, float score);
    bool RemoveMerge(DataOrientedRoamNodeIndex eitherSide);
    [[nodiscard]] DataOrientedRoamNodeIndex MergeRepresentative(DataOrientedRoamNodeIndex node) const;
    [[nodiscard]] DataOrientedRoamNodeIndex MergePartner(DataOrientedRoamNodeIndex representative) const;

    // 下列全堆核查只用于外部诊断，不属于根选择或正常规划出口
    [[nodiscard]] bool Validate(NativePlanningQueueKind kind) const;
    [[nodiscard]] NativePlanningQueueMetrics Metrics(NativePlanningQueueKind kind) const;
    [[nodiscard]] std::size_t MergeRelationRecords() const noexcept { return _representatives.size() + _partners.size(); }

private:
    /// <summary>
    /// 截断隐藏来源尾部，曾写记录仍保留以计量真实覆盖和维护成本
    /// </summary>
    struct Heap
    {
        std::size_t Length{0}, SourceVisible{0};
        std::map<std::size_t, NativePlanningQueueEntry> Cells;
        std::map<DataOrientedRoamNodeIndex, std::size_t> Reverse;
        mutable NativePlanningQueueMetrics Counters;
        mutable std::set<std::size_t> SourceSlotsRead;
    };
    [[nodiscard]] bool Precedes(NativePlanningQueueKind kind,
        NativePlanningQueueEntry left, NativePlanningQueueEntry right) const;
    void Write(NativePlanningQueueKind kind, std::size_t index, NativePlanningQueueEntry entry);
    void Swap(NativePlanningQueueKind kind, std::size_t left, std::size_t right);
    void Restore(NativePlanningQueueKind kind, std::size_t index);
    void Upsert(NativePlanningQueueKind kind, DataOrientedRoamNodeIndex node, float score);
    bool Remove(NativePlanningQueueKind kind, DataOrientedRoamNodeIndex node);

    NativePlanningView& _view;
    bool _collectReadCoverage;
    std::array<Heap, 2> _heaps;
    std::map<DataOrientedRoamNodeIndex, DataOrientedRoamNodeIndex> _representatives, _partners;
};
}
