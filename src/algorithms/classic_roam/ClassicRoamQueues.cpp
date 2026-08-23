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

Tools::PerformanceTimer::TimePoint BeginQueueMembershipTiming(bool enabled)
{
    // 普通交互帧不读取高精度时钟，避免观测功能形成固定开销
    return enabled ? Tools::PerformanceTimer::Now() : Tools::PerformanceTimer::TimePoint{};
}

float EndQueueMembershipTiming(bool enabled, Tools::PerformanceTimer::TimePoint start)
{
    // 禁用阶段证据时传入的是空时间点，此处直接返回零
    return enabled
        ? Tools::PerformanceTimer::ElapsedMilliseconds(start, Tools::PerformanceTimer::Now())
        : 0.0F;
}
} // 匿名命名空间

void ClassicRoamMeshBuilder::InitializePersistentQueues()
{
    // 拓扑重置后只有两个根叶节点属于当前活动三角网格
    _splitQueue.clear();
    _mergeQueue.clear();
    InsertSplitQueueNode(_rootA);
    InsertSplitQueueNode(_rootB);
}

float ClassicRoamMeshBuilder::SplitQueueScore(const ClassicRoamNode& node) const
{
    // Q_s 保留全部活动叶节点；本帧不可细分的节点使用最低分沉到堆底
    if (!node.Active || !IsLeaf(&node) || node.Depth >= _settings.MaxDepth ||
        node.SplitBlockedBuildId == _buildSequence || node.MergeBuildId == _buildSequence)
    {
        return BlockedSplitScore;
    }

    return ComputeScreenErrorScore(node);
}

float ClassicRoamMeshBuilder::MergeQueueScore(const ClassicRoamNode& node) const
{
    // Q_m 成员始终反映当前拓扑，本次刚细分的菱形在当前更新中暂不参与合并
    if (node.SplitBuildId == _buildSequence ||
        (node.MergeQueuePartner != nullptr && node.MergeQueuePartner->SplitBuildId == _buildSequence))
    {
        return std::numeric_limits<float>::max();
    }

    // 按论文定义，菱形的合并分数取两侧父节点屏幕误差的较大值
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
    // Q_s 使用最大堆，PathId 为同分节点提供跨帧稳定顺序
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
    // Q_m 使用最小堆，合并后画质损失最小的菱形位于队首
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
    // 交换堆元素时同步更新节点内的下标，调用方无需保存会失效的迭代器
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
    // 相机变化会使全部分数失效，统一刷新后自底向上建堆只需 O(N)
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

    // 插入成本包含评分、追加条目和向上修复堆
    const Tools::PerformanceTimer::TimePoint membershipStart =
        BeginQueueMembershipTiming(_settings.EnablePassEvidence);
    node->SplitQueueIndex = _splitQueue.size();
    _splitQueue.push_back(SplitQueueEntry{node, SplitQueueScore(*node)});
    SiftSplitQueueUp(node->SplitQueueIndex);
    ++_stats.QueueMembershipUpdateCount;
    ++_stats.SplitQueueMembershipUpdateCount;
    _stats.CandidatePeakCount = std::max(
        _stats.CandidatePeakCount,
        _splitQueue.size() + _mergeQueue.size());
    _stats.SplitQueueMembershipUpdateMilliseconds += EndQueueMembershipTiming(
        _settings.EnablePassEvidence,
        membershipStart);
}

