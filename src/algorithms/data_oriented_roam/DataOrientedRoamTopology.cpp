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
// Split 现有 batch 很小，保留原阈值；Merge 使用配对实验测得的保守交叉点。
constexpr std::size_t MinParallelSplitCommitCandidateCount = 32;
constexpr std::size_t MinParallelMergeCommitCandidateCount = 160;

void NormalizeQueueNeighborhood(std::vector<DataOrientedRoamNodeIndex>& nodes);

// 以下环境变量仅服务 benchmark 的独立进程配对实验：它们可以固定候选阈值，
// 并把并行提交限制到指定 Build 和指定 phase。未设置变量时全部回落到上述产品默认值，
// 因而正常运行路径不会因为实验仪表而改变提交策略。
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
    // 实验覆盖只在进程启动时读取一次；未设置或非法值保持产品默认值。
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
    // build=0 表示所有 Build；非零值让配对实验只改变目标帧的提交策略。
    static const std::size_t targetBuild = ParseDiagnosticSize(
        "PARALLEL_ROAM_DOD_PARALLEL_COMMIT_BUILD",
        0U);
    if (targetBuild != 0U && state.BuildSequence != targetBuild)
    {
        return false;
    }

    // phase 默认 both；实验可只开启 split 或 merge，隔离同一 Build 内的输入。
    static const std::string selectedPhase = []() {
        const std::string value = ReadDiagnosticEnvironmentVariable(
            "PARALLEL_ROAM_DOD_PARALLEL_COMMIT_PHASE");
        return value.empty() ? std::string{"both"} : value;
    }();
    return selectedPhase == "both" || selectedPhase == phase;
}

/// <summary>
/// 并发 worker 的本地提交计数，join 后再合并回全局 stats
/// </summary>
struct TopologyCommitCounters
{
    // split 和 forced split 分开保留，便于维持原有统计语义
    std::size_t SplitCount{0};
    std::size_t ForcedSplitCount{0};
    std::size_t RejectedSplitCount{0};
    std::size_t BudgetRejectedSplitCount{0};
    // 约束传播理论上不会出现在并发 split，但计数器仍保留防御口径
    std::size_t ConstraintPassCount{0};
    std::size_t MergeCount{0};
};

/// <summary>
/// 并发 split 成功后返回给串行 priority queue 的增量节点
/// </summary>
struct CommittedSplit
{
    // Node 已经从 leaf 变为 internal
    DataOrientedRoamNodeIndex Node{InvalidDataOrientedRoamNodeIndex};
    // split 前的 base neighbor 用于重新评价 diamond 对侧 child
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
    // active index 由主线程维护：串行提交直接调用，并行提交在 join 后调用。
    // position 非 sentinel 表示节点已被登记，重复 split 不能制造重复条目。
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
    // swap-remove 保持移除为 O(1)，同时修正被移动节点的反向位置。
    // 集合顺序不承载优先级，merge candidate 会在后续阶段按 score 排序。
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
    // 活动叶视图保持稠密但不再承担 heap，评分刷新不会改变这里的顺序
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
    // 先从 Q_s 删除，随后用 O(1) swap-remove 更新独立活动叶视图
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
    // 一个 leaf split 的集合变化固定为 -1 leaf、+1 internal、+2 leaf。
    // 净增一个 leaf，与两种 split budget 的 token 语义完全一致。
    DeactivateLeafNode(state, node);
    ActivateInternalNode(state, node);
    ActivateLeafNode(state, state.Nodes.LeftChildAt(node));
    ActivateLeafNode(state, state.Nodes.RightChildAt(node));
    // 串行提交和并行 join 后都从这里进入，因此 Mesh edit 始终由主线程记录。
    RecordMeshSplit(state, node);
}

void ApplyMergeIndexTransition(DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    // 一个 parent merge 是 split 的逆操作：两个 child 退出，parent 回到 leaf 集合。
    // diamond merge 会由调用者分别对两侧 parent 执行一次转换。
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
        // invalid node 不能被分配给任何 chunk
        return InvalidDataOrientedRoamChunkId;
    }

    // node 创建时已缓存 chunk 归属
    return state.Nodes.InteriorChunkIdAt(node);
}

