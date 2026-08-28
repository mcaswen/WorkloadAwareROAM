#include "algorithms/data_oriented_roam/DataOrientedRoamParallel.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamCandidateMarking.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamMeshEmit.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamQueues.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamScoring.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamStateOps.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamTopology.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamValidation.h"
#include "tools/PerformanceTimer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
namespace
{
constexpr std::size_t MaxTopologyCommitWorkerCount = 8;

void NormalizeQueueNeighborhood(std::vector<DataOrientedRoamNodeIndex>& nodes);

// 并行候选阈值来自当前实验设置，因此同一设置可以被输入哈希和报告完整记录
// 细分与合并保留独立阈值，因为两类候选的数量分布和线程调度成本不同
std::size_t ResolveMinParallelCommitCandidateCount(
    const DataOrientedRoamState& state,
    std::string_view phase)
{
    return phase == "merge"
        ? state.Settings.PassPolicy.MergeTopologyMinParallelCandidateCount
        : state.Settings.PassPolicy.SplitTopologyMinParallelCandidateCount;
}

// 限定更新编号用于单独测量某次拓扑提交，0 表示不限制更新编号
// 限定阶段只关闭不需要测量的并行辅助入口，后续串行收敛仍会照常执行
bool PassPolicyAllowsParallelCommit(const DataOrientedRoamState& state, std::string_view phase)
{
    const std::size_t targetBuild = state.Settings.PassPolicy.ParallelTopologyTargetBuild;
    if (targetBuild != 0U && state.BuildSequence != targetBuild)
    {
        return false;
    }

    switch (state.Settings.PassPolicy.ParallelTopologyPhase)
    {
    case TerrainLodParallelTopologyPhase::Both:
        // 默认允许细分和合并分别根据各自阈值决定线程数量
        return true;
    case TerrainLodParallelTopologyPhase::SplitOnly:
        // 只测量细分时，合并候选继续交给主线程处理
        return phase == "split";
    case TerrainLodParallelTopologyPhase::MergeOnly:
        // 只测量合并时，细分候选继续交给主线程处理
        return phase == "merge";
    }
    return false;
}

/// <summary>
/// 保存单个线程修改拓扑时产生的统计，等待全部线程结束后再汇总到总结果
/// </summary>
struct TopologyCommitCounters
{
    // 普通细分与强制细分分别计数，保持原有统计含义
    std::size_t SplitCount{0};
    std::size_t ForcedSplitCount{0};
    std::size_t RejectedSplitCount{0};
    std::size_t BudgetRejectedSplitCount{0};
    // 分给线程的细分不应再触发相邻三角形的连锁细分，此计数用于发现筛选遗漏
    std::size_t ConstraintPassCount{0};
    std::size_t MergeCount{0};
};

/// <summary>
/// 记录线程成功细分的节点，供主线程随后更新活动索引和长期保留的队列
/// </summary>
struct CommittedSplit
{
    // Node 已经从叶节点变为内部节点
    DataOrientedRoamNodeIndex Node{InvalidDataOrientedRoamNodeIndex};
    // 细分前的底边邻居用于重新评估菱形对侧子节点
    DataOrientedRoamNodeIndex BaseNeighborBeforeSplit{InvalidDataOrientedRoamNodeIndex};
};

struct CommittedMerge
{
    DataOrientedRoamNodeIndex Node{InvalidDataOrientedRoamNodeIndex};
    DataOrientedRoamNodeIndex BaseNeighbor{InvalidDataOrientedRoamNodeIndex};
    bool MergedBaseNeighbor{false};
    DataOrientedRoamNodeIndex Parent{InvalidDataOrientedRoamNodeIndex};
    DataOrientedRoamNodeIndex BaseParent{InvalidDataOrientedRoamNodeIndex};
};

void ActivateInternalNode(DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    // 活动索引由主线程维护，主线程修改拓扑后立即更新，其他线程的结果在全部结束后统一更新
    // 位置不是无效值时表示节点已经登记，重复通知不会产生重复条目
    if (!state.IsValidNode(node) || node >= state.NodeMembership.size())
    {
        return;
    }

    DataOrientedRoamNodeMembership& membership = state.NodeMembership[node];
    if (membership.ActiveInternalPosition != InvalidActiveNodePosition)
    {
        return;
    }

    membership.ActiveInternalPosition = static_cast<DataOrientedRoamPosition>(
        state.ActiveInternalNodes.size());
    state.ActiveInternalNodes.push_back(node);
}

void DeactivateInternalNode(DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    // 用末尾元素填补空位可在 O(1) 时间移除，并同步修正被移动节点的反向位置
    // 活动集合顺序不表示优先级，合并候选会在后续按分数排序
    if (!state.IsValidNode(node) || node >= state.NodeMembership.size())
    {
        return;
    }

    const std::size_t position = state.NodeMembership[node].ActiveInternalPosition;
    if (position == InvalidActiveNodePosition ||
        position >= state.ActiveInternalNodes.size())
    {
        return;
    }

    const DataOrientedRoamNodeIndex movedNode = state.ActiveInternalNodes.back();
    state.ActiveInternalNodes[position] = movedNode;
    state.NodeMembership[movedNode].ActiveInternalPosition =
        static_cast<DataOrientedRoamPosition>(position);
    state.ActiveInternalNodes.pop_back();
    state.NodeMembership[node].ActiveInternalPosition = InvalidActiveNodePosition;
}

void ActivateLeafNode(DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    // 活动叶数组保持稠密但不承担堆功能，评分刷新不会改变其顺序
    if (!state.IsValidNode(node) || !state.IsLeaf(node) ||
        node >= state.NodeMembership.size())
    {
        return;
    }

    DataOrientedRoamNodeMembership& membership = state.NodeMembership[node];
    if (membership.ActiveLeafPosition == InvalidActiveNodePosition)
    {
        membership.ActiveLeafPosition = static_cast<DataOrientedRoamPosition>(
            state.ActiveLeafNodes.size());
        state.ActiveLeafNodes.push_back(node);
    }
    InsertPersistentSplitQueueNode(state, node);
}

void DeactivateLeafNode(DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    // 先从 Q_s 移除节点，再用 O(1) 末尾填洞更新独立活动叶数组
    RemovePersistentSplitQueueNode(state, node);
    if (!state.IsValidNode(node) || node >= state.NodeMembership.size())
    {
        return;
    }

    const std::size_t position = state.NodeMembership[node].ActiveLeafPosition;
    if (position == InvalidActiveNodePosition || position >= state.ActiveLeafNodes.size())
    {
        return;
    }

    const std::size_t last = state.ActiveLeafNodes.size() - 1U;
    if (position != last)
    {
        const DataOrientedRoamNodeIndex movedNode = state.ActiveLeafNodes.back();
        state.ActiveLeafNodes[position] = movedNode;
        state.NodeMembership[movedNode].ActiveLeafPosition =
            static_cast<DataOrientedRoamPosition>(position);
    }
    state.ActiveLeafNodes.pop_back();
    state.NodeMembership[node].ActiveLeafPosition = InvalidActiveNodePosition;
}

void ApplySplitIndexTransition(DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    // 细分一个叶节点时移除一个叶、增加一个内部节点和两个新叶
    // 活动叶净增一个，与串行和并行预算各消耗一个名额的含义一致
    DeactivateLeafNode(state, node);
    ActivateInternalNode(state, node);
    ActivateLeafNode(state, state.Nodes.LeftChildAt(node));
    ActivateLeafNode(state, state.Nodes.RightChildAt(node));
    // 主线程直接修改拓扑或整理其他线程的结果时都会经过这里，因此网格变化始终由主线程记录
    RecordMeshSplit(state, node);
}

void ApplyMergeIndexTransition(DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    // 合并一个父节点是细分的逆操作，两个子节点退出，父节点回到活动叶集合
    // 合并完整菱形时调用方会分别转换两侧父节点
    DeactivateInternalNode(state, node);
    DeactivateLeafNode(state, state.Nodes.LeftChildAt(node));
    DeactivateLeafNode(state, state.Nodes.RightChildAt(node));
    ActivateLeafNode(state, node);
    RecordMeshMerge(state, node);
}

DataOrientedRoamChunkId InteriorChunkIdForNode(const DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    if (!state.IsValidNode(node))
    {
        // 无效节点不能分配给任何分块
        return InvalidDataOrientedRoamChunkId;
    }

    // 节点创建时已经记录整个三角形所在的单一分块编号
    return state.Nodes.InteriorChunkIdAt(node);
}

bool NodeBelongsToChunk(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node,
    DataOrientedRoamChunkId chunkId)
{
    if (!state.IsValidNode(node))
    {
        // 无效邻居不会被写入，因此不影响不同分块同时修改拓扑
        return true;
    }

    return InteriorChunkIdForNode(state, node) == chunkId;
}

/// <summary>
/// 根据单个拓扑阶段的策略、候选规模和非空分块数量选择线程数量
/// 诊断开关或安全阈值不满足时仍会退回主线程处理
/// </summary>
std::size_t ResolveTopologyCommitWorkerCount(
    const DataOrientedRoamState& state,
    std::size_t candidateCount,
    std::size_t nonEmptyChunkCount,
    std::size_t requestedWorkerCount,
    std::string_view phase)
{
    if (!PassPolicyAllowsParallelCommit(state, phase))
    {
        return 1U;
    }

    if (candidateCount < ResolveMinParallelCommitCandidateCount(state, phase) || nonEmptyChunkCount < 2U)
    {
        // 只有一个非空分块或候选过少时，多线程节省的时间无法抵消调度成本
        return 1U;
    }

    if (requestedWorkerCount == 1U)
    {
        return 1U;
    }

    if (requestedWorkerCount == 0U)
    {
        // 自动模式使用保守上限，避免拓扑修改占用过多线程
        const unsigned int hardwareWorkerCount = std::thread::hardware_concurrency();
        requestedWorkerCount = hardwareWorkerCount == 0U ? 1U : static_cast<std::size_t>(hardwareWorkerCount);
        requestedWorkerCount = std::min(requestedWorkerCount, MaxTopologyCommitWorkerCount);
    }

    return std::clamp(requestedWorkerCount, std::size_t{1}, nonEmptyChunkCount);
}

void MergeCountersIntoStats(DataOrientedRoamState& state, const TopologyCommitCounters& counters)
{
    // 线程本地计数由主线程统一汇总，避免并发写入 Stats
    state.Stats.SplitCount += counters.SplitCount;
    state.Stats.ForcedSplitCount += counters.ForcedSplitCount;
    state.Stats.RejectedSplitCount += counters.RejectedSplitCount;
    state.Stats.BudgetRejectedSplitCount += counters.BudgetRejectedSplitCount;
    state.Stats.ConstraintPassCount += counters.ConstraintPassCount;
    state.Stats.MergeCount += counters.MergeCount;
}

/// <summary>
/// 定义主线程修改拓扑时如何使用预算、填写统计并维护活动索引
/// 所有操作都在主线程完成，无需为每次细分执行原子操作
/// 活动索引立即更新，后续队列可以直接读取最新状态
/// </summary>
struct SerialTopologyCommitPolicy
{
    static constexpr bool UpdatesSharedIndices = true;

    bool TryAcquireSplitBudget(DataOrientedRoamState& state) const
    {
        if (state.RemainingSerialSplitBudget == 0U)
        {
            RecordBudgetRejectedSplit(state);
            return false;
        }

        --state.RemainingSerialSplitBudget;
        return true;
    }

