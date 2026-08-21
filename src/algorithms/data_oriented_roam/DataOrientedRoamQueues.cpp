#include "algorithms/data_oriented_roam/DataOrientedRoamQueues.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamScoring.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamParallel.h"

#include <algorithm>
#include <limits>
#include <thread>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
namespace
{
constexpr DataOrientedRoamPosition InvalidQueuePosition = InvalidDataOrientedRoamPosition;
constexpr std::size_t MinParallelPriorityRefreshCount = 256U;
constexpr std::size_t MaxPriorityRefreshWorkerCount = 8U;
constexpr float BlockedSplitScore = -std::numeric_limits<float>::max();

std::size_t ResolvePriorityRefreshWorkerCount(
    const DataOrientedRoamState& state,
    std::size_t entryCount)
{
    // 队列较小时保持串行，避免线程调度成本超过评分本身
    // 自动模式最多使用 8 个线程，与 DOD 其他批量阶段采用相同保守上限
    // 显式线程数仍尊重调用方设置，便于基准测试观察同时工作线程数量的影响
    if (entryCount == 0U)
    {
        return 0U;
    }
    if (state.Settings.ErrorEvaluationWorkerCount == 1U ||
        entryCount < MinParallelPriorityRefreshCount)
    {
        return 1U;
    }

    std::size_t requested = state.Settings.ErrorEvaluationWorkerCount;
    if (requested == 0U)
    {
        const unsigned int hardwareCount = std::thread::hardware_concurrency();
        requested = hardwareCount == 0U ? 1U : static_cast<std::size_t>(hardwareCount);
        requested = std::min(requested, MaxPriorityRefreshWorkerCount);
    }
    return std::clamp(requested, std::size_t{1U}, entryCount);
}

// ActiveInternalNodes 只记录从根节点仍可到达的活动内部节点
// 合并后历史节点仍留在 SoA 节点池中，因此不能只看 IsSplit 判断节点是否还在当前拓扑中
bool IsActiveInternalNode(const DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    if (!state.IsValidNode(node) || node >= state.NodeMembership.size())
    {
        return false;
    }
    // 成员位置有效就表示节点仍在活动内部节点列表中
    // 正向和反向表的完整对照只在验证器中执行，正常更新不再扫描 ActiveInternalNodes
    return state.NodeMembership[node].ActiveInternalPosition != InvalidActiveNodePosition;
}

// 反向位置表可以直接判断节点是否仍然活动，无需依赖节点池中保留的历史标志
// 已经过期的活动索引项不会进入长期保留的合并堆
bool IsActiveLeafNode(const DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    if (!state.IsValidNode(node) || node >= state.NodeMembership.size())
    {
        return false;
    }
    return state.NodeMembership[node].ActiveLeafPosition != InvalidActiveNodePosition;
}

float SplitQueueScore(const DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    // Q_s 保存全部活动叶节点，包括达到最大深度或本次更新暂时不再尝试的节点
    // 这些节点只会移到堆尾而不会退出队列，合并恢复的父节点在同一次更新中不会再次细分
    if (!IsActiveLeafNode(state, node) || state.Nodes.DepthAt(node) >= state.Settings.MaxDepth ||
        state.SplitQueueBlockedBuildIds[node] == state.BuildSequence ||
        state.Nodes.MergeBuildIdAt(node) == state.BuildSequence)
    {
        return BlockedSplitScore;
    }
    return ComputeScreenErrorScore(state, node);
}

void MirrorSplitQueueScore(
    DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node,
    float score)
{
    if (state.Settings.MirrorSplitScoresToNodePool)
    {
        state.Nodes.ScreenErrorAt(node) = score;
    }
}

bool SplitEntryPrecedes(
    const DataOrientedRoamState& state,
    const DataOrientedRoamSplitQueueEntry& left,
    const DataOrientedRoamSplitQueueEntry& right)
{
    // 常规比较直接读取连续堆条目，只有分数相同时才访问稳定 PathId
    if (left.Score != right.Score)
    {
        return left.Score > right.Score;
    }
    return state.Nodes.PathIdAt(left.Node) < state.Nodes.PathIdAt(right.Node);
}

void SwapSplitQueueEntries(DataOrientedRoamState& state, std::size_t left, std::size_t right)
{
    // Q_s 与活动叶数组分离，交换堆条目只更新队列自己的反向位置
    if (left == right)
    {
        return;
    }
    std::swap(state.SplitQueue[left], state.SplitQueue[right]);
    state.NodeMembership[state.SplitQueue[left].Node].SplitQueuePosition =
        static_cast<DataOrientedRoamPosition>(left);
    state.NodeMembership[state.SplitQueue[right].Node].SplitQueuePosition =
        static_cast<DataOrientedRoamPosition>(right);
}

void SiftSplitQueueUp(DataOrientedRoamState& state, std::size_t index)
{
    // 新激活的子节点和合并后恢复的父节点通过这条 O(log N) 路径入队
    while (index > 0U)
    {
        const std::size_t parent = (index - 1U) / 2U;
        if (!SplitEntryPrecedes(state, state.SplitQueue[index], state.SplitQueue[parent]))
        {
            break;
        }
        SwapSplitQueueEntries(state, index, parent);
        index = parent;
    }
}

void SiftSplitQueueDown(DataOrientedRoamState& state, std::size_t index)
{
    // 本次暂不再尝试的队首节点，以及删除任意项后换入的节点，都通过这里向下调整
    for (;;)
    {
        const std::size_t left = index * 2U + 1U;
        if (left >= state.SplitQueue.size())
        {
            return;
        }
        const std::size_t right = left + 1U;
        std::size_t best = left;
        if (right < state.SplitQueue.size() &&
            SplitEntryPrecedes(state, state.SplitQueue[right], state.SplitQueue[left]))
        {
            best = right;
        }
        if (!SplitEntryPrecedes(state, state.SplitQueue[best], state.SplitQueue[index]))
        {
            return;
        }
        SwapSplitQueueEntries(state, index, best);
        index = best;
    }
}

void RestoreSplitQueueAt(DataOrientedRoamState& state, std::size_t index)
{
    // 删除任意位置后，换入节点可能需要向上或向下调整
    // 先与父条目比较即可选择方向，避免两边都执行一次堆修复
    if (index >= state.SplitQueue.size())
    {
        return;
    }
    if (index > 0U && SplitEntryPrecedes(
            state,
            state.SplitQueue[index],
            state.SplitQueue[(index - 1U) / 2U]))
    {
        SiftSplitQueueUp(state, index);
    }
    else
    {
        SiftSplitQueueDown(state, index);
    }
}

// Q_m 中有哪些节点只由拓扑决定，处理堆顶时才根据相机和阈值决定是否合并
// 普通阈值合并和预算交换可以共用同一队列，无需因分数上限不同而重建
bool IsMergeableTopology(const DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    // 本函数只判断当前活动拓扑是否允许合并，不读取屏幕误差阈值
    // 把拓扑条件和视点分数分开后，相机移动只需刷新堆分数
    // 无需重新发现哪些父节点属于 Q_m
    if (!IsActiveInternalNode(state, node))
    {
        return false;
    }

    const DataOrientedRoamNodeIndex leftChild = state.Nodes.LeftChildAt(node);
    const DataOrientedRoamNodeIndex rightChild = state.Nodes.RightChildAt(node);
    if (!IsActiveLeafNode(state, leftChild) || !IsActiveLeafNode(state, rightChild))
    {
        return false;
    }

    // 地形边界上的父节点，以及底边邻居为活动叶的父节点，可以单独合并
    const DataOrientedRoamNodeIndex baseNeighbor = state.Nodes.BaseNeighborAt(node);
    if (!state.IsValidNode(baseNeighbor) || IsActiveLeafNode(state, baseNeighbor))
    {
        return !state.IsValidNode(baseNeighbor) || IsActiveLeafNode(state, baseNeighbor);
    }

    // 底边邻居为内部节点时，双方父节点必须互为底边邻居
    // 且四个子节点都为活动叶，才能构成合法菱形
    if (!IsActiveInternalNode(state, baseNeighbor))
    {
        return false;
    }
    // 双向底边邻接和四个叶子节点共同定义一个可合并菱形
    return state.Nodes.BaseNeighborAt(baseNeighbor) == node &&
           IsActiveLeafNode(state, state.Nodes.LeftChildAt(baseNeighbor)) &&
           IsActiveLeafNode(state, state.Nodes.RightChildAt(baseNeighbor));
}

// 一个完整菱形只对应一次队列操作，而不是两次父节点操作
// 即使 SoA 分配顺序变化，也使用 PathId 选出稳定且唯一的代表节点
DataOrientedRoamNodeIndex CanonicalMergeQueueNode(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node)
{
    if (!IsMergeableTopology(state, node))
    {
        return InvalidDataOrientedRoamNodeIndex;
    }

    const DataOrientedRoamNodeIndex baseNeighbor = state.Nodes.BaseNeighborAt(node);
    if (IsActiveInternalNode(state, baseNeighbor) && state.Nodes.BaseNeighborAt(baseNeighbor) == node)
    {
        return state.Nodes.PathIdAt(node) < state.Nodes.PathIdAt(baseNeighbor) ? node : baseNeighbor;
    }
    return node;
}

// 菱形的合并损失取两侧父节点分数的较大值
// 本次更新中刚由细分形成的菱形暂时不参与合并，避免刚细分就立即还原
float MergeQueueScore(const DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    const DataOrientedRoamNodeIndex partner = state.NodeMembership[node].MergeQueuePartner;
    if (state.Nodes.SplitBuildIdAt(node) == state.BuildSequence ||
        (state.IsValidNode(partner) && state.Nodes.SplitBuildIdAt(partner) == state.BuildSequence))
    {
        return std::numeric_limits<float>::max();
    }

    float score = ComputeScreenErrorScore(state, node);
    if (state.IsValidNode(partner))
    {
        score = std::max(score, ComputeScreenErrorScore(state, partner));
    }
    return score;
}

// Q_m 使用最小堆，分数相同时按 PathId 稳定排序
// 即使三角形数量上限使处理提前停止，也能得到确定的顺序
bool MergeEntryPrecedes(
    const DataOrientedRoamState& state,
    const DataOrientedRoamMergeQueueEntry& left,
    const DataOrientedRoamMergeQueueEntry& right)
{
    if (left.Score != right.Score)
    {
        return left.Score < right.Score;
    }
    return state.Nodes.PathIdAt(left.Node) < state.Nodes.PathIdAt(right.Node);
}

// 每次交换堆条目都同步更新代表节点的反向位置
// 队列不保留可能随动态数组移动而失效的迭代器或裸指针
void SwapMergeQueueEntries(DataOrientedRoamState& state, std::size_t left, std::size_t right)
{
    if (left == right)
    {
        return;
    }
    std::swap(state.MergeQueue[left], state.MergeQueue[right]);
    state.NodeMembership[state.MergeQueue[left].Node].MergeQueuePosition =
        static_cast<DataOrientedRoamPosition>(left);
    state.NodeMembership[state.MergeQueue[right].Node].MergeQueuePosition =
        static_cast<DataOrientedRoamPosition>(right);
}

// 局部插入复杂度为 O(log M)，M 是当前可合并菱形数量
void SiftMergeQueueUp(DataOrientedRoamState& state, std::size_t index)
{
    while (index > 0U)
    {
        const std::size_t parent = (index - 1U) / 2U;
        if (!MergeEntryPrecedes(state, state.MergeQueue[index], state.MergeQueue[parent]))
        {
            break;
        }
        SwapMergeQueueEntries(state, index, parent);
        index = parent;
    }
}

// 原地建堆和按下标删除共用这条向下调整路径
void SiftMergeQueueDown(DataOrientedRoamState& state, std::size_t index)
{
    for (;;)
    {
        const std::size_t left = index * 2U + 1U;
        if (left >= state.MergeQueue.size())
        {
            return;
        }
        const std::size_t right = left + 1U;
        std::size_t best = left;
        if (right < state.MergeQueue.size() &&
            MergeEntryPrecedes(state, state.MergeQueue[right], state.MergeQueue[left]))
        {
            best = right;
        }
        if (!MergeEntryPrecedes(state, state.MergeQueue[best], state.MergeQueue[index]))
        {
            return;
        }
        SwapMergeQueueEntries(state, index, best);
        index = best;
    }
}

// 末尾换入空位后可能破坏任一方向的堆关系
// 根据换入条目与父条目的关系选择向上或向下修复
void RestoreMergeQueueAt(DataOrientedRoamState& state, std::size_t index)
{
    if (index >= state.MergeQueue.size())
    {
        return;
    }
    if (index > 0U && MergeEntryPrecedes(
            state,
            state.MergeQueue[index],
            state.MergeQueue[(index - 1U) / 2U]))
    {
        SiftMergeQueueUp(state, index);
    }
    else
    {
        SiftMergeQueueDown(state, index);
    }
}

// 每个菱形只插入唯一代表节点
// 成员信息将两侧父节点关联到同一堆条目，因此从任意一侧都能删除整个菱形
void InsertMergeQueueNodeIfEligible(DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    const DataOrientedRoamNodeIndex representative = CanonicalMergeQueueNode(state, node);
    if (!state.IsValidNode(representative) ||
        state.NodeMembership[representative].MergeQueueRepresentative !=
            InvalidDataOrientedRoamNodeIndex)
    {
        return;
    }

    // 底边邻居不是互指内部节点时属于单父节点合并
    // 只有两侧内部父节点互为底边邻居时才记录合并伙伴
    DataOrientedRoamNodeIndex partner = state.Nodes.BaseNeighborAt(representative);
    if (!IsActiveInternalNode(state, partner) || state.Nodes.BaseNeighborAt(partner) != representative)
    {
        partner = InvalidDataOrientedRoamNodeIndex;
    }
    // 防御性清理避免过期关联使同一父节点同时属于两个菱形
    // 正常拓扑修改不应触发该路径
    if (state.IsValidNode(partner) &&
        state.NodeMembership[partner].MergeQueueRepresentative !=
            InvalidDataOrientedRoamNodeIndex)
    {
        RemovePersistentMergeQueueCandidate(state, partner);
    }

    state.NodeMembership[representative].MergeQueueRepresentative = representative;
    state.NodeMembership[representative].MergeQueuePartner = partner;
    if (state.IsValidNode(partner))
    {
        state.NodeMembership[partner].MergeQueueRepresentative = representative;
    }

    // 同一次更新内相机参数不变，候选入队时可直接计算分数
    // 下一次更新会在读取堆顶前刷新全部现有成员
    const std::size_t position = state.MergeQueue.size();
    state.NodeMembership[representative].MergeQueuePosition =
        static_cast<DataOrientedRoamPosition>(position);
    state.MergeQueue.push_back(
        DataOrientedRoamMergeQueueEntry{MergeQueueScore(state, representative), representative});
    SiftMergeQueueUp(state, position);
    ++state.Stats.QueueMembershipUpdateCount;
    state.Stats.CandidatePeakCount = std::max(
        state.Stats.CandidatePeakCount,
        state.ActiveLeafNodes.size() + state.MergeQueue.size());
}

// 多个线程收集时可以暂时出现重复节点，主线程汇总后再统一排序去重
void AppendIfValid(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node,
    std::vector<DataOrientedRoamNodeIndex>& nodes)
{
    if (state.IsValidNode(node))
    {
        nodes.push_back(node);
    }
}

// 串行局部邻域在插入时直接去重，无需在拓扑修改后再次排序
void AppendIfValid(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node,
    DataOrientedRoamNeighborhood& nodes)
{
    if (state.IsValidNode(node))
    {
        nodes.append_unique(node);
    }
}
} // 匿名命名空间