bool NodeBelongsToChunk(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node,
    DataOrientedRoamChunkId chunkId)
{
    if (!state.IsValidNode(node))
    {
        // invalid neighbor 不会被写入，视作不阻塞 chunk 内提交
        return true;
    }

    return InteriorChunkIdForNode(state, node) == chunkId;
}

std::size_t ResolveTopologyCommitWorkerCount(
    const DataOrientedRoamState& state,
    std::size_t candidateCount,
    std::size_t nonEmptyChunkCount,
    std::string_view phase)
{
    if (!DiagnosticBuildAllowsParallelCommit(state, phase))
    {
        return 1U;
    }

    if (candidateCount < ResolveMinParallelCommitCandidateCount(phase) || nonEmptyChunkCount < 2U)
    {
        // 单 chunk 或小批量没有并发提交价值
        return 1U;
    }

    if (state.Settings.ErrorEvaluationWorkerCount == 1U)
    {
        // 拓扑提交沿用 worker 设置，避免新增 UI 参数
        return 1U;
    }

    std::size_t requestedWorkerCount = state.Settings.ErrorEvaluationWorkerCount;
    if (requestedWorkerCount == 0U)
    {
        // 自动模式保守封顶，避免 topology commit 抢占过多线程
        const unsigned int hardwareWorkerCount = std::thread::hardware_concurrency();
        requestedWorkerCount = hardwareWorkerCount == 0U ? 1U : static_cast<std::size_t>(hardwareWorkerCount);
        requestedWorkerCount = std::min(requestedWorkerCount, MaxTopologyCommitWorkerCount);
    }

    return std::clamp(requestedWorkerCount, std::size_t{1}, nonEmptyChunkCount);
}

void MergeCountersIntoStats(DataOrientedRoamState& state, const TopologyCommitCounters& counters)
{
    // worker 本地计数在主线程合并，避免 stats 字段数据竞争
    state.Stats.SplitCount += counters.SplitCount;
    state.Stats.ForcedSplitCount += counters.ForcedSplitCount;
    state.Stats.RejectedSplitCount += counters.RejectedSplitCount;
    state.Stats.BudgetRejectedSplitCount += counters.BudgetRejectedSplitCount;
    state.Stats.ConstraintPassCount += counters.ConstraintPassCount;
    state.Stats.MergeCount += counters.MergeCount;
}