    void ReleaseSplitBudget(DataOrientedRoamState& state) const
    {
        ++state.RemainingSerialSplitBudget;
    }

    void RecordConstraintPass(DataOrientedRoamState& state) const
    {
        ++state.Stats.ConstraintPassCount;
    }

    void RecordRejectedSplit(DataOrientedRoamState& state) const
    {
        ++state.Stats.RejectedSplitCount;
    }

    void RecordBudgetRejectedSplit(DataOrientedRoamState& state) const
    {
        RecordRejectedSplit(state);
        ++state.Stats.BudgetRejectedSplitCount;
    }

    void RecordSplit(
        DataOrientedRoamState& state,
        std::uint64_t parentPathId,
        DataOrientedRoamSplitReason reason) const
    {
        state.CurrentSplitPaths.insert(parentPathId);
        ++state.Stats.SplitCount;
        if (reason != DataOrientedRoamSplitReason::Requested)
        {
            ++state.Stats.ForcedSplitCount;
        }
    }

    void RecordMerge(DataOrientedRoamState& state) const
    {
        ++state.Stats.MergeCount;
    }
};

/// <summary>
/// 定义其他线程能够执行的受限拓扑修改
/// 多个线程通过一个原子计数共享剩余预算，并且只写各自的统计，不修改共享活动索引
/// 全部线程结束后由主线程统一汇总统计、更新索引，并刷新跨帧保留队列的受影响节点
/// </summary>
struct ParallelTopologyCommitPolicy
{
    static constexpr bool UpdatesSharedIndices = false;

    explicit ParallelTopologyCommitPolicy(TopologyCommitCounters& counters)
        : Counters(counters)
    {
    }

    bool TryAcquireSplitBudget(DataOrientedRoamState& state) const
    {
        std::size_t remaining = state.RemainingParallelSplitBudget.load(std::memory_order_relaxed);
        while (remaining > 0U)
        {
            if (state.RemainingParallelSplitBudget.compare_exchange_weak(
                    remaining,
                    remaining - 1U,
                    std::memory_order_relaxed,
                    std::memory_order_relaxed))
            {
                return true;
            }
        }

        RecordBudgetRejectedSplit(state);
        return false;
    }

    void ReleaseSplitBudget(DataOrientedRoamState& state) const
    {
        state.RemainingParallelSplitBudget.fetch_add(1U, std::memory_order_relaxed);
    }

    void RecordConstraintPass(DataOrientedRoamState&) const
    {
        ++Counters.ConstraintPassCount;
    }

    void RecordRejectedSplit(DataOrientedRoamState&) const
    {
        ++Counters.RejectedSplitCount;
    }

    void RecordBudgetRejectedSplit(DataOrientedRoamState& state) const
    {
        RecordRejectedSplit(state);
        ++Counters.BudgetRejectedSplitCount;
    }

    void RecordSplit(
        DataOrientedRoamState&,
        std::uint64_t,
        DataOrientedRoamSplitReason reason) const
    {
        ++Counters.SplitCount;
        if (reason != DataOrientedRoamSplitReason::Requested)
        {
            ++Counters.ForcedSplitCount;
        }
    }

    void RecordMerge(DataOrientedRoamState&) const
    {
        ++Counters.MergeCount;
    }

