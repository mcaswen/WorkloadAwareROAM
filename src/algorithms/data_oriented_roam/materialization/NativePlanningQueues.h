#pragma once

#include "algorithms/data_oriented_roam/materialization/NativePlanningView.h"
#include "algorithms/data_oriented_roam/materialization/NativePlanningHeapSlots.h"
#include "algorithms/data_oriented_roam/materialization/NativePlanningMembership.h"

#include <array>
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
    /// <summary>
    /// 封闭维护区间内先撤销逻辑代表，刷新结束再删除没有恢复的旧 heap 条目
    /// 区间内禁止查询合并队首或外部审计，不得跨根、重入或遗漏结束调用
    /// </summary>
    void BeginMergeMaintenance();
    void InvalidateMerge(DataOrientedRoamNodeIndex eitherSide);
    void FinishMergeMaintenance();
    [[nodiscard]] DataOrientedRoamNodeIndex MergeRepresentative(DataOrientedRoamNodeIndex node) const;
    [[nodiscard]] DataOrientedRoamNodeIndex MergePartner(DataOrientedRoamNodeIndex representative) const;

    // 下列全堆核查只用于外部诊断，不属于根选择或正常规划出口
    [[nodiscard]] bool Validate(NativePlanningQueueKind kind) const;
    [[nodiscard]] NativePlanningQueueMetrics Metrics(NativePlanningQueueKind kind) const;
    [[nodiscard]] std::size_t MergeRelationRecords() const noexcept
    { return _members.Records(NativePlanningMembership::Field::Representative) + _members.Records(NativePlanningMembership::Field::Partner); }
    [[nodiscard]] std::array<NativePlanningStorageMetrics, 6> StorageMetrics() const;

private:
    /// <summary>
    /// 截断隐藏来源尾部，曾写记录仍保留以计量真实覆盖和维护成本
    /// </summary>
    struct Heap
    {
        std::size_t Length{0}, SourceVisible{0};
        NativePlanningHeapSlots Cells;
        mutable NativePlanningQueueMetrics Counters;
        mutable std::set<std::size_t> SourceSlotsRead;
    };
    [[nodiscard]] bool Precedes(NativePlanningQueueKind kind,
        NativePlanningQueueEntry left, NativePlanningQueueEntry right) const;
    void Restore(NativePlanningQueueKind kind, std::size_t index, NativePlanningQueueEntry entry);
    void Upsert(NativePlanningQueueKind kind, DataOrientedRoamNodeIndex node, float score);
    bool Remove(NativePlanningQueueKind kind, DataOrientedRoamNodeIndex node);

    NativePlanningView& _view;
    bool _collectReadCoverage;
    std::array<Heap, 2> _heaps;
    NativePlanningMembership _members;
    std::vector<DataOrientedRoamNodeIndex> _deferredMerge;
    bool _mergeMaintenance{false};
};
}