// 拓扑重置后根据当前活动叶集合重建反向位置
// 初始两个根节点都是叶节点，因此 Q_m 为空，该流程也兼容以后增加其他初始形态
void InitializePersistentSplitQueue(DataOrientedRoamState& state)
{
    // ResetTopology 已建立活动叶数组，此处据此创建独立 Q_s 和反向位置
    state.SplitQueue.clear();
    for (DataOrientedRoamNodeMembership& membership : state.NodeMembership)
    {
        membership.SplitQueuePosition = InvalidQueuePosition;
    }
    for (DataOrientedRoamNodeIndex node : state.ActiveLeafNodes)
    {
        const float score = SplitQueueScore(state, node);
        MirrorSplitQueueScore(state, node, score);
        state.NodeMembership[node].SplitQueuePosition = static_cast<DataOrientedRoamPosition>(
            state.SplitQueue.size());
        state.SplitQueue.push_back(DataOrientedRoamSplitQueueEntry{score, node});
    }
    for (std::size_t index = state.SplitQueue.size() / 2U; index > 0U; --index)
    {
        SiftSplitQueueDown(state, index - 1U);
    }
}

void RefreshPersistentSplitQueuePriorities(DataOrientedRoamState& state)
{
    // 相机移动会使全部分数失效，但不会改变队列成员
    // DOD 并行评分互不重叠的堆条目区间，最后用线性复杂度原地建堆恢复顺序
    std::vector<DataOrientedRoamNodeIndex> activeLeafOrderBeforeRefresh;
    if (state.Settings.EnableTopologyValidation)
    {
        // 活动叶数组与 Q_s 相互独立，验证模式会检查评分前后的活动叶顺序是否一致
        // 这样可以防止以后误将堆交换写回网格输出顺序
        activeLeafOrderBeforeRefresh = state.ActiveLeafNodes;
    }

    const std::size_t entryCount = state.SplitQueue.size();
    const std::size_t workerCount = ResolvePriorityRefreshWorkerCount(state, entryCount);
    state.Stats.CollectWorkerCount = std::max(state.Stats.CollectWorkerCount, workerCount);
    state.Stats.ErrorEvaluationWorkerCount = workerCount;
    state.Stats.CandidateMarkWorkerCount = std::max(
        state.Stats.CandidateMarkWorkerCount,
        workerCount);

    const auto refreshRange = [&state](std::size_t begin, std::size_t end) {
        // 所有评分线程结束前 Q_s 结构保持不变，每个线程只写自己的条目分数
        for (std::size_t index = begin; index < end; ++index)
        {
            DataOrientedRoamSplitQueueEntry& entry = state.SplitQueue[index];
            entry.Score = SplitQueueScore(state, entry.Node);
            MirrorSplitQueueScore(state, entry.Node, entry.Score);
        }
    };
    if (workerCount <= 1U)
    {
        refreshRange(0U, entryCount);
    }
    else
    {
        const std::size_t chunkSize = (entryCount + workerCount - 1U) / workerCount;
        RunDataOrientedRoamWorkers(state, workerCount, [&](std::size_t workerIndex) {
            const std::size_t begin = workerIndex * chunkSize;
            const std::size_t end = std::min(begin + chunkSize, entryCount);
            if (begin < end)
            {
                refreshRange(begin, end);
            }
        });
    }

    // 自底向上建堆为 O(N)，低于逐个重新插入全部叶节点的 O(N log N)
    for (std::size_t index = state.SplitQueue.size() / 2U; index > 0U; --index)
    {
        SiftSplitQueueDown(state, index - 1U);
    }
    if (state.Settings.EnableTopologyValidation &&
        state.ActiveLeafNodes != activeLeafOrderBeforeRefresh)
    {
        ++state.Stats.InvalidTopologyCount;
    }
    state.Stats.ErrorEvaluationCount = entryCount;
    // Q_s 的成员数量等于活动叶数量，也用于计算剩余三角形预算
    const std::size_t activeLeafCount = state.ActiveLeafNodes.size();
    const std::size_t remainingBudget = state.Settings.TriangleBudget > activeLeafCount
        ? state.Settings.TriangleBudget - activeLeafCount
        : 0U;
    state.RemainingSerialSplitBudget = remainingBudget;
    state.RemainingParallelSplitBudget.store(remainingBudget, std::memory_order_relaxed);
}