    TopologyCommitCounters& Counters;
};

/// <summary>
/// 多线程处理结束后，根据最终活动叶数量恢复主线程使用的普通预算计数
/// 此处把原子计数转换为主线程使用的普通计数，之后不再访问原子字段
/// </summary>
void SynchronizeSerialSplitBudget(DataOrientedRoamState& state)
{
    const std::size_t activeLeafCount = state.ActiveLeafNodes.size();
    state.RemainingSerialSplitBudget = state.Settings.TriangleBudget > activeLeafCount
        ? state.Settings.TriangleBudget - activeLeafCount
        : 0U;
}

void ReplaceNeighborReference(
    DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex neighbor,
    DataOrientedRoamNodeIndex oldNode,
    DataOrientedRoamNodeIndex newNode)
{
    if (!state.IsValidNode(neighbor))
    {
        return;
    }

    if (state.Nodes.BaseNeighbors[neighbor] == oldNode)
    {
        // 底边仍引用旧父节点时，改为指向细分后共享该边的子节点
        state.Nodes.BaseNeighbors[neighbor] = newNode;
    }

    if (state.Nodes.LeftNeighbors[neighbor] == oldNode)
    {
        // 左边仍引用旧父节点时同步替换
        state.Nodes.LeftNeighbors[neighbor] = newNode;
    }

    if (state.Nodes.RightNeighbors[neighbor] == oldNode)
    {
        // 右边仍引用旧父节点时同步替换
        state.Nodes.RightNeighbors[neighbor] = newNode;
    }
}

void PrepareSplitNodeState(
    DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node,
    DataOrientedRoamNodeIndex leftChild,
    DataOrientedRoamNodeIndex rightChild,
    DataOrientedRoamSplitReason reason)
{
    // 父节点转为内部节点，两个复用子节点清除旧邻接后重新加入本次更新
    state.Nodes.IsSplits[node] = 1U;
    state.Nodes.SplitBuildIds[node] = state.BuildSequence;

    const auto activateChild = [&state, reason](DataOrientedRoamNodeIndex child) {
        state.Nodes.BaseNeighbors[child] = InvalidDataOrientedRoamNodeIndex;
        state.Nodes.LeftNeighbors[child] = InvalidDataOrientedRoamNodeIndex;
        state.Nodes.RightNeighbors[child] = InvalidDataOrientedRoamNodeIndex;
        state.Nodes.ActivatedBuildIds[child] = state.BuildSequence;
        state.Nodes.ActivatedByForcedSplits[child] =
            reason == DataOrientedRoamSplitReason::Requested ? 0U : 1U;
    };
    activateChild(leftChild);
    activateChild(rightChild);
}

void PrepareMergedNodeState(DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    // 父节点恢复为叶节点，并记录本次合并以供统计和调试着色
    state.Nodes.IsSplits[node] = 0U;
    state.Nodes.ActivatedBuildIds[node] = state.BuildSequence;
    state.Nodes.MergeBuildIds[node] = state.BuildSequence;
    state.Nodes.ActivatedByForcedSplits[node] = 0U;
}

void LinkSplitNeighbors(
    DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node,
    DataOrientedRoamNodeIndex baseNeighbor)
{
    if (!state.IsValidNode(node))
    {
        return;
    }

    const DataOrientedRoamNodeIndex leftChild = state.Nodes.LeftChildAt(node);
    const DataOrientedRoamNodeIndex rightChild = state.Nodes.RightChildAt(node);
    if (!state.IsValidNode(leftChild) || !state.IsValidNode(rightChild))
    {
        return;
    }

    state.Nodes.LeftNeighbors[leftChild] = rightChild;
    state.Nodes.RightNeighbors[rightChild] = leftChild;
    // 两个子节点共享本次细分产生的中线

    // 两个子节点的底边分别继承父节点的左边和右边
    // 外侧邻居仍指向旧父节点时，必须改为指向与其共享整条边的子节点
    const DataOrientedRoamNodeIndex leftNeighbor = state.Nodes.LeftNeighborAt(node);
    const DataOrientedRoamNodeIndex rightNeighbor = state.Nodes.RightNeighborAt(node);
    state.Nodes.BaseNeighbors[leftChild] = leftNeighbor;
    state.Nodes.BaseNeighbors[rightChild] = rightNeighbor;
    ReplaceNeighborReference(state, leftNeighbor, node, leftChild);
    ReplaceNeighborReference(state, rightNeighbor, node, rightChild);

    if (!state.IsValidNode(baseNeighbor) || state.IsLeaf(baseNeighbor))
    {
        // 对侧尚未细分时没有可连接的完整菱形子节点
        return;
    }

    // 底边邻居已经细分时，两侧四个子节点共同组成菱形
    const DataOrientedRoamNodeIndex baseLeftChild = state.Nodes.LeftChildAt(baseNeighbor);
    const DataOrientedRoamNodeIndex baseRightChild = state.Nodes.RightChildAt(baseNeighbor);
    state.Nodes.RightNeighbors[leftChild] = baseRightChild;
    state.Nodes.LeftNeighbors[rightChild] = baseLeftChild;
    if (state.IsValidNode(baseRightChild))
    {
        state.Nodes.LeftNeighbors[baseRightChild] = leftChild;
    }

    if (state.IsValidNode(baseLeftChild))
    {
        state.Nodes.RightNeighbors[baseLeftChild] = rightChild;
    }
}

/// <summary>
/// 普通细分、强制细分和邻接修复共用同一实现
/// CommitPolicy 在编译期决定预算来源、统计写入位置和活动索引的更新时间
/// </summary>
template <typename CommitPolicy>
bool SplitNodeImpl(
    DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node,
    DataOrientedRoamSplitReason reason,
    DataOrientedRoamNodeIndex forcedFrom,
    CommitPolicy& commitPolicy)
{
    if (!state.IsValidNode(node) || !state.IsLeaf(node))
    {
        // 内部节点已经由子节点接管后续细分决策
        return false;
    }

    if (state.Nodes.DepthAt(node) >= state.Settings.MaxDepth)
    {
        // 最大深度是硬限制，不能通过邻接约束继续向下传播
        commitPolicy.RecordRejectedSplit(state);
        return false;
    }

    // 每次叶节点细分都会净增一个活动三角形，因此要先预留名额，再沿邻接关系执行必要的强制细分
    // 这样补齐所有相邻三角形所需的连锁细分即使中途失败，也不会超过数量上限
    if (!commitPolicy.TryAcquireSplitBudget(state))
    {
        return false;
    }

    DataOrientedRoamNodeIndex baseNeighbor = state.Nodes.BaseNeighborAt(node);
    if (state.Settings.EnableLocalConstraints)
    {
        // 只有启用局部约束时，才会沿邻接关系继续强制细分
        int guard = 0;
        // 底边邻居尚未互指时，先沿邻接链细分到合法菱形
        // 否则单侧细分会使一条粗边直接连接多条细边
        while (state.IsValidNode(baseNeighbor) &&
               baseNeighbor != forcedFrom &&
               state.Nodes.BaseNeighborAt(baseNeighbor) != node &&
               guard < state.Settings.MaxDepth + 2)
        {
            commitPolicy.RecordConstraintPass(state);
            if (!SplitNodeImpl(
                    state,
                    baseNeighbor,
                    DataOrientedRoamSplitReason::ForcedByBaseNeighbor,
                    node,
                    commitPolicy))
            {
                // 无法完成邻接关系要求的全部细分时，当前细分也必须取消
                commitPolicy.ReleaseSplitBudget(state);
                return false;
            }

            baseNeighbor = state.Nodes.BaseNeighborAt(node);
            ++guard;
        }
    }

    if (state.Settings.EnableLocalConstraints &&
        state.IsValidNode(baseNeighbor) &&
        state.IsLeaf(baseNeighbor) &&
        baseNeighbor != forcedFrom)
    {
        // 对侧仍是叶节点时先细分底边邻居
        // forcedFrom 防止两个互为底边邻居的叶节点递归回跳
        commitPolicy.RecordConstraintPass(state);
        if (!SplitNodeImpl(
                state,
                baseNeighbor,
                DataOrientedRoamSplitReason::ForcedByBaseNeighbor,
                node,
                commitPolicy))
        {
            // 无法补齐对侧叶节点时不能只细分当前一侧
            commitPolicy.ReleaseSplitBudget(state);
            return false;
        }

        baseNeighbor = state.Nodes.BaseNeighborAt(node);
    }

    const std::uint64_t parentPathId = state.Nodes.PathIdAt(node);
    const DataOrientedRoamNodeIndex leftChildBefore = state.Nodes.LeftChildAt(node);
    const DataOrientedRoamNodeIndex rightChildBefore = state.Nodes.RightChildAt(node);
    if (!state.IsValidNode(leftChildBefore) || !state.IsValidNode(rightChildBefore))
    {
        // 首次细分创建子节点，合并后再次细分时复用相同子节点下标
        const TriangleDomain domain = state.Nodes.DomainAt(node);
        const int childDepth = state.Nodes.DepthAt(node) + 1;
        const TriangleDomainChildren childDomains = SplitTriangleDomain(domain);
        const std::uint8_t varianceTreeIndex = state.Nodes.VarianceTreeIndexAt(node);
        const std::size_t varianceIndex = state.Nodes.VarianceIndexAt(node);
        const DataOrientedRoamNodeIndex leftChild =
            AddNode(
                state,
                childDomains.Left,
                node,
                childDepth,
                LeftChildPathId(parentPathId),
                varianceTreeIndex,
                varianceIndex * 2U + 1U);
        const DataOrientedRoamNodeIndex rightChild =
            AddNode(
                state,
                childDomains.Right,
                node,
                childDepth,
                RightChildPathId(parentPathId),
                varianceTreeIndex,
                varianceIndex * 2U + 2U);
        state.Nodes.LeftChildren[node] = leftChild;
        state.Nodes.RightChildren[node] = rightChild;
    }

    DataOrientedRoamNeighborhood mergeQueueNeighborhood;
    if constexpr (CommitPolicy::UpdatesSharedIndices)
    {
        AppendPersistentMergeQueueNeighborhood(state, node, mergeQueueNeighborhood);
        AppendPersistentMergeQueueNeighborhood(state, baseNeighbor, mergeQueueNeighborhood);
        InvalidatePersistentMergeQueueNeighborhood(state, mergeQueueNeighborhood);
    }

    const DataOrientedRoamNodeIndex leftChild = state.Nodes.LeftChildAt(node);
    const DataOrientedRoamNodeIndex rightChild = state.Nodes.RightChildAt(node);
    // 父节点继续保留在节点池中，但退出活动叶集合
    PrepareSplitNodeState(state, node, leftChild, rightChild, reason);

    // 子节点可能来自历史合并状态，重新激活前必须清空旧邻居
    LinkSplitNeighbors(state, node, baseNeighbor);
    if constexpr (CommitPolicy::UpdatesSharedIndices)
    {
        // 其他线程修改拓扑时，活动索引要等全部线程结束后再由主线程更新
        ApplySplitIndexTransition(state, node);
        AppendPersistentMergeQueueNeighborhood(state, node, mergeQueueNeighborhood);
        AppendPersistentMergeQueueNeighborhood(state, baseNeighbor, mergeQueueNeighborhood);
        RefreshPersistentMergeQueueNeighborhood(state, mergeQueueNeighborhood);
    }
    // 主线程会立即记录细分路径，更新收尾时仍会根据最终拓扑完整重建
    commitPolicy.RecordSplit(state, parentPathId, reason);
    return true;
}

/// <summary>
/// 执行单侧父节点合并，调用时选择由主线程立即维护预算和索引，或在线程结束后统一维护
/// </summary>
template <typename CommitPolicy>
void MergeSingleNodeImpl(
    DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node,
    CommitPolicy& commitPolicy)
{
    if (!state.IsValidNode(node) ||
        !state.IsValidNode(state.Nodes.LeftChildAt(node)) ||
        !state.IsValidNode(state.Nodes.RightChildAt(node)))
    {
        return;
    }

    const DataOrientedRoamNodeIndex leftChild = state.Nodes.LeftChildAt(node);
    const DataOrientedRoamNodeIndex rightChild = state.Nodes.RightChildAt(node);
    const DataOrientedRoamNodeIndex newLeftNeighbor = state.Nodes.BaseNeighborAt(leftChild);
    const DataOrientedRoamNodeIndex newRightNeighbor = state.Nodes.BaseNeighborAt(rightChild);

    // 父节点恢复为叶节点后，外部邻居必须从停用子节点改回父节点
    ReplaceNeighborReference(state, newLeftNeighbor, leftChild, node);
    ReplaceNeighborReference(state, newRightNeighbor, rightChild, node);
    state.Nodes.LeftNeighbors[node] = newLeftNeighbor;
    state.Nodes.RightNeighbors[node] = newRightNeighbor;
    PrepareMergedNodeState(state, node);
    if constexpr (CommitPolicy::UpdatesSharedIndices)
    {
        // 父节点重新进入活动叶集合，两个子节点同时退出
        ApplyMergeIndexTransition(state, node);
    }
    // 每次父节点合并净释放一个活动叶名额，合并完整菱形时会执行两次
    commitPolicy.ReleaseSplitBudget(state);
    commitPolicy.RecordMerge(state);
}

/// <summary>
/// 统一处理单侧和完整菱形合并，串行与并行入口共用相同拓扑规则
/// </summary>
template <typename CommitPolicy>
bool MergeNodeOrDiamondWithScoreLimitImpl(
    DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node,
    float maximumScore,
    CommitPolicy& commitPolicy)
{
    if (!CanMergeNode(state, node, maximumScore))
    {
        return false;
    }

    const DataOrientedRoamNodeIndex baseNeighbor = state.Nodes.BaseNeighborAt(node);
    DataOrientedRoamNeighborhood mergeQueueNeighborhood;
    if constexpr (CommitPolicy::UpdatesSharedIndices)
    {
        AppendPersistentMergeQueueNeighborhood(state, node, mergeQueueNeighborhood);
        AppendPersistentMergeQueueNeighborhood(state, baseNeighbor, mergeQueueNeighborhood);
        InvalidatePersistentMergeQueueNeighborhood(state, mergeQueueNeighborhood);
    }

    if (state.IsValidNode(baseNeighbor) && !state.IsLeaf(baseNeighbor))
    {
        // 完整菱形必须同时合并两侧父节点
        // 只合并一侧会使对侧细子边直接连接粗边
        if (state.Nodes.BaseNeighborAt(baseNeighbor) != node)
        {
            return false;
        }

        state.Nodes.BaseNeighbors[node] = baseNeighbor;
        state.Nodes.BaseNeighbors[baseNeighbor] = node;
        // MergeSingleNode 不修改底边邻居，因此需要在合并前显式恢复双方互指
        MergeSingleNodeImpl(state, node, commitPolicy);
        MergeSingleNodeImpl(state, baseNeighbor, commitPolicy);
        state.Nodes.BaseNeighbors[node] = baseNeighbor;
        state.Nodes.BaseNeighbors[baseNeighbor] = node;
        if constexpr (CommitPolicy::UpdatesSharedIndices)
        {
            AppendPersistentMergeQueueNeighborhood(state, node, mergeQueueNeighborhood);
            AppendPersistentMergeQueueNeighborhood(state, baseNeighbor, mergeQueueNeighborhood);
            RefreshPersistentMergeQueueNeighborhood(state, mergeQueueNeighborhood);
        }
        return true;
    }

    MergeSingleNodeImpl(state, node, commitPolicy);
    if constexpr (CommitPolicy::UpdatesSharedIndices)
    {
        AppendPersistentMergeQueueNeighborhood(state, node, mergeQueueNeighborhood);
        RefreshPersistentMergeQueueNeighborhood(state, mergeQueueNeighborhood);
    }
    return true;
}

template <typename CommitPolicy>
bool MergeNodeOrDiamondImpl(
    DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node,
    CommitPolicy& commitPolicy)
{
    return MergeNodeOrDiamondWithScoreLimitImpl(
        state,
        node,
        state.Settings.MergeThreshold,
        commitPolicy);
}

bool SplitNodeSerial(
    DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node,
    DataOrientedRoamSplitReason reason,
    DataOrientedRoamNodeIndex forcedFrom)
{
    SerialTopologyCommitPolicy commitPolicy;
    return SplitNodeImpl(state, node, reason, forcedFrom, commitPolicy);
}

bool SplitNodeParallel(
    DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node,
    DataOrientedRoamSplitReason reason,
    DataOrientedRoamNodeIndex forcedFrom,
    TopologyCommitCounters& counters)
{
    ParallelTopologyCommitPolicy commitPolicy{counters};
    return SplitNodeImpl(state, node, reason, forcedFrom, commitPolicy);
}

bool MergeNodeOrDiamondSerialWithScoreLimit(
    DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node,
    float maximumScore)
{
    SerialTopologyCommitPolicy commitPolicy;
    return MergeNodeOrDiamondWithScoreLimitImpl(state, node, maximumScore, commitPolicy);
}

bool MergeNodeOrDiamondSerial(
    DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node)
{
    SerialTopologyCommitPolicy commitPolicy;
    return MergeNodeOrDiamondImpl(state, node, commitPolicy);
}

bool MergeNodeOrDiamondParallel(
    DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node,
    TopologyCommitCounters& counters)
{
    ParallelTopologyCommitPolicy commitPolicy{counters};
    return MergeNodeOrDiamondImpl(state, node, commitPolicy);
}

bool HasReusableChildren(const DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    // 当前并行细分不扩展节点池，只处理已有可复用子节点的候选
    return state.IsValidNode(node) &&
           state.IsValidNode(state.Nodes.LeftChildAt(node)) &&
           state.IsValidNode(state.Nodes.RightChildAt(node));
}

bool SplitWouldNeedForcedNeighbor(const DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    if (!state.Settings.EnableLocalConstraints)
    {
        // 关闭局部约束时不会递归细分底边邻居
        return false;
    }

    const DataOrientedRoamNodeIndex baseNeighbor = state.Nodes.BaseNeighborAt(node);
    if (!state.IsValidNode(baseNeighbor))
    {
        // 地形外边界没有对侧三角形，可以独立细分
        return false;
    }

    if (state.IsLeaf(baseNeighbor))
    {
        // 底边邻居仍为叶节点时需要连锁细分，必须交给主线程处理
        return true;
    }

    // 尚未形成底边互指的邻接链需要递归修复，也必须交给主线程处理
    return state.Nodes.BaseNeighborAt(baseNeighbor) != node;
}

DataOrientedRoamChunkId SafeInteriorSplitChunkId(const DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    if (!state.IsValidNode(node) ||
        !state.IsLeaf(node) ||
        state.Nodes.DepthAt(node) >= state.Settings.MaxDepth ||
        !HasReusableChildren(state, node) ||
        SplitWouldNeedForcedNeighbor(state, node))
    {
        // 任一安全条件不满足时都保留在原有串行细分队列中
        return InvalidDataOrientedRoamChunkId;
    }

    const DataOrientedRoamChunkId chunkId = InteriorChunkIdForNode(state, node);
    if (chunkId == InvalidDataOrientedRoamChunkId)
    {
        // 跨分块三角形可能与其他任务修改同一邻居，因此必须交给主线程处理
        return InvalidDataOrientedRoamChunkId;
    }

    const DataOrientedRoamNodeIndex leftChild = state.Nodes.LeftChildAt(node);
    const DataOrientedRoamNodeIndex rightChild = state.Nodes.RightChildAt(node);
    const DataOrientedRoamNodeIndex baseNeighbor = state.Nodes.BaseNeighborAt(node);
    // 细分会修改父节点、两个子节点以及左右外侧邻居
    if (!NodeBelongsToChunk(state, leftChild, chunkId) ||
        !NodeBelongsToChunk(state, rightChild, chunkId) ||
        !NodeBelongsToChunk(state, state.Nodes.LeftNeighborAt(node), chunkId) ||
        !NodeBelongsToChunk(state, state.Nodes.RightNeighborAt(node), chunkId))
    {
        return InvalidDataOrientedRoamChunkId;
    }

    if (state.IsValidNode(baseNeighbor) && !state.IsLeaf(baseNeighbor))
    {
        // 菱形对侧已细分时还会修改对侧子节点的邻接关系
        if (!NodeBelongsToChunk(state, baseNeighbor, chunkId) ||
            !NodeBelongsToChunk(state, state.Nodes.LeftChildAt(baseNeighbor), chunkId) ||
            !NodeBelongsToChunk(state, state.Nodes.RightChildAt(baseNeighbor), chunkId))
        {
            return InvalidDataOrientedRoamChunkId;
        }
    }

    return chunkId;
}

bool HasMergeReadyChildren(const DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    if (!state.IsValidNode(node) || state.IsLeaf(node))
    {
        // 合并只作用于当前活动内部节点
        return false;
    }

    const DataOrientedRoamNodeIndex leftChild = state.Nodes.LeftChildAt(node);
    const DataOrientedRoamNodeIndex rightChild = state.Nodes.RightChildAt(node);
    // 按分块归类时只检查拓扑形状，真正修改前还会重新确认屏幕误差
    return state.IsValidNode(leftChild) &&
           state.IsValidNode(rightChild) &&
           state.IsLeaf(leftChild) &&
           state.IsLeaf(rightChild);
}

bool HasMergeReadyDiamond(const DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    const DataOrientedRoamNodeIndex baseNeighbor = state.Nodes.BaseNeighborAt(node);
    if (!state.IsValidNode(baseNeighbor) || state.IsLeaf(baseNeighbor))
    {
        // 没有对侧内部菱形时可以单独合并当前父节点
        return true;
    }

    // 对侧菱形也必须由两个可合并叶节点组成
    return state.Nodes.BaseNeighborAt(baseNeighbor) == node && HasMergeReadyChildren(state, baseNeighbor);
}

DataOrientedRoamChunkId SafeInteriorMergeChunkId(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node,
    bool validateMergeScore)
{
    if (!HasMergeReadyChildren(state, node) || !HasMergeReadyDiamond(state, node))
    {
        // 合并所需的拓扑条件不满足时，不加入任何待处理队列
        return InvalidDataOrientedRoamChunkId;
    }

    if (validateMergeScore && !CanMergeNode(state, node))
    {
        // 线程真正修改拓扑前再次检查整个菱形的合并分数
        return InvalidDataOrientedRoamChunkId;
    }

    const DataOrientedRoamChunkId chunkId = InteriorChunkIdForNode(state, node);
    if (chunkId == InvalidDataOrientedRoamChunkId)
    {
        // 父节点自身跨越分块时不能并行合并
        return InvalidDataOrientedRoamChunkId;
    }

    const DataOrientedRoamNodeIndex leftChild = state.Nodes.LeftChildAt(node);
    const DataOrientedRoamNodeIndex rightChild = state.Nodes.RightChildAt(node);
    // MergeSingleNode 会修改两个子节点底边邻居指向的外侧节点
    if (!NodeBelongsToChunk(state, leftChild, chunkId) ||
        !NodeBelongsToChunk(state, rightChild, chunkId) ||
        !NodeBelongsToChunk(state, state.Nodes.BaseNeighborAt(leftChild), chunkId) ||
        !NodeBelongsToChunk(state, state.Nodes.BaseNeighborAt(rightChild), chunkId))
    {
        return InvalidDataOrientedRoamChunkId;
    }

    const DataOrientedRoamNodeIndex baseNeighbor = state.Nodes.BaseNeighborAt(node);
    if (state.IsValidNode(baseNeighbor) && !state.IsLeaf(baseNeighbor))
    {
        // 合并完整菱形时还会同时修改底边邻居一侧
        if (!NodeBelongsToChunk(state, baseNeighbor, chunkId) ||
            !NodeBelongsToChunk(state, state.Nodes.LeftChildAt(baseNeighbor), chunkId) ||
            !NodeBelongsToChunk(state, state.Nodes.RightChildAt(baseNeighbor), chunkId) ||
            !NodeBelongsToChunk(
                state,
                state.Nodes.BaseNeighborAt(state.Nodes.LeftChildAt(baseNeighbor)),
                chunkId) ||
            !NodeBelongsToChunk(
                state,
                state.Nodes.BaseNeighborAt(state.Nodes.RightChildAt(baseNeighbor)),
                chunkId))
        {
            return InvalidDataOrientedRoamChunkId;
        }
    }

    return chunkId;
}

std::vector<std::vector<DataOrientedRoamSplitCandidate>> BuildInteriorSplitChunks(
    DataOrientedRoamState& state,
    const std::vector<DataOrientedRoamSplitCandidate>& candidates)
{
    // 先恢复原优先队列顺序，再筛选能够由单个线程独立修改的候选
    std::vector<DataOrientedRoamSplitCandidate> sortedCandidates = candidates;
    std::sort(
        sortedCandidates.begin(),
        sortedCandidates.end(),
        [](const DataOrientedRoamSplitCandidate& left, const DataOrientedRoamSplitCandidate& right) {
            if (left.Score == right.Score)
            {
                return left.Sequence < right.Sequence;
            }

            return left.Score > right.Score;
        });

    std::vector<std::vector<DataOrientedRoamSplitCandidate>> chunks(
        static_cast<std::size_t>(
            DataOrientedRoamTopologyChunkGridSize * DataOrientedRoamTopologyChunkGridSize));
    const std::size_t earlyCommitBudget = state.RemainingSerialSplitBudget;
    std::size_t scheduledInteriorCount = 0U;
    for (const DataOrientedRoamSplitCandidate& candidate : sortedCandidates)
    {
        const DataOrientedRoamChunkId chunkId = SafeInteriorSplitChunkId(state, candidate.Node);
        if (chunkId == InvalidDataOrientedRoamChunkId)
        {
            // 边界候选保留给串行队列处理
            ++state.Stats.BoundarySplitCandidateCount;
            continue;
        }

        ++state.Stats.InteriorSplitCandidateCount;
        if (scheduledInteriorCount >= earlyCommitBudget)
        {
            // 超出剩余预算的安全内部候选仍留在长期队列，由串行收敛继续比较
            continue;
        }

        // 先按全局优先级截取预算内候选，再用分块下标分配给独立线程
        chunks[chunkId].push_back(candidate);
        ++scheduledInteriorCount;
    }

    return chunks;
}

std::vector<std::vector<DataOrientedRoamMergeCandidate>> BuildInteriorMergeChunks(
    DataOrientedRoamState& state,
    const std::vector<DataOrientedRoamMergeCandidate>& candidates)
{
    std::vector<std::vector<DataOrientedRoamMergeCandidate>> chunks(
        static_cast<std::size_t>(
            DataOrientedRoamTopologyChunkGridSize * DataOrientedRoamTopologyChunkGridSize));

    // 合并不依赖细分队列，全部安全内部候选都可以先分配到各分块
    for (const DataOrientedRoamMergeCandidate& candidate : candidates)
    {
        // 合并候选已经按分数排序，分块内继续保留该顺序
        const DataOrientedRoamChunkId chunkId = SafeInteriorMergeChunkId(state, candidate.Node, false);
        if (chunkId == InvalidDataOrientedRoamChunkId)
        {
            // 跨分块菱形仍交给主线程顺序处理
            ++state.Stats.BoundaryMergeCandidateCount;
            continue;
        }

        // 同一分块内的候选由同一个线程按顺序处理
        chunks[chunkId].push_back(candidate);
        ++state.Stats.InteriorMergeCandidateCount;
    }

    return chunks;
}

std::size_t CountNonEmptyChunks(const auto& chunks)
{
    // 空分块不需要建立任务
    // 这里只统计真正含有安全内部候选的分块
    // 调度器据此限制实际线程数量
    std::size_t nonEmptyChunkCount = 0U;
    for (const auto& chunk : chunks)
    {
        if (!chunk.empty())
        {
            // 非空分块数量决定最多可以并行执行多少个独立任务
            ++nonEmptyChunkCount;
        }
    }

    return nonEmptyChunkCount;
}

std::size_t CountChunkCandidates(const auto& chunks)
{
    // 分块建立后需要区分原始候选和提前提交候选
    // 原始候选还包含边界项和预算外项目
    // 这里返回的只是进入安全提前提交集合的数量
    std::size_t candidateCount = 0U;
    for (const auto& chunk : chunks)
    {
        // 此处只统计完全位于单个分块内的候选，不包含交给主线程的跨分块候选
        candidateCount += chunk.size();
    }

    return candidateCount;
}

void NormalizeQueueNeighborhood(std::vector<DataOrientedRoamNodeIndex>& nodes)
{
    // 多个拓扑修改可能触及同一个队列成员
    // 主线程刷新前先排序去重
    // 这样不会重复删除或重新插入同一节点
    std::sort(nodes.begin(), nodes.end());
    nodes.erase(std::unique(nodes.begin(), nodes.end()), nodes.end());
}

std::vector<DataOrientedRoamSplitCandidate> FlattenSplitChunks(
    const std::vector<std::vector<DataOrientedRoamSplitCandidate>>& chunks)
{
    // 串行配对不能沿用分块遍历顺序
    // 先恢复冻结快照中的全局优先级顺序
    // 同分项继续使用稳定序号决定先后
    std::vector<DataOrientedRoamSplitCandidate> candidates;
    candidates.reserve(CountChunkCandidates(chunks));
    for (const auto& chunk : chunks)
    {
        candidates.insert(candidates.end(), chunk.begin(), chunk.end());
    }
    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const DataOrientedRoamSplitCandidate& left,
           const DataOrientedRoamSplitCandidate& right) {
            return left.Score == right.Score
                ? left.Sequence < right.Sequence
                : left.Score > right.Score;
        });
    return candidates;
}

