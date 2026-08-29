#include "experiment/TerrainLodExperimentCsv.h"

#include <cstddef>
#include <ostream>
#include <string>

namespace ParallelRoam::Experiment
{
namespace
{
template <typename Value>
void WriteCsvField(std::ostream& output, bool& first, const Value& value)
{
    if (!first)
    {
        output << ',';
    }
    output << value;
    first = false;
}

void WritePassTraceCsvHeader(std::ostream& output, bool& first)
{
    for (std::size_t index = 0U; index < Algorithms::TerrainLodPassCount; ++index)
    {
        const std::string name{
            Algorithms::ToString(static_cast<Algorithms::TerrainLodPassId>(index))};
        WriteCsvField(output, first, "pass_" + name + "RequestedAction");
        WriteCsvField(output, first, "pass_" + name + "EffectiveAction");
        WriteCsvField(output, first, "pass_" + name + "FallbackReason");
        WriteCsvField(output, first, "pass_" + name + "MembershipUpdate");
        WriteCsvField(output, first, "pass_" + name + "PriorityRefresh");
        WriteCsvField(output, first, "pass_" + name + "DataUpdate");
        WriteCsvField(output, first, "pass_" + name + "RequestedWorkerCount");
        WriteCsvField(output, first, "pass_" + name + "EffectiveWorkerCount");
        WriteCsvField(output, first, "pass_" + name + "CandidateCount");
        WriteCsvField(output, first, "pass_" + name + "DirtyItemCount");
        WriteCsvField(output, first, "pass_" + name + "ScoreMs");
        WriteCsvField(output, first, "pass_" + name + "HeapifyMs");
        WriteCsvField(output, first, "pass_" + name + "CandidateSnapshotMs");
        WriteCsvField(output, first, "pass_" + name + "MembershipUpdateCount");
        WriteCsvField(output, first, "pass_" + name + "MembershipUpdateMs");
        WriteCsvField(output, first, "pass_" + name + "WallMs");
    }
}

void WritePassTraceCsvValues(
    std::ostream& output,
    bool& first,
    const Algorithms::TerrainLodPassTraceArray& traces)
{
    for (const Algorithms::TerrainLodPassTrace& trace : traces)
    {
        WriteCsvField(output, first, Algorithms::ToString(trace.RequestedAction));
        WriteCsvField(output, first, Algorithms::ToString(trace.EffectiveAction));
        WriteCsvField(output, first, Algorithms::ToString(trace.FallbackReason));
        WriteCsvField(output, first, Algorithms::ToString(trace.MembershipUpdate));
        WriteCsvField(output, first, Algorithms::ToString(trace.PriorityRefresh));
        WriteCsvField(output, first, Algorithms::ToString(trace.DataUpdate));
        WriteCsvField(output, first, trace.RequestedWorkerCount);
        WriteCsvField(output, first, trace.EffectiveWorkerCount);
        WriteCsvField(output, first, trace.CandidateCount);
        WriteCsvField(output, first, trace.DirtyItemCount);
        WriteCsvField(output, first, trace.ScoreMilliseconds);
        WriteCsvField(output, first, trace.HeapifyMilliseconds);
        WriteCsvField(output, first, trace.CandidateSnapshotMilliseconds);
        WriteCsvField(output, first, trace.MembershipUpdateCount);
        WriteCsvField(output, first, trace.MembershipUpdateMilliseconds);
        WriteCsvField(output, first, trace.WallMilliseconds);
    }
}

void WriteTopologyReplayCsvHeader(
    std::ostream& output,
    bool& first,
    const std::string& prefix)
{
    WriteCsvField(output, first, prefix + "Action");
    WriteCsvField(output, first, prefix + "TopologyHash");
    WriteCsvField(output, first, prefix + "ActiveLeafHash");
    WriteCsvField(output, first, prefix + "QueueMembershipHash");
    WriteCsvField(output, first, prefix + "MeshEditHash");
    WriteCsvField(output, first, prefix + "ActiveTriangleCount");
    WriteCsvField(output, first, prefix + "InteriorCandidateCount");
    WriteCsvField(output, first, prefix + "BoundaryCandidateCount");
    WriteCsvField(output, first, prefix + "EffectiveWorkerCount");
    WriteCsvField(output, first, prefix + "EarlyCommitCount");
    WriteCsvField(output, first, prefix + "BudgetViolationCount");
    WriteCsvField(output, first, prefix + "QueueInvariantViolationCount");
    WriteCsvField(output, first, prefix + "TjunctionCount");
    WriteCsvField(output, first, prefix + "InvalidNeighborCount");
    WriteCsvField(output, first, prefix + "InvalidTopologyCount");
    WriteCsvField(output, first, prefix + "StateCloneMs");
    WriteCsvField(output, first, prefix + "ChunkBuildMs");
    WriteCsvField(output, first, prefix + "QueueInvalidationMs");
    WriteCsvField(output, first, prefix + "CommitMs");
    WriteCsvField(output, first, prefix + "ResultMergeMs");
    WriteCsvField(output, first, prefix + "IndexQueueRefreshMs");
    WriteCsvField(output, first, prefix + "SerialConvergenceMs");
    WriteCsvField(output, first, prefix + "WallMs");
}

void WriteTopologyReplayCsvValues(
    std::ostream& output,
    bool& first,
    const Algorithms::TerrainLodTopologyReplayEvidence& evidence)
{
    WriteCsvField(output, first, Algorithms::ToString(evidence.Action));
    WriteCsvField(output, first, evidence.TopologyHash);
    WriteCsvField(output, first, evidence.ActiveLeafHash);
    WriteCsvField(output, first, evidence.QueueMembershipHash);
    WriteCsvField(output, first, evidence.MeshEditHash);
    WriteCsvField(output, first, evidence.ActiveTriangleCount);
    WriteCsvField(output, first, evidence.InteriorCandidateCount);
    WriteCsvField(output, first, evidence.BoundaryCandidateCount);
    WriteCsvField(output, first, evidence.EffectiveWorkerCount);
    WriteCsvField(output, first, evidence.EarlyCommitCount);
    WriteCsvField(output, first, evidence.BudgetViolationCount);
    WriteCsvField(output, first, evidence.QueueInvariantViolationCount);
    WriteCsvField(output, first, evidence.TjunctionCount);
    WriteCsvField(output, first, evidence.InvalidNeighborCount);
    WriteCsvField(output, first, evidence.InvalidTopologyCount);
    WriteCsvField(output, first, evidence.StateCloneMilliseconds);
    WriteCsvField(output, first, evidence.ChunkBuildMilliseconds);
    WriteCsvField(output, first, evidence.QueueInvalidationMilliseconds);
    WriteCsvField(output, first, evidence.CommitMilliseconds);
    WriteCsvField(output, first, evidence.ResultMergeMilliseconds);
    WriteCsvField(output, first, evidence.IndexQueueRefreshMilliseconds);
    WriteCsvField(output, first, evidence.SerialConvergenceMilliseconds);
    WriteCsvField(output, first, evidence.WallMilliseconds);
}

void WriteTopologyPairCsvHeader(
    std::ostream& output,
    bool& first,
    const std::string& prefix)
{
    WriteCsvField(output, first, prefix + "Evaluated");
    WriteCsvField(output, first, prefix + "Equivalent");
    WriteCsvField(output, first, prefix + "FrozenCandidateHash");
    WriteCsvField(output, first, prefix + "FrozenCandidateCount");
    WriteCsvField(output, first, prefix + "CandidateSnapshotMs");
    WriteCsvField(output, first, prefix + "EvidenceMs");
    WriteTopologyReplayCsvHeader(output, first, prefix + "Serial");
    WriteTopologyReplayCsvHeader(output, first, prefix + "Parallel");
}

void WriteTopologyPairCsvValues(
    std::ostream& output,
    bool& first,
    const Algorithms::TerrainLodTopologyPairEvidence& evidence)
{
    WriteCsvField(output, first, evidence.Evaluated ? "true" : "false");
    WriteCsvField(output, first, evidence.Equivalent ? "true" : "false");
    WriteCsvField(output, first, evidence.FrozenCandidateHash);
    WriteCsvField(output, first, evidence.FrozenCandidateCount);
    WriteCsvField(output, first, evidence.CandidateSnapshotMilliseconds);
    WriteCsvField(output, first, evidence.EvidenceMilliseconds);
    WriteTopologyReplayCsvValues(output, first, evidence.Serial);
    WriteTopologyReplayCsvValues(output, first, evidence.Parallel);
}

// 成员名与 CSV 名在同一张表中定义，新增统计时只需补一行
#define TERRAIN_LOD_STAT_FIELDS(X) \
    X(BuildSequence, buildSequence) \
    X(ReplayInputHash, replayInputHash) \
    X(TopologyHash, topologyHash) \
    X(ActiveLeafHash, activeLeafHash) \
    X(MeshHash, meshHash) \
    X(NormalizedMeshHash, normalizedMeshHash) \
    X(TriangleBudget, evidenceTriangleBudget) \
    X(BudgetViolationCount, budgetViolationCount) \
    X(QueueInvariantViolationCount, queueInvariantViolationCount) \
    X(ResourceValidationFailureCount, resourceValidationFailureCount) \
    X(ResultValidationEvaluated, resultValidationEvaluated) \
    X(ResultValidationPassed, resultValidationPassed) \
    X(ResultValidationFailureMask, resultValidationFailureMask) \
    X(ReferenceComparisonEvaluated, referenceComparisonEvaluated) \
    X(ReferenceComparisonPassed, referenceComparisonPassed) \
    X(ReferenceComparisonDifferenceMask, referenceComparisonDifferenceMask) \
    X(PassEvidenceMilliseconds, passEvidenceMilliseconds) \
    X(ActiveTriangleCount, activeTriangleCount) \
    X(ActiveNodeCount, activeNodeCount) \
    X(OriginalTriangleCount, originalTriangleCount) \
    X(SubdividedTriangleCount, subdividedTriangleCount) \
    X(RebuiltTriangleCount, rebuiltTriangleCount) \
    X(ActiveSplitCount, activeSplitCount) \
    X(SplitCount, splitCount) \
    X(ForcedSplitCount, forcedSplitCount) \
    X(MergeCount, mergeCount) \
    X(CrackRiskCount, crackRiskCount) \
    X(ConstraintPassCount, constraintPassCount) \
    X(CandidatePeakCount, candidatePeakCount) \
    X(PersistentSplitQueueSize, persistentSplitQueueSize) \
    X(PersistentMergeQueueSize, persistentMergeQueueSize) \
    X(QueueCrossoverCount, queueCrossoverCount) \
    X(QueueMembershipUpdateCount, queueMembershipUpdateCount) \
    X(CpuMeshFullRebuildCount, cpuMeshFullRebuildCount) \
    X(CpuMeshUpdatedTriangleCount, cpuMeshUpdatedTriangleCount) \
    X(CpuMeshReusedTriangleCount, cpuMeshReusedTriangleCount) \
    X(CpuMeshDirtyRangeCount, cpuMeshDirtyRangeCount) \
    X(RejectedSplitCount, rejectedSplitCount) \
    X(BudgetRejectedSplitCount, budgetRejectedSplitCount) \
    X(RejectedMergeCount, rejectedMergeCount) \
    X(TjunctionCount, tjunctionCount) \
    X(InvalidNeighborCount, invalidNeighborCount) \
    X(InvalidTopologyCount, invalidTopologyCount) \
    X(CpuGpuUploadBytes, cpuGpuUploadBytes) \
    X(CpuGpuReadbackBytes, cpuGpuReadbackBytes) \
    X(CpuWorkerCount, cpuWorkerCount) \
    X(TopologyCommitMinCandidateCount, topologyCommitMinCandidateCount) \
    X(SplitTopologyCommitMinCandidateCount, splitTopologyCommitMinCandidateCount) \
    X(MergeTopologyCommitMinCandidateCount, mergeTopologyCommitMinCandidateCount) \
    X(SplitTopologyCandidateCount, splitTopologyCandidateCount) \
    X(SplitTopologyNonEmptyChunkCount, splitTopologyNonEmptyChunkCount) \
    X(SplitTopologyCommitWorkerCount, splitTopologyCommitWorkerCount) \
    X(ParallelSplitCommitCount, parallelSplitCommitCount) \
    X(MergeTopologyCandidateCount, mergeTopologyCandidateCount) \
    X(MergeTopologyNonEmptyChunkCount, mergeTopologyNonEmptyChunkCount) \
    X(MergeTopologyCommitWorkerCount, mergeTopologyCommitWorkerCount) \
    X(ParallelMergeCommitCount, parallelMergeCommitCount) \
    X(InteriorSplitCandidateCount, interiorSplitCandidateCount) \
    X(BoundarySplitCandidateCount, boundarySplitCandidateCount) \
    X(InteriorMergeCandidateCount, interiorMergeCandidateCount) \
    X(BoundaryMergeCandidateCount, boundaryMergeCandidateCount) \
    X(CpuUpdateMilliseconds, cpuUpdateMilliseconds) \
    X(CpuUtilizationPercent, cpuUtilizationPercent) \
    X(CpuPrepareMilliseconds, cpuPrepareMilliseconds) \
    X(CpuMergeCandidateMarkMilliseconds, cpuMergeCandidateMarkMilliseconds) \
    X(CpuMergeTopologyMilliseconds, cpuMergeTopologyMilliseconds) \
    X(CpuSplitTopologyChunkBuildMilliseconds, cpuSplitTopologyChunkBuildMilliseconds) \
    X(CpuSplitTopologyQueueInvalidationMilliseconds, cpuSplitTopologyQueueInvalidationMilliseconds) \
    X(CpuSplitTopologyParallelCommitMilliseconds, cpuSplitTopologyParallelCommitMilliseconds) \
    X(CpuSplitTopologyResultMergeMilliseconds, cpuSplitTopologyResultMergeMilliseconds) \
    X(CpuSplitTopologyIndexQueueRefreshMilliseconds, cpuSplitTopologyIndexQueueRefreshMilliseconds) \
    X(CpuSplitTopologySerialConvergenceMilliseconds, cpuSplitTopologySerialConvergenceMilliseconds) \
    X(CpuMergeTopologyChunkBuildMilliseconds, cpuMergeTopologyChunkBuildMilliseconds) \
    X(CpuMergeTopologyQueueInvalidationMilliseconds, cpuMergeTopologyQueueInvalidationMilliseconds) \
    X(CpuMergeTopologyParallelCommitMilliseconds, cpuMergeTopologyParallelCommitMilliseconds) \
    X(CpuMergeTopologyResultMergeMilliseconds, cpuMergeTopologyResultMergeMilliseconds) \
    X(CpuMergeTopologyIndexQueueRefreshMilliseconds, cpuMergeTopologyIndexQueueRefreshMilliseconds) \
    X(CpuMergeTopologySerialConvergenceMilliseconds, cpuMergeTopologySerialConvergenceMilliseconds) \
    X(CpuBudgetLeafCollectMilliseconds, cpuBudgetLeafCollectMilliseconds) \
    X(CpuErrorEvalMilliseconds, cpuErrorEvalMilliseconds) \
    X(CpuSplitCandidateMarkMilliseconds, cpuSplitCandidateMarkMilliseconds) \
    X(CpuSplitTopologyMilliseconds, cpuSplitTopologyMilliseconds) \
    X(CpuFinalLeafCollectMilliseconds, cpuFinalLeafCollectMilliseconds) \
    X(CpuMeshEmitMilliseconds, cpuMeshEmitMilliseconds) \
    X(CpuFinalizeMilliseconds, cpuFinalizeMilliseconds) \
    X(CpuUploadMilliseconds, cpuUploadMilliseconds) \
    X(RenderMilliseconds, algorithmRenderMilliseconds) \
    X(SplitMilliseconds, splitMilliseconds) \
    X(MergeMilliseconds, mergeMilliseconds) \
    X(EmitMilliseconds, emitMilliseconds) \
    X(ValidateMilliseconds, validateMilliseconds) \
    X(MaxActiveDepth, maxActiveDepth)
} // 匿名命名空间

void WriteTerrainLodSettingsCsvHeader(std::ostream& output)
{
    bool first = true;
    WriteCsvField(output, first, "experimentSchemaVersion");
    WriteCsvField(output, first, "terrainSize");
    WriteCsvField(output, first, "heightScale");
    WriteCsvField(output, first, "maxDepth");
    WriteCsvField(output, first, "screenSpaceSplitThresholdPixels");
    WriteCsvField(output, first, "screenSpaceMergeThresholdPixels");
    WriteCsvField(output, first, "triangleBudget");
    WriteCsvField(output, first, "dodParallelSplitEnabled");
    WriteCsvField(output, first, "localConstraintsEnabled");
    WriteCsvField(output, first, "topologyValidationEnabled");
    WriteCsvField(output, first, "passEvidenceEnabled");
    WriteCsvField(output, first, "topologyPairEvidenceEnabled");
    WriteCsvField(output, first, "mergeScoreAction");
    WriteCsvField(output, first, "splitScoreAction");
    WriteCsvField(output, first, "mergeTopologyAction");
    WriteCsvField(output, first, "splitTopologyAction");
    WriteCsvField(output, first, "meshEmitAction");
    WriteCsvField(output, first, "cpuUploadAction");
    WriteCsvField(output, first, "mergeScoreWorkerLimit");
    WriteCsvField(output, first, "splitScoreWorkerLimit");
    WriteCsvField(output, first, "mergeTopologyWorkerLimit");
    WriteCsvField(output, first, "splitTopologyWorkerLimit");
    WriteCsvField(output, first, "meshEmitWorkerLimit");
    WriteCsvField(output, first, "splitTopologyMinParallelCandidateCount");
    WriteCsvField(output, first, "mergeTopologyMinParallelCandidateCount");
    WriteCsvField(output, first, "parallelTopologyTargetBuild");
    WriteCsvField(output, first, "parallelTopologyPhase");
}

void WriteTerrainLodSettingsCsvValues(
    std::ostream& output,
    const Algorithms::TerrainLodSettings& settings)
{
    bool first = true;
    const Algorithms::TerrainLodPassPolicy& policy = settings.PassPolicy;
    WriteCsvField(output, first, TerrainLodExperimentCsvSchemaVersion);
    WriteCsvField(output, first, settings.TerrainSize);
    WriteCsvField(output, first, settings.HeightScale);
    WriteCsvField(output, first, settings.MaxDepth);
    WriteCsvField(output, first, settings.ScreenSpaceSplitThresholdPixels);
    WriteCsvField(output, first, settings.ScreenSpaceMergeThresholdPixels);
    WriteCsvField(output, first, settings.TriangleBudget);
    WriteCsvField(output, first, settings.EnableParallelSplit ? "true" : "false");
    WriteCsvField(output, first, settings.EnableLocalConstraints ? "true" : "false");
    WriteCsvField(output, first, settings.EnableTopologyValidation ? "true" : "false");
    WriteCsvField(output, first, settings.EnablePassEvidence ? "true" : "false");
    WriteCsvField(output, first, settings.EnableTopologyPairEvidence ? "true" : "false");
    WriteCsvField(output, first, Algorithms::ToString(policy.MergeScore));
    WriteCsvField(output, first, Algorithms::ToString(policy.SplitScore));
    WriteCsvField(output, first, Algorithms::ToString(policy.MergeTopology));
    WriteCsvField(output, first, Algorithms::ToString(policy.SplitTopology));
    WriteCsvField(output, first, Algorithms::ToString(policy.MeshEmit));
    WriteCsvField(output, first, Algorithms::ToString(policy.CpuUpload));
    WriteCsvField(output, first, policy.MergeScoreWorkerCount);
    WriteCsvField(output, first, policy.SplitScoreWorkerCount);
    WriteCsvField(output, first, policy.MergeTopologyWorkerCount);
    WriteCsvField(output, first, policy.SplitTopologyWorkerCount);
    WriteCsvField(output, first, policy.MeshEmitWorkerCount);
    WriteCsvField(output, first, policy.SplitTopologyMinParallelCandidateCount);
    WriteCsvField(output, first, policy.MergeTopologyMinParallelCandidateCount);
    WriteCsvField(output, first, policy.ParallelTopologyTargetBuild);
    WriteCsvField(output, first, Algorithms::ToString(policy.ParallelTopologyPhase));
}

void WriteTerrainLodStatsCsvHeader(std::ostream& output)
{
    bool first = true;
#define WRITE_STAT_HEADER(member, name) WriteCsvField(output, first, #name);
    TERRAIN_LOD_STAT_FIELDS(WRITE_STAT_HEADER)
#undef WRITE_STAT_HEADER
    WriteTopologyPairCsvHeader(output, first, "mergeTopologyPair");
    WriteTopologyPairCsvHeader(output, first, "splitTopologyPair");
    WritePassTraceCsvHeader(output, first);
}

void WriteTerrainLodStatsCsvValues(
    std::ostream& output,
    const Algorithms::TerrainLodStats& stats)
{
    bool first = true;
#define WRITE_STAT_VALUE(member, name) WriteCsvField(output, first, stats.member);
    TERRAIN_LOD_STAT_FIELDS(WRITE_STAT_VALUE)
#undef WRITE_STAT_VALUE
    WriteTopologyPairCsvValues(output, first, stats.MergeTopologyPair);
    WriteTopologyPairCsvValues(output, first, stats.SplitTopologyPair);
    WritePassTraceCsvValues(output, first, stats.PassTraces);
}

#undef TERRAIN_LOD_STAT_FIELDS
} // 命名空间 ParallelRoam::Experiment
