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
    // 小队列保持串行，避免线程调度成本高于分数计算本身。
    // 自动模式最多使用 8 个 worker，与 DOD 其他批量阶段采用相同的保守上限。
    // 显式 worker 数仍尊重调用方设置，便于 benchmark 隔离并行宽度的影响。
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

// ActiveInternalNodes 是当前活动切分的权威索引。merge 后历史节点仍保留在
// SoA 节点池中，因此这里只看 IsSplit 无法区分节点是否仍然活动。
bool IsActiveInternalNode(const DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    if (!state.IsValidNode(node) || node >= state.NodeMembership.size())
    {
        return false;
    }
    // membership position 是 topology commit 的权威活动标记；完整的正向/反向
    // 对照只在 validator 中执行，热路径不再额外读取 ActiveInternalNodes。
    return state.NodeMembership[node].ActiveInternalPosition != InvalidActiveNodePosition;
}

// 反向位置表让检查不依赖节点池的历史状态，也能在过期的活动索引项进入
// 持久 heap 之前将其拦截。
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
    // Q_s 保存全部活动叶节点，包括达到最大深度或本次 Build 暂时被屏蔽的节点。
    // 被屏蔽节点只会沉到 heap 尾部，不会退出队列。merge 产生的 parent
    // 在同一个 Build 中不能再次被选中。
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
    // 常规比较直接读取连续 heap entry；只有同分时才访问稳定 PathId
    if (left.Score != right.Score)
    {
        return left.Score > right.Score;
    }
    return state.Nodes.PathIdAt(left.Node) < state.Nodes.PathIdAt(right.Node);
}