/// <summary>
/// 串行 topology 的预算和统计写入策略。
/// 普通计数只由主线程访问，避免每次 split 执行 atomic CAS；
/// shared index 也由同一调用栈立即维护，因此后续队列始终可直接消费。
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
/// 并行 topology worker 的提交策略。
/// worker 只竞争 atomic budget 并写自己的 counters，不能修改共享活动索引；
/// join 后由主线程统一合并统计、索引和持久队列邻域。
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
/// 并行预提交结束后，根据已经合并完成的活动叶集合恢复串行预算。
/// 这是 atomic 与普通计数之间唯一的 Build 内同步点，后续串行收敛只访问普通字段。
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
        // base edge 对应旧 parent 时改到 split 后 child
        state.Nodes.BaseNeighbors[neighbor] = newNode;
    }

    if (state.Nodes.LeftNeighbors[neighbor] == oldNode)
    {
        // left edge 引用旧 parent 时同步替换
        state.Nodes.LeftNeighbors[neighbor] = newNode;
    }

    if (state.Nodes.RightNeighbors[neighbor] == oldNode)
    {
        // right edge 引用旧 parent 时同步替换
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
    // parent 转为 internal，两个可复用 child 清除旧邻接后进入当前 Build
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
    // parent 恢复为 leaf，并记录本次回收供统计和调试着色使用
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
    // 两个 child 之间共享 split 中线

    // child 的 base edge 分别来自父节点 left edge 和 right edge
    // 外侧 neighbor 若仍指向旧 parent，必须改到共享完整边的 child
    const DataOrientedRoamNodeIndex leftNeighbor = state.Nodes.LeftNeighborAt(node);
    const DataOrientedRoamNodeIndex rightNeighbor = state.Nodes.RightNeighborAt(node);
    state.Nodes.BaseNeighbors[leftChild] = leftNeighbor;
    state.Nodes.BaseNeighbors[rightChild] = rightNeighbor;
    ReplaceNeighborReference(state, leftNeighbor, node, leftChild);
    ReplaceNeighborReference(state, rightNeighbor, node, rightChild);

    if (!state.IsValidNode(baseNeighbor) || state.IsLeaf(baseNeighbor))
    {
        // 对侧没有 split 时没有完整 diamond child 可以连接
        return;
    }

    // baseNeighbor 已 split 时四个 child 共同组成 diamond
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
/// split、forced split 和邻接修复共用同一份实现；
/// CommitPolicy 在编译期决定预算来源、统计落点和 shared index 是否立即更新。
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
        // internal node 已经由 child 接管细分决策
        return false;
    }

    if (state.Nodes.DepthAt(node) >= state.Settings.MaxDepth)
    {
        // maxDepth 是硬限制，不进入约束传播
        commitPolicy.RecordRejectedSplit(state);
        return false;
    }

    // 每次 leaf split 恰好增加一个 active triangle，先预留 token 再传播 forced split
    // 这样完整的约束闭包始终不会超过预算上限
    if (!commitPolicy.TryAcquireSplitBudget(state))
    {
        return false;
    }

    DataOrientedRoamNodeIndex baseNeighbor = state.Nodes.BaseNeighborAt(node);
    if (state.Settings.EnableLocalConstraints)
    {
        // local constraint 只在设置开启时传播 forced split
        int guard = 0;
        // 非互为 base 的邻接链必须先追到合法 diamond
        // 否则单侧 split 会把一条粗边贴到多条细边上
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
                // 约束传播失败时当前 split 也必须失败
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
        // 对侧仍是 leaf 时先补齐 base neighbor split
        // forcedFrom 防止互为 base 的两个 leaf 递归回跳
        commitPolicy.RecordConstraintPass(state);
        if (!SplitNodeImpl(
                state,
                baseNeighbor,
                DataOrientedRoamSplitReason::ForcedByBaseNeighbor,
                node,
                commitPolicy))
        {
            // 对侧 leaf 无法补齐时不能单侧 split
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
        // 首次 split 创建 child，merge 后再次 split 时复用同一 child index
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
    // parent 留在 node pool 中，但不再是 active leaf
    PrepareSplitNodeState(state, node, leftChild, rightChild, reason);

    // child 可能从历史 merge 状态复用，激活前必须清空旧 neighbor
    LinkSplitNeighbors(state, node, baseNeighbor);
    if constexpr (CommitPolicy::UpdatesSharedIndices)
    {
        // 并行提交由 join 后的主线程统一更新索引，避免 worker 竞争 vector
        ApplySplitIndexTransition(state, node);
        AppendPersistentMergeQueueNeighborhood(state, node, mergeQueueNeighborhood);
        AppendPersistentMergeQueueNeighborhood(state, baseNeighbor, mergeQueueNeighborhood);
        RefreshPersistentMergeQueueNeighborhood(state, mergeQueueNeighborhood);
    }
    // 串行路径会记录 path，最终仍由 CollectActiveSplitPaths 重建一次
    commitPolicy.RecordSplit(state, parentPathId, reason);
    return true;
}

/// <summary>
/// 单侧 parent merge 的拓扑修改保持统一，预算释放与索引维护由策略接管。
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

    // parent 重新成为 leaf 后，外部 neighbor 必须从 inactive child 改回 parent
    ReplaceNeighborReference(state, newLeftNeighbor, leftChild, node);
    ReplaceNeighborReference(state, newRightNeighbor, rightChild, node);
    state.Nodes.LeftNeighbors[node] = newLeftNeighbor;
    state.Nodes.RightNeighbors[node] = newRightNeighbor;
    PrepareMergedNodeState(state, node);
    if constexpr (CommitPolicy::UpdatesSharedIndices)
    {
        // parent 重新成为 leaf，同时两个 child 退出 active leaf 集合。
        ApplyMergeIndexTransition(state, node);
    }
    // 每个 parent merge 净释放一个 leaf token；diamond 会调用两次。
    commitPolicy.ReleaseSplitBudget(state);
    commitPolicy.RecordMerge(state);
}

