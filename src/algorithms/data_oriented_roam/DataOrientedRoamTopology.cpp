#include "algorithms/data_oriented_roam/DataOrientedRoamParallel.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamCandidateMarking.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamMeshEmit.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamQueues.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamScoring.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamStateOps.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamTopology.h"
#include "tools/PerformanceTimer.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
namespace
{
constexpr std::size_t MaxTopologyCommitWorkerCount = 8;
// 细分候选通常较少，因此沿用原阈值；合并阈值来自相同输入下串行与并行结果的对照实验
constexpr std::size_t MinParallelSplitCommitCandidateCount = 32;
constexpr std::size_t MinParallelMergeCommitCandidateCount = 160;

void NormalizeQueueNeighborhood(std::vector<DataOrientedRoamNodeIndex>& nodes);

// 以下环境变量只用于基准测试中的独立进程配对实验
// 它们可以固定候选阈值，并将多线程拓扑处理限制到指定更新和指定阶段
// 未设置时使用上面的默认值，不会改变正常运行路径
std::string ReadDiagnosticEnvironmentVariable(const char* name)
{
#if defined(_MSC_VER)
    char* rawValue = nullptr;
    std::size_t rawValueLength = 0U;
    if (_dupenv_s(&rawValue, &rawValueLength, name) != 0 || rawValue == nullptr)
    {
        return {};
    }
    std::string value{rawValue};
    std::free(rawValue);
    return value;
#else
    const char* rawValue = std::getenv(name);
    return rawValue == nullptr ? std::string{} : std::string{rawValue};
#endif
}

std::size_t ParseDiagnosticSize(const char* name, std::size_t fallback)
{
    const std::string ownedValue = ReadDiagnosticEnvironmentVariable(name);
    const std::string_view value{ownedValue};
    if (value.empty())
    {
        return fallback;
    }

    std::size_t parsed = 0U;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    return error == std::errc{} && end == value.data() + value.size() ? parsed : fallback;
}

std::size_t ResolveMinParallelCommitCandidateCount(std::string_view phase)
{
    // 实验设置只在进程首次访问时读取，未设置或值无效时保持默认配置
    static const std::size_t splitThreshold = ParseDiagnosticSize(
        "PARALLEL_ROAM_DOD_MIN_PARALLEL_COMMIT_CANDIDATES",
        MinParallelSplitCommitCandidateCount);
    static const std::size_t mergeThreshold = ParseDiagnosticSize(
        "PARALLEL_ROAM_DOD_MIN_PARALLEL_COMMIT_CANDIDATES",
        MinParallelMergeCommitCandidateCount);
    return phase == "merge" ? mergeThreshold : splitThreshold;
}

bool DiagnosticBuildAllowsParallelCommit(const DataOrientedRoamState& state, std::string_view phase)
{
    // 更新编号为 0 时影响每次更新，非零值只改变指定更新的拓扑处理方式
    static const std::size_t targetBuild = ParseDiagnosticSize(
        "PARALLEL_ROAM_DOD_PARALLEL_COMMIT_BUILD",
        0U);
    if (targetBuild != 0U && state.BuildSequence != targetBuild)
    {
        return false;
    }

    // 阶段参数默认同时影响细分和合并，也可只启用其中一项以便单独测量
    static const std::string selectedPhase = []() {
        const std::string value = ReadDiagnosticEnvironmentVariable(
            "PARALLEL_ROAM_DOD_PARALLEL_COMMIT_PHASE");
        return value.empty() ? std::string{"both"} : value;
    }();
    return selectedPhase == "both" || selectedPhase == phase;
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
    if (!DiagnosticBuildAllowsParallelCommit(state, phase))
    {
        return 1U;
    }

    if (candidateCount < ResolveMinParallelCommitCandidateCount(phase) || nonEmptyChunkCount < 2U)
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
    for (const DataOrientedRoamSplitCandidate& candidate : sortedCandidates)
    {
        const DataOrientedRoamChunkId chunkId = SafeInteriorSplitChunkId(state, candidate.Node);
        if (chunkId == InvalidDataOrientedRoamChunkId)
        {
            // 边界候选保留给串行队列处理
            ++state.Stats.BoundarySplitCandidateCount;
            continue;
        }

        // 分块下标决定由哪个线程单独处理该候选
        chunks[chunkId].push_back(candidate);
        ++state.Stats.InteriorSplitCandidateCount;
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

std::size_t CountNonEmptyChunks(auto& chunks)
{
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

std::size_t CountChunkCandidates(auto& chunks)
{
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
    std::sort(nodes.begin(), nodes.end());
    nodes.erase(std::unique(nodes.begin(), nodes.end()), nodes.end());
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
    const std::size_t minimumCandidateCount = ResolveMinParallelCommitCandidateCount("split");
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
    const std::size_t minimumCandidateCount = ResolveMinParallelCommitCandidateCount("merge");
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
} // 匿名命名空间

void RefineWithSplitQueue(DataOrientedRoamState& state)
{
    state.Stats.TopologyChunkCount = static_cast<std::size_t>(
        DataOrientedRoamTopologyChunkGridSize * DataOrientedRoamTopologyChunkGridSize);
    RefreshPersistentSplitQueuePriorities(state);
    if (state.Settings.PassPolicy.SplitTopology != TerrainLodTopologyAction::SerialImmediate)
    {
        std::vector<DataOrientedRoamSplitCandidate> initialCandidates;
        Tools::PerformanceTimer snapshotTimer;
        SnapshotPersistentSplitQueueCandidates(state, initialCandidates);
        state.Stats.SplitCandidateSnapshotMilliseconds += snapshotTimer.Stop();
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
    // 多线程处理结束后，主线程继续读取当前 Q_s 和 Q_m 的堆顶，按全局分数顺序细分或合并
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
            // 当前 Q_m 堆顶已经不再满足合并条件，移除后继续检查下一个候选
            continue;
        }

        if (closureNeedsBudget)
        {
            // 没有画质损失更低的菱形可以合并时，当前预算下已经无法继续调整
            break;
        }

        // 补齐相邻三角形所需的连锁细分失败后，节点仍属于 Q_s，但本次更新不能让它在堆顶反复重试
        BlockPersistentSplitQueueNodeForCurrentBuild(state, splitNode);
    }
    const float totalConvergenceMilliseconds = serialConvergenceTimer.Stop();
    state.Stats.SplitTopologySerialConvergenceMilliseconds += std::max(
        0.0F,
        totalConvergenceMilliseconds - crossoverMergeMilliseconds);
}

void MergeWithDiamondQueue(DataOrientedRoamState& state)
{
    state.Stats.TopologyChunkCount = static_cast<std::size_t>(
        DataOrientedRoamTopologyChunkGridSize * DataOrientedRoamTopologyChunkGridSize);
    RefreshPersistentMergeQueuePriorities(state);
    if (state.Settings.PassPolicy.MergeTopology != TerrainLodTopologyAction::SerialImmediate)
    {
        std::vector<DataOrientedRoamMergeCandidate> candidates;
        Tools::PerformanceTimer snapshotTimer;
        SnapshotPersistentMergeQueueCandidates(state, state.Settings.MergeThreshold, candidates);
        state.Stats.MergeCandidateSnapshotMilliseconds += snapshotTimer.Stop();
        state.Stats.MergeCandidateCount = candidates.size();
        Tools::PerformanceTimer chunkBuildTimer;
        std::sort(
            candidates.begin(),
            candidates.end(),
            [](const DataOrientedRoamMergeCandidate& left, const DataOrientedRoamMergeCandidate& right) {
                return left.Score < right.Score;
            });

        std::vector<std::vector<DataOrientedRoamMergeCandidate>> interiorChunks =
            BuildInteriorMergeChunks(state, candidates);
        state.Stats.MergeTopologyChunkBuildMilliseconds += chunkBuildTimer.Stop();
        CommitInteriorMergeChunks(state, interiorChunks);
    }
    else
    {
        state.Stats.MergeCandidateCount = 0U;
    }

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
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