void SwapSplitQueueEntries(DataOrientedRoamState& state, std::size_t left, std::size_t right)
{
    // Q_s 与活动叶视图分离，heap 交换只修正自己的反向位置
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
    // 新激活的 child 或 merge 后恢复的 parent 通过这条 O(log N) 路径入队。
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
    // 被屏蔽的队首节点或按索引删除后换入的节点通过这里向下调整。
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
    // 删除任意位置后可能需要向上或向下调整。先检查 parent，可以避免
    // 两个方向都执行一次 heap 修复。
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

// Q_m 的队列成员只由拓扑决定，相机相关阈值在消费 heap 时才参与判断。
// 因此阈值 merge 和预算重新分配可以共用同一队列，不需要因分数上限不同而重建。
bool IsMergeableTopology(const DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    // 本函数只判断当前活动拓扑是否允许 merge，不读取阈值。
    // 分离拓扑资格和视点分数后，相机移动只需刷新 heap 分数，
    // 不需要重新发现哪些 parent 属于 Q_m。
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

    // 地形边界上的 parent，以及 base neighbor 为活动叶节点的 parent，可以单独 merge。
    const DataOrientedRoamNodeIndex baseNeighbor = state.Nodes.BaseNeighborAt(node);
    if (!state.IsValidNode(baseNeighbor) || IsActiveLeafNode(state, baseNeighbor))
    {
        return !state.IsValidNode(baseNeighbor) || IsActiveLeafNode(state, baseNeighbor);
    }

    // base neighbor 为 internal 节点时，只有双方 parent 互为 base neighbor，
    // 且四个 child 都是活动叶节点，才能构成合法 diamond。
    if (!IsActiveInternalNode(state, baseNeighbor))
    {
        return false;
    }
    // 双向 base-neighbor 和四个 leaf child 共同定义可回收 diamond。
    return state.Nodes.BaseNeighborAt(baseNeighbor) == node &&
           IsActiveLeafNode(state, state.Nodes.LeftChildAt(baseNeighbor)) &&
           IsActiveLeafNode(state, state.Nodes.RightChildAt(baseNeighbor));
}

// 一个完整 diamond 只对应一次队列操作，而不是两次 parent 操作。
// 即使 SoA 分配顺序变化，也用 PathId 为双方选出稳定且唯一的代表节点。
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

// diamond 的回收损失取双方 parent 分数的最大值。本次 Build 中刚由 split
// 形成的 diamond 会被暂时屏蔽，避免立即发生反向 merge。
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

// Q_m 是最小堆。分数相同时按 PathId 稳定排序，使硬三角形预算在中途停止
// 消费队列时仍能得到确定的处理顺序。
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

// 每次交换 heap 项时都同步更新代表节点的反向位置，不保留可能随 vector
// 移动而失效的迭代器或裸指针。
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

// 局部插入复杂度为 O(log M)，M 是当前可 merge 的 diamond 数量。
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

// 原地建堆和按索引删除共用这条向下调整路径。
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

// swap-remove 后可能破坏任一方向的 heap 关系，因此根据换入节点与其 parent
// 的关系选择修复方向。
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

// 每个 diamond 只插入唯一的代表节点。sidecar representative 将双方 parent
// 都关联到同一个 heap 项，因此从任意一侧都能删除该 diamond。
void InsertMergeQueueNodeIfEligible(DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    const DataOrientedRoamNodeIndex representative = CanonicalMergeQueueNode(state, node);
    if (!state.IsValidNode(representative) ||
        state.NodeMembership[representative].MergeQueueRepresentative !=
            InvalidDataOrientedRoamNodeIndex)
    {
        return;
    }

    // base neighbor 不是互指 internal 节点时，这是单 parent merge；只有双方
    // internal parent 互为 base neighbor 时才记录 partner。
    DataOrientedRoamNodeIndex partner = state.Nodes.BaseNeighborAt(representative);
    if (!IsActiveInternalNode(state, partner) || state.Nodes.BaseNeighborAt(partner) != representative)
    {
        partner = InvalidDataOrientedRoamNodeIndex;
    }
    // 防御性清理用于避免局部过期关联让同一个 parent 同时属于两个 diamond；
    // 正常拓扑事务不应触发这条路径。
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

    // 同一个 Build 内相机参数保持不变，因此入队时直接计算分数；下一次 Build
    // 会在读取 heap 前刷新所有仍在队列中的节点。
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

// 并行批量收集保留重复项，调用方最后统一排序和去重。
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

// 串行局部邻域在插入时直接去重，避免事务结束后再排序。
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

// 拓扑 reset 根据当前活动切分重建反向位置表。初始两个 root 都是叶节点，
// 因此 Q_m 为空；这条路径也支持以后扩展其他 reset 初始形态。
void InitializePersistentSplitQueue(DataOrientedRoamState& state)
{
    // ResetTopology 先建立活动叶视图，这里据此创建独立 Q_s 和反向位置
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
    // 相机移动会使全部分数失效，但不会改变队列成员。DOD 并行评估互不重叠的
    // SoA 区间，最后用一次线性复杂度的原地建堆恢复顺序。
    std::vector<DataOrientedRoamNodeIndex> activeLeafOrderBeforeRefresh;
    if (state.Settings.EnableTopologyValidation)
    {
        // C1 将活动叶视图与 Q_s 分离；验证模式下直接锁定这条契约，
        // 防止以后误把 heap swap 再次写回 Mesh 输出顺序。
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
        // 所有评分 worker 结束前 Q_s 结构保持只读，每个 worker 只写自己的 entry score
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

    // 自底向上建堆为 O(N)，比逐个重新插入全部叶节点的 O(N log N) 更低。
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
    // Q_s 的准确成员数量同时用于计算硬预算剩余 token。
    const std::size_t activeLeafCount = state.ActiveLeafNodes.size();
    const std::size_t remainingBudget = state.Settings.TriangleBudget > activeLeafCount
        ? state.Settings.TriangleBudget - activeLeafCount
        : 0U;
    state.RemainingSerialSplitBudget = remainingBudget;
    state.RemainingParallelSplitBudget.store(remainingBudget, std::memory_order_relaxed);
}

void InsertPersistentSplitQueueNode(DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    // 调用本函数前活动叶视图已经更新，Q_s 反向位置让重复通知保持幂等
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
    // forced split 可能删除不在 heap 顶部的候选，因此 Q_s 必须支持按索引删除，
    // 不能只提供弹出队首的 priority_queue 语义。
    if (!state.IsValidNode(node) || node >= state.NodeMembership.size())
    {
        return;
    }
    const std::size_t position = state.NodeMembership[node].SplitQueuePosition;
    if (position == InvalidQueuePosition || position >= state.SplitQueue.size())
    {
        return;
    }

    // swap-remove 保持存储连续，只需修复被换入位置的节点。
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
    // 约束闭包提交失败后，该节点仍属于活动拓扑。本次 Build 将它沉到 heap
    // 尾部以避免无限重试，下一帧仍会重新评分。
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
    // heap 顶部是当前视点下全局优先级最高的活动叶节点。
    return state.SplitQueue.empty()
        ? InvalidDataOrientedRoamNodeIndex
        : state.SplitQueue.front().Node;
}

float TopPersistentSplitQueueScore(const DataOrientedRoamState& state)
{
    // Q_s 为空时返回与不可 split 叶节点相同的屏蔽分数。
    return state.SplitQueue.empty() ? BlockedSplitScore : state.SplitQueue.front().Score;
}

void SnapshotPersistentSplitQueueCandidates(
    const DataOrientedRoamState& state,
    std::vector<DataOrientedRoamSplitCandidate>& candidates)
{
    // 并行 chunk 提交读取不可变快照；所有 worker 结束后，串行约束闭包继续
    // 直接消费实时 heap。
    candidates.clear();
    candidates.reserve(state.SplitQueue.size());
    std::uint64_t sequence = 0U;
    for (const DataOrientedRoamSplitQueueEntry& entry : state.SplitQueue)
    {
        // 当前 Build 已统一刷新过分数，生成快照时不再重复计算屏幕误差。
        if (ShouldSplitWithScore(state, entry.Node, entry.Score))
        {
            candidates.push_back(DataOrientedRoamSplitCandidate{entry.Score, sequence++, entry.Node});
        }
    }
}

void InitializePersistentMergeQueue(DataOrientedRoamState& state)
{
    // reset 时允许遍历一次全部活动 internal 节点来建立初始真值。
    // 正常 Build 不再走这条全量路径，之后完全由局部拓扑事务维护。
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

// 队列成员会跨帧保留，但视点相关优先级不会。这里只刷新 M 个现有队列成员，
// 并以 O(M) 原地建堆，替代旧实现对全部活动 internal 节点的扫描和重复评分。
void RefreshPersistentMergeQueuePriorities(DataOrientedRoamState& state)
{
    const std::size_t entryCount = state.MergeQueue.size();
    const std::size_t workerCount = ResolvePriorityRefreshWorkerCount(state, entryCount);
    state.Stats.CandidateMarkWorkerCount = std::max(
        state.Stats.CandidateMarkWorkerCount,
        workerCount);
    const auto refreshRange = [&state](std::size_t begin, std::size_t end) {
        // worker 只读取拓扑，并写入互不重叠的 heap 分数项。所有区间处理结束前
        // 不修改 heap 结构。
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

    // 并行评分全部结束后才能开始原地建堆。
    for (std::size_t index = state.MergeQueue.size() / 2U; index > 0U; --index)
    {
        SiftMergeQueueDown(state, index - 1U);
    }
}

// 可合并性取决于被修改节点、亲属节点、直接邻居，以及第一圈节点的 parent/base
// 关系。这个依赖范围与 Classic 持久队列一致，但不引入指针所有权。
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

// 串行拓扑修改之前或并行 worker 启动之前，先移除受影响的队列项，避免 heap
// 继续排序已经发生变化的 diamond。
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

// 活动 leaf/internal 索引提交完成后再刷新邻域。刚变得可合并的节点会立即进入
// Q_m，使 merge 能在同一个 Build 内向父级连续传播。
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

// diamond 双方都会解析到同一个代表节点。带反向索引的 swap-remove 使任意
// 局部删除保持 O(log M)，并同时清除双方关联。
void RemovePersistentMergeQueueCandidate(
    DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node)
{
    // node 可以是代表节点，也可以是 diamond 的另一侧；反向关联会先把它们
    // 统一解析到同一个 heap 位置，确保一次删除同时清理双方状态。
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
        // 将末尾项移入空位，再修复它的 heap 关系。
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

// 消费者只读取 heap 顶部；拓扑提交负责删除或重新排列相关项。
DataOrientedRoamNodeIndex TopPersistentMergeQueueNode(const DataOrientedRoamState& state)
{
    return state.MergeQueue.empty() ? InvalidDataOrientedRoamNodeIndex : state.MergeQueue.front().Node;
}

// 空队列返回无穷大的 merge 损失，使消费循环自然停止。
float TopPersistentMergeQueueScore(const DataOrientedRoamState& state)
{
    return state.MergeQueue.empty() ? std::numeric_limits<float>::max() : state.MergeQueue.front().Score;
}

// 并行 chunk 提交仍需要不可变候选快照。过滤 Q_m 的复杂度为 O(M)，且不会
// 重复计算拓扑可合并性或屏幕误差。
void SnapshotPersistentMergeQueueCandidates(
    const DataOrientedRoamState& state,
    float maximumScore,
    std::vector<DataOrientedRoamMergeCandidate>& candidates)
{
    // 快照中每个 diamond 只有一个代表节点，因此不同 chunk 不会从这里分别
    // 收到同一个 diamond 的两侧。排序仍由调用者负责，因为阈值过滤还会服务
    // 直接消费实时 heap 的路径。
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
    // 诊断过程只读。若在这里顺便修复 heap，会掩盖没有正确维护队列的拓扑事务。
    // 该检查只在启用拓扑验证时运行，不进入默认性能测试的热路径。
    std::size_t violations = 0U;
    const std::size_t nodeCount = state.Nodes.size();
    if (state.NodeMembership.size() != nodeCount ||
        state.SplitQueueBlockedBuildIds.size() != nodeCount)
    {
        // 反向表长度不匹配时，后续所有索引检查都不再安全。
        return 1U;
    }

    if (state.SplitQueue.size() != state.ActiveLeafNodes.size())
    {
        ++violations;
    }

    // Q_s 必须满足最大堆顺序，每一项都必须能通过反向位置查回自身。
    // 活动切分是否完整由 validator 通过独立路径检查。
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
        // 按 Q_s 最大堆比较规则，child 不能排在 parent 前面。
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

    // 每个活动 leaf 必须在独立 Q_s 中恰好出现一次
    for (DataOrientedRoamNodeIndex node : state.ActiveLeafNodes)
    {
        const std::size_t position = state.NodeMembership[node].SplitQueuePosition;
        if (position == InvalidQueuePosition || position >= state.SplitQueue.size() ||
            state.SplitQueue[position].Node != node)
        {
            ++violations;
        }
    }

    // Q_m 每个 diamond 只保存唯一代表节点。双方 parent 都指回该代表节点，
    // 但只有代表节点持有 heap 索引。
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

        // PathId 顺序用于确认该项确实是互指 diamond 中稳定且较小的一侧，
        // 而不是由遍历顺序任意选出的 parent。
        const DataOrientedRoamNodeIndex partner = state.NodeMembership[node].MergeQueuePartner;
        if (state.IsValidNode(partner) &&
            (state.NodeMembership[partner].MergeQueueRepresentative != node ||
             state.Nodes.PathIdAt(node) >= state.Nodes.PathIdAt(partner)))
        {
            ++violations;
        }

        // Q_m 使用最小堆顺序：child 的 merge 损失不能低于 parent，否则全局
        // 回收损失最低的 diamond 就不会位于索引 0。
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

    // 队列完整性与排除过期项同样重要：每个当前可 merge 的活动 parent
    // 都必须解析到所属 diamond 的唯一队列项。
    for (DataOrientedRoamNodeIndex node : state.ActiveInternalNodes)
    {
        // 只有可选验证阶段可以扫描 ActiveInternalNodes；正常帧路径通过拓扑事务
        // 增量维护这项关系。
        const DataOrientedRoamNodeIndex representative = CanonicalMergeQueueNode(state, node);
        if (state.IsValidNode(representative) &&
            state.NodeMembership[node].MergeQueueRepresentative != representative)
        {
            ++violations;
        }
    }
    // 同一个错误事务可能产生多个检查症状；调用者只按是否为零判断正确性，
    // 不把该值当作错误节点数。零表示两个持久队列都与活动拓扑一致。
    return violations;
}
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