std::vector<DataOrientedRoamMergeCandidate> FlattenMergeChunks(
    const DataOrientedRoamState& state,
    const std::vector<std::vector<DataOrientedRoamMergeCandidate>>& chunks)
{
    // 合并路径同样需要从分块集合恢复全局顺序
    // 低分项优先回收以减少细节损失
    // 同分项使用稳定路径编号消除容器顺序影响
    std::vector<DataOrientedRoamMergeCandidate> candidates;
    candidates.reserve(CountChunkCandidates(chunks));
    for (const auto& chunk : chunks)
    {
        candidates.insert(candidates.end(), chunk.begin(), chunk.end());
    }
    std::sort(
        candidates.begin(),
        candidates.end(),
        [&state](const DataOrientedRoamMergeCandidate& left,
                 const DataOrientedRoamMergeCandidate& right) {
            return left.Score == right.Score
                ? state.Nodes.PathIdAt(left.Node) < state.Nodes.PathIdAt(right.Node)
                : left.Score < right.Score;
        });
    return candidates;
}

std::size_t CommitInteriorSplitChunksSerial(
    DataOrientedRoamState& state,
    const std::vector<std::vector<DataOrientedRoamSplitCandidate>>& chunks)
{
    // 串行回放只消费与并行路径相同的安全内部集合
    // 每次提交仍调用正式的串行拓扑事务
    // 动态失效项不强行修改，随后由长期队列继续收敛
    const std::size_t candidateCount = CountChunkCandidates(chunks);
    state.Stats.SplitTopologyCandidateCount = candidateCount;
    state.Stats.SplitTopologyNonEmptyChunkCount = CountNonEmptyChunks(chunks);
    state.Stats.SplitTopologyCommitWorkerCount = candidateCount == 0U ? 0U : 1U;
    state.Stats.TopologyCommitWorkerCount = std::max(
        state.Stats.TopologyCommitWorkerCount,
        state.Stats.SplitTopologyCommitWorkerCount);

    const std::size_t splitCountBefore = state.Stats.SplitCount;
    Tools::PerformanceTimer commitTimer;
    for (const DataOrientedRoamSplitCandidate& candidate : FlattenSplitChunks(chunks))
    {
        if (SafeInteriorSplitChunkId(state, candidate.Node) == InvalidDataOrientedRoamChunkId)
        {
            // 前面的提交可能改变同一分块中的邻接关系，失败候选留给串行收敛
            continue;
        }
        SplitNodeSerial(
            state,
            candidate.Node,
            DataOrientedRoamSplitReason::Requested,
            InvalidDataOrientedRoamNodeIndex);
    }
    state.Stats.SplitTopologySerialCommitMilliseconds += commitTimer.Stop();
    return state.Stats.SplitCount - splitCountBefore;
}

