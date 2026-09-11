#pragma once

#include "algorithms/cpu_cbt/CpuCbtState.h"
#include "algorithms/cpu_cbt/CpuCbtBisect.h"
#include "algorithms/ITerrainLodAlgorithm.h"

#include <optional>

namespace ParallelRoam::Algorithms::CpuCbt
{
/// <summary>
/// 细分提交的实现边界；两个值共享分类、计划、预算和后续合并语义
/// </summary>
enum class CpuCbtBisectImplementation { ReferenceSerial, LocalTemplates };

/// <summary>
/// 控制只读预算诊断和细分实现；候选选择与预算规则不随执行方式改变
/// </summary>
struct CpuCbtUpdateOptions
{
    bool EvaluatePoolOnlyDemand{false};
    CpuCbtBisectImplementation BisectImplementation{CpuCbtBisectImplementation::ReferenceSerial};
};

/// <summary>
/// 同步一轮的工作量和成本；需求诊断未启用时保留空值，不编码成零需求
/// 新内核单独报告内部成本；原参考的内部成本未采集，不用零值冒充测量
/// </summary>
struct CpuCbtUpdateReport
{
    bool Success{false};
    std::string Error;
    std::uint32_t TriangleCountBefore{0U}, TriangleCountAfterSplit{0U}, TriangleCountAfter{0U};
    std::uint32_t SplitProposalCount{0U}, MergeProposalCount{0U};
    std::uint32_t RequiredSlots{0U}, AcceptedSlots{0U}, ReleasedSlots{0U};
    std::uint32_t ReservationRejectedCount{0U}, DuplicateCandidateCount{0U}, PlannerRemainingSlots{0U};
    std::uint32_t OldFreeDynamicSlots{0U};
    std::uint32_t BudgetRemainingBefore{0U}, BudgetRemainingAfterSplit{0U}, BudgetRemainingAfter{0U};
    std::uint32_t PairMergeCount{0U}, QuadMergeCount{0U};
    std::array<std::uint32_t, 4> TemplateCounts{};
    bool BudgetLimitedRound{false}, JointlyLimitedRound{false};
    std::optional<std::uint32_t> PoolOnlyRequiredSlots, PoolOnlyReservationRejections;
    double ClassificationGeometryMs{0.0}, ClassificationMs{0.0}, MappingMs{0.0};
    double PlanningMs{0.0}, AllocationMs{0.0}, PreparationMs{0.0};
    double BisectCommitMs{0.0}, SimplifyCommitMs{0.0}, PublishMs{0.0};
    // 诊断耗时单列；含诊断的调用总耗时不参与性能对照
    double DemandDiagnosticMs{0.0}, UpdateMs{0.0};
    std::optional<CpuCbtBisectTimings> LocalBisectTimings;
};

/// <summary>
/// 按固定物理顺序完成分类、联合细分与旧合并重验，返回前发布完整下一代
/// 失败保持输入状态；调用者不得并发更新或读取该状态
/// </summary>
[[nodiscard]] CpuCbtUpdateReport UpdateCpuCbt(CpuCbtState& state,
    const Terrain::HeightMap& heightMap, const TerrainLodViewInput& view,
    const CpuCbtUpdateOptions& options = {}, const CpuCbtRangeExecutor& executor = {});
} // 命名空间 ParallelRoam::Algorithms::CpuCbt
