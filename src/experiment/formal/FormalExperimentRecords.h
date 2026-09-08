#pragma once

#include "experiment/formal/FormalExperimentTypes.h"

namespace ParallelRoam::Experiment::Formal
{
/// <summary>
/// 不完整和失败必须保留为独立状态，不能凭 CSV 文件存在推断数据有效
/// </summary>
enum class CpuRecordStatus { Incomplete, Valid, NoWork, Failed };

/// <summary>
/// 记录输入准备状态与完整性，仅 inputs_validated 表示结构校验成功，不代表配对或外部摘要已验证
/// </summary>
struct InputPreparationSummary
{
    std::string Status{"preparing"};
    std::size_t ScenarioCount{0};
    std::size_t ExpectedCameraCount{0};
    std::size_t CameraCount{0};
    std::size_t TargetCount{0};
    std::string TargetStatus{"not_provided"};
    std::string Backend;
    std::string BuildConfiguration;
    std::string Compiler;
    std::string Error;
};

/// <summary>
/// 发现记录保留执行前与规划期间工作量，耗时不能作为目标选择输入
/// </summary>
struct CpuDiscoveryRecord
{
    std::string ScenarioId;
    std::uint32_t SampleIndex{0};
    Algorithms::TerrainLodPassId PassId{Algorithms::TerrainLodPassId::MergeScore};
    std::uint64_t ViewInputHash{0};
    std::uint64_t ReplayInputHash{0};
    std::size_t PreWorkCount{0};
    std::size_t PlanningWorkCount{0};
    double FeatureCollectionMilliseconds{0};
    CpuRecordStatus Status{CpuRecordStatus::Incomplete};
    std::string Failure;
    std::uint64_t CameraPoseHash{0U};
    std::uint32_t PassInputVersion{0U};
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
    float PrimaryWorkValue{0.0F};
    std::string FeatureVector;
    std::uint64_t SelectionFeatureHash{0U};
    // 显式区分尚未诊断与检查通过以免零违规数量被误读
    bool ValidationPerformed{false};
    bool ValidationPassed{false};
    bool SelectionEligible{false};
    std::string SelectionExclusionReason{"not_checked"};
    double InputHashMilliseconds{0.0};
    double ValidationMilliseconds{0.0};
};

/// <summary>
/// 每组覆盖不足仍是有效发现事实且不复制目标填满配额
/// </summary>
struct CpuTargetCoverageRecord
{
    std::string ScenarioId;
    Algorithms::TerrainLodPassId PassId{Algorithms::TerrainLodPassId::MergeScore};
    std::uint32_t SelectorVersion{0U};
    std::uint32_t SelectionSeed{0U};
    std::uint32_t RequestedCount{0U};
    std::size_t EligibleCount{0U};
    std::size_t SelectedCount{0U};
    std::array<std::size_t, 4> StratumCounts{};
    std::string InsufficiencyReason;
};

/// <summary>
/// 发现摘要绑定选择配置和完整性且不宣称存在性能交叉
/// </summary>
struct CpuDiscoverySummary
{
    std::string Status{"discovering"};
    std::size_t ScenarioCount{0U};
    std::size_t CompletedScenarioCount{0U};
    std::size_t ExpectedRecordCount{0U};
    std::size_t RecordCount{0U};
    std::size_t ValidRecordCount{0U};
    std::size_t NoWorkRecordCount{0U};
    std::size_t FailedRecordCount{0U};
    std::size_t TargetCount{0U};
    std::size_t InsufficientGroupCount{0U};
    std::uint32_t TargetsPerPass{4U};
    std::uint32_t SelectorVersion{0U};
    std::uint32_t PassInputVersion{0U};
    std::uint32_t SelectionSeed{0U};
    std::string TargetStatus{"not_published"};
    std::string Error;
};

/// <summary>
/// 单次策略样本保留配对身份、请求与实际执行信息，结果等价由执行器给出
/// </summary>
struct CpuPairRecord
{
    std::string ScenarioId;
    std::uint32_t SampleIndex{0};
    Algorithms::TerrainLodPassId PassId{Algorithms::TerrainLodPassId::MergeScore};
    std::uint32_t RepeatIndex{0};
    bool IsWarmup{false};
    std::string BlockOrder;
    std::uint32_t OrderIndex{0};
    Algorithms::TerrainLodPassAction RequestedAction{Algorithms::TerrainLodPassAction::NotRun};
    Algorithms::TerrainLodPassAction ActualAction{Algorithms::TerrainLodPassAction::NotRun};
    std::size_t RequestedWorkerCount{0};
    std::size_t ActualWorkerCount{0};
    Algorithms::TerrainLodPassFallbackReason Fallback{Algorithms::TerrainLodPassFallbackReason::None};
    std::uint64_t ReplayInputHash{0};
    std::uint64_t ResultHash{0};
    double WallMilliseconds{0};
    bool Correct{false};
    bool Equivalent{false};
    CpuRecordStatus Status{CpuRecordStatus::Incomplete};
    std::string Failure;
};
} // 命名空间 ParallelRoam::Experiment::Formal
