#pragma once

#include "algorithms/ITerrainLodAlgorithm.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamTypes.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
struct DataOrientedRoamState;

/// <summary>
/// 区分工程诊断与关闭额外诊断的测量，两种模式的耗时不能混合解释
/// </summary>
enum class DataOrientedRoamPassExperimentMode
{
    Diagnostic,
    PilotMeasurement,
};

/// <summary>
/// 控制同一冻结输入上的预热次数、正式重复次数和并行线程上限
/// </summary>
struct DataOrientedRoamPassExperimentConfig
{
    std::size_t WarmupCount{5U};
    std::size_t MeasuredRepeatCount{30U};
    std::size_t ParallelWorkerCount{8U};
    DataOrientedRoamPassExperimentMode Mode{DataOrientedRoamPassExperimentMode::Diagnostic};
    // 筛选只决定记录哪些阶段，来源仍按固定串行顺序推进
    std::vector<TerrainLodPassId> Passes{TerrainLodPassId::MergeScore, TerrainLodPassId::MergeTopology,
        TerrainLodPassId::SplitScore, TerrainLodPassId::SplitTopology, TerrainLodPassId::MeshEmit};
};

/// <summary>
/// 保存一次阶段策略执行的输入规模、实际行为、结果证据和完整耗时
/// </summary>
struct DataOrientedRoamPassExperimentSample
{
    TerrainLodPassId PassId{TerrainLodPassId::MergeScore};
    TerrainLodPassAction RequestedAction{TerrainLodPassAction::NotRun};
    TerrainLodPassAction EffectiveAction{TerrainLodPassAction::NotRun};
    TerrainLodPassFallbackReason FallbackReason{TerrainLodPassFallbackReason::None};
    std::size_t RepeatIndex{0U};
    std::size_t ExecutionOrder{0U};
    std::size_t AbsoluteBlockIndex{0U};
    bool IsWarmup{false};
    std::string BlockOrder;
    std::string ExecutionPath;
    std::string FallbackDetail;
    std::string FailureMessage;
    std::size_t RequestedWorkerCount{0U};
    std::size_t EffectiveWorkerCount{0U};
    std::size_t CandidateCount{0U};
    std::size_t InteriorCandidateCount{0U};
    std::size_t BoundaryCandidateCount{0U};
    std::size_t NonEmptyChunkCount{0U};
    std::size_t EarlyCommitCount{0U};
    std::size_t ActiveTriangleCount{0U};
    std::size_t DirtyTriangleCount{0U};
    std::size_t DirtyRangeCount{0U};
    std::uint64_t FrozenStateHash{0U};
    // 独立保留真实阶段输入身份，不能与旧诊断输入的编码混用
    std::uint64_t StageInputHash{0U};
    std::uint64_t ResultHash{0U};
    bool ValidationPerformed{false};
    bool DiagnosticsDisabledAtExecution{false};
    bool Equivalent{false};
    bool Correct{false};
    float StateCloneMilliseconds{0.0F};
    float InputCheckMilliseconds{0.0F};
    float ValidationMilliseconds{0.0F};
    float WorkerPreparationMilliseconds{0.0F};
    float ScoreMilliseconds{0.0F};
    float HeapifyMilliseconds{0.0F};
    float CandidateSnapshotMilliseconds{0.0F};
    float ChunkBuildMilliseconds{0.0F};
    float QueueInvalidationMilliseconds{0.0F};
    float CommitMilliseconds{0.0F};
    float ResultMergeMilliseconds{0.0F};
    float IndexQueueRefreshMilliseconds{0.0F};
    float SerialConvergenceMilliseconds{0.0F};
    float WallMilliseconds{0.0F};
};

/// <summary>
/// 汇总选中阶段的配对样本与正确性，预热证据独立于正式记录
/// </summary>
struct DataOrientedRoamPassExperimentResult
{
    bool Passed{false};
    std::size_t WarmupExecutionCount{0U};
    float WorkerPreparationMilliseconds{0.0F};
    std::vector<DataOrientedRoamPassExperimentSample> WarmupSamples;
    std::vector<DataOrientedRoamPassExperimentSample> Samples;
    std::string FailureMessage;
};

/// <summary>
/// 同步测量一个真实阶段输入的完整策略块，返回前不保留来源或线程池引用
/// 配置无效或任一策略不正确时返回失败，并保留已有执行证据
/// </summary>
[[nodiscard]] DataOrientedRoamPassExperimentResult RunFrozenDataOrientedRoamPassExperiment(
    const DataOrientedRoamState& stageInput, TerrainLodPassId passId,
    const DataOrientedRoamPassExperimentConfig& config);

/// <summary>
/// 从上一帧结束状态构造下一帧输入，按配置筛选阶段并测量完整策略集合
/// </summary>
[[nodiscard]] DataOrientedRoamPassExperimentResult RunDataOrientedRoamPassExperiment(
    const DataOrientedRoamState& previousFrameState,
    const TerrainLodViewInput& nextView,
    const DataOrientedRoamSettings& settings,
    const DataOrientedRoamPassExperimentConfig& config);
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
