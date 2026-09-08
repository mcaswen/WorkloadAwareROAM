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
