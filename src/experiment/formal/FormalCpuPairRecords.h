#pragma once

#include "experiment/formal/FormalExperimentTypes.h"

namespace ParallelRoam::Experiment::Formal
{
inline constexpr std::uint32_t CpuPairSchemaVersion = 2U;
inline constexpr std::uint32_t CpuMeasurementProtocolVersion = 1U;
inline constexpr std::uint32_t CpuTimingCalibrationSampleCount = 1000U;

/// <summary>
/// 同时保留清单参数与实际测量参数，覆盖只属于当前尝试
/// </summary>
struct CpuPairConfiguration
{
    std::uint32_t ManifestWarmupCount{5U};
    std::uint32_t ManifestRepeatCount{30U};
    std::uint32_t ManifestParallelWorkerCount{8U};
    std::uint32_t WarmupCount{5U};
    std::uint32_t MeasuredRepeatCount{30U};
    std::uint32_t ParallelWorkerCount{8U};
    [[nodiscard]] bool operator==(const CpuPairConfiguration&) const = default;
};

/// <summary>
/// 单次策略记录绑定冻结目标、完整块与执行证据，所有计时外成本独立保存
/// pre_ 与 planning_ 来自共同输入，post_ 只描述本次策略的输出
/// </summary>
struct CpuPairRecord
{
    std::string RunId;
    std::string ScenarioId;
    std::string TerrainId;
    std::string AnalysisSplit;
    std::uint32_t SampleIndex{0U};
    Algorithms::TerrainLodPassId PassId{Algorithms::TerrainLodPassId::MergeScore};
    std::uint32_t SelectionRank{0U};
    std::string SelectionStratum;
    std::uint32_t SelectionSeed{0U};
    std::uint32_t SelectorVersion{0U};
    std::uint32_t PassInputVersion{0U};
    std::uint64_t CameraPoseHash{0U};
    std::uint64_t ViewInputHash{0U};
    std::uint64_t ReplayInputHash{0U};
    std::uint64_t SelectionFeatureHash{0U};
    float PrimaryWorkValue{0.0F};
    std::string FeatureVector;
    CpuPairConfiguration Configuration;
    std::uint32_t AbsoluteBlockIndex{0U};
    std::uint32_t RepeatIndex{0U};
    bool IsWarmup{false};
    std::string BlockOrder;
    std::uint32_t OrderIndex{0U};
    Algorithms::TerrainLodPassAction RequestedAction{Algorithms::TerrainLodPassAction::NotRun};
    Algorithms::TerrainLodPassAction ActualAction{Algorithms::TerrainLodPassAction::NotRun};
    std::size_t RequestedWorkerCount{0U};
    std::size_t ActualWorkerCount{0U};
    Algorithms::TerrainLodPassFallbackReason Fallback{Algorithms::TerrainLodPassFallbackReason::None};
    std::string ExecutionPath;
    std::string FallbackDetail;
    std::uint64_t ResultHash{0U};
    bool ValidationPerformed{false};
    bool DiagnosticsDisabledAtExecution{false};
    bool Correct{false};
    bool Equivalent{false};
    CpuRecordStatus Status{CpuRecordStatus::Incomplete};
    std::string Failure;
    double WallMilliseconds{0.0};
    double StateCloneMilliseconds{0.0};
    double InputCheckMilliseconds{0.0};
    double ValidationMilliseconds{0.0};
    double WorkerPreparationMilliseconds{0.0};
    double FeatureCollectionMilliseconds{0.0};
    std::size_t PreMergeQueueEntryCount{0U};
    std::size_t PreSplitQueueEntryCount{0U};
    std::size_t PreActiveTriangleCount{0U};
    std::size_t PreTriangleBudget{0U};
    std::size_t PreRemainingTriangleBudget{0U};
    std::size_t PreTopologyEditCount{0U};
    int PreMaxActiveDepth{0};
    int PreMaxDepth{0};
    std::size_t PlanningInteriorCandidateCount{0U};
    std::size_t PlanningBoundaryCandidateCount{0U};
    std::size_t PlanningScheduledCandidateCount{0U};
    std::size_t PlanningNonEmptyChunkCount{0U};
    std::size_t PlanningDirtyTriangleCount{0U};
    std::size_t PlanningDirtyRangeCount{0U};
    std::string PlanningMeshReason{"not_applicable"};
    std::size_t PostEarlyCommitCount{0U};
    std::size_t PostActiveTriangleCount{0U};
    std::size_t PostDirtyTriangleCount{0U};
    std::size_t PostDirtyRangeCount{0U};
    double ScoreMilliseconds{0.0};
    double HeapifyMilliseconds{0.0};
    double CandidateSnapshotMilliseconds{0.0};
    double ChunkBuildMilliseconds{0.0};
    double QueueInvalidationMilliseconds{0.0};
    double CommitMilliseconds{0.0};
    double ResultMergeMilliseconds{0.0};
    double IndexQueueRefreshMilliseconds{0.0};
    double SerialConvergenceMilliseconds{0.0};
};

/// <summary>
/// 汇总一个实际目标的完整性，SampleIndex 为零时只表示该场景阶段没有冻结目标
/// 只有来源帧结束并通过诊断后，已测目标才能被确认为有效
/// </summary>
struct CpuTargetPairSummary
{
    std::string RunId;
    TargetStateRef Target;
    CpuPairConfiguration Configuration;
    std::string Status{"incomplete"};
    std::size_t WarmupRowCount{0U};
    std::size_t MeasuredRowCount{0U};
    bool SourceValidationPerformed{false};
    bool SourceValidationPassed{false};
    double WorkerPreparationMilliseconds{0.0};
    double RebuildMilliseconds{0.0};
    std::string Failure;
};

/// <summary>
/// 保存内部发布状态与实际数量，外部脚本仍须校验文件和执行来源
/// </summary>
struct CpuPairSummary
{
    std::string RunId;
    std::string Status{"pairing"};
    std::string Backend;
    std::string BuildConfiguration;
    std::size_t ScenarioCount{0U};
    std::size_t TargetCount{0U};
    std::size_t CompletedTargetCount{0U};
    std::size_t UnavailableGroupCount{0U};
    std::size_t WarmupRowCount{0U};
    std::size_t MeasuredRowCount{0U};
    bool CalibrationComplete{false};
    bool TimingEnvironmentValid{false};
    double TotalMilliseconds{0.0};
    std::string Failure;
};

/// <summary>
/// 保留一次原始标定值及所属批次分位数，无工作值必须来自真实根阶段执行
/// 分位数只作为噪声参照，不从任何策略时间中扣除
/// </summary>
struct CpuTimingCalibrationRecord
{
    std::string RunId;
    std::string Position;
    std::string Kind;
    std::string ScenarioId;
    std::uint32_t Index{0U};
    double WallMilliseconds{0.0};
    double P50Milliseconds{0.0};
    double P95Milliseconds{0.0};
    double P99Milliseconds{0.0};
    double ClockResolutionNanoseconds{0.0};
    std::uint64_t ReplayInputHash{0U};
    std::uint64_t ResultHash{0U};
    bool Correct{false};
    bool ValidationPerformed{false};
    bool DiagnosticsDisabledAtExecution{false};
};
}