std::size_t CommitInteriorMergeChunksSerial(
    DataOrientedRoamState& state,
    const std::vector<std::vector<DataOrientedRoamMergeCandidate>>& chunks)
{
    // 串行合并回放沿用与并行路径相同的资格筛选
    // 不创建线程任务，也不经过线程本地统计
    // 返回值表示进入普通收敛前已经完成的合并数量
    const std::size_t candidateCount = CountChunkCandidates(chunks);
    state.Stats.MergeTopologyCandidateCount = candidateCount;
    state.Stats.MergeTopologyNonEmptyChunkCount = CountNonEmptyChunks(chunks);
    state.Stats.MergeTopologyCommitWorkerCount = candidateCount == 0U ? 0U : 1U;
    state.Stats.TopologyCommitWorkerCount = std::max(
        state.Stats.TopologyCommitWorkerCount,
        state.Stats.MergeTopologyCommitWorkerCount);

    const std::size_t mergeCountBefore = state.Stats.MergeCount;
    Tools::PerformanceTimer commitTimer;
    for (const DataOrientedRoamMergeCandidate& candidate : FlattenMergeChunks(state, chunks))
    {
        if (SafeInteriorMergeChunkId(state, candidate.Node, true) ==
            InvalidDataOrientedRoamChunkId)
        {
            // 动态失效候选仍保留在长期队列中，由后续串行收敛重新判断
            continue;
        }
        MergeNodeOrDiamondSerial(state, candidate.Node);
    }
    state.Stats.MergeTopologySerialCommitMilliseconds += commitTimer.Stop();
    return state.Stats.MergeCount - mergeCountBefore;
}

std::vector<CommittedSplit> CommitInteriorSplitChunks(
    DataOrientedRoamState& state,
    std::vector<std::vector<DataOrientedRoamSplitCandidate>>& chunks)
{
    std::vector<CommittedSplit> committedSplits;
    const std::size_t nonEmptyChunkCount = CountNonEmptyChunks(chunks);
    const std::size_t candidateCount = CountChunkCandidates(chunks);
    const std::size_t workerCount = ResolveTopologyCommitWorkerCount(
        state,
        candidateCount,
        nonEmptyChunkCount,
        state.Settings.PassPolicy.SplitTopologyWorkerCount,
        "split");
    const std::size_t minimumCandidateCount = ResolveMinParallelCommitCandidateCount(state, "split");
    state.Stats.TopologyCommitMinCandidateCount = minimumCandidateCount;
    state.Stats.SplitTopologyCommitMinCandidateCount = minimumCandidateCount;
    state.Stats.SplitTopologyCandidateCount = candidateCount;
    state.Stats.SplitTopologyNonEmptyChunkCount = nonEmptyChunkCount;
    state.Stats.SplitTopologyCommitWorkerCount = workerCount;
    state.Stats.TopologyCommitWorkerCount = std::max(state.Stats.TopologyCommitWorkerCount, workerCount);

    if (workerCount <= 1U)
    {
        // 线程不足时不提前处理细分，主线程仍按原来的队列顺序执行
        return committedSplits;
    }

    std::vector<TopologyCommitCounters> localCounters(workerCount);
    std::vector<std::vector<CommittedSplit>> localCommittedSplits(workerCount);
    // 暂时移除受影响队列成员和线程修改拓扑分别计时，避免把主线程工作误算成并行收益
    Tools::PerformanceTimer queueInvalidationTimer;
    std::vector<DataOrientedRoamNodeIndex> mergeQueueNeighborhood;
    for (const std::vector<DataOrientedRoamSplitCandidate>& chunk : chunks)
    {
        for (const DataOrientedRoamSplitCandidate& candidate : chunk)
        {
            AppendPersistentMergeQueueNeighborhood(state, candidate.Node, mergeQueueNeighborhood);
        }
    }
    NormalizeQueueNeighborhood(mergeQueueNeighborhood);
    InvalidatePersistentMergeQueueNeighborhood(state, mergeQueueNeighborhood);
    state.Stats.SplitTopologyQueueInvalidationMilliseconds += queueInvalidationTimer.Stop();

    Tools::PerformanceTimer parallelCommitTimer;
    RunDataOrientedRoamWorkers(state, workerCount, [&](std::size_t workerIndex) {
        // 每个分块只由一个线程访问
        for (std::size_t chunkIndex = workerIndex; chunkIndex < chunks.size(); chunkIndex += workerCount)
        {
            for (const DataOrientedRoamSplitCandidate& candidate : chunks[chunkIndex])
            {
                const DataOrientedRoamNodeIndex node = candidate.Node;
                // 同一分块内前面的细分可能改变邻接关系，因此要重新确认该候选不会与其他线程冲突
                const DataOrientedRoamChunkId chunkId = SafeInteriorSplitChunkId(state, node);
                if (chunkId != chunkIndex)
                {
                    // 前面的细分可能使后续候选不再适合由当前线程独立处理
                    continue;
                }

                const DataOrientedRoamNodeIndex baseNeighborBeforeSplit = state.Nodes.BaseNeighborAt(node);
                // 并行细分只接受无需分配新节点的安全候选
                if (SplitNodeParallel(
                        state,
                        node,
                        DataOrientedRoamSplitReason::Requested,
                        InvalidDataOrientedRoamNodeIndex,
                        localCounters[workerIndex]))
                {
                    // 子节点由主线程重新加入 Q_s，使后续串行阶段仍可继续细分
                    localCommittedSplits[workerIndex].push_back(CommittedSplit{node, baseNeighborBeforeSplit});
                }
            }
        }
    });
    state.Stats.SplitTopologyParallelCommitMilliseconds += parallelCommitTimer.Stop();

    // 其他线程已经全部结束，此后的计时只表示主线程整理结果的成本
    Tools::PerformanceTimer resultMergeTimer;
    std::size_t totalCommittedCount = 0U;
    for (const TopologyCommitCounters& counters : localCounters)
    {
        // 所有全局统计都在主线程汇总，避免数据竞争
        MergeCountersIntoStats(state, counters);
        totalCommittedCount += counters.SplitCount;
    }

    for (const std::vector<CommittedSplit>& localSplits : localCommittedSplits)
    {
    // 主线程整理结果的顺序只影响同分候选的先后编号，不影响最终拓扑
        committedSplits.insert(committedSplits.end(), localSplits.begin(), localSplits.end());
    }
    state.Stats.SplitTopologyResultMergeMilliseconds += resultMergeTimer.Stop();

    Tools::PerformanceTimer indexQueueRefreshTimer;
    for (const CommittedSplit& split : committedSplits)
    {
        // 每个线程只修改自己分块内的 SoA 拓扑，结束后由主线程统一更新两个共享活动索引
        ApplySplitIndexTransition(state, split.Node);
        AppendPersistentMergeQueueNeighborhood(state, split.Node, mergeQueueNeighborhood);
        AppendPersistentMergeQueueNeighborhood(
            state,
            split.BaseNeighborBeforeSplit,
            mergeQueueNeighborhood);
    }
    NormalizeQueueNeighborhood(mergeQueueNeighborhood);
    RefreshPersistentMergeQueueNeighborhood(state, mergeQueueNeighborhood);
    state.Stats.SplitTopologyIndexQueueRefreshMilliseconds += indexQueueRefreshTimer.Stop();

    state.Stats.ParallelSplitCommitCount += totalCommittedCount;
    return committedSplits;
}

