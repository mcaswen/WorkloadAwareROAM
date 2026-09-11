#pragma once

#include "algorithms/data_oriented_roam/DataOrientedRoamTopologyExperiment.h"

#include <array>
#include <chrono>
#include <functional>
#include <string>

namespace ParallelRoam::Algorithms::DataOrientedRoam::OrderingRelaxation
{
/// <summary>
/// 区分严格执行、严格连续前缀和优先级带；零松弛不通过多成员窗口模拟
/// </summary>
enum class OrderingMode { Strict, StrictPrefix, PriorityBand };

/// <summary>
/// 固定选择器只改变 Alpha，检查和接收上限不随结果调整
/// 严格与前缀模式要求 Alpha 为零，正松弛参数属于 (0,1]
/// </summary>
struct OrderingConfiguration
{
    OrderingMode Mode{OrderingMode::Strict};
    double Alpha{0.0};
};

/// <summary>
/// 每次候选暴露只有一个主原因；未检查项不能被解释为已知依赖冲突
/// Accepted 同时用作没有发现两根冲突的返回值
/// </summary>
enum class RejectionReason
{
    Accepted, PriorityWindow, InspectionLimit, BatchCapacity, DependencyUnknown,
    SharedForcedClosure, WriteWriteOverlap, ReadWriteDependency, CoordinatorDependency,
    BudgetReservation, NodeAllocation, OperationFailed, PrefixStopped, HeadFailed, ResourceLimit, Count
};
[[nodiscard]] const char* RejectionName(RejectionReason reason);

/// <summary>
/// 只保存实际检查根的明细；带外及未检查项由计划中的独立分母汇总
/// ConflictPath 标明阻挡它的已选根，零表示不是两根间冲突
/// </summary>
struct CandidateDecision
{
    std::uint64_t Path{0U};
    std::size_t Rank{0U};
    float Score{0.0F};
    RejectionReason Reason{RejectionReason::Accepted};
    std::uint64_t ConflictPath{0U};
    // 已拒绝根仍保留足迹证据，后续审计不必重跑试算来猜测阻挡原因
    TopologySplitObservation Observation;
};

/// <summary>
/// 保存同一开始快照上试算的完整根描述，不持有副本指针
/// 前态读资源和后态写资源按逻辑路径编码，用于发现足迹相同但效果不同的应用
/// </summary>
struct PlannedSplit
{
    float Score{0.0F};
    TopologySplitObservation Operation;
    std::vector<std::uint64_t> ReadState;
    std::vector<std::uint64_t> WrittenState;
};

/// <summary>
/// 构造后只读消费的封闭计划；失败头和控制动作交回严格单步
/// 试算成本与实际应用分列，不把寻找独立工作的代价混入算法工作量
/// </summary>
struct BatchPlan
{
    OrderingConfiguration Configuration;
    std::uint64_t InputHash{0U};
    TopologySplitIteration Position;
    std::vector<PlannedSplit> Members;
    std::vector<CandidateDecision> Inspected;
    std::array<std::size_t, static_cast<std::size_t>(RejectionReason::Count)> Reasons{};
    std::size_t CandidateCount{0U};
    std::size_t InBandCount{0U};
    std::size_t PeakReservation{0U};
    std::size_t NewNodes{0U};
    std::size_t LastAcceptedRank{0U};
    double MaximumScore{0.0};
    double MinimumScore{0.0};
    bool StrictFallback{true};
    bool ResourceLimited{false};
    TopologyOperationWork TrialWork;
    std::size_t TrialMerges{0U};
    double ConstructionMilliseconds{0.0};
    double TrialMilliseconds{0.0};
};

/// <summary>
/// 独立资格只授予完整吻合的批次；控制中断留下的前缀全部按串行记
/// 模型或硬约束不成立时停止变体，不修补计划继续执行
/// </summary>
enum class ApplicationStatus
{
    ValidatedBatch, StrictStep, ControlInterrupted, PlanMismatch, InvalidResult, ResourceLimited
};
[[nodiscard]] const char* ApplicationName(ApplicationStatus status);

/// <summary>
/// 保存实际根和协调器工作，失败前已完成的 primitive 仍计入总量
/// Steps 与 Roots 分开，因为一个循环体也可能只有合并或停止
/// </summary>
struct BatchApplication
{
    ApplicationStatus Status{ApplicationStatus::StrictStep};
    std::string Detail;
    std::vector<TopologySplitStep> Steps;
    std::vector<TopologySplitObservation> Roots;
    TopologyOperationWork Work;
    std::size_t MergeAttempts{0U};
    std::size_t MergeFailures{0U};
    std::size_t PrimitiveMerges{0U};
    std::size_t CoordinatorQueueCalls{0U};
    double Milliseconds{0.0};
};

/// <summary>
/// 有界排列只审计同一封闭集合；有限通过不代表所有批次或真实并发已获证明
/// Difference 保存首个反例，Cost 只描述审计自身的操作量
/// </summary>
struct PermutationAudit
{
    bool Performed{false};
    bool Passed{false};
    bool ResourceLimited{false};
    std::size_t Applications{0U};
    std::string Difference;
    TopologyOperationWork Cost;
    double Milliseconds{0.0};
};

/// <summary>
/// 汇总一个真实阶段变体；结果网格仍由调用方拥有的 state 导出
/// 有效多成员工作是局部内核机会，不包含仍需串行执行的共享维护
/// </summary>
struct OrderingResult
{
    ApplicationStatus Status{ApplicationStatus::StrictStep};
    std::string Detail;
    TopologySplitIteration Position;
    std::size_t Batches{0U};
    std::size_t ValidatedBatches{0U};
    std::size_t MultiBatches{0U};
    std::size_t MaximumWidth{0U};
    std::size_t FeasibleRoots{0U};
    std::size_t FeasiblePrimitives{0U};
    std::size_t RootAttempts{0U};
    std::size_t InterruptedBatches{0U};
    TopologyOperationWork ActualWork;
    TopologyOperationWork TrialWork;
    std::size_t PrimitiveMerges{0U};
    std::size_t MergeAttempts{0U};
    std::size_t MergeFailures{0U};
    std::size_t CoordinatorQueueCalls{0U};
    std::array<std::size_t, static_cast<std::size_t>(RejectionReason::Count)> Reasons{};
    std::vector<TopologySplitStep> Decisions;
    PermutationAudit Audit;
    double Milliseconds{0.0};
};

using BatchObserver = std::function<void(std::size_t, const BatchPlan&, const BatchApplication&)>;
using ExperimentDeadline = std::chrono::steady_clock::time_point;

/// <summary>
/// 使用观察器提供的已排序资源集合判定局部内核冲突
/// Accepted 不免除调用方对控制器和联合预算的检查
/// </summary>
[[nodiscard]] RejectionReason DependencyConflict(
    const TopologySplitObservation& a, const TopologySplitObservation& b);
/// <summary>
/// 输入应已完成评分并初始化阶段预算，位置必须属于同一独占状态
/// 构造只拥有逐根临时副本，计划中不保存这些副本的引用
/// </summary>
[[nodiscard]] BatchPlan BuildBatch(const DataOrientedRoamState& state, const OrderingConfiguration& configuration,
    const TopologySplitIteration& position, ExperimentDeadline deadline = ExperimentDeadline::max());
/// <summary>
/// 应用前核对完整开始身份，应用中只消费已封闭的根
/// 返回中断或失败时，已经发生的拓扑修改仍由调用方状态持有
/// </summary>
[[nodiscard]] BatchApplication ApplyBatch(DataOrientedRoamState& state, const BatchPlan& plan,
    TopologySplitIteration& position);
/// <summary>
/// 每个排列都从同一 source 重新复制，不能沿用上一排列的后态
/// 未完成的审计返回证据缺口，不等同于找到依赖反例
/// </summary>
[[nodiscard]] PermutationAudit AuditBatch(const DataOrientedRoamState& source, const BatchPlan& plan,
    ExperimentDeadline deadline = ExperimentDeadline::max());

/// <summary>
/// 从调用方独占状态开始一次完整收敛，不复制或重置来源帧
/// auditFirstBatch 控制是否审计首个实际成功多成员批次，回调只消费本批只读记录
/// </summary>
[[nodiscard]] OrderingResult RunOrderingExperiment(DataOrientedRoamState& state,
    const OrderingConfiguration& configuration, const BatchObserver& observer = {}, bool auditFirstBatch = false);
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam::OrderingRelaxation