void ClassicRoamMeshBuilder::RemoveSplitQueueNode(ClassicRoamNode* node)
{
    if (node == nullptr || node->SplitQueueIndex == InvalidQueueIndex)
    {
        return;
    }

    // 删除成本包含末尾换位和换入条目的堆修复
    const Tools::PerformanceTimer::TimePoint membershipStart =
        BeginQueueMembershipTiming(_settings.EnablePassEvidence);
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
    ++_stats.SplitQueueMembershipUpdateCount;
    _stats.SplitQueueMembershipUpdateMilliseconds += EndQueueMembershipTiming(
        _settings.EnablePassEvidence,
        membershipStart);
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

    // 若需要重新检查的局部节点漏掉旧关联，先移除旧项，防止一个父节点同时属于两个菱形
    if (partner != nullptr && partner->MergeQueueRepresentative != nullptr)
    {
        RemoveMergeQueueCandidate(partner);
    }

    const Tools::PerformanceTimer::TimePoint membershipStart =
        BeginQueueMembershipTiming(_settings.EnablePassEvidence);
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
    ++_stats.MergeQueueMembershipUpdateCount;
    _stats.CandidatePeakCount = std::max(
        _stats.CandidatePeakCount,
        _splitQueue.size() + _mergeQueue.size());
    _stats.MergeQueueMembershipUpdateMilliseconds += EndQueueMembershipTiming(
        _settings.EnablePassEvidence,
        membershipStart);
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
    const bool removesQueueEntry = index != InvalidQueueIndex && index < _mergeQueue.size();
    const Tools::PerformanceTimer::TimePoint membershipStart = removesQueueEntry
        ? BeginQueueMembershipTiming(_settings.EnablePassEvidence)
        : Tools::PerformanceTimer::TimePoint{};
    if (removesQueueEntry)
    {
        const std::size_t last = _mergeQueue.size() - 1U;
        if (index != last)
        {
            SwapMergeQueueEntries(index, last);
        }
        _mergeQueue.pop_back();
        RestoreMergeQueueAt(index);
        ++_stats.QueueMembershipUpdateCount;
        ++_stats.MergeQueueMembershipUpdateCount;
    }

    representative->MergeQueueIndex = InvalidQueueIndex;
    representative->MergeQueueRepresentative = nullptr;
    representative->MergeQueuePartner = nullptr;
    if (partner != nullptr)
    {
        partner->MergeQueueRepresentative = nullptr;
    }
    if (removesQueueEntry)
    {
        _stats.MergeQueueMembershipUpdateMilliseconds += EndQueueMembershipTiming(
            _settings.EnablePassEvidence,
            membershipStart);
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

    // 可合并性还取决于相邻父节点的子节点状态，因此邻域需要沿父节点和底边再扩一层
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
    _stats.SplitScoreEntryCount = _splitQueue.size();
    // 分数重算和原地建堆严格分段，完整刷新耗时由两者相加得到
    // 刷新期间不会插入或删除队列成员
    Tools::PerformanceTimer splitScoreTimer;
    for (SplitQueueEntry& entry : _splitQueue)
    {
        entry.Score = SplitQueueScore(*entry.Node);
    }
    _stats.SplitScoreMilliseconds = splitScoreTimer.Stop();
    Tools::PerformanceTimer splitHeapifyTimer;
    HeapifySplitQueue();
    _stats.SplitHeapifyMilliseconds = splitHeapifyTimer.Stop();
    _stats.SplitInitialScanMilliseconds =
        _stats.SplitScoreMilliseconds + _stats.SplitHeapifyMilliseconds;

    _stats.MergeScoreEntryCount = _mergeQueue.size();
    // Q_m 使用同一计时边界，菱形成员集合在本阶段保持不变
    // 每个菱形仍只计算代表节点的合并损失
    Tools::PerformanceTimer mergeScoreTimer;
    for (MergeQueueEntry& entry : _mergeQueue)
    {
        entry.Score = MergeQueueScore(*entry.Node);
    }
    _stats.MergeScoreMilliseconds = mergeScoreTimer.Stop();
    Tools::PerformanceTimer mergeHeapifyTimer;
    HeapifyMergeQueue();
    _stats.MergeHeapifyMilliseconds = mergeHeapifyTimer.Stop();
    _stats.MergeCandidateMarkMilliseconds =
        _stats.MergeScoreMilliseconds + _stats.MergeHeapifyMilliseconds;
}

void ClassicRoamMeshBuilder::OptimizeWithPersistentDualQueues()
{
    // 相机变化只刷新队列分数，队列成员只随局部拓扑修改而变化
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

        // 先合并误差明确低于阈值的菱形，回收当前画面不再需要的细节
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

        // 有剩余预算时先计算补齐相邻三角形需要执行的全部细分，确保整组操作不会超过上限
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
                // 严格预算下先合并再重试细分，避免论文伪代码产生瞬时超限
                const bool merged = mergeDuringSplitConvergence(mergeNode);
                if (merged)
                {
                    ++_stats.QueueCrossoverCount;
                    continue;
                }
                RemoveMergeQueueCandidate(mergeNode);
                ++_stats.RejectedMergeCount;
            }

            // 本次无法完成整组连锁细分时只暂停该节点，下一帧刷新分数后再尝试
            splitNode->SplitBlockedBuildId = _buildSequence;
            UpdateSplitQueueScore(splitNode, BlockedSplitScore);
            if (!closureNeedsMoreBudget)
            {
                ++_stats.RejectedSplitCount;
            }
            continue;
        }

        // 预算已满时只在合并损失低于细分收益的情况下交换三角形资源
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