/// <summary>
/// diamond 判定和双侧 merge 规则不因串行/并行入口而复制。
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
        // 完整 diamond merge 要同时回收两侧 parent
        // 只回收一侧会让对侧 child 贴上粗边
        if (state.Nodes.BaseNeighborAt(baseNeighbor) != node)
        {
            return false;
        }

        state.Nodes.BaseNeighbors[node] = baseNeighbor;
        state.Nodes.BaseNeighbors[baseNeighbor] = node;
        // MergeSingleNode 不改 baseNeighbor，互指关系需要前后显式保持
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
    // 并发 split 第一版不做 node pool 分配，只复用历史 child
    return state.IsValidNode(node) &&
           state.IsValidNode(state.Nodes.LeftChildAt(node)) &&
           state.IsValidNode(state.Nodes.RightChildAt(node));
}

bool SplitWouldNeedForcedNeighbor(const DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    if (!state.Settings.EnableLocalConstraints)
    {
        // 关闭约束时不会递归触发 base neighbor split
        return false;
    }

    const DataOrientedRoamNodeIndex baseNeighbor = state.Nodes.BaseNeighborAt(node);
    if (!state.IsValidNode(baseNeighbor))
    {
        // 地形边界没有对侧三角形
        return false;
    }

    if (state.IsLeaf(baseNeighbor))
    {
        // leaf base neighbor 会触发 forced split，必须串行处理
        return true;
    }

    // 非互指 diamond 需要沿 neighbor 链修复，也交给串行路径
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
        // 任一条件不满足都会回退到原有串行 split queue
        return InvalidDataOrientedRoamChunkId;
    }

    const DataOrientedRoamChunkId chunkId = InteriorChunkIdForNode(state, node);
    if (chunkId == InvalidDataOrientedRoamChunkId)
    {
        // 跨 chunk 三角形属于 boundary candidate
        return InvalidDataOrientedRoamChunkId;
    }

    const DataOrientedRoamNodeIndex leftChild = state.Nodes.LeftChildAt(node);
    const DataOrientedRoamNodeIndex rightChild = state.Nodes.RightChildAt(node);
    const DataOrientedRoamNodeIndex baseNeighbor = state.Nodes.BaseNeighborAt(node);
    // split 会写 parent、两个 child 和左右外侧 neighbor
    if (!NodeBelongsToChunk(state, leftChild, chunkId) ||
        !NodeBelongsToChunk(state, rightChild, chunkId) ||
        !NodeBelongsToChunk(state, state.Nodes.LeftNeighborAt(node), chunkId) ||
        !NodeBelongsToChunk(state, state.Nodes.RightNeighborAt(node), chunkId))
    {
        return InvalidDataOrientedRoamChunkId;
    }

    if (state.IsValidNode(baseNeighbor) && !state.IsLeaf(baseNeighbor))
    {
        // diamond 对侧已 split 时还会写入对侧 child 的 neighbor
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
        // merge 只作用于 active internal node
        return false;
    }

    const DataOrientedRoamNodeIndex leftChild = state.Nodes.LeftChildAt(node);
    const DataOrientedRoamNodeIndex rightChild = state.Nodes.RightChildAt(node);
    // 分桶时只检查拓扑形状，score 校验留到提交前
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
        // 没有对侧 internal diamond 时可以单侧 merge
        return true;
    }

    // 对侧 diamond 也必须是可回收的两片 leaf
    return state.Nodes.BaseNeighborAt(baseNeighbor) == node && HasMergeReadyChildren(state, baseNeighbor);
}