void InsertPersistentSplitQueueNode(DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    // 调用前活动叶数组已经更新，Q_s 的反向位置可以防止重复插入同一节点
    if (!IsActiveLeafNode(state, node) || !state.IsLeaf(node) ||
        node >= state.NodeMembership.size() ||
        state.NodeMembership[node].SplitQueuePosition != InvalidQueuePosition)
    {
        return;
    }

    const std::size_t position = state.SplitQueue.size();
    const float score = SplitQueueScore(state, node);
    MirrorSplitQueueScore(state, node, score);
    state.NodeMembership[node].SplitQueuePosition = static_cast<DataOrientedRoamPosition>(position);
    state.SplitQueue.push_back(DataOrientedRoamSplitQueueEntry{score, node});
    SiftSplitQueueUp(state, position);
    ++state.Stats.QueueMembershipUpdateCount;
}

void RemovePersistentSplitQueueNode(DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    // 强制细分可能删除不在堆顶的节点，因此 Q_s 必须支持按节点下标删除
    // 仅提供弹出队首的普通优先队列无法满足该需求
    if (!state.IsValidNode(node) || node >= state.NodeMembership.size())
    {
        return;
    }
    const std::size_t position = state.NodeMembership[node].SplitQueuePosition;
    if (position == InvalidQueuePosition || position >= state.SplitQueue.size())
    {
        return;
    }

    // 用末尾条目填补空位可以保持存储连续，只需修复换入条目的堆位置
    const std::size_t last = state.SplitQueue.size() - 1U;
    if (position != last)
    {
        SwapSplitQueueEntries(state, position, last);
    }
    state.SplitQueue.pop_back();
    state.NodeMembership[node].SplitQueuePosition = InvalidQueuePosition;
    RestoreSplitQueueAt(state, position);
    ++state.Stats.QueueMembershipUpdateCount;
}

