#include "algorithms/TerrainLodResultValidation.h"

#include <cstdint>
#include <iostream>

namespace
{
using namespace ParallelRoam::Algorithms;

bool HasBit(std::uint64_t mask, TerrainLodResultValidationFailure failure)
{
    return (mask & TerrainLodResultBit(failure)) != 0U;
}

bool HasBit(std::uint64_t mask, TerrainLodReferenceDifference difference)
{
    return (mask & TerrainLodReferenceBit(difference)) != 0U;
}

TerrainLodStats MakeValidStats()
{
    TerrainLodStats stats{};
    stats.BuildSequence = 1U;
    stats.ReplayInputHash = 11U;
    stats.TopologyHash = 13U;
    stats.ActiveLeafHash = 17U;
    stats.MeshHash = 19U;
    stats.NormalizedMeshHash = 23U;
    stats.TriangleBudget = 8U;
    stats.ActiveTriangleCount = 2U;
    stats.ActiveNodeCount = 3U;
    stats.ActiveSplitCount = 1U;
    stats.PersistentSplitQueueSize = 2U;
    stats.PersistentMergeQueueSize = 1U;
    stats.CpuMeshFullRebuildCount = 1U;
    stats.CpuMeshUpdatedTriangleCount = 2U;
    stats.CpuMeshDirtyRangeCount = 1U;

    TerrainLodPassTrace& mergeScore = TerrainLodPassTraceFor(
        stats.PassTraces,
        TerrainLodPassId::MergeScore);
    mergeScore.EffectiveAction = TerrainLodPassAction::SerialFullRefresh;
    mergeScore.MembershipUpdate = TerrainLodMembershipUpdateMode::Incremental;
    mergeScore.PriorityRefresh = TerrainLodPriorityRefreshMode::FullAllCurrentEntries;
    mergeScore.CandidateCount = 1U;

    TerrainLodPassTrace& splitScore = TerrainLodPassTraceFor(
        stats.PassTraces,
        TerrainLodPassId::SplitScore);
    splitScore.EffectiveAction = TerrainLodPassAction::SerialFullRefresh;
    splitScore.MembershipUpdate = TerrainLodMembershipUpdateMode::Incremental;
    splitScore.PriorityRefresh = TerrainLodPriorityRefreshMode::FullAllCurrentEntries;
    splitScore.CandidateCount = 2U;

    TerrainLodPassTrace& mergeTopology = TerrainLodPassTraceFor(
        stats.PassTraces,
        TerrainLodPassId::MergeTopology);
    mergeTopology.EffectiveAction = TerrainLodPassAction::SerialImmediate;
    mergeTopology.MembershipUpdate = TerrainLodMembershipUpdateMode::Incremental;
    mergeTopology.DataUpdate = TerrainLodDataUpdateMode::Incremental;

    TerrainLodPassTrace& splitTopology = TerrainLodPassTraceFor(
        stats.PassTraces,
        TerrainLodPassId::SplitTopology);
    splitTopology.EffectiveAction = TerrainLodPassAction::SerialImmediate;
    splitTopology.MembershipUpdate = TerrainLodMembershipUpdateMode::Incremental;
    splitTopology.DataUpdate = TerrainLodDataUpdateMode::Incremental;

    TerrainLodPassTrace& meshEmit = TerrainLodPassTraceFor(
        stats.PassTraces,
        TerrainLodPassId::MeshEmit);
    meshEmit.EffectiveAction = TerrainLodPassAction::SerialDirty;
    meshEmit.DataUpdate = TerrainLodDataUpdateMode::Incremental;
    return stats;
}

TerrainLodRenderPacket MakeValidPacket(ParallelRoam::Terrain::TerrainMeshData& mesh)
{
    mesh.Vertices.resize(6U);
    mesh.Indices = {0U, 1U, 2U, 3U, 4U, 5U};

    TerrainLodRenderPacket packet{};
    packet.BorrowedCpuMesh = &mesh;
    packet.CpuMeshLifetime = TerrainLodCpuMeshLifetime::UntilNextBuildOrReset;
    packet.CpuMeshGeneration = 1U;
    packet.CpuMeshUpdateRanges.push_back(TerrainLodCpuMeshUpdateRange{0U, 6U, 0U, 6U});
    packet.ActiveLeafCount = 2U;
    packet.ActiveTriangleCount = 2U;
    packet.IndexCount = 6U;
    return packet;
}
} // 匿名命名空间

