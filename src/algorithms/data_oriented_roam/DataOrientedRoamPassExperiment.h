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
/// 控制同一冻结输入上的预热次数、正式重复次数和并行线程上限
/// </summary>
struct DataOrientedRoamPassExperimentConfig
{
    std::size_t WarmupCount{5U};
    std::size_t MeasuredRepeatCount{30U};
    std::size_t ParallelWorkerCount{8U};
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
    std::uint64_t ResultHash{0U};
    bool Equivalent{false};
    bool Correct{false};
    float StateCloneMilliseconds{0.0F};
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
/// 汇总一个目标视点上五个 CPU 阶段的配对样本和正确性结果
/// </summary>
struct DataOrientedRoamPassExperimentResult
{
    bool Passed{false};
    std::size_t WarmupExecutionCount{0U};
    std::vector<DataOrientedRoamPassExperimentSample> Samples;
    std::string FailureMessage;
};

/// <summary>
/// 从上一帧结束状态构造下一帧输入，并在互不共享修改的副本上测量全部合法 CPU 策略
/// </summary>
[[nodiscard]] DataOrientedRoamPassExperimentResult RunDataOrientedRoamPassExperiment(
    const DataOrientedRoamState& previousFrameState,
    const TerrainLodViewInput& nextView,
    const DataOrientedRoamSettings& settings,
    const DataOrientedRoamPassExperimentConfig& config);
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
