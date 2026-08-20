#include "algorithms/classic_roam/ClassicRoamMeshBuilder.h"
#include "tools/PerformanceTimer.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ParallelRoam::Algorithms::ClassicRoam
{
namespace
{
constexpr std::size_t InvalidQueueIndex = std::numeric_limits<std::size_t>::max();
constexpr float BlockedSplitScore = -std::numeric_limits<float>::max();
} // 匿名命名空间

void ClassicRoamMeshBuilder::InitializePersistentQueues()
{
    // topology reset 后只有两个 root leaf 属于当前 triangulation
    _splitQueue.clear();
    _mergeQueue.clear();
    InsertSplitQueueNode(_rootA);
    InsertSplitQueueNode(_rootB);
}

float ClassicRoamMeshBuilder::SplitQueueScore(const ClassicRoamNode& node) const
{
    // Q_s 保留全部 active leaf membership，但不可 split 元素沉到 heap 底部
    if (!node.Active || !IsLeaf(&node) || node.Depth >= _settings.MaxDepth ||
        node.SplitBlockedBuildId == _buildSequence || node.MergeBuildId == _buildSequence)
    {
        return BlockedSplitScore;
    }

    return ComputeScreenErrorScore(node);
}

float ClassicRoamMeshBuilder::MergeQueueScore(const ClassicRoamNode& node) const
{
    // membership 始终反映 topology；刚 split 的 diamond 只在本 Build 暂停反向 merge。
    if (node.SplitBuildId == _buildSequence ||
        (node.MergeQueuePartner != nullptr && node.MergeQueuePartner->SplitBuildId == _buildSequence))
    {
        return std::numeric_limits<float>::max();
    }

    // 论文把 diamond priority 定义为两侧 parent priority 的最大值
    float score = ComputeScreenErrorScore(node);
    if (node.MergeQueuePartner != nullptr)
    {
        score = std::max(score, ComputeScreenErrorScore(*node.MergeQueuePartner));
    }
    return score;
}

bool ClassicRoamMeshBuilder::SplitEntryPrecedes(
    const SplitQueueEntry& left,
    const SplitQueueEntry& right) const
{
    // Q_s 是 max heap；PathId 为同分候选提供跨帧稳定顺序
    if (left.Score != right.Score)
    {
        return left.Score > right.Score;
    }
    return left.Node != nullptr && right.Node != nullptr && left.Node->PathId < right.Node->PathId;
}

bool ClassicRoamMeshBuilder::MergeEntryPrecedes(
    const MergeQueueEntry& left,
    const MergeQueueEntry& right) const
{
    // Q_m 是 min heap；最低损失 diamond 位于队首
    if (left.Score != right.Score)
    {
        return left.Score < right.Score;
    }
    return left.Node != nullptr && right.Node != nullptr && left.Node->PathId < right.Node->PathId;
}

void ClassicRoamMeshBuilder::SwapSplitQueueEntries(std::size_t left, std::size_t right)
{
    if (left == right)
    {
        return;
    }
    std::swap(_splitQueue[left], _splitQueue[right]);
    _splitQueue[left].Node->SplitQueueIndex = left;
    _splitQueue[right].Node->SplitQueueIndex = right;
}

void ClassicRoamMeshBuilder::SwapMergeQueueEntries(std::size_t left, std::size_t right)
{
    if (left == right)
    {
        return;
    }
    std::swap(_mergeQueue[left], _mergeQueue[right]);
    _mergeQueue[left].Node->MergeQueueIndex = left;
    _mergeQueue[right].Node->MergeQueueIndex = right;
}

void ClassicRoamMeshBuilder::SiftSplitQueueUp(std::size_t index)
{
    // intrusive index 随 swap 同步，调用者不需要保存不稳定 iterator
    while (index > 0U)
    {
        const std::size_t parent = (index - 1U) / 2U;
        if (!SplitEntryPrecedes(_splitQueue[index], _splitQueue[parent]))
        {
            break;
        }
        SwapSplitQueueEntries(index, parent);
        index = parent;
    }
}

void ClassicRoamMeshBuilder::SiftSplitQueueDown(std::size_t index)
{
    for (;;)
    {
        const std::size_t left = index * 2U + 1U;
        if (left >= _splitQueue.size())
        {
            return;
        }
        const std::size_t right = left + 1U;
        std::size_t best = left;
        if (right < _splitQueue.size() && SplitEntryPrecedes(_splitQueue[right], _splitQueue[left]))
        {
            best = right;
        }
        if (!SplitEntryPrecedes(_splitQueue[best], _splitQueue[index]))
        {
            return;
        }
        SwapSplitQueueEntries(index, best);
        index = best;
    }
}

void ClassicRoamMeshBuilder::SiftMergeQueueUp(std::size_t index)
{
    while (index > 0U)
    {
        const std::size_t parent = (index - 1U) / 2U;
        if (!MergeEntryPrecedes(_mergeQueue[index], _mergeQueue[parent]))
        {
            break;
        }
        SwapMergeQueueEntries(index, parent);
        index = parent;
    }
}

void ClassicRoamMeshBuilder::SiftMergeQueueDown(std::size_t index)
{
    for (;;)
    {
        const std::size_t left = index * 2U + 1U;
        if (left >= _mergeQueue.size())
        {
            return;
        }
        const std::size_t right = left + 1U;
        std::size_t best = left;
        if (right < _mergeQueue.size() && MergeEntryPrecedes(_mergeQueue[right], _mergeQueue[left]))
        {
            best = right;
        }
        if (!MergeEntryPrecedes(_mergeQueue[best], _mergeQueue[index]))
        {
            return;
        }
        SwapMergeQueueEntries(index, best);
        index = best;
    }
}

void ClassicRoamMeshBuilder::RestoreSplitQueueAt(std::size_t index)
{
    if (index >= _splitQueue.size())
    {
        return;
    }
    if (index > 0U && SplitEntryPrecedes(_splitQueue[index], _splitQueue[(index - 1U) / 2U]))
    {
        SiftSplitQueueUp(index);
    }
    else
    {
        SiftSplitQueueDown(index);
    }
}

void ClassicRoamMeshBuilder::RestoreMergeQueueAt(std::size_t index)
{
    if (index >= _mergeQueue.size())
    {
        return;
    }
    if (index > 0U && MergeEntryPrecedes(_mergeQueue[index], _mergeQueue[(index - 1U) / 2U]))
    {
        SiftMergeQueueUp(index);
    }
    else
    {
        SiftMergeQueueDown(index);
    }
}

void ClassicRoamMeshBuilder::HeapifySplitQueue()
{
    // 所有 view-dependent key 同时刷新后，自底向上 heapify 是 O(N)
    for (std::size_t index = _splitQueue.size() / 2U; index > 0U; --index)
    {
        SiftSplitQueueDown(index - 1U);
    }
}

void ClassicRoamMeshBuilder::HeapifyMergeQueue()
{
    for (std::size_t index = _mergeQueue.size() / 2U; index > 0U; --index)
    {
        SiftMergeQueueDown(index - 1U);
    }
}

void ClassicRoamMeshBuilder::InsertSplitQueueNode(ClassicRoamNode* node)
{
    if (node == nullptr || !node->Active || !IsLeaf(node) || node->SplitQueueIndex != InvalidQueueIndex)
    {
        return;
    }

    node->SplitQueueIndex = _splitQueue.size();
    _splitQueue.push_back(SplitQueueEntry{node, SplitQueueScore(*node)});
    SiftSplitQueueUp(node->SplitQueueIndex);
    ++_stats.QueueMembershipUpdateCount;
    _stats.CandidatePeakCount = std::max(
        _stats.CandidatePeakCount,
        _splitQueue.size() + _mergeQueue.size());
}

void ClassicRoamMeshBuilder::RemoveSplitQueueNode(ClassicRoamNode* node)
{
    if (node == nullptr || node->SplitQueueIndex == InvalidQueueIndex)
    {
        return;
    }

    const std::size_t index = node->SplitQueueIndex;
    const std::size_t last = _splitQueue.size() - 1U;
    if (index != last)
    {
        SwapSplitQueueEntries(index, last);
    }
    _splitQueue.pop_back();
    node->SplitQueueIndex = InvalidQueueIndex;
    RestoreSplitQueueAt(index);
    ++_stats.QueueMembershipUpdateCount;
}

void ClassicRoamMeshBuilder::UpdateSplitQueueScore(ClassicRoamNode* node, float score)
{
    if (node == nullptr || node->SplitQueueIndex == InvalidQueueIndex)
    {
        return;
    }
    const std::size_t index = node->SplitQueueIndex;
    _splitQueue[index].Score = score;
    RestoreSplitQueueAt(index);
}

ClassicRoamMeshBuilder::ClassicRoamNode* ClassicRoamMeshBuilder::TopSplitQueueNode() const
{
    return _splitQueue.empty() ? nullptr : _splitQueue.front().Node;
}

bool ClassicRoamMeshBuilder::IsMergeableTopology(const ClassicRoamNode* node) const
{
    if (node == nullptr || !node->Active || IsLeaf(node) || node->LeftChild == nullptr ||
        node->RightChild == nullptr || !node->LeftChild->Active || !node->RightChild->Active ||
        !IsLeaf(node->LeftChild) || !IsLeaf(node->RightChild))
    {
        return false;
    }

    const ClassicRoamNode* baseNeighbor = node->BaseNeighbor;
    if (baseNeighbor == nullptr || IsLeaf(baseNeighbor))
    {
        return baseNeighbor == nullptr || baseNeighbor->Active;
    }

    return baseNeighbor->Active && baseNeighbor->BaseNeighbor == node &&
           baseNeighbor->LeftChild != nullptr && baseNeighbor->RightChild != nullptr &&
           baseNeighbor->LeftChild->Active && baseNeighbor->RightChild->Active &&
           IsLeaf(baseNeighbor->LeftChild) && IsLeaf(baseNeighbor->RightChild);
}

ClassicRoamMeshBuilder::ClassicRoamNode* ClassicRoamMeshBuilder::CanonicalMergeQueueNode(
    ClassicRoamNode* node) const
{
    if (!IsMergeableTopology(node))
    {
        return nullptr;
    }

    ClassicRoamNode* baseNeighbor = node->BaseNeighbor;
    if (baseNeighbor != nullptr && !IsLeaf(baseNeighbor) && baseNeighbor->BaseNeighbor == node)
    {
        return node->PathId < baseNeighbor->PathId ? node : baseNeighbor;
    }
    return node;
}

void ClassicRoamMeshBuilder::InsertMergeQueueNodeIfEligible(ClassicRoamNode* node)
{
    ClassicRoamNode* representative = CanonicalMergeQueueNode(node);
    if (representative == nullptr || representative->MergeQueueRepresentative != nullptr)
    {
        return;
    }

    ClassicRoamNode* partner = representative->BaseNeighbor;
    if (partner == nullptr || IsLeaf(partner) || partner->BaseNeighbor != representative)
    {
        partner = nullptr;
    }

    // 若局部失效集合遗漏了旧 association，先删除旧项，不能让一个 parent 属于两个 diamonds
    if (partner != nullptr && partner->MergeQueueRepresentative != nullptr)
    {
        RemoveMergeQueueCandidate(partner);
    }

    representative->MergeQueueRepresentative = representative;
    representative->MergeQueuePartner = partner;
    if (partner != nullptr)
    {
        partner->MergeQueueRepresentative = representative;
    }
    representative->MergeQueueIndex = _mergeQueue.size();
    _mergeQueue.push_back(MergeQueueEntry{representative, MergeQueueScore(*representative)});
    SiftMergeQueueUp(representative->MergeQueueIndex);
    ++_stats.QueueMembershipUpdateCount;
    _stats.CandidatePeakCount = std::max(
        _stats.CandidatePeakCount,
        _splitQueue.size() + _mergeQueue.size());
}

void ClassicRoamMeshBuilder::RemoveMergeQueueCandidate(ClassicRoamNode* node)
{
    if (node == nullptr || node->MergeQueueRepresentative == nullptr)
    {
        return;
    }

    ClassicRoamNode* representative = node->MergeQueueRepresentative;
    ClassicRoamNode* partner = representative->MergeQueuePartner;
    const std::size_t index = representative->MergeQueueIndex;
    if (index != InvalidQueueIndex && index < _mergeQueue.size())
    {
        const std::size_t last = _mergeQueue.size() - 1U;
        if (index != last)
        {
            SwapMergeQueueEntries(index, last);
        }
        _mergeQueue.pop_back();
        RestoreMergeQueueAt(index);
        ++_stats.QueueMembershipUpdateCount;
    }

    representative->MergeQueueIndex = InvalidQueueIndex;
    representative->MergeQueueRepresentative = nullptr;
    representative->MergeQueuePartner = nullptr;
    if (partner != nullptr)
    {
        partner->MergeQueueRepresentative = nullptr;
    }
}

ClassicRoamMeshBuilder::ClassicRoamNode* ClassicRoamMeshBuilder::TopMergeQueueNode() const
{
    return _mergeQueue.empty() ? nullptr : _mergeQueue.front().Node;
}

void ClassicRoamMeshBuilder::AppendQueueNeighborhood(
    ClassicRoamNode* seed,
    std::vector<ClassicRoamNode*>& nodes) const
{
    if (seed == nullptr)
    {
        return;
    }

    const auto appendUnique = [&nodes](ClassicRoamNode* node) {
        if (node != nullptr && std::find(nodes.begin(), nodes.end(), node) == nodes.end())
        {
            nodes.push_back(node);
        }
    };
    appendUnique(seed);
    appendUnique(seed->Parent);
    appendUnique(seed->LeftChild);
    appendUnique(seed->RightChild);
    appendUnique(seed->BaseNeighbor);
    appendUnique(seed->LeftNeighbor);
    appendUnique(seed->RightNeighbor);

    // mergeability 还依赖相邻 parent 的 child 状态，因此再扩一层 parent/base 关系
    const ClassicRoamNode* directNodes[] = {
        seed,
        seed->Parent,
        seed->LeftChild,
        seed->RightChild,
        seed->BaseNeighbor,
        seed->LeftNeighbor,
        seed->RightNeighbor,
    };
    for (const ClassicRoamNode* directNode : directNodes)
    {
        if (directNode != nullptr)
        {
            appendUnique(directNode->Parent);
            appendUnique(directNode->BaseNeighbor);
        }
    }
}

void ClassicRoamMeshBuilder::InvalidateMergeQueueNeighborhood(
    const std::vector<ClassicRoamNode*>& nodes)
{
    for (ClassicRoamNode* node : nodes)
    {
        RemoveMergeQueueCandidate(node);
    }
}

void ClassicRoamMeshBuilder::RefreshMergeQueueNeighborhood(
    const std::vector<ClassicRoamNode*>& nodes)
{
    for (ClassicRoamNode* node : nodes)
    {
        InsertMergeQueueNodeIfEligible(node);
    }
}

void ClassicRoamMeshBuilder::RefreshPersistentQueuePriorities()
{
    Tools::PerformanceTimer splitTimer;
    for (SplitQueueEntry& entry : _splitQueue)
    {
        entry.Score = SplitQueueScore(*entry.Node);
    }
    HeapifySplitQueue();
    _stats.SplitInitialScanMilliseconds = splitTimer.Stop();

    Tools::PerformanceTimer mergeTimer;
    for (MergeQueueEntry& entry : _mergeQueue)
    {
        entry.Score = MergeQueueScore(*entry.Node);
    }
    HeapifyMergeQueue();
    _stats.MergeCandidateMarkMilliseconds = mergeTimer.Stop();
}

void ClassicRoamMeshBuilder::OptimizeWithPersistentDualQueues()
{
    // priority 值随相机变化，membership 则只由局部 topology 事务修改
    RefreshPersistentQueuePriorities();
    _remainingSplitBudget = _settings.TriangleBudget > _splitQueue.size()
        ? _settings.TriangleBudget - _splitQueue.size()
        : 0U;

    const std::size_t maximumIterations = std::max<std::size_t>(
        1024U,
        _settings.TriangleBudget * 8U + _nodes.size() * 4U);
    std::size_t iteration = 0U;
    float crossoverMergeMilliseconds = 0.0F;
    const auto mergeDuringSplitConvergence =
        [this, &crossoverMergeMilliseconds](ClassicRoamNode* node) {
            Tools::PerformanceTimer mergeTimer(_stats.MergeTopologyMilliseconds);
            const bool merged = MergeNodeOrDiamond(node, std::numeric_limits<float>::max());
            crossoverMergeMilliseconds += mergeTimer.Stop();
            return merged;
        };
    Tools::PerformanceTimer serialConvergenceTimer;
    while (iteration++ < maximumIterations)
    {
        ClassicRoamNode* splitNode = TopSplitQueueNode();
        ClassicRoamNode* mergeNode = TopMergeQueueNode();
        const float splitScore = splitNode != nullptr ? _splitQueue.front().Score : BlockedSplitScore;
        const float mergeScore = mergeNode != nullptr
            ? _mergeQueue.front().Score
            : std::numeric_limits<float>::max();

        // accuracy target 优先回收明确低于 merge threshold 的 diamond
        if (mergeNode != nullptr && mergeScore < _settings.MergeThreshold)
        {
            const bool merged = mergeDuringSplitConvergence(mergeNode);
            if (!merged)
            {
                RemoveMergeQueueCandidate(mergeNode);
                ++_stats.RejectedMergeCount;
            }
            continue;
        }

        if (splitNode == nullptr || !ShouldSplitWithScore(*splitNode, splitScore))
        {
            break;
        }

        // 有空余 token 时先尝试完整 forced-split closure；reservation guard 保证失败不超预算
        if (_remainingSplitBudget > 0U)
        {
            const std::size_t budgetRejectBefore = _stats.BudgetRejectedSplitCount;
            Tools::PerformanceTimer splitTimer(_stats.SplitQueueTopologyMilliseconds);
            const bool split = SplitNode(splitNode, SplitReason::Requested, nullptr, 0U);
            splitTimer.Stop();
            if (split)
            {
                continue;
            }

            const bool closureNeedsMoreBudget = _stats.BudgetRejectedSplitCount > budgetRejectBefore;
            mergeNode = TopMergeQueueNode();
            const float currentMergeScore = mergeNode != nullptr
                ? _mergeQueue.front().Score
                : std::numeric_limits<float>::max();
            if (closureNeedsMoreBudget && mergeNode != nullptr && splitScore > currentMergeScore)
            {
                // strict capacity 下先 merge 再重试 split，避免论文伪代码的瞬时超预算
                const bool merged = mergeDuringSplitConvergence(mergeNode);
                if (merged)
                {
                    ++_stats.QueueCrossoverCount;
                    continue;
                }
                RemoveMergeQueueCandidate(mergeNode);
                ++_stats.RejectedMergeCount;
            }

            // 当前 closure 无法提交时只屏蔽到本帧，下一帧 priority refresh 会重新激活
            splitNode->SplitBlockedBuildId = _buildSequence;
            UpdateSplitQueueScore(splitNode, BlockedSplitScore);
            if (!closureNeedsMoreBudget)
            {
                ++_stats.RejectedSplitCount;
            }
            continue;
        }

        // 已满预算时执行论文 crossover：只有回收损失低于最高 split 收益才交换资源
        if (mergeNode != nullptr && splitScore > mergeScore)
        {
            const bool merged = mergeDuringSplitConvergence(mergeNode);
            if (merged)
            {
                ++_stats.QueueCrossoverCount;
                continue;
            }
            RemoveMergeQueueCandidate(mergeNode);
            ++_stats.RejectedMergeCount;
            continue;
        }

        ++_stats.BudgetRejectedSplitCount;
        break;
    }
    const float totalConvergenceMilliseconds = serialConvergenceTimer.Stop();
    _stats.SplitSerialConvergenceMilliseconds = std::max(
        0.0F,
        totalConvergenceMilliseconds - crossoverMergeMilliseconds);

    _stats.PersistentSplitQueueSize = _splitQueue.size();
    _stats.PersistentMergeQueueSize = _mergeQueue.size();
    _stats.CandidatePeakCount = std::max(
        _stats.CandidatePeakCount,
        _splitQueue.size() + _mergeQueue.size());
}
} // 命名空间 ParallelRoam::Algorithms::ClassicRoam