std::vector<CommittedMerge> CommitInteriorMergeChunks(
    DataOrientedRoamState& state,
    std::vector<std::vector<DataOrientedRoamMergeCandidate>>& chunks)
{
    std::vector<CommittedMerge> committedMerges;
    const std::size_t nonEmptyChunkCount = CountNonEmptyChunks(chunks);
    const std::size_t candidateCount = CountChunkCandidates(chunks);
    const std::size_t workerCount = ResolveTopologyCommitWorkerCount(
        state,
        candidateCount,
        nonEmptyChunkCount,
        state.Settings.PassPolicy.MergeTopologyWorkerCount,
        "merge");
    const std::size_t minimumCandidateCount = ResolveMinParallelCommitCandidateCount(state, "merge");
    state.Stats.TopologyCommitMinCandidateCount = minimumCandidateCount;
    state.Stats.MergeTopologyCommitMinCandidateCount = minimumCandidateCount;
    state.Stats.MergeTopologyCandidateCount = candidateCount;
    state.Stats.MergeTopologyNonEmptyChunkCount = nonEmptyChunkCount;
    state.Stats.MergeTopologyCommitWorkerCount = workerCount;
    state.Stats.TopologyCommitWorkerCount = std::max(state.Stats.TopologyCommitWorkerCount, workerCount);

    if (workerCount <= 1U)
    {
        // 合并候选过少时直接由主线程按原顺序处理
        return committedMerges;
    }

    std::vector<TopologyCommitCounters> localCounters(workerCount);
    std::vector<std::vector<CommittedMerge>> localCommittedMerges(workerCount);
    // 合并与细分使用相同的六项耗时统计，报告可以直接比较
    Tools::PerformanceTimer queueInvalidationTimer;
    std::vector<DataOrientedRoamNodeIndex> mergeQueueNeighborhood;
    for (const std::vector<DataOrientedRoamMergeCandidate>& chunk : chunks)
    {
        for (const DataOrientedRoamMergeCandidate& candidate : chunk)
        {
            AppendPersistentMergeQueueNeighborhood(state, candidate.Node, mergeQueueNeighborhood);
        }
    }
    NormalizeQueueNeighborhood(mergeQueueNeighborhood);
    InvalidatePersistentMergeQueueNeighborhood(state, mergeQueueNeighborhood);
    state.Stats.MergeTopologyQueueInvalidationMilliseconds += queueInvalidationTimer.Stop();

    Tools::PerformanceTimer parallelCommitTimer;
    RunDataOrientedRoamWorkers(state, workerCount, [&](std::size_t workerIndex) {
        // 每个分块只交给一个线程，保证不同线程不会修改同一组邻居
        for (std::size_t chunkIndex = workerIndex; chunkIndex < chunks.size(); chunkIndex += workerCount)
        {
            for (const DataOrientedRoamMergeCandidate& candidate : chunks[chunkIndex])
            {
                const DataOrientedRoamNodeIndex node = candidate.Node;
                const DataOrientedRoamChunkId chunkId = SafeInteriorMergeChunkId(state, node, true);
                if (chunkId != chunkIndex)
                {
                    // 同一分块内的前序合并可能已经改变菱形结构
                    continue;
                }

                // 真正修改拓扑时仍调用统一的菱形合并逻辑
                const DataOrientedRoamNodeIndex baseNeighbor = state.Nodes.BaseNeighborAt(node);
                const bool mergedBaseNeighbor = state.IsValidNode(baseNeighbor) && !state.IsLeaf(baseNeighbor);
                const DataOrientedRoamNodeIndex parent = state.Nodes.ParentAt(node);
                const DataOrientedRoamNodeIndex baseParent = state.IsValidNode(baseNeighbor)
                    ? state.Nodes.ParentAt(baseNeighbor)
                    : InvalidDataOrientedRoamNodeIndex;
                if (MergeNodeOrDiamondParallel(state, node, localCounters[workerIndex]))
                {
                    localCommittedMerges[workerIndex].push_back(
                        CommittedMerge{node, baseNeighbor, mergedBaseNeighbor, parent, baseParent});
                }
            }
        }
    });
    state.Stats.MergeTopologyParallelCommitMilliseconds += parallelCommitTimer.Stop();

    Tools::PerformanceTimer resultMergeTimer;
    std::size_t totalCommittedCount = 0U;
    for (const TopologyCommitCounters& counters : localCounters)
    {
        // 合并成功次数由各线程本地计数后统一汇总
        MergeCountersIntoStats(state, counters);
        totalCommittedCount += counters.MergeCount;
    }

    state.Stats.ParallelMergeCommitCount += totalCommittedCount;
    for (const std::vector<CommittedMerge>& localMerges : localCommittedMerges)
    {
        committedMerges.insert(committedMerges.end(), localMerges.begin(), localMerges.end());
    }
    state.Stats.MergeTopologyResultMergeMilliseconds += resultMergeTimer.Stop();

    Tools::PerformanceTimer indexQueueRefreshTimer;
    for (const CommittedMerge& merge : committedMerges)
    {
        // Node 一定已经合并，BaseNeighbor 只有在完整菱形合并时才同时转换
        ApplyMergeIndexTransition(state, merge.Node);
        if (merge.MergedBaseNeighbor &&
            state.IsValidNode(merge.BaseNeighbor) &&
            merge.BaseNeighbor != merge.Node)
        {
            ApplyMergeIndexTransition(state, merge.BaseNeighbor);
        }
        AppendPersistentMergeQueueNeighborhood(state, merge.Node, mergeQueueNeighborhood);
        AppendPersistentMergeQueueNeighborhood(state, merge.BaseNeighbor, mergeQueueNeighborhood);
        AppendPersistentMergeQueueNeighborhood(state, merge.Parent, mergeQueueNeighborhood);
        AppendPersistentMergeQueueNeighborhood(state, merge.BaseParent, mergeQueueNeighborhood);
    }
    NormalizeQueueNeighborhood(mergeQueueNeighborhood);
    RefreshPersistentMergeQueueNeighborhood(state, mergeQueueNeighborhood);
    state.Stats.MergeTopologyIndexQueueRefreshMilliseconds += indexQueueRefreshTimer.Stop();

    return committedMerges;
}

void RunSplitSerialConvergence(DataOrientedRoamState& state)
{
    // 提前提交只负责不会相互冲突的内部候选
    // 预算交换、强制闭合和全部边界项仍在这里按队首顺序完成
    // 正式执行与两条冻结回放路径共同使用这一收尾过程
    state.Stats.CandidatePeakCount = std::max(
        state.Stats.CandidatePeakCount,
        state.ActiveLeafNodes.size() + state.MergeQueue.size());

    const std::size_t maximumIterations = std::max<std::size_t>(
        1024U,
        state.Settings.TriangleBudget * 8U + state.Nodes.size() * 4U);
    std::size_t iteration = 0U;
    float crossoverMergeMilliseconds = 0.0F;
    const auto mergeDuringSplitConvergence =
        [&state, &crossoverMergeMilliseconds](DataOrientedRoamNodeIndex node) {
            Tools::PerformanceTimer mergeTimer;
            const bool merged = MergeNodeOrDiamondSerialWithScoreLimit(
                state,
                node,
                std::numeric_limits<float>::max());
            const float elapsedMilliseconds = mergeTimer.Stop();
            crossoverMergeMilliseconds += elapsedMilliseconds;
            state.Stats.MergeCrossoverMilliseconds += elapsedMilliseconds;
            state.Stats.MergeTopologySerialConvergenceMilliseconds += elapsedMilliseconds;
            return merged;
        };

    // 提前提交结束后仍由主线程按全局队首顺序完成预算交换和强制闭合
    Tools::PerformanceTimer serialConvergenceTimer;
    while (iteration++ < maximumIterations)
    {
        DataOrientedRoamNodeIndex mergeNode = TopPersistentMergeQueueNode(state);
        float mergeScore = TopPersistentMergeQueueScore(state);
        if (state.IsValidNode(mergeNode) && mergeScore < state.Settings.MergeThreshold)
        {
            if (!mergeDuringSplitConvergence(mergeNode))
            {
                RemovePersistentMergeQueueCandidate(state, mergeNode);
                ++state.Stats.RejectedMergeCount;
            }
            continue;
        }

        const DataOrientedRoamNodeIndex splitNode = TopPersistentSplitQueueNode(state);
        const float splitScore = TopPersistentSplitQueueScore(state);
        if (!state.IsValidNode(splitNode) ||
            !ShouldSplitWithScore(state, splitNode, splitScore))
        {
            break;
        }

        const std::size_t budgetRejectedBefore = state.Stats.BudgetRejectedSplitCount;
        if (SplitNodeSerial(
                state,
                splitNode,
                DataOrientedRoamSplitReason::Requested,
                InvalidDataOrientedRoamNodeIndex))
        {
            continue;
        }

        const bool closureNeedsBudget =
            state.Stats.BudgetRejectedSplitCount > budgetRejectedBefore;
        mergeNode = TopPersistentMergeQueueNode(state);
        mergeScore = TopPersistentMergeQueueScore(state);
        if (closureNeedsBudget && state.IsValidNode(mergeNode) && splitScore > mergeScore)
        {
            if (mergeDuringSplitConvergence(mergeNode))
            {
                ++state.Stats.QueueCrossoverCount;
                continue;
            }
            RemovePersistentMergeQueueCandidate(state, mergeNode);
            ++state.Stats.RejectedMergeCount;
            continue;
        }

        if (closureNeedsBudget)
        {
            break;
        }

        // 非预算失败在本次更新内不应反复占据队首
        BlockPersistentSplitQueueNodeForCurrentBuild(state, splitNode);
    }
    const float totalConvergenceMilliseconds = serialConvergenceTimer.Stop();
    state.Stats.SplitTopologySerialConvergenceMilliseconds += std::max(
        0.0F,
        totalConvergenceMilliseconds - crossoverMergeMilliseconds);
}

void RunMergeSerialConvergence(DataOrientedRoamState& state)
{
    // 提前合并结束后重新读取长期合并队列
    // 动态失效项会被移除，仍满足阈值的项目继续顺序提交
    // 循环结束代表当前合并队首已经收敛
    Tools::PerformanceTimer serialConvergenceTimer;
    while (TopPersistentMergeQueueScore(state) <= state.Settings.MergeThreshold)
    {
        const DataOrientedRoamNodeIndex node = TopPersistentMergeQueueNode(state);
        if (!state.IsValidNode(node))
        {
            break;
        }
        if (!MergeNodeOrDiamondSerial(state, node))
        {
            RemovePersistentMergeQueueCandidate(state, node);
            ++state.Stats.RejectedMergeCount;
        }
    }
    state.Stats.MergeTopologySerialConvergenceMilliseconds += serialConvergenceTimer.Stop();
}

std::uint64_t HashFrozenSplitCandidates(
    const DataOrientedRoamState& state,
    const std::vector<DataOrientedRoamSplitCandidate>& candidates)
{
    // 候选哈希证明两条回放消费的是同一份冻结输入
    // 节点下标可能受历史分配影响，因此改用稳定路径编号
    // 分数和稳定序号也进入哈希以保留完整排序语义
    std::vector<DataOrientedRoamSplitCandidate> ordered = candidates;
    std::sort(
        ordered.begin(),
        ordered.end(),
        [](const DataOrientedRoamSplitCandidate& left,
           const DataOrientedRoamSplitCandidate& right) {
            return left.Score == right.Score
                ? left.Sequence < right.Sequence
                : left.Score > right.Score;
        });
    std::uint64_t hash = TerrainLodHashOffset;
    AppendTerrainLodHash(hash, ordered.size());
    for (const DataOrientedRoamSplitCandidate& candidate : ordered)
    {
        AppendTerrainLodHash(hash, state.Nodes.PathIdAt(candidate.Node));
        AppendTerrainLodHash(hash, candidate.Score);
        AppendTerrainLodHash(hash, candidate.Sequence);
    }
    return hash;
}

std::uint64_t HashFrozenMergeCandidates(
    const DataOrientedRoamState& state,
    const std::vector<DataOrientedRoamMergeCandidate>& candidates)
{
    // 合并候选按照低分优先的规则规范化
    // 同分项目由路径编号稳定排序
    // 哈希既描述成员集合也描述用于裁决的分数
    std::vector<DataOrientedRoamMergeCandidate> ordered = candidates;
    std::sort(
        ordered.begin(),
        ordered.end(),
        [&state](const DataOrientedRoamMergeCandidate& left,
                 const DataOrientedRoamMergeCandidate& right) {
            return left.Score == right.Score
                ? state.Nodes.PathIdAt(left.Node) < state.Nodes.PathIdAt(right.Node)
                : left.Score < right.Score;
        });
    std::uint64_t hash = TerrainLodHashOffset;
    AppendTerrainLodHash(hash, ordered.size());
    for (const DataOrientedRoamMergeCandidate& candidate : ordered)
    {
        AppendTerrainLodHash(hash, state.Nodes.PathIdAt(candidate.Node));
        AppendTerrainLodHash(hash, candidate.Score);
    }
    return hash;
}

std::uint64_t HashCurrentTopology(const DataOrientedRoamState& state)
{
    // 当前拓扑由所有已经展开的父节点唯一描述
    // 遍历节点池不会依赖活动内部数组的排列顺序
    // 最终再按路径编号规范化以便比较两条执行方式
    std::vector<std::uint64_t> pathIds;
    pathIds.reserve(state.ActiveInternalNodes.size());
    for (DataOrientedRoamNodeIndex node = 0U;
         node < static_cast<DataOrientedRoamNodeIndex>(state.Nodes.size());
         ++node)
    {
        if (state.Nodes.IsSplitAt(node))
        {
            pathIds.push_back(state.Nodes.PathIdAt(node));
        }
    }
    return HashTerrainLodPathIds(std::move(pathIds));
}