void BlockPersistentSplitQueueNodeForCurrentBuild(
    DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node)
{
    // 补齐相邻三角形所需的连锁细分失败后，该节点仍属于活动拓扑
    // 本次更新把它移到堆尾以避免无限重试，下一帧仍会重新评分
    if (!IsActiveLeafNode(state, node))
    {
        return;
    }
    state.SplitQueueBlockedBuildIds[node] = state.BuildSequence;
    const std::size_t position = state.NodeMembership[node].SplitQueuePosition;
    if (position == InvalidQueuePosition || position >= state.SplitQueue.size())
    {
        return;
    }
    state.SplitQueue[position].Score = BlockedSplitScore;
    MirrorSplitQueueScore(state, node, BlockedSplitScore);
    RestoreSplitQueueAt(state, position);
}

DataOrientedRoamNodeIndex TopPersistentSplitQueueNode(const DataOrientedRoamState& state)
{
    // 堆顶是当前视点下最值得细分的活动叶节点
    return state.SplitQueue.empty()
        ? InvalidDataOrientedRoamNodeIndex
        : state.SplitQueue.front().Node;
}

float TopPersistentSplitQueueScore(const DataOrientedRoamState& state)
{
    // Q_s 为空时返回与不可细分节点相同的最低分数，使处理循环自然停止
    return state.SplitQueue.empty() ? BlockedSplitScore : state.SplitQueue.front().Score;
}

