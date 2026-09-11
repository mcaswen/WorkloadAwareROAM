#pragma once

#include "algorithms/cpu_cbt/CpuCbtState.h"
#include "algorithms/ITerrainLodAlgorithm.h"

#include <optional>

namespace ParallelRoam::Algorithms::CpuCbt
{
/// <summary>
/// 控制只读预算诊断；开关不参与候选选择或真实状态演化
/// </summary>
struct CpuCbtUpdateOptions
{
    bool EvaluatePoolOnlyDemand{false};
};

/// <summary>
/// 同步一轮的工作量和成本；需求诊断未启用时保留空值，不编码成零需求
/// 细分与合并内部采用参考组合调用，耗时不冒充其尚未分解的内部阶段
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
};

/// <summary>
/// 按固定物理顺序完成分类、联合细分与旧合并重验，返回前发布完整下一代
/// 失败保持输入状态；调用者不得并发更新或读取该状态
/// </summary>
[[nodiscard]] CpuCbtUpdateReport UpdateCpuCbt(CpuCbtState& state,
    const Terrain::HeightMap& heightMap, const TerrainLodViewInput& view,
    const CpuCbtUpdateOptions& options = {});
} // 命名空间 ParallelRoam::Algorithms::CpuCbt
