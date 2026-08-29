#pragma once

#include "algorithms/ITerrainLodAlgorithm.h"

#include <cstdint>

namespace ParallelRoam::Algorithms
{
/// <summary>
/// 标识单次算法结果没有满足哪一项公共结果约束
/// </summary>
enum class TerrainLodResultValidationFailure : std::uint64_t
{
    None = 0U,
    MissingBuildSequence = 1ULL << 0U,
    MissingEvidence = 1ULL << 1U,
    MissingTriangleBudget = 1ULL << 2U,
    TriangleBudgetExceeded = 1ULL << 3U,
    SplitQueueMismatch = 1ULL << 4U,
    InvalidRenderPacket = 1ULL << 5U,
    MeshPopulationMismatch = 1ULL << 6U,
    IncrementalMeshStatsMismatch = 1ULL << 7U,
    CorrectnessFailure = 1ULL << 8U,
};

/// <summary>
/// 标识两条算法路径在同一帧上出现差异的结果维度
/// </summary>
enum class TerrainLodReferenceDifference : std::uint64_t
{
    None = 0U,
    InvalidResult = 1ULL << 0U,
    Input = 1ULL << 1U,
    Topology = 1ULL << 2U,
    ActiveLeaves = 1ULL << 3U,
    NormalizedMesh = 1ULL << 4U,
    Budget = 1ULL << 5U,
    ActivePopulation = 1ULL << 6U,
    QueuePopulation = 1ULL << 7U,
    TopologyEvents = 1ULL << 8U,
    PassSemantics = 1ULL << 9U,
};

/// <summary>
/// 保存单条算法路径的检查结果和失败项
/// </summary>
struct TerrainLodResultValidation
{
    bool Passed{false};
    std::uint64_t FailureMask{0U};
};

/// <summary>
/// 保存两条算法路径的结果是否等价以及差异项
/// </summary>
struct TerrainLodReferenceComparison
{
    bool Equivalent{false};
    std::uint64_t DifferenceMask{0U};
};

[[nodiscard]] constexpr std::uint64_t TerrainLodResultBit(
    TerrainLodResultValidationFailure failure)
{
    return static_cast<std::uint64_t>(failure);
}

[[nodiscard]] constexpr std::uint64_t TerrainLodReferenceBit(
    TerrainLodReferenceDifference difference)
{
    return static_cast<std::uint64_t>(difference);
}

/// <summary>
/// 使用公共统计和渲染结果检查单条算法路径是否满足统一结果约束
/// </summary>
[[nodiscard]] inline TerrainLodResultValidation ValidateTerrainLodResult(
    const TerrainLodStats& stats,
    const TerrainLodRenderPacket& packet,
    bool requireEvidence)
{
    std::uint64_t failures = 0U;
    const auto record = [&failures](bool failed, TerrainLodResultValidationFailure failure) {
        if (failed)
        {
            failures |= TerrainLodResultBit(failure);
        }
    };

    record(stats.BuildSequence == 0U, TerrainLodResultValidationFailure::MissingBuildSequence);
    record(
        requireEvidence &&
            (stats.ReplayInputHash == 0U || stats.TopologyHash == 0U ||
             stats.ActiveLeafHash == 0U || stats.MeshHash == 0U ||
             stats.NormalizedMeshHash == 0U),
        TerrainLodResultValidationFailure::MissingEvidence);
    record(stats.TriangleBudget == 0U, TerrainLodResultValidationFailure::MissingTriangleBudget);
    record(
        stats.BudgetViolationCount != 0U ||
            (stats.TriangleBudget != 0U && stats.ActiveTriangleCount > stats.TriangleBudget),
        TerrainLodResultValidationFailure::TriangleBudgetExceeded);
    record(
        stats.PersistentSplitQueueSize != 0U &&
            stats.PersistentSplitQueueSize != stats.ActiveTriangleCount,
        TerrainLodResultValidationFailure::SplitQueueMismatch);
    record(
        !packet.HasConsistentResourceContract() || stats.ResourceValidationFailureCount != 0U,
        TerrainLodResultValidationFailure::InvalidRenderPacket);

    const Terrain::TerrainMeshData* mesh = packet.ResolveCpuMesh();
    const bool cpuMeshMismatch = packet.Mode == TerrainLodRenderMode::CpuMesh &&
        (mesh == nullptr ||
         mesh->Vertices.size() != stats.ActiveTriangleCount * 3U ||
         mesh->Indices.size() != stats.ActiveTriangleCount * 3U ||
         packet.ActiveLeafCount != stats.ActiveTriangleCount ||
         packet.ActiveTriangleCount != stats.ActiveTriangleCount ||
         packet.IndexCount != mesh->Indices.size());
    record(cpuMeshMismatch, TerrainLodResultValidationFailure::MeshPopulationMismatch);

    const bool reportsIncrementalMesh =
        stats.CpuMeshFullRebuildCount != 0U ||
        stats.CpuMeshUpdatedTriangleCount != 0U ||
        stats.CpuMeshReusedTriangleCount != 0U ||
        stats.CpuMeshDirtyRangeCount != 0U;
    record(
        reportsIncrementalMesh &&
            (stats.CpuMeshFullRebuildCount > 1U ||
             stats.CpuMeshUpdatedTriangleCount + stats.CpuMeshReusedTriangleCount !=
                 stats.ActiveTriangleCount ||
             stats.CpuMeshDirtyRangeCount > stats.CpuMeshUpdatedTriangleCount),
        TerrainLodResultValidationFailure::IncrementalMeshStatsMismatch);
    record(
        stats.QueueInvariantViolationCount != 0U ||
            stats.TjunctionCount != 0U || stats.InvalidNeighborCount != 0U ||
            stats.InvalidTopologyCount != 0U,
        TerrainLodResultValidationFailure::CorrectnessFailure);

    return TerrainLodResultValidation{failures == 0U, failures};
}

/// <summary>
/// 比较两条算法路径的规范化结果和同帧行为，不要求实际执行方式相同
/// </summary>
[[nodiscard]] inline TerrainLodReferenceComparison CompareTerrainLodResults(
    const TerrainLodStats& reference,
    const TerrainLodStats& result)
{
    std::uint64_t differences = 0U;
    const auto record = [&differences](bool different, TerrainLodReferenceDifference difference) {
        if (different)
        {
            differences |= TerrainLodReferenceBit(difference);
        }
    };

    record(
        !reference.ResultValidationEvaluated || !reference.ResultValidationPassed ||
            !result.ResultValidationEvaluated || !result.ResultValidationPassed,
        TerrainLodReferenceDifference::InvalidResult);
    record(
        reference.BuildSequence != result.BuildSequence ||
            reference.ReplayInputHash != result.ReplayInputHash,
        TerrainLodReferenceDifference::Input);
    record(reference.TopologyHash != result.TopologyHash, TerrainLodReferenceDifference::Topology);
    record(
        reference.ActiveLeafHash != result.ActiveLeafHash,
        TerrainLodReferenceDifference::ActiveLeaves);
    record(
        reference.NormalizedMeshHash != result.NormalizedMeshHash,
        TerrainLodReferenceDifference::NormalizedMesh);
    record(
        reference.TriangleBudget != result.TriangleBudget ||
            reference.BudgetViolationCount != result.BudgetViolationCount,
        TerrainLodReferenceDifference::Budget);
    record(
        reference.ActiveTriangleCount != result.ActiveTriangleCount ||
            reference.ActiveSplitCount != result.ActiveSplitCount,
        TerrainLodReferenceDifference::ActivePopulation);
    record(
        reference.PersistentSplitQueueSize != result.PersistentSplitQueueSize ||
            reference.PersistentMergeQueueSize != result.PersistentMergeQueueSize,
        TerrainLodReferenceDifference::QueuePopulation);
    record(
        reference.SplitCount != result.SplitCount ||
            reference.ForcedSplitCount != result.ForcedSplitCount ||
            reference.MergeCount != result.MergeCount ||
            reference.QueueCrossoverCount != result.QueueCrossoverCount,
        TerrainLodReferenceDifference::TopologyEvents);

    const TerrainLodPassTrace& referenceMergeScore = TerrainLodPassTraceFor(
        reference.PassTraces,
        TerrainLodPassId::MergeScore);
    const TerrainLodPassTrace& resultMergeScore = TerrainLodPassTraceFor(
        result.PassTraces,
        TerrainLodPassId::MergeScore);
    const TerrainLodPassTrace& referenceSplitScore = TerrainLodPassTraceFor(
        reference.PassTraces,
        TerrainLodPassId::SplitScore);
    const TerrainLodPassTrace& resultSplitScore = TerrainLodPassTraceFor(
        result.PassTraces,
        TerrainLodPassId::SplitScore);

    const TerrainLodPassTrace& referenceMergeTopology = TerrainLodPassTraceFor(
        reference.PassTraces,
        TerrainLodPassId::MergeTopology);
    const TerrainLodPassTrace& resultMergeTopology = TerrainLodPassTraceFor(
        result.PassTraces,
        TerrainLodPassId::MergeTopology);
    const TerrainLodPassTrace& referenceSplitTopology = TerrainLodPassTraceFor(
        reference.PassTraces,
        TerrainLodPassId::SplitTopology);
    const TerrainLodPassTrace& resultSplitTopology = TerrainLodPassTraceFor(
        result.PassTraces,
        TerrainLodPassId::SplitTopology);
    const TerrainLodPassTrace& referenceMesh = TerrainLodPassTraceFor(
        reference.PassTraces,
        TerrainLodPassId::MeshEmit);
    const TerrainLodPassTrace& resultMesh = TerrainLodPassTraceFor(
        result.PassTraces,
        TerrainLodPassId::MeshEmit);
    record(
        referenceMergeScore.MembershipUpdate != resultMergeScore.MembershipUpdate ||
            referenceMergeScore.PriorityRefresh != resultMergeScore.PriorityRefresh ||
            referenceSplitScore.MembershipUpdate != resultSplitScore.MembershipUpdate ||
            referenceSplitScore.PriorityRefresh != resultSplitScore.PriorityRefresh ||
            referenceMergeTopology.MembershipUpdate != resultMergeTopology.MembershipUpdate ||
            referenceMergeTopology.DataUpdate != resultMergeTopology.DataUpdate ||
            referenceSplitTopology.MembershipUpdate != resultSplitTopology.MembershipUpdate ||
            referenceSplitTopology.DataUpdate != resultSplitTopology.DataUpdate ||
            referenceMesh.DataUpdate != resultMesh.DataUpdate,
        TerrainLodReferenceDifference::PassSemantics);
    return TerrainLodReferenceComparison{differences == 0U, differences};
}
} // 命名空间 ParallelRoam::Algorithms