std::uint64_t HashActiveLeaves(const DataOrientedRoamState& state)
{
    // 活动叶数组允许通过末尾填洞改变内部顺序
    // 研究结果关心的是活动切分集合
    // 因此只对稳定路径编号排序后计算哈希
    std::vector<std::uint64_t> pathIds;
    pathIds.reserve(state.ActiveLeafNodes.size());
    for (const DataOrientedRoamNodeIndex node : state.ActiveLeafNodes)
    {
        pathIds.push_back(state.Nodes.PathIdAt(node));
    }
    return HashTerrainLodPathIds(std::move(pathIds));
}

std::uint64_t HashQueueMembership(const DataOrientedRoamState& state)
{
    // 长期队列的堆排列不属于算法结果
    // 这里只比较两个队列各自包含哪些稳定路径
    // 队列不变量由独立检查继续验证
    std::vector<std::uint64_t> splitPaths;
    splitPaths.reserve(state.SplitQueue.size());
    for (const DataOrientedRoamSplitQueueEntry& entry : state.SplitQueue)
    {
        splitPaths.push_back(state.Nodes.PathIdAt(entry.Node));
    }
    std::sort(splitPaths.begin(), splitPaths.end());

    std::vector<std::uint64_t> mergePaths;
    mergePaths.reserve(state.MergeQueue.size());
    for (const DataOrientedRoamMergeQueueEntry& entry : state.MergeQueue)
    {
        mergePaths.push_back(state.Nodes.PathIdAt(entry.Node));
    }
    std::sort(mergePaths.begin(), mergePaths.end());

    std::uint64_t hash = TerrainLodHashOffset;
    AppendTerrainLodHash(hash, splitPaths.size());
    for (const std::uint64_t pathId : splitPaths)
    {
        AppendTerrainLodHash(hash, pathId);
    }
    AppendTerrainLodHash(hash, mergePaths.size());
    for (const std::uint64_t pathId : mergePaths)
    {
        AppendTerrainLodHash(hash, pathId);
    }
    return hash;
}

std::uint64_t HashMeshEdits(const DataOrientedRoamState& state)
{
    // 不同合法提交顺序可能产生不同的编辑记录顺序
    // 网格结果只要求相同节点发生相同类型的修改
    // 排序后的类型和路径组合提供规范化比较依据
    std::vector<std::pair<std::uint8_t, std::uint64_t>> edits;
    edits.reserve(state.IncrementalMesh.TopologyEdits.size());
    for (const DataOrientedRoamMeshTopologyEdit& edit : state.IncrementalMesh.TopologyEdits)
    {
        edits.emplace_back(
            static_cast<std::uint8_t>(edit.Type),
            state.Nodes.PathIdAt(edit.Node));
    }
    std::sort(edits.begin(), edits.end());

    std::uint64_t hash = TerrainLodHashOffset;
    AppendTerrainLodHash(hash, edits.size());
    for (const auto& [type, pathId] : edits)
    {
        AppendTerrainLodHash(hash, type);
        AppendTerrainLodHash(hash, pathId);
    }
    return hash;
}

struct FrozenTopologyExecutionSummary
{
    // 提前提交数量用于确认并行辅助路径是否真正做了工作
    std::size_t EarlyCommitCount{0U};
    // 总耗时只包住回放本身，不包含状态复制
    float WallMilliseconds{0.0F};
};

void PrepareFrozenReplayState(DataOrientedRoamState& state)
{
    // 回放副本保留冻结时的拓扑、队列和预算
    // 清空本帧统计与编辑记录，避免把正式路径的旧数据带入结果
    // 回放内部关闭再次配对，防止递归复制状态
    state.Stats = {};
    state.IncrementalMesh.TopologyEdits.clear();
    state.IncrementalMesh.TracksTopologyEdits = true;
    state.Settings.EnableTopologyPairEvidence = false;
    state.Settings.EnableTopologyValidation = true;
}

FrozenTopologyExecutionSummary ExecuteFrozenSplitTopology(
    DataOrientedRoamState& state,
    const std::vector<DataOrientedRoamSplitCandidate>& candidates,
    bool parallel)
{
    // 两种执行方式都从调用方提供的同一候选快照开始
    // 唯一区别是安全内部集合由主线程还是多个线程提前提交
    // 提前阶段结束后共同进入原有串行收敛流程
    PrepareFrozenReplayState(state);
    state.Settings.PassPolicy.SplitTopology = parallel
        ? TerrainLodTopologyAction::ParallelAssisted
        : TerrainLodTopologyAction::SerialImmediate;
    state.Settings.PassPolicy.ParallelTopologyTargetBuild = 0U;
    state.Settings.PassPolicy.ParallelTopologyPhase = TerrainLodParallelTopologyPhase::Both;
    state.Settings.PassPolicy.SplitTopologyMinParallelCandidateCount = 0U;
    if (parallel)
    {
        // 配对实验需要尝试可用的最大安全线程数量
        // 最终实际数量仍受非空分块数量限制
        state.Settings.PassPolicy.SplitTopologyWorkerCount = MaxTopologyCommitWorkerCount;
    }
    state.RemainingParallelSplitBudget.store(
        state.RemainingSerialSplitBudget,
        std::memory_order_relaxed);

    Tools::PerformanceTimer wallTimer;
    Tools::PerformanceTimer chunkBuildTimer;
    std::vector<std::vector<DataOrientedRoamSplitCandidate>> chunks =
        BuildInteriorSplitChunks(state, candidates);
    state.Stats.SplitTopologyChunkBuildMilliseconds = chunkBuildTimer.Stop();

    FrozenTopologyExecutionSummary summary{};
    if (parallel)
    {
        // 并行统计来自线程本地结果在主线程上的汇总
        CommitInteriorSplitChunks(state, chunks);
        summary.EarlyCommitCount = state.Stats.ParallelSplitCommitCount;
    }
    else
    {
        // 串行路径跳过线程任务但保留相同候选资格
        summary.EarlyCommitCount = CommitInteriorSplitChunksSerial(state, chunks);
    }
    SynchronizeSerialSplitBudget(state);
    RunSplitSerialConvergence(state);
    summary.WallMilliseconds = wallTimer.Stop();
    return summary;
}

FrozenTopologyExecutionSummary ExecuteFrozenMergeTopology(
    DataOrientedRoamState& state,
    const std::vector<DataOrientedRoamMergeCandidate>& candidates,
    bool parallel)
{
    // 合并回放与细分回放使用相同的实验结构
    // 候选筛选、普通事务和最终收敛规则都不随执行方式改变
    // 这使分项耗时和结果证据能够直接配对
    PrepareFrozenReplayState(state);
    state.Settings.PassPolicy.MergeTopology = parallel
        ? TerrainLodTopologyAction::ParallelAssisted
        : TerrainLodTopologyAction::SerialImmediate;
    state.Settings.PassPolicy.ParallelTopologyTargetBuild = 0U;
    state.Settings.PassPolicy.ParallelTopologyPhase = TerrainLodParallelTopologyPhase::Both;
    state.Settings.PassPolicy.MergeTopologyMinParallelCandidateCount = 0U;
    if (parallel)
    {
        // 最大请求值只表达实验意图
        // 安全分块不足时实现仍会退回较少线程
        state.Settings.PassPolicy.MergeTopologyWorkerCount = MaxTopologyCommitWorkerCount;
    }

    Tools::PerformanceTimer wallTimer;
    Tools::PerformanceTimer chunkBuildTimer;
    std::vector<std::vector<DataOrientedRoamMergeCandidate>> chunks =
        BuildInteriorMergeChunks(state, candidates);
    state.Stats.MergeTopologyChunkBuildMilliseconds = chunkBuildTimer.Stop();

    FrozenTopologyExecutionSummary summary{};
    if (parallel)
    {
        CommitInteriorMergeChunks(state, chunks);
        summary.EarlyCommitCount = state.Stats.ParallelMergeCommitCount;
    }
    else
    {
        summary.EarlyCommitCount = CommitInteriorMergeChunksSerial(state, chunks);
    }
    RunMergeSerialConvergence(state);
    summary.WallMilliseconds = wallTimer.Stop();
    return summary;
}

TerrainLodTopologyReplayEvidence CollectFrozenReplayEvidence(
    DataOrientedRoamState& state,
    bool splitPhase,
    bool parallel,
    const FrozenTopologyExecutionSummary& summary,
    float cloneMilliseconds)
{
    // 验证在副本内执行，不会改变正式算法状态
    // 结果同时保存规范化哈希、不变量和各子阶段耗时
    // 状态复制成本单列，避免被误当成拓扑实现自身成本
    ValidateTopology(state);

    TerrainLodTopologyReplayEvidence evidence{};
    evidence.Action = parallel
        ? TerrainLodPassAction::ParallelAssisted
        : TerrainLodPassAction::SerialImmediate;
    evidence.TopologyHash = HashCurrentTopology(state);
    evidence.ActiveLeafHash = HashActiveLeaves(state);
    evidence.QueueMembershipHash = HashQueueMembership(state);
    evidence.MeshEditHash = HashMeshEdits(state);
    evidence.ActiveTriangleCount = state.ActiveLeafNodes.size();
    evidence.InteriorCandidateCount = splitPhase
        ? state.Stats.InteriorSplitCandidateCount
        : state.Stats.InteriorMergeCandidateCount;
    evidence.BoundaryCandidateCount = splitPhase
        ? state.Stats.BoundarySplitCandidateCount
        : state.Stats.BoundaryMergeCandidateCount;
    evidence.EffectiveWorkerCount = splitPhase
        ? state.Stats.SplitTopologyCommitWorkerCount
        : state.Stats.MergeTopologyCommitWorkerCount;
    evidence.EarlyCommitCount = summary.EarlyCommitCount;
    evidence.BudgetViolationCount = state.ActiveLeafNodes.size() > state.Settings.TriangleBudget
        ? 1U
        : 0U;
    evidence.QueueInvariantViolationCount = CountPersistentQueueInvariantViolations(state);
    evidence.TjunctionCount = state.Stats.TjunctionCount;
    evidence.InvalidNeighborCount = state.Stats.InvalidNeighborCount;
    evidence.InvalidTopologyCount = state.Stats.InvalidTopologyCount;
    evidence.StateCloneMilliseconds = cloneMilliseconds;
    evidence.ChunkBuildMilliseconds = splitPhase
        ? state.Stats.SplitTopologyChunkBuildMilliseconds
        : state.Stats.MergeTopologyChunkBuildMilliseconds;
    evidence.QueueInvalidationMilliseconds = splitPhase
        ? state.Stats.SplitTopologyQueueInvalidationMilliseconds
        : state.Stats.MergeTopologyQueueInvalidationMilliseconds;
    evidence.CommitMilliseconds = parallel
        ? (splitPhase
            ? state.Stats.SplitTopologyParallelCommitMilliseconds
            : state.Stats.MergeTopologyParallelCommitMilliseconds)
        : (splitPhase
            ? state.Stats.SplitTopologySerialCommitMilliseconds
            : state.Stats.MergeTopologySerialCommitMilliseconds);
    evidence.ResultMergeMilliseconds = splitPhase
        ? state.Stats.SplitTopologyResultMergeMilliseconds
        : state.Stats.MergeTopologyResultMergeMilliseconds;
    evidence.IndexQueueRefreshMilliseconds = splitPhase
        ? state.Stats.SplitTopologyIndexQueueRefreshMilliseconds
        : state.Stats.MergeTopologyIndexQueueRefreshMilliseconds;
    evidence.SerialConvergenceMilliseconds = splitPhase
        ? state.Stats.SplitTopologySerialConvergenceMilliseconds
        : state.Stats.MergeTopologySerialConvergenceMilliseconds;
    evidence.WallMilliseconds = summary.WallMilliseconds;
    return evidence;
}