void SnapshotPersistentSplitQueueCandidates(
    const DataOrientedRoamState& state,
    std::vector<DataOrientedRoamSplitCandidate>& candidates)
{
    // 多个线程只读取这份不会再变化的候选副本
    // 所有线程结束后，主线程继续从当前 Q_s 中处理需要连锁细分相邻三角形的节点
    candidates.clear();
    candidates.reserve(state.SplitQueue.size());
    std::uint64_t sequence = 0U;
    for (const DataOrientedRoamSplitQueueEntry& entry : state.SplitQueue)
    {
        // 本次更新已经统一刷新分数，复制候选时无需重复计算屏幕误差
        if (ShouldSplitWithScore(state, entry.Node, entry.Score))
        {
            candidates.push_back(DataOrientedRoamSplitCandidate{entry.Score, sequence++, entry.Node});
        }
    }
}

void InitializePersistentMergeQueue(DataOrientedRoamState& state)
{
    // 拓扑重置时允许遍历全部活动内部节点来建立初始 Q_m
    // 普通更新不再遍历全部内部节点，后续成员只根据局部拓扑变化维护
    state.MergeQueue.clear();
    for (DataOrientedRoamNodeMembership& membership : state.NodeMembership)
    {
        membership.MergeQueuePosition = InvalidQueuePosition;
        membership.MergeQueueRepresentative = InvalidDataOrientedRoamNodeIndex;
        membership.MergeQueuePartner = InvalidDataOrientedRoamNodeIndex;
    }
    for (DataOrientedRoamNodeIndex node : state.ActiveInternalNodes)
    {
        InsertMergeQueueNodeIfEligible(state, node);
    }
}