DataOrientedRoamChunkId SafeInteriorMergeChunkId(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node,
    bool validateMergeScore)
{
    if (!HasMergeReadyChildren(state, node) || !HasMergeReadyDiamond(state, node))
    {
        // merge 前置拓扑不满足时不进入任何提交队列
        return InvalidDataOrientedRoamChunkId;
    }

    if (validateMergeScore && !CanMergeNode(state, node))
    {
        // worker 真正提交前再做完整 score 校验
        return InvalidDataOrientedRoamChunkId;
    }

    const DataOrientedRoamChunkId chunkId = InteriorChunkIdForNode(state, node);
    if (chunkId == InvalidDataOrientedRoamChunkId)
    {
        // parent 自身跨 chunk 时不能并发回收
        return InvalidDataOrientedRoamChunkId;
    }

    const DataOrientedRoamNodeIndex leftChild = state.Nodes.LeftChildAt(node);
    const DataOrientedRoamNodeIndex rightChild = state.Nodes.RightChildAt(node);
    // MergeSingleNode 会写两个 child 的 base neighbor 所指向的外侧节点
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
        // diamond merge 会同时回收 base neighbor 一侧
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
    // 先按原 priority queue 口径排序，再筛选可并发提交的安全候选
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
            // boundary candidate 保留给串行 queue
            ++state.Stats.BoundarySplitCandidateCount;
            continue;
        }

        // chunk 下标即并发任务的 ownership
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

    // merge 不受 split 队列影响，所有安全 interior 候选都可先分桶
    for (const DataOrientedRoamMergeCandidate& candidate : candidates)
    {
        // merge candidate 已按 score 排好序，chunk 内保留这个顺序
        const DataOrientedRoamChunkId chunkId = SafeInteriorMergeChunkId(state, candidate.Node, false);
        if (chunkId == InvalidDataOrientedRoamChunkId)
        {
            // 跨 chunk diamond 仍由串行路径提交
            ++state.Stats.BoundaryMergeCandidateCount;
            continue;
        }

        // 同一 chunk 内由同一个 worker 顺序提交
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
            // 非空 chunk 数决定最多能并行多少个独立任务
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
        // 只统计 interior candidate，不含串行 boundary 回退
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
        // worker 不足时不预提交 split，保持原串行 queue 语义
        return committedSplits;
    }

    std::vector<TopologyCommitCounters> localCounters(workerCount);
    std::vector<std::vector<CommittedSplit>> localCommittedSplits(workerCount);
    // 邻域失效与 worker 提交分开计时，避免把主线程队列维护误算为并行收益。
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
        // 每个 chunk 只会被一个 worker 访问
        for (std::size_t chunkIndex = workerIndex; chunkIndex < chunks.size(); chunkIndex += workerCount)
        {
            for (const DataOrientedRoamSplitCandidate& candidate : chunks[chunkIndex])
            {
                const DataOrientedRoamNodeIndex node = candidate.Node;
                // 同 chunk 前序提交后需要重新确认 cached chunk ownership
                const DataOrientedRoamChunkId chunkId = SafeInteriorSplitChunkId(state, node);
                if (chunkId != chunkIndex)
                {
                    // 同 chunk 前序提交可能让候选不再安全
                    continue;
                }

                const DataOrientedRoamNodeIndex baseNeighborBeforeSplit = state.Nodes.BaseNeighborAt(node);
                // 并发 split 只允许不分配新 node 的安全候选
                if (SplitNodeParallel(
                        state,
                        node,
                        DataOrientedRoamSplitReason::Requested,
                        InvalidDataOrientedRoamNodeIndex,
                        localCounters[workerIndex]))
                {
                    // child 会在主线程重新入队，保持级联细分
                    localCommittedSplits[workerIndex].push_back(CommittedSplit{node, baseNeighborBeforeSplit});
                }
            }
        }
    });
    state.Stats.SplitTopologyParallelCommitMilliseconds += parallelCommitTimer.Stop();

    // worker 已经 join；此后计时均表示主线程的提交结果整理成本。
    Tools::PerformanceTimer resultMergeTimer;
    std::size_t totalCommittedCount = 0U;
    for (const TopologyCommitCounters& counters : localCounters)
    {
        // 所有全局 stats 更新集中在主线程完成
        MergeCountersIntoStats(state, counters);
        totalCommittedCount += counters.SplitCount;
    }

    for (const std::vector<CommittedSplit>& localSplits : localCommittedSplits)
    {
        // 合并顺序只影响后续同分 sequence，不影响拓扑正确性
        committedSplits.insert(committedSplits.end(), localSplits.begin(), localSplits.end());
    }
    state.Stats.SplitTopologyResultMergeMilliseconds += resultMergeTimer.Stop();

    Tools::PerformanceTimer indexQueueRefreshTimer;
    for (const CommittedSplit& split : committedSplits)
    {
        // worker 只改 SoA 拓扑；join 后主线程再集中修改两个共享索引 vector。
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
        // 小批量 merge 直接交给原串行路径
        return committedMerges;
    }

    std::vector<TopologyCommitCounters> localCounters(workerCount);
    std::vector<std::vector<CommittedMerge>> localCommittedMerges(workerCount);
    // Merge 与 Split 使用相同六段边界，报告可以直接横向比较。
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
        // chunk ownership 保证不同 worker 不写同一组 neighbor
        for (std::size_t chunkIndex = workerIndex; chunkIndex < chunks.size(); chunkIndex += workerCount)
        {
            for (const DataOrientedRoamMergeCandidate& candidate : chunks[chunkIndex])
            {
                const DataOrientedRoamNodeIndex node = candidate.Node;
                const DataOrientedRoamChunkId chunkId = SafeInteriorMergeChunkId(state, node, true);
                if (chunkId != chunkIndex)
                {
                    // 前序 merge 可能已经改变 diamond 结构
                    continue;
                }

                // 真正提交前仍复用原 diamond merge 逻辑
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
        // merge 成功次数由 worker 本地计数器汇总
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
        // Node 一定被合并；BaseNeighbor 只有在完整 diamond merge 时才一起转换。
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
    Tools::PerformanceTimer candidateMarkTimer;
    RefreshPersistentSplitQueuePriorities(state);
    if (state.Settings.EnableParallelSplit)
    {
        std::vector<DataOrientedRoamSplitCandidate> initialCandidates;
        SnapshotPersistentSplitQueueCandidates(state, initialCandidates);
        state.Stats.SplitCandidateCount = initialCandidates.size();
        state.Stats.SplitCandidateMarkMilliseconds = candidateMarkTimer.Stop();

        Tools::PerformanceTimer chunkBuildTimer;
        std::vector<std::vector<DataOrientedRoamSplitCandidate>> interiorChunks =
            BuildInteriorSplitChunks(state, initialCandidates);
        state.Stats.SplitTopologyChunkBuildMilliseconds += chunkBuildTimer.Stop();
        CommitInteriorSplitChunks(state, interiorChunks);
    }
    else
    {
        // 串行逻辑分支只保留并行 Q_s 评分和建堆，不复制、排序或分桶候选。
        state.Stats.SplitCandidateCount = 0U;
        state.Stats.SplitCandidateMarkMilliseconds = candidateMarkTimer.Stop();
    }
    // worker join 后只同步一次普通预算；后续串行 split/merge 不再访问 atomic token。
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
    // 并行预提交之后仍需恢复双持久队列的严格优先级和预算语义。
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
            // 当前队首已失效，继续检查下一个 Q_m 候选。
            continue;
        }

        if (closureNeedsBudget)
        {
            // 没有回收损失更低的 diamond 时，双队列已经达到当前预算下的稳定状态。
            break;
        }

        // 约束闭包失败后节点仍属于 Q_s，但本次 Build 不能在 heap 顶部反复重试。
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
    Tools::PerformanceTimer queueRefreshTimer;
    RefreshPersistentMergeQueuePriorities(state);
    std::vector<DataOrientedRoamMergeCandidate> candidates;
    SnapshotPersistentMergeQueueCandidates(state, state.Settings.MergeThreshold, candidates);
    state.Stats.MergeCandidateCount = candidates.size();
    state.Stats.MergeCandidateMarkMilliseconds = queueRefreshTimer.Stop();
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
