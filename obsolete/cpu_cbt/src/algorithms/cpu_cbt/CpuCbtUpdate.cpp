#include "algorithms/cpu_cbt/CpuCbtUpdate.h"
#include "algorithms/cpu_cbt/CpuCbtMesh.h"
#include "algorithms/cbt_2024/CbtBisectCommit.h"
#include "algorithms/cbt_2024/CbtSimplifyCommit.h"
#include "algorithms/cbt_2024/CbtSplitPlanner.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace ParallelRoam::Algorithms::CpuCbt
{
namespace
{
using namespace Cbt2024;
using Clock = std::chrono::steady_clock;
double Elapsed(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

/// <summary>
/// 检查后续参考调用所需的数组、编号和邻接前提，避免越界进入未检查的参考访问
/// </summary>
void CheckInput(const CpuCbtState& state, const TerrainLodViewInput& view)
{
    std::string error;
    if (!ValidateCpuCbtSettings(state.Settings, error)) throw std::runtime_error(error);
    constexpr auto total = CpuCbtState::DynamicCapacity + CbtBaseBisectorCount;
    Require(state.HeapIds.size() == total && state.Neighbors.size() == total && state.Data.size() == total,
        "CPU CBT 物理数组长度错误");
    Require(state.ActiveIndices.size() == state.Occupancy.BitCount() + CbtBaseBisectorCount &&
        state.ActiveIndices.size() <= state.Settings.TriangleBudget, "CPU CBT 活动数量或预算无效");
    Require(std::is_sorted(state.ActiveIndices.begin(), state.ActiveIndices.end()) &&
        std::adjacent_find(state.ActiveIndices.begin(), state.ActiveIndices.end()) == state.ActiveIndices.end(),
        "CPU CBT 活动列表不唯一或未排序");
    for (const auto slot : state.ActiveIndices)
    {
        Require(slot < total && state.HeapIds[slot] != 0U, "CPU CBT 活动槽失效");
        const auto depth = static_cast<std::uint32_t>(std::bit_width(state.HeapIds[slot]));
        Require(depth >= CbtBaseDepth && depth <= state.Settings.MaxHeapBitDepth, "CPU CBT 活动编号深度越界");
        const auto prefix = state.HeapIds[slot] >> (depth - CbtBaseDepth);
        Require(prefix >= 8U && prefix < 14U, "CPU CBT 活动编号不属于六基础面");
        Require(slot >= CpuCbtState::DynamicCapacity || state.Occupancy.GetBit(slot), "CPU CBT 活动槽未占用");
    }
    for (auto slot = CpuCbtState::DynamicCapacity; slot < total; ++slot)
        Require(std::binary_search(state.ActiveIndices.begin(), state.ActiveIndices.end(), slot),
            "CPU CBT 永久槽丢失");
    Require(view.DrawableWidth > 0U && view.DrawableHeight > 0U, "CPU CBT 视图尺寸无效");
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            Require(std::isfinite(view.ViewProjection[column][row]), "CPU CBT 投影非有限");
    Require(std::isfinite(glm::determinant(view.ViewProjection)) && glm::determinant(view.ViewProjection) != 0.0F,
        "CPU CBT 投影矩阵不可逆");
}

/// <summary>
/// 参考计划器只接受活动局部节点，转换邻接时保留开放边界哨兵
/// </summary>
std::vector<CbtSplitPlanningNode> PlanningView(const CpuCbtState& state,
    const std::vector<CbtBisectorData>& data)
{
    std::vector<std::uint32_t> inverse(state.HeapIds.size(), InvalidCbtBisectorIndex);
    for (std::size_t i = 0U; i < state.ActiveIndices.size(); ++i)
        inverse[state.ActiveIndices[i]] = static_cast<std::uint32_t>(i);
    const auto local = [&](std::uint32_t physical) {
        if (physical == InvalidCbtBisectorIndex) return physical;
        Require(physical < inverse.size() && inverse[physical] != InvalidCbtBisectorIndex,
            "CPU CBT 邻接指向非活动物理槽");
        return inverse[physical];
    };
    std::vector<CbtSplitPlanningNode> nodes;
    nodes.reserve(state.ActiveIndices.size());
    for (const auto physical : state.ActiveIndices)
    {
        const auto& n = state.Neighbors[physical];
        nodes.push_back({state.HeapIds[physical], {local(n.Previous), local(n.Next), local(n.Twin)},
            static_cast<std::int32_t>(data[physical].BisectorState)});
    }
    return nodes;
}
}

CpuCbtUpdateReport UpdateCpuCbt(CpuCbtState& state, const Terrain::HeightMap& heightMap,
    const TerrainLodViewInput& view, const CpuCbtUpdateOptions& options, const CpuCbtRangeExecutor& executor)
{
    CpuCbtUpdateReport report;
    const auto started = Clock::now();
    try
    {
        auto stage = Clock::now();
        CheckInput(state, view);
        Require(options.BisectImplementation == CpuCbtBisectImplementation::ReferenceSerial ||
            options.BisectImplementation == CpuCbtBisectImplementation::LocalTemplates, "CPU CBT 未知细分实现");
        auto data = state.Data;
        report.PreparationMs = Elapsed(stage);
        report.TriangleCountBefore = static_cast<std::uint32_t>(state.ActiveIndices.size());
        report.OldFreeDynamicSlots = CpuCbtState::DynamicCapacity - state.Occupancy.BitCount();
        report.BudgetRemainingBefore = state.Settings.TriangleBudget - report.TriangleCountBefore;
        stage = Clock::now();
        const auto geometry = BuildCpuCbtClassificationGeometry(state, heightMap);
        report.ClassificationGeometryMs = Elapsed(stage);
        stage = Clock::now();
        std::vector<std::uint32_t> splits, merges;
        for (std::size_t i = 0U; i < state.ActiveIndices.size(); ++i)
        {
            const auto physical = state.ActiveIndices[i];
            const auto heapId = state.HeapIds[physical];
            const auto depth = static_cast<std::uint32_t>(std::bit_width(heapId));
            // 清除上轮传播与分配上下文，保留持久拓扑；决策不使用调试事件位
            auto& current = data[physical];
            current = {};
            current.Indices.fill(InvalidCbtBisectorIndex);
            current.ProblematicNeighbor = current.PropagationId = InvalidCbtBisectorIndex;
            const auto evaluation = EvaluateCbtClassification(geometry[i], view,
                state.Settings.TriangleAreaPixels, depth, state.Settings.MaxHeapBitDepth);
            Require(std::isfinite(evaluation.TriangleAreaPixels) && std::isfinite(evaluation.ParentAreaPixels),
                "CPU CBT 参考分类产生非有限面积");
            current.Flags = EncodeCbtActiveDepth(depth);
            if (evaluation.Result >= CbtClassificationResult::TooSmall) current.Flags |= CbtVisibleFlag;
            if (evaluation.Result == CbtClassificationResult::Bisect)
            {
                current.BisectorState = CbtBisectElement;
                splits.push_back(static_cast<std::uint32_t>(i));
            }
            else if (depth != CbtBaseDepth && evaluation.Result < CbtClassificationResult::Unchanged)
            {
                // 原候选列表保留物理编号，合并提交前由参考实现重新验证
                current.BisectorState = CbtSimplifyElement;
                if ((heapId & 1U) == 0U) merges.push_back(physical);
            }
        }
        report.SplitProposalCount = static_cast<std::uint32_t>(splits.size());
        report.MergeProposalCount = static_cast<std::uint32_t>(merges.size());
        report.ClassificationMs = Elapsed(stage);
        stage = Clock::now();
        const auto nodes = PlanningView(state, data);
        report.MappingMs = Elapsed(stage);
        if (options.EvaluatePoolOnlyDemand)
        {
            stage = Clock::now();
            const auto diagnostic = PlanCbtSplits(nodes, splits, CbtBaseDepth, report.OldFreeDynamicSlots);
            Require(diagnostic.Valid, "CPU CBT 只读需求计划失败");
            report.PoolOnlyRequiredSlots = diagnostic.RequiredSlotCount;
            report.PoolOnlyReservationRejections = diagnostic.RejectedCandidateCount;
            report.DemandDiagnosticMs = Elapsed(stage);
        }
        stage = Clock::now();
        const auto allowance = std::min(report.OldFreeDynamicSlots, report.BudgetRemainingBefore);
        const auto plan = PlanCbtSplits(nodes, splits, CbtBaseDepth, allowance);
        Require(plan.Valid && plan.RequiredSlotCount <= allowance, "CPU CBT 细分计划无效或超出硬预算");
        report.RequiredSlots = plan.RequiredSlotCount;
        report.ReservationRejectedCount = plan.RejectedCandidateCount;
        report.DuplicateCandidateCount = plan.DuplicateCandidateCount;
        report.PlannerRemainingSlots = plan.RemainingMemory;
        report.BudgetLimitedRound = plan.RejectedCandidateCount > 0U && report.BudgetRemainingBefore < report.OldFreeDynamicSlots;
        report.JointlyLimitedRound = plan.RejectedCandidateCount > 0U && report.BudgetRemainingBefore == report.OldFreeDynamicSlots;
        report.PlanningMs = Elapsed(stage);
        stage = Clock::now();
        const auto allocation = AllocateCbtSplitSlots(plan, state.Occupancy);
        Require(allocation.Valid && allocation.AllocatedSlotCount == plan.RequiredSlotCount, "CPU CBT 分配与计划不一致");
        std::vector<std::uint32_t> allocationNodes;
        std::vector<unsigned char> claimed(CpuCbtState::DynamicCapacity, 0U);
        for (const auto local : plan.AllocationNodes)
        {
            const auto physical = state.ActiveIndices.at(local);
            const auto pattern = plan.SubdivisionPatterns.at(local);
            const auto slots = static_cast<std::uint32_t>(std::popcount(pattern));
            const auto growth = pattern == CbtCenterSplitPattern ? 1U : 2U;
            Require(pattern == CbtCenterSplitPattern || pattern == CbtRightDoubleSplitPattern ||
                pattern == CbtLeftDoubleSplitPattern || pattern == CbtTripleSplitPattern, "CPU CBT 未知细分模板");
            Require(std::bit_width(state.HeapIds[physical]) + growth <= state.Settings.MaxHeapBitDepth,
                "CPU CBT 强制模板将越过深度上限");
            data[physical].SubdivisionPattern = pattern;
            data[physical].Indices = allocation.NodeIndices.at(local);
            for (std::uint32_t i = 0U; i < slots; ++i)
            {
                // 分配结果已经是物理槽，不能再通过活动视图做一次映射
                const auto slot = data[physical].Indices[i];
                Require(slot < CpuCbtState::DynamicCapacity && !claimed[slot] &&
                    state.HeapIds[slot] == 0U && !state.Occupancy.GetBit(slot), "CPU CBT 新槽不唯一或已占用");
                claimed[slot] = 1U;
            }
            allocationNodes.push_back(physical);
        }
        report.AllocationMs = Elapsed(stage);
        stage = Clock::now();
        CbtBisectCommitResult split;
        if (options.BisectImplementation == CpuCbtBisectImplementation::ReferenceSerial)
            split = CommitCbtBisects(state.HeapIds, state.Neighbors, data, allocationNodes, CpuCbtState::DynamicCapacity);
        else
        {
            // 只替换细分提交入口，合并候选和占用发布继续消费同一份计划结果
            auto local = CommitCpuCbtBisects(state.HeapIds, state.Neighbors, data, allocationNodes,
                CpuCbtState::DynamicCapacity, executor);
            report.LocalBisectTimings = local.Timings;
            split = std::move(local.Topology);
        }
        Require(split.Valid, "CPU CBT 参考细分提交失败");
        report.AcceptedSlots = static_cast<std::uint32_t>(split.CommittedDynamicSlots.size());
        report.TemplateCounts = split.TemplateCounts;
        report.TriangleCountAfterSplit = report.TriangleCountBefore + report.AcceptedSlots;
        Require(report.AcceptedSlots == report.RequiredSlots && report.TriangleCountAfterSplit <= state.Settings.TriangleBudget,
            "CPU CBT 细分后数量与硬预算不一致");
        report.BudgetRemainingAfterSplit = state.Settings.TriangleBudget - report.TriangleCountAfterSplit;
        report.BisectCommitMs = Elapsed(stage);
        stage = Clock::now();
        auto merged = CommitCbtSimplifications(split.HeapIds, split.Neighbors, split.BisectorData, merges, CpuCbtState::DynamicCapacity);
        Require(merged.Valid, "CPU CBT 参考合并提交失败");
        report.PairMergeCount = merged.PairMergeCount;
        report.QuadMergeCount = merged.QuadMergeCount;
        report.ReleasedSlots = static_cast<std::uint32_t>(merged.ReleasedDynamicSlots.size());
        Require(report.ReleasedSlots == report.PairMergeCount + 2U * report.QuadMergeCount &&
            report.ReleasedSlots <= report.TriangleCountAfterSplit - CbtBaseBisectorCount, "CPU CBT 合并释放数量异常");
        report.SimplifyCommitMs = Elapsed(stage);
        stage = Clock::now();
        auto occupancy = state.Occupancy;
        for (const auto slot : split.CommittedDynamicSlots)
            Require(occupancy.SetBit(slot, true), "CPU CBT 细分置位越界");
        for (const auto slot : merged.ReleasedDynamicSlots)
            Require(occupancy.GetBit(slot) && occupancy.SetBit(slot, false), "CPU CBT 合并释放非占用槽");
        occupancy.Reduce();
        auto active = occupancy.ActiveIndices();
        for (auto slot = CpuCbtState::DynamicCapacity; slot < state.HeapIds.size(); ++slot)
        {
            Require(merged.HeapIds[slot] != 0U, "CPU CBT 合并删除永久槽");
            active.push_back(slot);
        }
        report.TriangleCountAfter = report.TriangleCountAfterSplit - report.ReleasedSlots;
        Require(active.size() == report.TriangleCountAfter, "CPU CBT 归约结果与拓扑数量不一致");
        report.BudgetRemainingAfter = state.Settings.TriangleBudget - report.TriangleCountAfter;
        // 最后才移动所有持久数组，前面的任何失败均不改变调用者可见状态
        state.HeapIds = std::move(merged.HeapIds);
        state.Neighbors = std::move(merged.Neighbors);
        state.Data = std::move(merged.BisectorData);
        state.Occupancy = std::move(occupancy);
        state.ActiveIndices = std::move(active);
        ++state.Generation;
        report.PublishMs = Elapsed(stage);
        report.Success = true;
    }
    catch (const std::exception& exception)
    {
        report.Error = exception.what();
    }
    report.UpdateMs = Elapsed(started);
    return report;
}
} // 命名空间 ParallelRoam::Algorithms::CpuCbt
