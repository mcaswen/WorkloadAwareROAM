#pragma once

#include "algorithms/ITerrainLodAlgorithm.h"

#include <filesystem>
#include <string>
#include <vector>

namespace ParallelRoam::Experiment::Formal
{
inline constexpr std::uint32_t CpuPilotSchemaVersion = 1;
inline constexpr std::uint32_t CpuPilotSampleCount = 64;
inline constexpr std::array CpuPilotPassIds{
    Algorithms::TerrainLodPassId::MergeScore, Algorithms::TerrainLodPassId::MergeTopology,
    Algorithms::TerrainLodPassId::SplitScore, Algorithms::TerrainLodPassId::SplitTopology,
    Algorithms::TerrainLodPassId::MeshEmit};

/// <summary>
/// 保存清单中的场景参数与资产声明，供相机生成和后续状态重建使用
/// </summary>
struct FormalScenario
{
    std::string ScenarioId;
    std::string TerrainId;
    std::filesystem::path HeightMapPath;
    std::string HeightMapSha256;
    std::uint32_t HeightMapWidth{0};
    std::uint32_t HeightMapHeight{0};
    Algorithms::TerrainLodSettings Settings;
    std::string TrajectoryId{"A"};
    std::string AnalysisSplit{"train"};
    std::uint32_t SampleCount{CpuPilotSampleCount};
    std::uint32_t DrawableWidth{1280};
    std::uint32_t DrawableHeight{720};
    float FovDegrees{60.0F};
    float NearPlane{0.1F};
    float FarPlane{500.0F};
    std::uint32_t SerialWorkerCount{1};
    std::uint32_t ParallelWorkerCount{8};
    std::uint32_t PassWarmupCount{5};
    std::uint32_t PassMeasuredRepeatCount{30};
};

/// <summary>
/// 冻结单个采样点的逻辑姿态与 NO 投影输入，不保存 GUI 相机状态
/// </summary>
struct CameraSample
{
    std::string ScenarioId;
    std::string TerrainId;
    std::string TrajectoryId{"A"};
    std::uint32_t SampleIndex{0};
    glm::vec3 Position{0.0F};
    glm::vec3 Target{0.0F};
    glm::vec3 Up{0.0F, 1.0F, 0.0F};
    float FovDegrees{60.0F};
    float NearPlane{0.1F};
    float FarPlane{500.0F};
    std::uint32_t DrawableWidth{1280};
    std::uint32_t DrawableHeight{720};
    std::uint64_t CameraPoseHash{0};
    std::uint64_t ViewMatrixHash{0};
    std::uint64_t ProjectionNoHash{0};
    std::uint64_t ViewInputHash{0};

    [[nodiscard]] bool operator==(const CameraSample&) const = default;
};

/// <summary>
/// 将选择结果绑定到 CPU 阶段和冻结相机；DOD 输入身份仍需执行器重建核对
/// </summary>
struct TargetStateRef
{
    std::string ScenarioId;
    Algorithms::TerrainLodPassId PassId{Algorithms::TerrainLodPassId::MergeScore};
    std::uint32_t SampleIndex{0};
    std::string SelectionStratum;
    std::uint32_t SelectionRank{0};
    std::uint32_t SelectionSeed{20260830};
    float PrimaryWorkValue{0};
    std::string FeatureVector;
    std::uint64_t ReplayInputHash{0};
    std::uint64_t SelectionFeatureHash{0};
    std::string AnalysisSplit{"train"};
    std::uint64_t CameraPoseHash{0};
    std::uint64_t ViewInputHash{0};
};

/// <summary>
/// 保存清单驱动任务的文件路径与场景选择，运行参数由清单提供
/// </summary>
struct FormalInputRequest
{
    std::filesystem::path ScenarioManifest;
    std::filesystem::path CameraManifest;
    std::filesystem::path TargetManifest;
    std::filesystem::path OutputDirectory;
    std::vector<std::string> ScenarioIds;
};
} // 命名空间 ParallelRoam::Experiment::Formal
