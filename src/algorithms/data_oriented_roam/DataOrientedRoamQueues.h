#pragma once

#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"

#include <array>
#include <cstddef>
#include <vector>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
/// <summary>
/// 保存一次串行拓扑修改可能影响的局部节点集合，并自动去重
/// 常见情况下使用对象内置的固定数组，只有受影响节点过多时才改用可扩容数组
/// </summary>
class DataOrientedRoamNeighborhood
{
public:
    // 四个起始节点各自最多扩展到 21 个节点，额外空间用于容纳少量重叠外的节点
    static constexpr std::size_t InlineCapacity = 96U;

    void clear() noexcept
    {
        _size = 0U;
        _usingOverflow = false;
        _overflow.clear();
    }

    void append_unique(DataOrientedRoamNodeIndex node)
    {
        if (contains(node))
        {
            return;
        }

        if (!_usingOverflow && _size < InlineCapacity)
        {
            _inlineNodes[_size++] = node;
            return;
        }

        EnsureOverflowStorage();
        _overflow.push_back(node);
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        return _usingOverflow ? _overflow.size() : _size;
    }

    [[nodiscard]] const DataOrientedRoamNodeIndex* begin() const noexcept
    {
        return data();
    }

    [[nodiscard]] const DataOrientedRoamNodeIndex* end() const noexcept
    {
        return data() + size();
    }

private:
    [[nodiscard]] bool contains(DataOrientedRoamNodeIndex node) const noexcept
    {
        for (DataOrientedRoamNodeIndex existing : *this)
        {
            if (existing == node)
            {
                return true;
            }
        }
        return false;
    }

    void EnsureOverflowStorage()
    {
        if (!_usingOverflow)
        {
            _overflow.reserve(InlineCapacity * 2U);
            _overflow.insert(_overflow.end(), _inlineNodes.begin(), _inlineNodes.begin() + _size);
            _usingOverflow = true;
        }
    }

    [[nodiscard]] const DataOrientedRoamNodeIndex* data() const noexcept
    {
        return _usingOverflow ? _overflow.data() : _inlineNodes.data();
    }

    std::array<DataOrientedRoamNodeIndex, InlineCapacity> _inlineNodes{};
    std::vector<DataOrientedRoamNodeIndex> _overflow;
    std::size_t _size{0U};
    bool _usingOverflow{false};
};

/// <summary>
/// 维护 DOD 跨帧保留的细分队列 Q_s 和合并队列 Q_m
/// 队列只在拓扑重置时完整初始化，普通更新只刷新分数和受影响的局部成员
/// MergeWithDiamondQueue 和 RefineWithSplitQueue 从队首读取并提交拓扑修改
/// </summary>
void InitializePersistentMergeQueue(DataOrientedRoamState& state);
void InitializePersistentSplitQueue(DataOrientedRoamState& state);
// 细分队列独立保存节点和分数，建堆不会改变活动叶数组的顺序
void RefreshPersistentSplitQueuePriorities(DataOrientedRoamState& state);

// 以下接口维护细分队列成员和堆顶，并为并行处理复制一份当前候选列表
void InsertPersistentSplitQueueNode(
    DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node);
void RemovePersistentSplitQueueNode(
    DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node);
void BlockPersistentSplitQueueNodeForCurrentBuild(
    DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node);
[[nodiscard]] DataOrientedRoamNodeIndex TopPersistentSplitQueueNode(
    const DataOrientedRoamState& state);
[[nodiscard]] float TopPersistentSplitQueueScore(const DataOrientedRoamState& state);
void SnapshotPersistentSplitQueueCandidates(
    const DataOrientedRoamState& state,
    std::vector<DataOrientedRoamSplitCandidate>& candidates);

// 合并队列为每个菱形只保留一个固定代表节点，拓扑修改后只刷新受影响邻域
void RefreshPersistentMergeQueuePriorities(DataOrientedRoamState& state);
void AppendPersistentMergeQueueNeighborhood(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node,
    std::vector<DataOrientedRoamNodeIndex>& nodes);
void AppendPersistentMergeQueueNeighborhood(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node,
    DataOrientedRoamNeighborhood& nodes);
void InvalidatePersistentMergeQueueNeighborhood(
    DataOrientedRoamState& state,
    const std::vector<DataOrientedRoamNodeIndex>& nodes);
void InvalidatePersistentMergeQueueNeighborhood(
    DataOrientedRoamState& state,
    const DataOrientedRoamNeighborhood& nodes);
void RefreshPersistentMergeQueueNeighborhood(
    DataOrientedRoamState& state,
    const std::vector<DataOrientedRoamNodeIndex>& nodes);
void RefreshPersistentMergeQueueNeighborhood(
    DataOrientedRoamState& state,
    const DataOrientedRoamNeighborhood& nodes);
void RemovePersistentMergeQueueCandidate(
    DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node);
[[nodiscard]] DataOrientedRoamNodeIndex TopPersistentMergeQueueNode(
    const DataOrientedRoamState& state);
[[nodiscard]] float TopPersistentMergeQueueScore(const DataOrientedRoamState& state);
void SnapshotPersistentMergeQueueCandidates(
    const DataOrientedRoamState& state,
    float maximumScore,
    std::vector<DataOrientedRoamMergeCandidate>& candidates);

/// <summary>
/// 只读检查双队列的堆顺序、反向位置、成员完整性以及每个菱形是否只有一个代表节点
/// </summary>
[[nodiscard]] std::size_t CountPersistentQueueInvariantViolations(
    const DataOrientedRoamState& state);
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
