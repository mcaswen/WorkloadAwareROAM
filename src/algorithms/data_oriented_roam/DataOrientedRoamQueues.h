#pragma once

#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"

#include <array>
#include <cstddef>
#include <vector>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
/// <summary>
/// 串行 topology 事务使用的局部邻域容器
/// 常规邻域直接存放在栈内数组，只有推导上限被异常拓扑超过时才转入可扩容回退存储
/// </summary>
class DataOrientedRoamNeighborhood
{
public:
    // 四个 seed 各自最多扩张到 21 个节点，保留少量安全余量
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
/// DOD 的双持久优先队列接口
/// queue 容器由 DataOrientedRoamState 持有；初始化只在 reset，priority refresh 和局部成员更新发生在每帧 Build
/// queue pass 与 topology pass 会修改它；消费者是 MergeWithDiamondQueue 和 RefineWithSplitQueue
/// </summary>
void InitializePersistentMergeQueue(DataOrientedRoamState& state);
void InitializePersistentSplitQueue(DataOrientedRoamState& state);
// split queue 独立于活动叶视图保存 node/score，活动叶顺序不受 heapify 影响
void RefreshPersistentSplitQueuePriorities(DataOrientedRoamState& state);

// 下列接口维护 split queue 的 membership、堆顶和候选快照
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

// merge queue 每个 diamond 只保留一个代表节点；topology 修改后只刷新受影响邻域
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
[[nodiscard]] std::size_t CountPersistentQueueInvariantViolations(
    const DataOrientedRoamState& state);
} // namespace ParallelRoam::Algorithms::DataOrientedRoam
