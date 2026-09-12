#pragma once

#include "algorithms/data_oriented_roam/DataOrientedRoamTypes.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamDecisionTrace.h"

#include <cstddef>
#include <compare>
#include <cstdint>
#include <vector>

namespace ParallelRoam::Algorithms::DataOrientedRoam::Materialization
{
/// <summary>
/// 区分活动资格与节点曾经创建的事实，休眠缓存不属于当前目标 cut
/// </summary>
enum class NativePlanningActivity : std::uint64_t { Dormant, Leaf, Internal };

/// <summary>
/// 私有决策视图允许改写的字段，数值关系只在当前规划调用中有效
/// 此枚举不是 Γ 字段白名单，也不定义可发布的节点记录
/// </summary>
enum class NativePlanningField : std::size_t
{
    LeftChild, RightChild, BaseNeighbor, LeftNeighbor, RightNeighbor,
    ActivatedBuild, SplitBuild, MergeBuild, ForcedActivation, IsSplit,
    Activity, SplitBlockedBuild, CurrentSplitPath,
    Count
};

/// <summary>
/// 描述节点覆盖与读取规模，累计记录包含最后又恢复原值的节点
/// 不同来源读取数量仅在诊断开启时有效，常规规划只维护标量计数
/// </summary>
struct NativePlanningViewMetrics
{
    std::size_t Queries{0}, SourceQueries{0}, FieldWrites{0};
    std::size_t OldNodeRecords{0}, VirtualNodeRecords{0}, VirtualNodes{0};
    std::size_t DistinctSourceNodesRead{0}, ChangedFields{0};
    bool ReadCoverageCollected{false};
};

/// <summary>
/// 区分高分优先的细分堆与低分优先的合并堆，双方均以 PathId 打破平分
/// </summary>
enum class NativePlanningQueueKind : std::size_t { Split, Merge };

/// <summary>
/// 临时堆只保存规划节点引用与分数，节点引用不能用于生产状态发布
/// </summary>
struct NativePlanningQueueEntry
{
    float Score{0};
    DataOrientedRoamNodeIndex Node{InvalidDataOrientedRoamNodeIndex};
    bool operator==(const NativePlanningQueueEntry&) const = default;
};

/// <summary>
/// 堆访问、曾写槽位与反向成员各自计量，删除尾部不抹去累计覆盖
/// </summary>
struct NativePlanningQueueMetrics
{
    std::size_t Reads{0}, Writes{0}, Comparisons{0}, SourceReads{0};
    std::size_t OldSlotsWritten{0}, AppendedSlotsWritten{0}, ReverseRecords{0};
    std::size_t DistinctSourceSlotsRead{0}, MembershipReads{0}, MembershipWrites{0};
    bool ReadCoverageCollected{false};
};

/// <summary>
/// 净目标不能推导的具名续接义务，禁止在此加入邻接、物理槽位或完整堆
/// </summary>
enum class NativeObligationKind
{
    SplitBlockedBuild, FailedMergeRemoval, ActivatedBuild, SplitBuild, MergeBuild, ForcedActivation, CacheBirth
};

/// <summary>
/// 同一类别与身份只保留最终必要值；额外缓存只编码存在性和创建轮次
/// </summary>
struct NativeContinuationObligation
{
    NativeObligationKind Kind{};
    std::uint64_t Path{0}, Value{0};
    auto operator<=>(const NativeContinuationObligation&) const = default;
};

/// <summary>
/// 固定视图下实际计算过的纯分数，不包含队列资格、抑制分或可变节点字段
/// 仅能随当前同步事务复用，不能跨相机或构建环境复用
/// </summary>
struct NativeScoreEvaluation
{
    std::uint64_t Path{0};
    float Score{0};
    bool operator==(const NativeScoreEvaluation&) const = default;
};

/// <summary>
/// 区分尝试、成功 primitive 与私有维护工作，不用净差分替代实际执行量
/// </summary>
struct NativePlanningWork
{
    std::size_t SplitRoots{0}, SplitRootFailures{0}, MergeRoots{0}, MergeRootFailures{0}, Exchanges{0};
    std::size_t SplitAttempts{0}, ForcedAttempts{0}, PrimitiveSplits{0}, ForcedSplits{0}, PrimitiveMerges{0};
    std::size_t BudgetRejections{0}, ScoreEvaluations{0}, NeighborhoodVisits{0}, CandidateChecks{0};
    bool operator==(const NativePlanningWork&) const = default;
};

/// <summary>
/// 规划计费保留访问和累计覆盖，载荷字节不包含标准容器分配器开销
/// </summary>
struct NativePlanningMetrics
{
    NativePlanningViewMetrics View;
    NativePlanningQueueMetrics SplitQueue, MergeQueue;
    NativePlanningWork Work;
    std::size_t MergeRelationRecords{0}, ExtractionNodes{0}, ObligationPeak{0}, PayloadBytes{0};
};

/// <summary>
/// 正常返回只描述目标事件差分、续接义务和纯求值缓存，私有工作区随调用销毁
/// 不持有来源引用或虚拟下标，J 由来源事件谓词加净差分表达
/// </summary>
struct NativeTargetPlan
{
    static constexpr std::uint32_t Schema = 1;
    std::uint64_t BuildSequence{0};
    std::size_t SourceNodeCount{0}, SourceLeafCount{0}, BudgetCap{0};
    std::size_t FinalLeafCount{0}, RemainingBudget{0}, Iteration{0}, MaximumIterations{0};
    TopologySplitStop Stop{TopologySplitStop::NotStarted};
    std::vector<std::uint64_t> AddedEvents, RemovedEvents;
    std::vector<NativeContinuationObligation> Obligations;
    std::vector<NativeScoreEvaluation> Evaluations;
    NativePlanningMetrics Metrics;
};
}
