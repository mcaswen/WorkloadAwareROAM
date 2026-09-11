#pragma once

#include "algorithms/data_oriented_roam/DataOrientedRoamTypes.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
struct DataOrientedRoamState;

/// <summary>
/// 保存一次基础细分的尝试与结果；失败根之前完成的强制修改仍保留
/// 条目按递归进入顺序排列，实际完成顺序另由 CompletedPaths 表达
/// </summary>
struct TopologySplitAttempt
{
    std::uint64_t Path{0U};
    bool Forced{false};
    bool Completed{false};
};

/// <summary>
/// 在稳定操作边界计工作量，不代表字段访问次数或硬件指令数量
/// 根计数属于调用方，强制递归只增加基础尝试与完成数
/// </summary>
struct TopologyOperationWork
{
    std::size_t PrimitiveAttempts{0U};
    std::size_t PrimitiveCompleted{0U};
    std::size_t ForcedCompleted{0U};
    std::size_t NodesCreated{0U};
    std::size_t NodesReused{0U};
    std::size_t NeighborAssignments{0U};
    std::size_t ActiveIndexCalls{0U};
    // 队列 API 调用与真实成员变动分列，未计堆内部比较或交换次数
    std::size_t QueueCalls{0U};
    std::size_t QueueMembershipUpdates{0U};
    std::size_t MeshEditCalls{0U};
    std::size_t PathInsertCalls{0U};
};

/// <summary>
/// 描述同一根开始状态上的真实局部拓扑操作，不持有副本中的引用
/// 足迹以节点 PathId 为保守资源，CreatedPaths 区分本根产生的内部状态
/// 全局队列、索引和分配仍须串行维护，局部足迹完整不表示整根已可并行
/// </summary>
struct TopologySplitObservation
{
    std::uint64_t InputHash{0U};
    std::uint64_t RootPath{0U};
    bool RootSucceeded{false};
    bool LocalFootprintComplete{true};
    bool RequiresSerialMaintenance{false};
    // 先记录访问再去重；同值赋值也进入 WrittenPaths，不能用最终差分替代
    std::vector<std::uint64_t> ReadPaths;
    std::vector<std::uint64_t> WrittenPaths;
    std::vector<std::uint64_t> CreatedPaths;
    std::vector<std::uint64_t> CompletedPaths;
    std::vector<TopologySplitAttempt> Attempts;
    std::size_t BudgetBefore{0U};
    std::size_t BudgetAfter{0U};
    std::size_t PeakBudgetUse{0U};
    std::size_t BudgetRejected{0U};
    // 包含每次预留尝试与释放后的余量，峰值包含尚未完成的祖先预留
    std::vector<std::size_t> BudgetRemaining;
    TopologyOperationWork Work;
};

/// <summary>
/// 有界记录严格轨迹中的前若干根，后续根只累计总次数
/// 每根有独立开始状态，不能将这些连续请求称为一个 batch
/// </summary>
struct TopologyConvergenceObservation
{
    std::vector<TopologySplitObservation> Roots;
    std::size_t RootAttempts{0U};
    std::size_t MergeAttempts{0U};
    std::size_t MergeFailures{0U};
    std::size_t PrimitiveMerges{0U};
    std::size_t CoordinatorQueueCalls{0U};
};

/// <summary>
/// 区分严格收敛的正常停止与预算、迭代限制，停止后再次步进不再消费迭代
/// </summary>
enum class TopologySplitStop
{
    NotStarted,
    Running,
    NoEligibleSplit,
    BudgetBlocked,
    IterationLimit,
};

/// <summary>
/// 保存同一独占阶段状态的迭代位置；上限只在入口按当时节点规模计算
/// 调用期间不能重置来源、切换帧或重新初始化此值来延长收敛
/// </summary>
struct TopologySplitIteration
{
    std::size_t MaximumIterations{0U};
    std::size_t Iteration{0U};
    TopologySplitStop Stop{TopologySplitStop::NotStarted};
};

/// <summary>
/// 一个严格循环体可能同时尝试 split 和预算合并，分别保留两者结果
/// 根失败不排除强制修改已完成；具体闭包由操作观察记录
/// </summary>
struct TopologySplitStep
{
    std::uint64_t SplitPath{0U};
    std::uint64_t MergePath{0U};
    bool SplitAttempted{false};
    bool SplitSucceeded{false};
    bool BudgetRejected{false};
    bool SplitBlocked{false};
    bool MergeAttempted{false};
    bool MergeSucceeded{false};
};

/// <summary>
/// 只读预览原控制器的下一动作；预览不消费迭代，也不执行候选清理
/// Stop 为 Running 时，由 Merge 标志区分合并和细分
/// </summary>
struct TopologySplitDecision
{
    TopologySplitStop Stop{TopologySplitStop::NotStarted};
    bool Merge{false};
    DataOrientedRoamNodeIndex Node{InvalidDataOrientedRoamNodeIndex};
    std::uint64_t Path{0U};
    float Score{0.0F};
};

[[nodiscard]] TopologySplitDecision InspectSplitIteration(
    const DataOrientedRoamState& state, const TopologySplitIteration& iteration);

/// <summary>
/// 在原控制边界内应用已计划根，rootPath 为零时沿用严格头请求
/// 观察只含本次循环体；优先合并和停止不能被指定根绕过
/// 失败根保留部分修改，调用方必须撤销剩余批次并核查模型
/// </summary>
[[nodiscard]] TopologySplitStep AdvancePlannedSplitIteration(
    DataOrientedRoamState& state, TopologySplitIteration& iteration,
    std::uint64_t rootPath, TopologyConvergenceObservation& observation);

[[nodiscard]] TopologySplitIteration BeginStrictSplitIteration(DataOrientedRoamState& state);
/// <summary>
/// 只推进一个原循环体，保留原优先合并、失败屏蔽和预算交换规则
/// 不初始化迭代状态；逐步调用的离线耗时不冒充完整生产收敛的计时包络
/// </summary>
[[nodiscard]] TopologySplitStep AdvanceStrictSplitIteration(
    DataOrientedRoamState& state, TopologySplitIteration& iteration);

/// <summary>
/// 在自有副本上试算一个根；调用方仍须判断它是否获严格控制器批准
/// 不调整传入预算、不自动执行合并，也不把部分失败改为回滚
/// </summary>
[[nodiscard]] TopologySplitObservation AnalyzeSplitOperation(
    const DataOrientedRoamState& input, DataOrientedRoamNodeIndex root);

/// <summary>
/// 在调用方独占的副本上沿原严格规则演化，并记录最多 maximumRoots 个实际请求
/// 输入必须是已评分阶段状态；同步普通预算后继续原合并优先与预算交换流程
/// </summary>
[[nodiscard]] TopologyConvergenceObservation ObserveStrictSplitConvergence(
    DataOrientedRoamState& state, std::size_t maximumRoots);
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