// Q_m 成员跨帧保留，但与视点相关的分数每帧都会失效
// 此处只刷新 M 个现有成员并以 O(M) 原地建堆，避免扫描全部活动内部节点
void RefreshPersistentMergeQueuePriorities(DataOrientedRoamState& state)
{
    const std::size_t entryCount = state.MergeQueue.size();
    const std::size_t workerCount = ResolvePriorityRefreshWorkerCount(state, entryCount);
    state.Stats.CandidateMarkWorkerCount = std::max(
        state.Stats.CandidateMarkWorkerCount,
        workerCount);
    const auto refreshRange = [&state](std::size_t begin, std::size_t end) {
        // 评分线程只读取拓扑，并写入互不重叠的堆条目分数
        // 所有区间完成前不会修改堆结构
        for (std::size_t index = begin; index < end; ++index)
        {
            DataOrientedRoamMergeQueueEntry& entry = state.MergeQueue[index];
            entry.Score = MergeQueueScore(state, entry.Node);
        }
    };
    if (workerCount <= 1U)
    {
        refreshRange(0U, entryCount);
    }
    else
    {
        const std::size_t chunkSize = (entryCount + workerCount - 1U) / workerCount;
        RunDataOrientedRoamWorkers(state, workerCount, [&](std::size_t workerIndex) {
            const std::size_t begin = workerIndex * chunkSize;
            const std::size_t end = std::min(begin + chunkSize, entryCount);
            if (begin < end)
            {
                refreshRange(begin, end);
            }
        });
    }

    // 必须等待全部并行评分完成后才能原地建堆
    for (std::size_t index = state.MergeQueue.size() / 2U; index > 0U; --index)
    {
        SiftMergeQueueDown(state, index - 1U);
    }
}

// 可合并性取决于被修改节点、亲属节点、直接邻居以及第一圈节点的父节点和底边关系
// 需要检查的节点范围与 Classic 的跨帧队列一致，但这里全部使用节点下标表示关系
void AppendPersistentMergeQueueNeighborhood(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node,
    std::vector<DataOrientedRoamNodeIndex>& nodes)
{
    if (!state.IsValidNode(node))
    {
        return;
    }

    const DataOrientedRoamNodeIndex directNodes[] = {
        node,
        state.Nodes.ParentAt(node),
        state.Nodes.LeftChildAt(node),
        state.Nodes.RightChildAt(node),
        state.Nodes.BaseNeighborAt(node),
        state.Nodes.LeftNeighborAt(node),
        state.Nodes.RightNeighborAt(node),
    };
    for (DataOrientedRoamNodeIndex directNode : directNodes)
    {
        AppendIfValid(state, directNode, nodes);
    }
    for (DataOrientedRoamNodeIndex directNode : directNodes)
    {
        if (state.IsValidNode(directNode))
        {
            AppendIfValid(state, state.Nodes.ParentAt(directNode), nodes);
            AppendIfValid(state, state.Nodes.BaseNeighborAt(directNode), nodes);
        }
    }
}

void AppendPersistentMergeQueueNeighborhood(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node,
    DataOrientedRoamNeighborhood& nodes)
{
    if (!state.IsValidNode(node))
    {
        return;
    }

    const DataOrientedRoamNodeIndex directNodes[] = {
        node,
        state.Nodes.ParentAt(node),
        state.Nodes.LeftChildAt(node),
        state.Nodes.RightChildAt(node),
        state.Nodes.BaseNeighborAt(node),
        state.Nodes.LeftNeighborAt(node),
        state.Nodes.RightNeighborAt(node),
    };
    for (DataOrientedRoamNodeIndex directNode : directNodes)
    {
        AppendIfValid(state, directNode, nodes);
    }
    for (DataOrientedRoamNodeIndex directNode : directNodes)
    {
        if (state.IsValidNode(directNode))
        {
            AppendIfValid(state, state.Nodes.ParentAt(directNode), nodes);
            AppendIfValid(state, state.Nodes.BaseNeighborAt(directNode), nodes);
        }
    }
}