bool HasEquivalentFrozenTopologyResults(const TerrainLodTopologyPairEvidence& evidence)
{
    // 三角形数量相同不足以证明拓扑等价
    // 这里同时比较展开父节点、活动叶、队列成员和网格编辑集合
    // 任一路径出现预算外的结构错误都会使配对失败
    const TerrainLodTopologyReplayEvidence& serial = evidence.Serial;
    const TerrainLodTopologyReplayEvidence& parallel = evidence.Parallel;
    return serial.TopologyHash == parallel.TopologyHash &&
        serial.ActiveLeafHash == parallel.ActiveLeafHash &&
        serial.QueueMembershipHash == parallel.QueueMembershipHash &&
        serial.MeshEditHash == parallel.MeshEditHash &&
        serial.ActiveTriangleCount == parallel.ActiveTriangleCount &&
        serial.InteriorCandidateCount == parallel.InteriorCandidateCount &&
        serial.BoundaryCandidateCount == parallel.BoundaryCandidateCount &&
        serial.InteriorCandidateCount + serial.BoundaryCandidateCount ==
            evidence.FrozenCandidateCount &&
        parallel.InteriorCandidateCount + parallel.BoundaryCandidateCount ==
            evidence.FrozenCandidateCount &&
        serial.BudgetViolationCount == 0U &&
        parallel.BudgetViolationCount == 0U &&
        serial.QueueInvariantViolationCount == 0U &&
        parallel.QueueInvariantViolationCount == 0U &&
        serial.TjunctionCount == 0U &&
        parallel.TjunctionCount == 0U &&
        serial.InvalidNeighborCount == 0U &&
        parallel.InvalidNeighborCount == 0U &&
        serial.InvalidTopologyCount == 0U &&
        parallel.InvalidTopologyCount == 0U;
}

TerrainLodTopologyPairEvidence ReplayFrozenSplitTopologyPair(
    const DataOrientedRoamState& source,
    const std::vector<DataOrientedRoamSplitCandidate>& candidates,
    float snapshotMilliseconds)
{
    // 来源状态只读，串行与并行辅助分别使用自己的深复制副本
    // 两个副本接收完全相同的候选数组
    // 配对结果不会替换或推进正式算法拓扑
    TerrainLodTopologyPairEvidence evidence{};
    evidence.Evaluated = true;
    evidence.FrozenCandidateHash = HashFrozenSplitCandidates(source, candidates);
    evidence.FrozenCandidateCount = candidates.size();
    evidence.CandidateSnapshotMilliseconds = snapshotMilliseconds;
    Tools::PerformanceTimer evidenceTimer;

    {
        // 复制计时在回放总耗时之外单独保存
        Tools::PerformanceTimer cloneTimer;
        DataOrientedRoamState serialState{source};
        const float cloneMilliseconds = cloneTimer.Stop();
        const FrozenTopologyExecutionSummary summary = ExecuteFrozenSplitTopology(
            serialState,
            candidates,
            false);
        evidence.Serial = CollectFrozenReplayEvidence(
            serialState,
            true,
            false,
            summary,
            cloneMilliseconds);
    }
    {
        // 第二个副本从同一来源创建，不继承串行回放结果
        Tools::PerformanceTimer cloneTimer;
        DataOrientedRoamState parallelState{source};
        const float cloneMilliseconds = cloneTimer.Stop();
        const FrozenTopologyExecutionSummary summary = ExecuteFrozenSplitTopology(
            parallelState,
            candidates,
            true);
        evidence.Parallel = CollectFrozenReplayEvidence(
            parallelState,
            true,
            true,
            summary,
            cloneMilliseconds);
    }

    evidence.Equivalent = HasEquivalentFrozenTopologyResults(evidence);
    evidence.EvidenceMilliseconds = evidenceTimer.Stop();
    return evidence;
}

TerrainLodTopologyPairEvidence ReplayFrozenMergeTopologyPair(
    const DataOrientedRoamState& source,
    const std::vector<DataOrientedRoamMergeCandidate>& candidates,
    float snapshotMilliseconds)
{
    // 合并配对沿用细分配对的隔离方式
    // 来源状态、候选成员和候选顺序在两次回放之间保持不变
    // 最终只把证据结构写回正式统计
    TerrainLodTopologyPairEvidence evidence{};
    evidence.Evaluated = true;
    evidence.FrozenCandidateHash = HashFrozenMergeCandidates(source, candidates);
    evidence.FrozenCandidateCount = candidates.size();
    evidence.CandidateSnapshotMilliseconds = snapshotMilliseconds;
    Tools::PerformanceTimer evidenceTimer;

    {
        // 串行副本完全跳过其他线程
        Tools::PerformanceTimer cloneTimer;
        DataOrientedRoamState serialState{source};
        const float cloneMilliseconds = cloneTimer.Stop();
        const FrozenTopologyExecutionSummary summary = ExecuteFrozenMergeTopology(
            serialState,
            candidates,
            false);
        evidence.Serial = CollectFrozenReplayEvidence(
            serialState,
            false,
            false,
            summary,
            cloneMilliseconds);
    }
    {
        // 并行副本只让安全内部集合提前提交
        Tools::PerformanceTimer cloneTimer;
        DataOrientedRoamState parallelState{source};
        const float cloneMilliseconds = cloneTimer.Stop();
        const FrozenTopologyExecutionSummary summary = ExecuteFrozenMergeTopology(
            parallelState,
            candidates,
            true);
        evidence.Parallel = CollectFrozenReplayEvidence(
            parallelState,
            false,
            true,
            summary,
            cloneMilliseconds);
    }

    evidence.Equivalent = HasEquivalentFrozenTopologyResults(evidence);
    evidence.EvidenceMilliseconds = evidenceTimer.Stop();
    return evidence;
}
} // 匿名命名空间

void RefineWithSplitQueue(DataOrientedRoamState& state)
{
    // 正式细分路径仍以长期细分队列作为唯一决策来源
    // 只有请求并行辅助或显式证据时才复制候选
    // 配对回放在副本中执行，随后正式状态按原策略继续
    state.Stats.TopologyChunkCount = static_cast<std::size_t>(
        DataOrientedRoamTopologyChunkGridSize * DataOrientedRoamTopologyChunkGridSize);
    RefreshPersistentSplitQueuePriorities(state);

    const bool parallelAssisted =
        state.Settings.PassPolicy.SplitTopology != TerrainLodTopologyAction::SerialImmediate;
    std::vector<DataOrientedRoamSplitCandidate> initialCandidates;
    float snapshotMilliseconds = 0.0F;
    if (parallelAssisted || state.Settings.EnableTopologyPairEvidence)
    {
        Tools::PerformanceTimer snapshotTimer;
        SnapshotPersistentSplitQueueCandidates(state, initialCandidates);
        snapshotMilliseconds = snapshotTimer.Stop();
    }

    if (state.Settings.EnableTopologyPairEvidence)
    {
        // 证据模式只用于研究回归，普通交互默认关闭
        state.Stats.SplitTopologyPair = ReplayFrozenSplitTopologyPair(
            state,
            initialCandidates,
            snapshotMilliseconds);
    }

    if (parallelAssisted)
    {
        // 正式并行路径的快照和分块成本继续进入本帧阶段统计
        state.Stats.SplitCandidateSnapshotMilliseconds += snapshotMilliseconds;
        state.Stats.SplitCandidateCount = initialCandidates.size();
        Tools::PerformanceTimer chunkBuildTimer;
        std::vector<std::vector<DataOrientedRoamSplitCandidate>> interiorChunks =
            BuildInteriorSplitChunks(state, initialCandidates);
        state.Stats.SplitTopologyChunkBuildMilliseconds += chunkBuildTimer.Stop();
        CommitInteriorSplitChunks(state, interiorChunks);
    }
    else
    {
        // 即使拓扑只由主线程修改，Q_s 的分数仍可由多个线程刷新，但不会复制、排序或划分候选
        state.Stats.SplitCandidateCount = 0U;
    }
    // 其他线程结束后只根据最终叶数量恢复一次普通预算，之后的细分和合并不再访问原子计数
    SynchronizeSerialSplitBudget(state);
    RunSplitSerialConvergence(state);
}

void MergeWithDiamondQueue(DataOrientedRoamState& state)
{
    // 正式合并路径先刷新长期合并队列的分数
    // 候选快照只在并行辅助或证据模式下建立
    // 配对完成后仍由调用方选择的正式策略修改真实状态
    state.Stats.TopologyChunkCount = static_cast<std::size_t>(
        DataOrientedRoamTopologyChunkGridSize * DataOrientedRoamTopologyChunkGridSize);
    RefreshPersistentMergeQueuePriorities(state);

    const bool parallelAssisted =
        state.Settings.PassPolicy.MergeTopology != TerrainLodTopologyAction::SerialImmediate;
    std::vector<DataOrientedRoamMergeCandidate> candidates;
    float snapshotMilliseconds = 0.0F;
    if (parallelAssisted || state.Settings.EnableTopologyPairEvidence)
    {
        Tools::PerformanceTimer snapshotTimer;
        SnapshotPersistentMergeQueueCandidates(state, state.Settings.MergeThreshold, candidates);
        snapshotMilliseconds = snapshotTimer.Stop();
        std::sort(
            candidates.begin(),
            candidates.end(),
            [&state](const DataOrientedRoamMergeCandidate& left,
                     const DataOrientedRoamMergeCandidate& right) {
                return left.Score == right.Score
                    ? state.Nodes.PathIdAt(left.Node) < state.Nodes.PathIdAt(right.Node)
                    : left.Score < right.Score;
            });
    }

    if (state.Settings.EnableTopologyPairEvidence)
    {
        // 合并证据和细分证据分别保存，避免混淆两个阶段的输入
        state.Stats.MergeTopologyPair = ReplayFrozenMergeTopologyPair(
            state,
            candidates,
            snapshotMilliseconds);
    }

    if (parallelAssisted)
    {
        // 只有正式并行辅助路径才把候选准备成本算入阶段包络
        state.Stats.MergeCandidateSnapshotMilliseconds += snapshotMilliseconds;
        state.Stats.MergeCandidateCount = candidates.size();
        Tools::PerformanceTimer chunkBuildTimer;
        std::vector<std::vector<DataOrientedRoamMergeCandidate>> interiorChunks =
            BuildInteriorMergeChunks(state, candidates);
        state.Stats.MergeTopologyChunkBuildMilliseconds += chunkBuildTimer.Stop();
        CommitInteriorMergeChunks(state, interiorChunks);
    }
    else
    {
        state.Stats.MergeCandidateCount = 0U;
    }
    RunMergeSerialConvergence(state);
}
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