int main()
{
    using namespace ParallelRoam::Algorithms;

    ParallelRoam::Terrain::TerrainMeshData mesh;
    const TerrainLodRenderPacket packet = MakeValidPacket(mesh);
    TerrainLodStats reference = MakeValidStats();

    const TerrainLodResultValidation valid = ValidateTerrainLodResult(reference, packet, true);
    if (!valid.Passed || valid.FailureMask != 0U)
    {
        std::cerr << "Valid terrain LOD result was rejected\n";
        return 1;
    }

    TerrainLodStats missingEvidence = reference;
    missingEvidence.MeshHash = 0U;
    const TerrainLodResultValidation missingEvidenceResult =
        ValidateTerrainLodResult(missingEvidence, packet, true);
    if (missingEvidenceResult.Passed ||
        !HasBit(missingEvidenceResult.FailureMask, TerrainLodResultValidationFailure::MissingEvidence))
    {
        std::cerr << "Missing mesh evidence was not reported\n";
        return 1;
    }

    TerrainLodStats overBudget = reference;
    overBudget.TriangleBudget = 1U;
    const TerrainLodResultValidation overBudgetResult =
        ValidateTerrainLodResult(overBudget, packet, true);
    if (overBudgetResult.Passed ||
        !HasBit(overBudgetResult.FailureMask, TerrainLodResultValidationFailure::TriangleBudgetExceeded))
    {
        std::cerr << "Triangle budget overflow was not reported\n";
        return 1;
    }

    TerrainLodRenderPacket wrongPopulation = packet;
    wrongPopulation.ActiveLeafCount = 1U;
    const TerrainLodResultValidation wrongPopulationResult =
        ValidateTerrainLodResult(reference, wrongPopulation, true);
    if (wrongPopulationResult.Passed ||
        !HasBit(
            wrongPopulationResult.FailureMask,
            TerrainLodResultValidationFailure::MeshPopulationMismatch))
    {
        std::cerr << "Render packet population mismatch was not reported\n";
        return 1;
    }

    reference.ResultValidationEvaluated = true;
    reference.ResultValidationPassed = true;
    TerrainLodStats equivalent = reference;
    equivalent.ActiveNodeCount = 5U;
    equivalent.CpuMeshFullRebuildCount = 0U;
    equivalent.CpuMeshUpdatedTriangleCount = 1U;
    equivalent.CpuMeshReusedTriangleCount = 1U;
    TerrainLodPassTraceFor(equivalent.PassTraces, TerrainLodPassId::MergeScore).EffectiveAction =
        TerrainLodPassAction::ParallelFullRefresh;
    TerrainLodPassTraceFor(equivalent.PassTraces, TerrainLodPassId::SplitScore).CandidateCount = 1U;
    TerrainLodPassTraceFor(equivalent.PassTraces, TerrainLodPassId::SplitTopology).EffectiveAction =
        TerrainLodPassAction::ParallelAssisted;
    const TerrainLodReferenceComparison equivalentResult =
        CompareTerrainLodResults(reference, equivalent);
    if (!equivalentResult.Equivalent || equivalentResult.DifferenceMask != 0U)
    {
        std::cerr << "Equivalent results with different execution actions were rejected\n";
        return 1;
    }

    TerrainLodStats differentMesh = equivalent;
    differentMesh.NormalizedMeshHash = 29U;
    const TerrainLodReferenceComparison differentMeshResult =
        CompareTerrainLodResults(reference, differentMesh);
    if (differentMeshResult.Equivalent ||
        !HasBit(
            differentMeshResult.DifferenceMask,
            TerrainLodReferenceDifference::NormalizedMesh))
    {
        std::cerr << "Normalized mesh difference was not reported\n";
        return 1;
    }

    TerrainLodStats differentSemantics = equivalent;
    TerrainLodPassTraceFor(
        differentSemantics.PassTraces,
        TerrainLodPassId::SplitScore).MembershipUpdate =
        TerrainLodMembershipUpdateMode::FullRebuild;
    const TerrainLodReferenceComparison differentSemanticsResult =
        CompareTerrainLodResults(reference, differentSemantics);
    if (differentSemanticsResult.Equivalent ||
        !HasBit(
            differentSemanticsResult.DifferenceMask,
            TerrainLodReferenceDifference::PassSemantics))
    {
        std::cerr << "Pass semantic difference was not reported\n";
        return 1;
    }

    return 0;
}