// 在主线程修改拓扑或启动其他线程前，先移除可能受影响的 Q_m 条目
// 这样堆中不会继续保留即将变化的菱形
void InvalidatePersistentMergeQueueNeighborhood(
    DataOrientedRoamState& state,
    const std::vector<DataOrientedRoamNodeIndex>& nodes)
{
    for (DataOrientedRoamNodeIndex node : nodes)
    {
        RemovePersistentMergeQueueCandidate(state, node);
    }
}

void InvalidatePersistentMergeQueueNeighborhood(
    DataOrientedRoamState& state,
    const DataOrientedRoamNeighborhood& nodes)
{
    for (DataOrientedRoamNodeIndex node : nodes)
    {
        RemovePersistentMergeQueueCandidate(state, node);
    }
}

// 活动叶和内部节点索引更新完成后再刷新邻域
// 新变得可合并的节点会立即进入 Q_m，使合并能在同一次更新内继续向父级传播
void RefreshPersistentMergeQueueNeighborhood(
    DataOrientedRoamState& state,
    const std::vector<DataOrientedRoamNodeIndex>& nodes)
{
    for (DataOrientedRoamNodeIndex node : nodes)
    {
        InsertMergeQueueNodeIfEligible(state, node);
    }
}

void RefreshPersistentMergeQueueNeighborhood(
    DataOrientedRoamState& state,
    const DataOrientedRoamNeighborhood& nodes)
{
    for (DataOrientedRoamNodeIndex node : nodes)
    {
        InsertMergeQueueNodeIfEligible(state, node);
    }
}

// 菱形两侧都会解析到同一代表节点
// 带反向位置的末尾换位删除保持 O(log M)，并同时清除双方关联
void RemovePersistentMergeQueueCandidate(
    DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node)
{
    // 传入节点可以是代表节点或菱形另一侧，反向关联会先解析到同一堆位置
    // 一次删除即可同时清理双方状态
    if (!state.IsValidNode(node) || node >= state.NodeMembership.size())
    {
        return;
    }
    const DataOrientedRoamNodeIndex representative =
        state.NodeMembership[node].MergeQueueRepresentative;
    if (!state.IsValidNode(representative))
    {
        return;
    }

    const DataOrientedRoamNodeIndex partner =
        state.NodeMembership[representative].MergeQueuePartner;
    const std::size_t position = state.NodeMembership[representative].MergeQueuePosition;
    if (position != InvalidQueuePosition && position < state.MergeQueue.size())
    {
        // 将末尾条目移入空位，再修复其堆关系
        const std::size_t last = state.MergeQueue.size() - 1U;
        if (position != last)
        {
            SwapMergeQueueEntries(state, position, last);
        }
        state.MergeQueue.pop_back();
        RestoreMergeQueueAt(state, position);
        ++state.Stats.QueueMembershipUpdateCount;
    }

    state.NodeMembership[representative].MergeQueuePosition = InvalidQueuePosition;
    state.NodeMembership[representative].MergeQueueRepresentative =
        InvalidDataOrientedRoamNodeIndex;
    state.NodeMembership[representative].MergeQueuePartner =
        InvalidDataOrientedRoamNodeIndex;
    if (state.IsValidNode(partner))
    {
        state.NodeMembership[partner].MergeQueueRepresentative =
            InvalidDataOrientedRoamNodeIndex;
    }
}

// 处理循环只读取堆顶，修改拓扑的代码负责删除或重新排列受影响条目
DataOrientedRoamNodeIndex TopPersistentMergeQueueNode(const DataOrientedRoamState& state)
{
    return state.MergeQueue.empty() ? InvalidDataOrientedRoamNodeIndex : state.MergeQueue.front().Node;
}

// 空队列返回无穷大的合并损失，使处理循环自然停止
float TopPersistentMergeQueueScore(const DataOrientedRoamState& state)
{
    return state.MergeQueue.empty() ? std::numeric_limits<float>::max() : state.MergeQueue.front().Score;
}

// 多个线程按分块处理时需要一份不会再变化的候选副本
// 筛选 Q_m 的复杂度为 O(M)，且不会重复判断拓扑条件或计算屏幕误差
void SnapshotPersistentMergeQueueCandidates(
    const DataOrientedRoamState& state,
    float maximumScore,
    std::vector<DataOrientedRoamMergeCandidate>& candidates)
{
    // 候选副本中每个菱形只有一个代表节点，不同分块不会分别收到同一菱形的两侧
    // 调用方仍负责排序，使这套筛选规则也能用于直接处理当前堆的路径
    candidates.clear();
    candidates.reserve(state.MergeQueue.size());
    for (const DataOrientedRoamMergeQueueEntry& entry : state.MergeQueue)
    {
        if (entry.Score <= maximumScore)
        {
            candidates.push_back(DataOrientedRoamMergeCandidate{entry.Score, entry.Node});
        }
    }
}

