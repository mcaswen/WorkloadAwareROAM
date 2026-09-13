#pragma once

#include "algorithms/data_oriented_roam/DataOrientedRoamTypes.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamDecisionTrace.h"

#include <cstddef>
#include <compare>
#include <cstdint>
#include <array>
#include <chrono>
#include <vector>

namespace ParallelRoam::Algorithms::DataOrientedRoam::Materialization
{
/// <summary>
/// 规划调用内的互斥成本区间，细项只在独立诊断遍启用
/// 前置控制保留递归与未被其他区间覆盖的访问，不把调用顺序当作必要关键路径
/// </summary>
enum class NativePlanningCost : std::size_t
{
    Construct, Control, Prerequisite, MergeControl, Neighborhood, CandidateMaintenance,
    Eligibility, Score, Queue, Create, SplitChanges, MergeChanges,
    Extract, Sort, Metrics, Destroy, Count
};

/// <summary>
/// 同步借用的成本接收器，仅累加当前活动区间，嵌套调用不会重复计时
/// 诊断扰动留在测得的包络内，不能把这些子项冒充普通遍的精确耗时
/// </summary>
struct NativePlanningCosts
{
    static constexpr auto Count = static_cast<std::size_t>(NativePlanningCost::Count);
    std::array<double, Count> Milliseconds{};
    std::array<std::size_t, Count> Calls{};
    bool Detailed{false};
    NativePlanningCost Active{NativePlanningCost::Count};
    std::chrono::steady_clock::time_point Since{};
};

/// <summary>
/// 暂停父区间并在退出时恢复，普通调用不读取时钟
/// 作用域不可复制；提前结束用于在销毁工作区之前结束指标计量
/// </summary>
class NativePlanningCostScope
{
public:
    NativePlanningCostScope(NativePlanningCosts* costs, NativePlanningCost kind, bool detail = false)
        : _costs(costs && (!detail || costs->Detailed) ? costs : nullptr)
    {
        if (!_costs) return;
        _previous = _costs->Active;
        Account();
        _costs->Active = kind;
        ++_costs->Calls[static_cast<std::size_t>(kind)];
    }
    NativePlanningCostScope(const NativePlanningCostScope&) = delete;
    NativePlanningCostScope& operator=(const NativePlanningCostScope&) = delete;
    ~NativePlanningCostScope() { Finish(); }
    void Finish()
    {
        if (!_costs) return;
        Account();
        _costs->Active = _previous;
        _costs = nullptr;
    }
private:
    void Account()
    {
        const auto now = std::chrono::steady_clock::now();
        if (_costs->Active != NativePlanningCost::Count)
            _costs->Milliseconds[static_cast<std::size_t>(_costs->Active)] +=
                std::chrono::duration<double, std::milli>(now - _costs->Since).count();
        _costs->Since = now;
    }
    NativePlanningCosts* _costs;
    NativePlanningCost _previous{NativePlanningCost::Count};
};

/// <summary>
/// 区分有效记录、预留容量和增长费用；峰值只描述本表请求的同时存活字节
/// 不包含分配器元数据或整进程驻留，多个表峰值之和只能作为上界
/// </summary>
struct NativePlanningStorageMetrics
{
    std::size_t Records{0}, RecordCapacity{0}, IndexCapacity{0}, RecordBytes{0}, IndexBytes{0};
    std::size_t ReservedBytes{0}, PeakReservedBytes{0}, Allocations{0}, Rehashes{0};
    std::size_t MovedRecords{0}, RehashedRecords{0}, InitializedSlots{0};
    std::size_t Lookups{0}, Probes{0}, MaximumProbe{0};
    // 页容量与目录高水位分开，哈希版本保持为零；两种索引的探测含义不同
    bool PagedIndex{false};
    bool DenseIndex{false};
    std::size_t SourceCopies{0};
    std::size_t MovedIndexEntries{0};
    std::size_t IndexPages{0}, DirectorySize{0}, DirectoryCapacity{0}, DirectoryInitialized{0}, DirectoryMoved{0};
};

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
    // 稳定身份在规划内不变，搬移时随条目携带，避免平分比较重新访问节点来源
    std::uint64_t Path{0};
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
    // 逻辑失效与实际 heap 删除分开，区间内恢复的成员无需往返搬运
    std::size_t DeferredRemovals{0}, RestoredEntries{0}, DeferredErases{0}, UnchangedUpserts{0};
    std::size_t DeferredPeak{0}, DeferredCapacity{0};
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
    std::size_t ScoreRequests{0}, ScoreCacheHits{0};
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
    // 节点、两堆槽、共享成员投影和评分分别计费；共享字段的历史表位保持零值
    std::array<NativePlanningStorageMetrics, 8> Storage{};
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
