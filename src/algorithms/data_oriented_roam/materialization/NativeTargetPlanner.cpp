#include "algorithms/data_oriented_roam/materialization/NativeTargetPlanner.h"
#include "algorithms/data_oriented_roam/materialization/NativeRefinementSimulation.h"

#include <algorithm>
#include <array>
#include <optional>
#include <stdexcept>

namespace ParallelRoam::Algorithms::DataOrientedRoam::Materialization
{
namespace
{
using Node = DataOrientedRoamNodeIndex;
using Field = NativePlanningField;
using Activity = NativePlanningActivity;
using Kind = NativePlanningQueueKind;
using Obligation = NativeObligationKind;
constexpr auto Invalid = InvalidDataOrientedRoamNodeIndex;

// 默认恢复只使用来源和净活动转移；额外历史写入必须通过具名义务解释
std::uint64_t DefaultHistory(const NativePlanningView& view, Node node, Field field,
    Activity oldActivity, Activity targetActivity)
{
    const auto& source = view.Source();
    const auto value = view.Initial(node, field);
    // removed 包括随后又休眠的内部节点，其合并历史不能只给最终叶恢复
    const bool removed = oldActivity == Activity::Internal && targetActivity != Activity::Internal;
    const bool added = oldActivity != Activity::Internal && targetActivity == Activity::Internal;
    const bool activated = oldActivity == Activity::Dormant && targetActivity != Activity::Dormant;
    if (field == Field::SplitBuild && added) return source.BuildSequence;
    if (field == Field::MergeBuild && removed) return source.BuildSequence;
    if (field == Field::ActivatedBuild && (removed || activated)) return source.BuildSequence;
    if (field == Field::ForcedActivation && (removed || activated)) return 0;
    return value;
}

template<bool Trace>
void Extract(NativePlanningView& view,
    NativeRefinementSimulation<Trace>& simulation, NativeTargetPlan& result)
{
    const auto& source = view.Source();
    // 累计触及记录可以大于净差分，往返修改仍须检查是否留下历史义务
    result.Metrics.ExtractionNodes = view.TouchedCount();
    constexpr std::array fields{Field::ActivatedBuild, Field::SplitBuild, Field::MergeBuild,
        Field::ForcedActivation, Field::SplitBlockedBuild};
    constexpr std::array kinds{Obligation::ActivatedBuild, Obligation::SplitBuild, Obligation::MergeBuild,
        Obligation::ForcedActivation, Obligation::SplitBlockedBuild};
    for (std::size_t record = 0; record < view.TouchedCount(); ++record)
    {
        const auto node = view.TouchedNode(record);
        const auto oldActivity = static_cast<Activity>(view.Initial(node, Field::Activity));
        const auto targetActivity = static_cast<Activity>(view.ReadTouched(record, Field::Activity));
        const auto path = view.Path(node);
        // 事件是否存在由活动内部资格决定，不能由缓存 IsSplit 或物理创建次序代替
        if (oldActivity != Activity::Internal && targetActivity == Activity::Internal) result.AddedEvents.push_back(path);
        if (oldActivity == Activity::Internal && targetActivity != Activity::Internal) result.RemovedEvents.push_back(path);
        for (std::size_t index = 0; index < fields.size(); ++index)
        {
            const auto value = view.ReadTouched(record, fields[index]);
            if (value != DefaultHistory(view, node, fields[index], oldActivity, targetActivity))
                result.Obligations.push_back({kinds[index], path, value});
        }
    }
    // 目标活动层次已经隐含的缓存不用再登记，只有额外休眠缓存需要存在性义务
    for (std::size_t index = source.Nodes.size(); index < view.NodeCount(); ++index)
    {
        const auto node = static_cast<Node>(index);
        if (view.Activity(node) == Activity::Dormant)
            result.Obligations.push_back({Obligation::CacheBirth, view.Path(node), view.CreatedBuild(node)});
    }
    for (const auto path : simulation.FailedMergeRemovals())
    {
        // 最终拓扑已淘汰的失败候选无需额外抑制，避免 Γ 携带过期控制历史
        if (simulation.MissingFailedMerge(path))
            result.Obligations.push_back({Obligation::FailedMergeRemoval, path, 1});
    }
    // 遍历的是最终工作区记录，每个键只产出一次；排序仅规范化输出而不构造完整 J
    result.Metrics.ObligationPeak = result.Obligations.size();
    NativePlanningCostScope sortCost{view.Costs(), NativePlanningCost::Sort, true};
    std::sort(result.AddedEvents.begin(), result.AddedEvents.end());
    std::sort(result.RemovedEvents.begin(), result.RemovedEvents.end());
    std::sort(result.Obligations.begin(), result.Obligations.end());
    sortCost.Finish();
    result.Evaluations = simulation.Evaluations();
    result.Metrics.PayloadBytes = (result.AddedEvents.size() + result.RemovedEvents.size()) * sizeof(std::uint64_t) +
        result.Obligations.size() * sizeof(NativeContinuationObligation) + result.Evaluations.size() * sizeof(NativeScoreEvaluation);
}

template<bool Trace>
NativeTargetPlan Build(const DataOrientedRoamState& source, const NativePlanningAudit& audit)
{
    NativePlanningCostScope construction{audit.Costs, NativePlanningCost::Construct};
    // 显式结束拥有者的寿命以计入销毁，optional 的就地存储不引入额外堆对象
    std::optional<NativePlanningView> viewOwner{std::in_place, source, audit.CollectReadCoverage, audit.Costs};
    auto& view = *viewOwner;
    std::optional<NativePlanningQueues> queuesOwner{std::in_place, view, audit.CollectReadCoverage};
    auto& queues = *queuesOwner;
    DecisionTraceCursor trace{audit.Decisions};
    std::optional<NativeRefinementSimulation<Trace>> simulationOwner{
        std::in_place, view, queues, Trace ? &trace : nullptr, !audit.DisableScoreCache};
    auto& simulation = *simulationOwner;
    NativeTargetPlan result;
    construction.Finish();
    NativePlanningCostScope control{audit.Costs, NativePlanningCost::Control};
    result.BuildSequence = source.BuildSequence;
    result.SourceNodeCount = source.Nodes.size(); result.SourceLeafCount = source.ActiveLeafNodes.size();
    // 这些值描述本次同步输入，不是可跨状态套用的序列化事务认证令牌
    result.BudgetCap = source.Settings.TriangleBudget;
    result.MaximumIterations = std::max<std::size_t>(1024, result.BudgetCap * 8U + source.Nodes.size() * 4U);
    result.Stop = TopologySplitStop::Running;
    const auto stop = [&](TopologySplitStop reason) {
        result.Stop = reason;
        if constexpr (Trace)
        {
            trace.Iteration = result.Iteration; trace.Select(0);
            trace.Emit(DecisionEventKind::Stop, DecisionReason::None, view.RemainingBudget(), view.RemainingBudget(),
                static_cast<std::size_t>(reason), 0, result.MaximumIterations);
        }
    };
    const auto merge = [&](Node node, float score, bool exchange) {
        // 普通低分合并和预算交换共用 primitive，但仍保留各自的根选择原因
        ++simulation.Work.MergeRoots;
        if constexpr (Trace)
        {
            trace.Select(view.Path(node));
            trace.Emit(DecisionEventKind::SelectMerge, exchange ? DecisionReason::BudgetExchange : DecisionReason::LowScoreMerge,
                0, 0, 0, score);
        }
        const bool success = simulation.Merge(node);
        if constexpr (Trace)
        {
            trace.Select(view.Path(node));
            trace.Emit(DecisionEventKind::MergeRootEnd, success ? DecisionReason::Success : DecisionReason::Failure);
        }
        if (success) { if (exchange) ++simulation.Work.Exchanges; }
        else
        {
            ++simulation.Work.MergeRootFailures;
            simulation.RemoveFailedMerge(node);
            if constexpr (Trace) trace.Emit(DecisionEventKind::RemoveMerge, DecisionReason::Failure);
        }
    };
    const auto step = [&] {
        // 上限在入口冻结，后增判断包括最后的失败判断，不随缓存增长延长循环
        if (!(result.Iteration++ < result.MaximumIterations)) { stop(TopologySplitStop::IterationLimit); return; }
        if constexpr (Trace) trace.Iteration = result.Iteration;
        const auto lowMerge = queues.Top(Kind::Merge);
        if (view.IsValidNode(lowMerge.Node) && lowMerge.Score < source.Settings.MergeThreshold)
        {
            merge(lowMerge.Node, lowMerge.Score, false);
            return;
        }
        const auto split = queues.Top(Kind::Split);
        // 资格判断只针对当前严格头部，不跳过停止项去寻找更低分请求
        if (!simulation.WantsSplit(split.Node, split.Score)) { stop(TopologySplitStop::NoEligibleSplit); return; }
        if constexpr (Trace)
        {
            trace.Select(view.Path(split.Node));
            trace.Emit(DecisionEventKind::SelectSplit, DecisionReason::Requested, 0, 0, 0, split.Score);
        }
        ++simulation.Work.SplitRoots;
        const auto rejectionsBefore = simulation.Work.BudgetRejections;
        const bool success = simulation.Split(split.Node);
        const bool rejected = simulation.Work.BudgetRejections > rejectionsBefore;
        if constexpr (Trace) trace.Emit(DecisionEventKind::SplitRootEnd,
            success ? DecisionReason::Success : DecisionReason::Failure, 0, 0, rejected ? 1U : 0U);
        if (success) return;
        ++simulation.Work.SplitRootFailures;
        // 部分前置可能已改变合并队首，交换必须在失败后重新查询
        const auto currentMerge = queues.Top(Kind::Merge);
        if (rejected && view.IsValidNode(currentMerge.Node) && split.Score > currentMerge.Score)
        {
            merge(currentMerge.Node, currentMerge.Score, true);
            return;
        }
        if (rejected) { stop(TopologySplitStop::BudgetBlocked); return; }
        simulation.Block(split.Node);
        if constexpr (Trace) trace.Emit(DecisionEventKind::BlockSplit, DecisionReason::Failure);
    };
    while (result.Stop == TopologySplitStop::Running)
    {
        step();
        if (audit.AfterStep) audit.AfterStep(audit.Context, view, queues);
    }
    result.FinalLeafCount = queues.Size(Kind::Split);
    // 所有根已退出，递归预留必须全部结清，此时 Q_s 长度才能与余量直接核账
    result.RemainingBudget = view.RemainingBudget();
    if (result.FinalLeafCount + result.RemainingBudget != result.BudgetCap)
        throw std::logic_error("native planning budget ledger disagrees with leaf count");
    control.Finish();
    {
        NativePlanningCostScope extraction{audit.Costs, NativePlanningCost::Extract};
        Extract(view, simulation, result);
    }
    // 先冻结执行和提取成本，再让外部验证器做全量遍历，避免将审计扫描混入覆盖报告
    NativePlanningCostScope metrics{audit.Costs, NativePlanningCost::Metrics};
    result.Metrics.View = view.Metrics();
    // 读覆盖来自诊断遍；其中包括轻量轨迹取稳定身份的查询，不能冒充无诊断 load 数
    result.Metrics.SplitQueue = queues.Metrics(Kind::Split); result.Metrics.MergeQueue = queues.Metrics(Kind::Merge);
    result.Metrics.MergeRelationRecords = queues.MergeRelationRecords(); result.Metrics.Work = simulation.Work;
    result.Metrics.Storage[0] = view.StorageMetrics();
    const auto queueStorage = queues.StorageMetrics();
    std::copy(queueStorage.begin(), queueStorage.end(), result.Metrics.Storage.begin() + 1);
    result.Metrics.Storage[7] = simulation.StorageMetrics();
    metrics.Finish();
    if (audit.Finished) audit.Finished(audit.Context, view, queues, result);
    {
        NativePlanningCostScope destruction{audit.Costs, NativePlanningCost::Destroy};
        simulationOwner.reset(); queuesOwner.reset(); viewOwner.reset();
    }
    // 返回后局部容器析构仍在调用包络内，调用方另计结果所有权的清理成本
    return result;
}
}

NativeTargetPlan BuildNativeSplitTarget(const DataOrientedRoamState& source, const NativePlanningAudit& audit)
{
    if (audit.Costs && (audit.Decisions.Append || audit.AfterStep || audit.Finished || audit.CollectReadCoverage))
        throw std::invalid_argument("cost observation must not include semantic audits");
    // 此入口验证固定实验契约，不悄悄改变来源设置或将非共形状态修成另一任务
    if (!source.Settings.EnableLocalConstraints || source.Settings.MirrorSplitScoresToNodePool ||
        source.Settings.TriangleBudget < source.ActiveLeafNodes.size())
        throw std::invalid_argument("native target requires conforming, budget-valid input with score mirroring disabled");
    return audit.Decisions.Append ? Build<true>(source, audit) : Build<false>(source, audit);
}
}