std::size_t CountPersistentQueueInvariantViolations(const DataOrientedRoamState& state)
{
    // 诊断过程保持只读，在此处修复堆会掩盖拓扑修改未正确维护队列的问题
    // 该检查只在启用拓扑验证时运行，默认性能测试不会执行
    std::size_t violations = 0U;
    const std::size_t nodeCount = state.Nodes.size();
    if (state.NodeMembership.size() != nodeCount ||
        state.SplitQueueBlockedBuildIds.size() != nodeCount)
    {
        // 反向表长度不匹配时，后续位置检查都不再安全
        return 1U;
    }

    if (state.SplitQueue.size() != state.ActiveLeafNodes.size())
    {
        ++violations;
    }

    // Q_s 必须满足最大堆顺序，每个条目都要能通过反向位置查回自身
    // 活动叶成员是否完整由验证器通过独立根遍历检查
    for (std::size_t index = 0U; index < state.SplitQueue.size(); ++index)
    {
        const DataOrientedRoamSplitQueueEntry& entry = state.SplitQueue[index];
        const DataOrientedRoamNodeIndex node = entry.Node;
        if (!state.IsValidNode(node) ||
            state.NodeMembership[node].SplitQueuePosition != index ||
            !IsActiveLeafNode(state, node))
        {
            ++violations;
            continue;
        }
        // 按 Q_s 最大堆规则，子条目不能排在父条目前面
        const std::size_t left = index * 2U + 1U;
        const std::size_t right = left + 1U;
        if (left < state.SplitQueue.size() &&
            SplitEntryPrecedes(state, state.SplitQueue[left], entry))
        {
            ++violations;
        }
        if (right < state.SplitQueue.size() &&
            SplitEntryPrecedes(state, state.SplitQueue[right], entry))
        {
            ++violations;
        }
    }

    // 每个活动叶节点必须在独立 Q_s 中恰好出现一次
    for (DataOrientedRoamNodeIndex node : state.ActiveLeafNodes)
    {
        const std::size_t position = state.NodeMembership[node].SplitQueuePosition;
        if (position == InvalidQueuePosition || position >= state.SplitQueue.size() ||
            state.SplitQueue[position].Node != node)
        {
            ++violations;
        }
    }

    // Q_m 为每个菱形只保存一个代表节点
    // 两侧父节点都指回该代表，但只有代表节点持有堆下标
    for (std::size_t index = 0U; index < state.MergeQueue.size(); ++index)
    {
        const DataOrientedRoamNodeIndex node = state.MergeQueue[index].Node;
        if (!state.IsValidNode(node) ||
            state.NodeMembership[node].MergeQueuePosition != index ||
            state.NodeMembership[node].MergeQueueRepresentative != node ||
            CanonicalMergeQueueNode(state, node) != node)
        {
            ++violations;
            continue;
        }

        // PathId 用于确认该条目确实是互指菱形中稳定且较小的一侧
        // 代表节点不能由遍历顺序任意决定
        const DataOrientedRoamNodeIndex partner = state.NodeMembership[node].MergeQueuePartner;
        if (state.IsValidNode(partner) &&
            (state.NodeMembership[partner].MergeQueueRepresentative != node ||
             state.Nodes.PathIdAt(node) >= state.Nodes.PathIdAt(partner)))
        {
            ++violations;
        }

        // Q_m 使用最小堆，子条目的合并损失不能低于父条目
        // 否则全局损失最低的菱形不会位于堆顶
        const std::size_t left = index * 2U + 1U;
        const std::size_t right = left + 1U;
        if (left < state.MergeQueue.size() &&
            MergeEntryPrecedes(state, state.MergeQueue[left], state.MergeQueue[index]))
        {
            ++violations;
        }
        if (right < state.MergeQueue.size() &&
            MergeEntryPrecedes(state, state.MergeQueue[right], state.MergeQueue[index]))
        {
            ++violations;
        }
    }

    // 除了排除过期项，还要保证每个当前可合并父节点都能解析到所属菱形的唯一条目
    for (DataOrientedRoamNodeIndex node : state.ActiveInternalNodes)
    {
        // 只有可选验证阶段扫描 ActiveInternalNodes
        // 普通帧通过局部拓扑修改增量维护该关系
        const DataOrientedRoamNodeIndex representative = CanonicalMergeQueueNode(state, node);
        if (state.IsValidNode(representative) &&
            state.NodeMembership[node].MergeQueueRepresentative != representative)
        {
            ++violations;
        }
    }
    // 同一个错误修改可能触发多个检查项，因此该值不表示错误节点数量
    // 调用方只判断是否为零，零表示两个跨帧保留的队列都与活动拓扑一致
    return violations;
}
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
