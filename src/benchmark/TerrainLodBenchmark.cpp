#include "benchmark/TerrainLodBenchmark.h"

#include "algorithms/ITerrainLodAlgorithm.h"
#include "algorithms/TerrainLodView.h"
#include "algorithms/classic_roam/ClassicRoamTerrainLodAlgorithm.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamTerrainLodAlgorithm.h"
#include "experiment/TerrainLodExperimentCsv.h"
#include "terrain/HeightMap.h"
#include "tools/PerformanceTimer.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ParallelRoam::Benchmark
{
namespace
{
// benchmark 的核心约束是两种 CPU ROAM 共享同一组输入
// 算法只通过 ITerrainLodAlgorithm 边界接入
struct BenchmarkCameraKeyframe
{
    std::string Name;
    glm::vec3 Position{0.0F};
    float TimeSeconds{0.0F};
    glm::vec3 Target{0.0F};
};

// HeightMap、terrain size、LOD 阈值和相机路径都由这里固定
// 这样 Classic 和 DOD 输出统计才能横向比较
struct BenchmarkScenario
{
    std::string Name;
    std::filesystem::path HeightMapPath;
    Algorithms::TerrainLodSettings Settings;
    std::vector<BenchmarkCameraKeyframe> CameraPath;
    // Smoke 会打开拓扑验证并要求近处细节增长
    bool RequireTopologyClean{false};
    bool RequireNearDetailIncrease{false};
    // 预算重入场景要求转向后的第一次 Build 同时回收旧细节并细分新区域
    bool RequireImmediateBudgetReallocation{false};
    // 预算饱和场景要求整条相机路径都维持在配置的活动叶三角形上限
    bool RequireBudgetSaturation{false};
    // 重置算法后重复固定轨迹，并逐帧比较输入与结果哈希
    bool RequireDeterministicReplay{false};
};

// smoke、budget-reentry 和 incremental-emit 偏回归测试，standard 偏性能样本
// 四者共用同一套 frame result 和 CSV 字段
struct BenchmarkFrameResult
{
    std::string AlgorithmName;
    std::string ProfileName;
    std::string CameraName;
    int FrameIndex{0};
    float TimeSeconds{0.0F};
    glm::vec3 CameraPosition{0.0F};
    int HeightMapWidth{0};
    int HeightMapHeight{0};
    std::size_t VertexCount{0};
    std::size_t IndexCount{0};
    std::size_t TriangleCount{0};
    Algorithms::TerrainLodStats Stats;
    // wall clock 用于发现接口外开销
    float BuildWallMilliseconds{0.0F};
    bool Passed{false};
};

// 新算法缺失时只在 all 模式下 skip
// 显式选择缺失算法必须失败以防漏跑
struct BenchmarkAlgorithmRun
{
    std::string AlgorithmName;
    std::string UnavailableReason;
    // Available 和 Passed 分开保存，all 模式不会把 skip 当作失败
    bool Available{false};
    bool Passed{false};
    std::vector<BenchmarkFrameResult> Frames;
};

std::string ToString(BenchmarkProfile profile)
{
    switch (profile)
    {
    case BenchmarkProfile::Smoke:
        return "smoke";
    case BenchmarkProfile::BudgetReentry:
        return "budget-reentry";
    case BenchmarkProfile::BudgetSaturation:
        return "budget-saturation";
    case BenchmarkProfile::IncrementalEmit:
        return "incremental-emit";
    case BenchmarkProfile::PassTraceReplay:
        return "pass-trace-replay";
    case BenchmarkProfile::PassPolicyReplay:
        return "pass-policy-replay";
    case BenchmarkProfile::Standard:
        return "standard";
    }

    return "unknown";
}

std::string ToString(BenchmarkAlgorithmSelection selection)
{
    switch (selection)
    {
    case BenchmarkAlgorithmSelection::Classic:
        return "classic";
    case BenchmarkAlgorithmSelection::DataOriented:
        return "dod";
    case BenchmarkAlgorithmSelection::All:
        return "all";
    }

    return "unknown";
}

std::string ToString(BenchmarkPassPolicySelection selection)
{
    switch (selection)
    {
    case BenchmarkPassPolicySelection::Default: return "default";
    case BenchmarkPassPolicySelection::SerialIncremental: return "serial-incremental";
    case BenchmarkPassPolicySelection::MaximumParallelIncremental:
        return "maximum-parallel-incremental";
    case BenchmarkPassPolicySelection::SerialFull: return "serial-full";
    case BenchmarkPassPolicySelection::MaximumParallelFull: return "maximum-parallel-full";
    }
    return "unknown";
}

void ApplyPassPolicy(
    BenchmarkPassPolicySelection selection,
    Algorithms::TerrainLodSettings& settings)
{
    const std::size_t splitMinimum = settings.PassPolicy.SplitTopologyMinParallelCandidateCount;
    const std::size_t mergeMinimum = settings.PassPolicy.MergeTopologyMinParallelCandidateCount;
    const std::size_t targetBuild = settings.PassPolicy.ParallelTopologyTargetBuild;
    const Algorithms::TerrainLodParallelTopologyPhase phase =
        settings.PassPolicy.ParallelTopologyPhase;
    switch (selection)
    {
    case BenchmarkPassPolicySelection::Default:
        settings.PassPolicy = {};
        break;
    case BenchmarkPassPolicySelection::SerialIncremental:
        settings.PassPolicy = Algorithms::MakeTerrainLodSerialIncrementalPolicy();
        break;
    case BenchmarkPassPolicySelection::MaximumParallelIncremental:
        settings.PassPolicy = Algorithms::MakeTerrainLodMaximumSafeParallelIncrementalPolicy();
        break;
    case BenchmarkPassPolicySelection::SerialFull:
        settings.PassPolicy = Algorithms::MakeTerrainLodSerialFullOutputPolicy();
        break;
    case BenchmarkPassPolicySelection::MaximumParallelFull:
        settings.PassPolicy = Algorithms::MakeTerrainLodMaximumSafeParallelFullOutputPolicy();
        break;
    }
    settings.PassPolicy.SplitTopologyMinParallelCandidateCount = splitMinimum;
    settings.PassPolicy.MergeTopologyMinParallelCandidateCount = mergeMinimum;
    settings.PassPolicy.ParallelTopologyTargetBuild = targetBuild;
    settings.PassPolicy.ParallelTopologyPhase = phase;
}

void ApplyTopologyExperimentSettings(
    const BenchmarkOptions& options,
    Algorithms::TerrainLodSettings& settings)
{
    settings.PassPolicy.SplitTopologyMinParallelCandidateCount =
        options.SplitTopologyMinParallelCandidateCount;
    settings.PassPolicy.MergeTopologyMinParallelCandidateCount =
        options.MergeTopologyMinParallelCandidateCount;
    settings.PassPolicy.ParallelTopologyTargetBuild = options.ParallelTopologyTargetBuild;
    settings.PassPolicy.ParallelTopologyPhase = options.ParallelTopologyPhase;
}

bool ParseSize(std::string_view value, std::size_t& output)
{
    if (value.empty())
    {
        return false;
    }
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), output);
    return error == std::errc{} && end == value.data() + value.size();
}

bool ParseParallelTopologyPhase(
    std::string_view value,
    Algorithms::TerrainLodParallelTopologyPhase& output)
{
    if (value == "both")
    {
        output = Algorithms::TerrainLodParallelTopologyPhase::Both;
        return true;
    }
    if (value == "split")
    {
        output = Algorithms::TerrainLodParallelTopologyPhase::SplitOnly;
        return true;
    }
    if (value == "merge")
    {
        output = Algorithms::TerrainLodParallelTopologyPhase::MergeOnly;
        return true;
    }
    return false;
}

std::vector<BenchmarkCameraKeyframe> MakeStandardCameraPath()
{
    // Standard 使用 64 帧闭合 flyover
    // 轨迹覆盖远景、中心、侧向和高度变化
    // 目的是让 split 与 merge 都在同一次回放中出现
    std::vector<BenchmarkCameraKeyframe> path;
    path.reserve(64);

    constexpr int FrameCount = 64;
    constexpr float Radius = 22.0F;
    constexpr float Height = 7.5F;
    constexpr float DurationSeconds = 16.0F;

    for (int index = 0; index < FrameCount; ++index)
    {
        // wave 让路径不是纯圆
        // 这样同一距离下会经过不同局部高度变化区域
        const float t = static_cast<float>(index) / static_cast<float>(FrameCount - 1);
        const float angle = t * 6.28318530718F;
        const float wave = std::sin(t * 12.56637061436F) * 3.5F;
        BenchmarkCameraKeyframe frame{};
        frame.Name = "flyover-" + std::to_string(index);
        frame.Position = glm::vec3{
            std::cos(angle) * Radius,
            Height + std::sin(angle * 1.7F) * 2.0F,
            std::sin(angle) * Radius + wave,
        };
        frame.TimeSeconds = t * DurationSeconds;
        path.push_back(frame);
    }

    return path;
}

std::vector<BenchmarkCameraKeyframe> MakeBudgetSaturationCameraPath()
{
    // Peking 高度图的最细网格容量显著超过 200000 个三角形。
    // 低空闭合环绕让视锥内持续存在超过 200000 个高优先级 leaf，
    // 内圈移动目标则迫使满预算拓扑在不同区域之间重新分配。
    std::vector<BenchmarkCameraKeyframe> path;
    constexpr int FrameCount = 24;
    constexpr float CameraRadius = 58.0F;
    constexpr float TargetRadius = 10.0F;
    constexpr float DurationSeconds = 12.0F;
    path.reserve(FrameCount);

    for (int index = 0; index < FrameCount; ++index)
    {
        const float t = static_cast<float>(index) / static_cast<float>(FrameCount - 1);
        const float angle = t * 6.28318530718F;
        BenchmarkCameraKeyframe frame{};
        frame.Name = "budget-orbit-" + std::to_string(index);
        frame.Position = glm::vec3{
            std::cos(angle) * CameraRadius,
            20.0F + std::sin(angle * 2.0F) * 3.0F,
            std::sin(angle) * CameraRadius,
        };
        frame.Target = glm::vec3{
            std::cos(angle + 0.55F) * TargetRadius,
            4.0F,
            std::sin(angle + 0.55F) * TargetRadius,
        };
        frame.TimeSeconds = t * DurationSeconds;
        path.push_back(frame);
    }

    return path;
}

Algorithms::TerrainLodViewInput BuildBenchmarkView(const BenchmarkCameraKeyframe& camera)
{
    constexpr std::uint32_t DrawableWidth = 1280U;
    constexpr std::uint32_t DrawableHeight = 720U;
    glm::vec3 forward = camera.Target - camera.Position;
    if (glm::dot(forward, forward) <= 0.000001F)
    {
        forward = glm::vec3{0.0F, -1.0F, 0.0F};
    }
    else
    {
        forward = glm::normalize(forward);
    }
    // 正上方视点需要改用 Z 轴 up，避免 lookAt 的 forward/up 共线
    const glm::vec3 worldUp = std::abs(glm::dot(forward, glm::vec3{0.0F, 1.0F, 0.0F})) > 0.99F
        ? glm::vec3{0.0F, 0.0F, -1.0F}
        : glm::vec3{0.0F, 1.0F, 0.0F};
    const glm::mat4 view = glm::lookAtRH(camera.Position, camera.Position + forward, worldUp);
    const glm::mat4 projection = glm::perspectiveRH_NO(
        glm::radians(60.0F),
        static_cast<float>(DrawableWidth) / static_cast<float>(DrawableHeight),
        0.1F,
        500.0F);
    return Algorithms::BuildTerrainLodViewInput(
        view,
        projection,
        camera.Position,
        forward,
        DrawableWidth,
        DrawableHeight,
        false);
}

BenchmarkScenario MakeScenario(BenchmarkProfile profile)
{
    BenchmarkScenario scenario{};
    // 这些参数是三版本对比的控制变量
    // 不在算法内部各自决定
    // 否则 CSV 里的时间和三角形数没有公平比较意义
    scenario.Name = ToString(profile);
    scenario.Settings.TerrainSize = 30.0F;
    scenario.Settings.HeightScale = 4.0F;
    scenario.Settings.MaxDepth = 14;
    scenario.Settings.ScreenSpaceSplitThresholdPixels = 4.0F;
    scenario.Settings.ScreenSpaceMergeThresholdPixels = 2.0F;
    scenario.Settings.TriangleBudget = 20000U;
    scenario.Settings.EnableLocalConstraints = true;
    scenario.Settings.EnablePassEvidence = true;

    if (profile == BenchmarkProfile::Smoke ||
        profile == BenchmarkProfile::PassTraceReplay ||
        profile == BenchmarkProfile::PassPolicyReplay)
    {
        // Smoke 使用小高度图和代表性视点
        // 拓扑验证开启
        // 适合提交前快速发现裂缝和近细远粗退化
        scenario.HeightMapPath = "assets/heightmaps/Hm_Terrain_Test_129.pgm";
        scenario.Settings.EnableTopologyValidation = true;
        scenario.RequireTopologyClean = true;
        scenario.RequireNearDetailIncrease = true;
        scenario.RequireDeterministicReplay = profile == BenchmarkProfile::PassTraceReplay;
        scenario.CameraPath = {
            // far 建立远处低细节基线
            BenchmarkCameraKeyframe{"far", glm::vec3{0.0F, 14.0F, 28.0F}, 0.0F},
            // center 要显著增加 active triangles
            BenchmarkCameraKeyframe{"center", glm::vec3{0.0F, 4.0F, 0.0F}, 1.0F},
            // away 与 center 位置相同但向上看，检查相机背后的地形能否立即回收
            BenchmarkCameraKeyframe{"away", glm::vec3{0.0F, 4.0F, 0.0F}, 2.0F, glm::vec3{0.0F, 8.0F, 0.0F}},
            // near-corner 检查非中心区域也能触发局部细分
            BenchmarkCameraKeyframe{"near-corner", glm::vec3{-13.0F, 2.5F, -13.0F}, 3.0F},
            // far-return 检查一次 Build 是否可以从深层 leaf 向 parent 连续 merge
            BenchmarkCameraKeyframe{"far-return", glm::vec3{0.0F, 60.0F, 120.0F}, 4.0F},
            // center-return 检查持久拓扑和 merge 后的再次细分稳定性
            BenchmarkCameraKeyframe{"center-return", glm::vec3{0.0F, 4.0F, 0.0F}, 5.0F},
        };
        return scenario;
    }

    if (profile == BenchmarkProfile::BudgetReentry)
    {
        // 低预算放大视点相关的优先级变化，稳定复现池满时的细节重分配
        scenario.HeightMapPath = "assets/heightmaps/Hm_Terrain_Test_129.pgm";
        scenario.Settings.TriangleBudget = 512U;
        scenario.Settings.EnableTopologyValidation = true;
        scenario.RequireTopologyClean = true;
        scenario.RequireImmediateBudgetReallocation = true;
        scenario.CameraPath = {
            BenchmarkCameraKeyframe{
                "focus-left",
                glm::vec3{0.0F, 5.0F, 30.0F},
                0.0F,
                glm::vec3{-8.0F, 0.0F, 0.0F}},
            BenchmarkCameraKeyframe{
                "reentry-minus-four",
                glm::vec3{0.0F, 5.0F, 30.0F},
                1.0F,
                glm::vec3{-4.0F, 0.0F, 0.0F}},
            BenchmarkCameraKeyframe{
                "reentry-center",
                glm::vec3{0.0F, 5.0F, 30.0F},
                2.0F,
                glm::vec3{0.0F, 0.0F, 0.0F}},
            BenchmarkCameraKeyframe{
                "reentry-plus-four",
                glm::vec3{0.0F, 5.0F, 30.0F},
                3.0F,
                glm::vec3{4.0F, 0.0F, 0.0F}},
            BenchmarkCameraKeyframe{
                "reentry-plus-eight",
                glm::vec3{0.0F, 5.0F, 30.0F},
                4.0F,
                glm::vec3{8.0F, 0.0F, 0.0F}},
        };
        return scenario;
    }

    if (profile == BenchmarkProfile::BudgetSaturation)
    {
        scenario.HeightMapPath = "assets/heightmaps/Hm_Terrain_Peking_513.png";
        scenario.Settings.TerrainSize = 80.0F;
        scenario.Settings.HeightScale = 12.0F;
        scenario.Settings.MaxDepth = 20;
        scenario.Settings.ScreenSpaceSplitThresholdPixels = 0.25F;
        scenario.Settings.ScreenSpaceMergeThresholdPixels = 0.10F;
        scenario.Settings.TriangleBudget = 200000U;
        scenario.Settings.EnableTopologyValidation = false;
        scenario.RequireBudgetSaturation = true;
        scenario.CameraPath = MakeBudgetSaturationCameraPath();
        return scenario;
    }

    if (profile == BenchmarkProfile::IncrementalEmit)
    {
        scenario.HeightMapPath = "assets/heightmaps/Hm_Terrain_Test_129.pgm";
        scenario.Settings.EnableTopologyValidation = true;
        scenario.RequireTopologyClean = true;
        const BenchmarkCameraKeyframe stableCamera{
            "stable-center",
            glm::vec3{0.0F, 4.0F, 0.0F},
            0.0F,
        };
        scenario.CameraPath = {stableCamera, stableCamera, stableCamera};
        scenario.CameraPath[1].Name = "stable-center-debug-transition";
        scenario.CameraPath[1].TimeSeconds = 1.0F;
        scenario.CameraPath[2].Name = "stable-center-reuse";
        scenario.CameraPath[2].TimeSeconds = 2.0F;
        return scenario;
    }

    // Standard 使用更大 HeightMap 和较长路径
    // 不要求每帧拓扑验证
    // 重点是记录稳定的各 pass 耗时分布
    scenario.HeightMapPath = "assets/heightmaps/Hm_Terrain_Peking_513.png";
    scenario.Settings.EnableTopologyValidation = false;
    scenario.RequireTopologyClean = false;
    scenario.RequireNearDetailIncrease = false;
    scenario.CameraPath = MakeStandardCameraPath();
    return scenario;
}

std::unique_ptr<Algorithms::ITerrainLodAlgorithm> CreateAlgorithm(BenchmarkAlgorithmSelection selection)
{
    // benchmark factory 是算法可用性的单一入口
    // 新算法接入后必须在这里注册
    // renderer 侧不需要知道 benchmark 的选择枚举
    if (selection == BenchmarkAlgorithmSelection::Classic)
    {
        return std::make_unique<Algorithms::ClassicRoam::ClassicRoamTerrainLodAlgorithm>();
    }

    if (selection == BenchmarkAlgorithmSelection::DataOriented)
    {
        return std::make_unique<Algorithms::DataOrientedRoam::DataOrientedRoamTerrainLodAlgorithm>();
    }

    return nullptr;
}

std::vector<BenchmarkAlgorithmSelection> ExpandAlgorithmSelection(BenchmarkAlgorithmSelection selection)
{
    if (selection != BenchmarkAlgorithmSelection::All)
    {
        return {selection};
    }

    // all 的顺序固定为 Classic、DOD
    // 输出和 CSV 都能保持稳定列对比
    return {
        BenchmarkAlgorithmSelection::Classic,
        BenchmarkAlgorithmSelection::DataOriented,
    };
}

bool HasInvalidTopology(const Algorithms::TerrainLodStats& stats)
{
    // 三类拓扑错误都属于 smoke profile 的硬失败
    // Standard profile 可选择关闭 validator 只采集性能
    return stats.TjunctionCount != 0U ||
           stats.InvalidNeighborCount != 0U ||
           stats.InvalidTopologyCount != 0U;
}

bool ValidateFrame(
    const BenchmarkScenario& scenario,
    const Algorithms::TerrainLodRenderPacket& renderPacket,
    const Algorithms::TerrainLodStats& stats,
    bool buildSucceeded)
{
    if (!buildSucceeded)
    {
        // 算法显式失败时不再继续解释 mesh 内容
        return false;
    }

    if (!renderPacket.HasConsistentResourceContract())
    {
        return false;
    }

    const Terrain::TerrainMeshData* cpuMesh = renderPacket.ResolveCpuMesh();
    if (renderPacket.Mode == Algorithms::TerrainLodRenderMode::CpuMesh &&
        (cpuMesh == nullptr || cpuMesh->Vertices.empty() || cpuMesh->Indices.empty()))
    {
        // 当前 Classic 和 DOD 都必须输出 CPU mesh
        return false;
    }

    if (stats.ActiveTriangleCount == 0U || stats.MaxActiveDepth > scenario.Settings.MaxDepth)
    {
        // max depth 越界通常说明算法没有正确遵守统一 settings
        return false;
    }

    if (scenario.Settings.EnablePassEvidence &&
        (stats.BuildSequence == 0U ||
         stats.ReplayInputHash == 0U ||
         stats.TopologyHash == 0U ||
         stats.ActiveLeafHash == 0U ||
         stats.MeshHash == 0U ||
         stats.NormalizedMeshHash == 0U ||
         stats.TriangleBudget != scenario.Settings.TriangleBudget ||
         stats.BudgetViolationCount != 0U ||
         stats.QueueInvariantViolationCount != 0U ||
         stats.ResourceValidationFailureCount != 0U))
    {
        return false;
    }

    if (scenario.Settings.EnablePassEvidence)
    {
        for (std::size_t passIndex = 0U;
             passIndex < static_cast<std::size_t>(Algorithms::TerrainLodPassId::CpuUpload);
             ++passIndex)
        {
            const Algorithms::TerrainLodPassTrace& trace = stats.PassTraces[passIndex];
            if (trace.Id != static_cast<Algorithms::TerrainLodPassId>(passIndex) ||
                trace.RequestedAction == Algorithms::TerrainLodPassAction::NotRun ||
                trace.EffectiveAction == Algorithms::TerrainLodPassAction::NotRun)
            {
                return false;
            }
        }
        const Algorithms::TerrainLodPassTrace& mergeScore = Algorithms::TerrainLodPassTraceFor(
            stats.PassTraces,
            Algorithms::TerrainLodPassId::MergeScore);
        const Algorithms::TerrainLodPassTrace& splitScore = Algorithms::TerrainLodPassTraceFor(
            stats.PassTraces,
            Algorithms::TerrainLodPassId::SplitScore);
        const Algorithms::TerrainLodPassTrace& mergeTopology = Algorithms::TerrainLodPassTraceFor(
            stats.PassTraces,
            Algorithms::TerrainLodPassId::MergeTopology);
        const Algorithms::TerrainLodPassTrace& splitTopology = Algorithms::TerrainLodPassTraceFor(
            stats.PassTraces,
            Algorithms::TerrainLodPassId::SplitTopology);
        const Algorithms::TerrainLodPassTrace& meshEmit = Algorithms::TerrainLodPassTraceFor(
            stats.PassTraces,
            Algorithms::TerrainLodPassId::MeshEmit);
        const auto validScoreBreakdown = [](const Algorithms::TerrainLodPassTrace& trace) {
            constexpr float timingToleranceMilliseconds = 0.001F;
            return trace.CandidateSnapshotMilliseconds == 0.0F &&
                trace.ScoreMilliseconds >= 0.0F &&
                trace.HeapifyMilliseconds >= 0.0F &&
                trace.MembershipUpdateMilliseconds >= 0.0F &&
                std::abs(
                    trace.WallMilliseconds -
                    (trace.ScoreMilliseconds + trace.HeapifyMilliseconds)) <=
                    timingToleranceMilliseconds;
        };
        if (mergeScore.MembershipUpdate != Algorithms::TerrainLodMembershipUpdateMode::Incremental ||
            splitScore.MembershipUpdate != Algorithms::TerrainLodMembershipUpdateMode::Incremental ||
            mergeScore.PriorityRefresh != Algorithms::TerrainLodPriorityRefreshMode::FullAllCurrentEntries ||
            splitScore.PriorityRefresh != Algorithms::TerrainLodPriorityRefreshMode::FullAllCurrentEntries ||
            !validScoreBreakdown(mergeScore) ||
            !validScoreBreakdown(splitScore) ||
            mergeScore.MembershipUpdateCount + splitScore.MembershipUpdateCount !=
                stats.QueueMembershipUpdateCount ||
            mergeTopology.CandidateSnapshotMilliseconds > mergeTopology.WallMilliseconds + 0.001F ||
            splitTopology.CandidateSnapshotMilliseconds > splitTopology.WallMilliseconds + 0.001F ||
            mergeTopology.DataUpdate != Algorithms::TerrainLodDataUpdateMode::Incremental ||
            splitTopology.DataUpdate != Algorithms::TerrainLodDataUpdateMode::Incremental ||
            meshEmit.DataUpdate == Algorithms::TerrainLodDataUpdateMode::NotApplicable)
        {
            return false;
        }
    }

    if (renderPacket.Mode == Algorithms::TerrainLodRenderMode::CpuMesh &&
        (cpuMesh->Vertices.size() != stats.ActiveTriangleCount * 3U ||
         cpuMesh->Indices.size() != stats.ActiveTriangleCount * 3U ||
         renderPacket.IndexCount != cpuMesh->Indices.size()))
    {
        return false;
    }

    // CPU 持久 Q_s 成员必须精确对应 active cut。
    if (stats.PersistentSplitQueueSize != 0U &&
        (stats.PersistentSplitQueueSize != stats.ActiveTriangleCount ||
         stats.PersistentSplitQueueSize > scenario.Settings.TriangleBudget ||
         stats.SplitCount + stats.MergeCount > stats.ActiveNodeCount))
    {
        // 每个 parent 在同一 Build 最多执行一次正向或反向事务。
        // 事件数超过持久节点池规模通常意味着 split/merge 发生了同帧振荡。
        return false;
    }

    const bool reportsIncrementalCpuMesh =
        stats.CpuMeshFullRebuildCount != 0U ||
        stats.CpuMeshUpdatedTriangleCount != 0U ||
        stats.CpuMeshReusedTriangleCount != 0U ||
        stats.CpuMeshDirtyRangeCount != 0U;
    if (reportsIncrementalCpuMesh &&
        (stats.CpuMeshFullRebuildCount > 1U ||
         stats.CpuMeshUpdatedTriangleCount + stats.CpuMeshReusedTriangleCount !=
             stats.ActiveTriangleCount ||
         stats.CpuMeshDirtyRangeCount > stats.CpuMeshUpdatedTriangleCount))
    {
        return false;
    }

    return !scenario.RequireTopologyClean || !HasInvalidTopology(stats);
}

bool HasEquivalentReplay(
    const BenchmarkAlgorithmRun& first,
    const BenchmarkAlgorithmRun& replay)
{
    if (!first.Available || !replay.Available || !replay.Passed ||
        first.Frames.size() != replay.Frames.size())
    {
        return false;
    }

    for (std::size_t index = 0U; index < first.Frames.size(); ++index)
    {
        const Algorithms::TerrainLodStats& left = first.Frames[index].Stats;
        const Algorithms::TerrainLodStats& right = replay.Frames[index].Stats;
        if (left.BuildSequence != right.BuildSequence ||
            left.ReplayInputHash != right.ReplayInputHash ||
            left.TopologyHash != right.TopologyHash ||
            left.ActiveLeafHash != right.ActiveLeafHash ||
            left.MeshHash != right.MeshHash ||
            left.NormalizedMeshHash != right.NormalizedMeshHash ||
            left.ActiveTriangleCount != right.ActiveTriangleCount ||
            left.TriangleBudget != right.TriangleBudget ||
            left.BudgetViolationCount != right.BudgetViolationCount ||
            left.QueueInvariantViolationCount != right.QueueInvariantViolationCount ||
            left.ResourceValidationFailureCount != right.ResourceValidationFailureCount)
        {
            return false;
        }
    }
    return true;
}

bool HasEquivalentPolicyResults(
    const BenchmarkAlgorithmRun& leftRun,
    const BenchmarkAlgorithmRun& rightRun)
{
    if (!leftRun.Available || !rightRun.Available ||
        !leftRun.Passed || !rightRun.Passed ||
        leftRun.Frames.size() != rightRun.Frames.size())
    {
        return false;
    }

    for (std::size_t index = 0U; index < leftRun.Frames.size(); ++index)
    {
        const Algorithms::TerrainLodStats& left = leftRun.Frames[index].Stats;
        const Algorithms::TerrainLodStats& right = rightRun.Frames[index].Stats;
        const Algorithms::TerrainLodPassTrace& leftMergeScore = Algorithms::TerrainLodPassTraceFor(
            left.PassTraces,
            Algorithms::TerrainLodPassId::MergeScore);
        const Algorithms::TerrainLodPassTrace& rightMergeScore = Algorithms::TerrainLodPassTraceFor(
            right.PassTraces,
            Algorithms::TerrainLodPassId::MergeScore);
        const Algorithms::TerrainLodPassTrace& leftSplitScore = Algorithms::TerrainLodPassTraceFor(
            left.PassTraces,
            Algorithms::TerrainLodPassId::SplitScore);
        const Algorithms::TerrainLodPassTrace& rightSplitScore = Algorithms::TerrainLodPassTraceFor(
            right.PassTraces,
            Algorithms::TerrainLodPassId::SplitScore);
        if (left.TopologyHash != right.TopologyHash ||
            left.ActiveLeafHash != right.ActiveLeafHash ||
            left.NormalizedMeshHash != right.NormalizedMeshHash ||
            leftMergeScore.CandidateCount != rightMergeScore.CandidateCount ||
            leftSplitScore.CandidateCount != rightSplitScore.CandidateCount ||
            left.ActiveTriangleCount != right.ActiveTriangleCount ||
            left.TriangleBudget != right.TriangleBudget ||
            left.BudgetViolationCount != right.BudgetViolationCount ||
            left.QueueInvariantViolationCount != right.QueueInvariantViolationCount ||
            left.ResourceValidationFailureCount != right.ResourceValidationFailureCount ||
            left.TjunctionCount != right.TjunctionCount ||
            left.InvalidNeighborCount != right.InvalidNeighborCount ||
            left.InvalidTopologyCount != right.InvalidTopologyCount)
        {
            return false;
        }
    }
    return true;
}

bool HasExpectedPolicyTrace(
    const BenchmarkAlgorithmRun& run,
    BenchmarkPassPolicySelection policy)
{
    if (!run.Available || !run.Passed)
    {
        return false;
    }

    for (const BenchmarkFrameResult& frame : run.Frames)
    {
        const Algorithms::TerrainLodPassTrace& mergeScore = Algorithms::TerrainLodPassTraceFor(
            frame.Stats.PassTraces,
            Algorithms::TerrainLodPassId::MergeScore);
        const Algorithms::TerrainLodPassTrace& splitScore = Algorithms::TerrainLodPassTraceFor(
            frame.Stats.PassTraces,
            Algorithms::TerrainLodPassId::SplitScore);
        const Algorithms::TerrainLodPassTrace& mergeTopology = Algorithms::TerrainLodPassTraceFor(
            frame.Stats.PassTraces,
            Algorithms::TerrainLodPassId::MergeTopology);
        const Algorithms::TerrainLodPassTrace& splitTopology = Algorithms::TerrainLodPassTraceFor(
            frame.Stats.PassTraces,
            Algorithms::TerrainLodPassId::SplitTopology);
        const Algorithms::TerrainLodPassTrace& meshEmit = Algorithms::TerrainLodPassTraceFor(
            frame.Stats.PassTraces,
            Algorithms::TerrainLodPassId::MeshEmit);

        const bool requestsParallelExecution =
            policy == BenchmarkPassPolicySelection::MaximumParallelIncremental ||
            policy == BenchmarkPassPolicySelection::MaximumParallelFull;
        const bool requestsFullOutput =
            policy == BenchmarkPassPolicySelection::SerialFull ||
            policy == BenchmarkPassPolicySelection::MaximumParallelFull;
        const Algorithms::TerrainLodPassAction expectedScoreAction = requestsParallelExecution
            ? Algorithms::TerrainLodPassAction::ParallelFullRefresh
            : Algorithms::TerrainLodPassAction::SerialFullRefresh;
        const Algorithms::TerrainLodPassAction expectedTopologyAction = requestsParallelExecution
            ? Algorithms::TerrainLodPassAction::ParallelAssisted
            : Algorithms::TerrainLodPassAction::SerialImmediate;
        const Algorithms::TerrainLodPassAction expectedMeshAction = requestsFullOutput
            ? Algorithms::TerrainLodPassAction::SerialFull
            : (requestsParallelExecution
                ? Algorithms::TerrainLodPassAction::ParallelDirty
                : Algorithms::TerrainLodPassAction::SerialDirty);
        if (mergeScore.RequestedAction != expectedScoreAction ||
            splitScore.RequestedAction != expectedScoreAction ||
            mergeTopology.RequestedAction != expectedTopologyAction ||
            splitTopology.RequestedAction != expectedTopologyAction ||
            meshEmit.RequestedAction != expectedMeshAction)
        {
            return false;
        }

        // 两种固定串行预设都要求全部 CPU 算法阶段实际使用不超过一个线程
        if (!requestsParallelExecution)
        {
            for (std::size_t passIndex = 0U;
                 passIndex <= static_cast<std::size_t>(Algorithms::TerrainLodPassId::MeshEmit);
                 ++passIndex)
            {
                const Algorithms::TerrainLodPassTrace& trace = frame.Stats.PassTraces[passIndex];
                if (trace.EffectiveWorkerCount > 1U ||
                    trace.EffectiveAction == Algorithms::TerrainLodPassAction::ParallelFullRefresh ||
                    trace.EffectiveAction == Algorithms::TerrainLodPassAction::ParallelAssisted ||
                    trace.EffectiveAction == Algorithms::TerrainLodPassAction::ParallelDirty)
                {
                    return false;
                }
            }
        }

        if (requestsFullOutput)
        {
            if (meshEmit.EffectiveAction != Algorithms::TerrainLodPassAction::SerialFull ||
                meshEmit.DataUpdate != Algorithms::TerrainLodDataUpdateMode::Full ||
                meshEmit.EffectiveWorkerCount > 1U ||
                frame.Stats.CpuMeshFullRebuildCount != 1U)
            {
                return false;
            }
        }
    }
    return true;
}

void RelabelPolicyRun(BenchmarkAlgorithmRun& run, std::string_view policyName)
{
    run.AlgorithmName += "/" + std::string{policyName};
    for (BenchmarkFrameResult& frame : run.Frames)
    {
        frame.AlgorithmName = run.AlgorithmName;
    }
}

bool ValidateRunShape(const BenchmarkScenario& scenario, std::vector<BenchmarkFrameResult>& frames)
{
    bool passed = std::all_of(
        frames.begin(),
        frames.end(),
        [](const BenchmarkFrameResult& frame) {
            return frame.Passed;
        });

    if (scenario.RequireNearDetailIncrease && frames.size() >= 4U)
    {
        const std::size_t farTriangles = frames[0].TriangleCount;
        const bool frustumAwareRoam =
            frames[0].AlgorithmName == "classic_cpu_roam" ||
            frames[0].AlgorithmName == "data_oriented_cpu_roam";
        // 视锥感知后，近景只覆盖小块地形，总三角形数可以低于能看到全图的远景
        const bool centerHasMoreDetail = frustumAwareRoam
            ? frames[1].Stats.MaxActiveDepth == scenario.Settings.MaxDepth
            : frames[1].TriangleCount > farTriangles * 2U;
        const bool cornerHasMoreDetail = frustumAwareRoam
            ? frames[3].Stats.MaxActiveDepth == scenario.Settings.MaxDepth
            : frames[3].TriangleCount > farTriangles * 2U;
        const bool returnHasMoreDetail = frustumAwareRoam
            ? frames.back().Stats.MaxActiveDepth == scenario.Settings.MaxDepth
            : frames.back().TriangleCount > farTriangles * 2U;
        frames[1].Passed = frames[1].Passed && centerHasMoreDetail;
        frames[3].Passed = frames[3].Passed && cornerHasMoreDetail;
        frames.back().Passed = frames.back().Passed && returnHasMoreDetail;
        passed = passed && centerHasMoreDetail && cornerHasMoreDetail && returnHasMoreDetail;
    }

    if (scenario.RequireImmediateBudgetReallocation && frames.size() >= 2U)
    {
        const std::size_t minimumFilledBudget = scenario.Settings.TriangleBudget > 2U
            ? scenario.Settings.TriangleBudget - 2U
            : scenario.Settings.TriangleBudget;
        const bool initialBudgetFilled = frames.front().TriangleCount >= minimumFilledBudget;
        frames.front().Passed = frames.front().Passed && initialBudgetFilled;
        passed = passed && initialBudgetFilled;
        for (std::size_t index = 1U; index < frames.size(); ++index)
        {
            const bool reallocatedInOneBuild =
                frames[index].Stats.MergeCount > 0U &&
                frames[index].Stats.SplitCount > 0U &&
                frames[index].TriangleCount >= minimumFilledBudget;
            frames[index].Passed = frames[index].Passed && reallocatedInOneBuild;
            passed = passed && reallocatedInOneBuild;
        }
    }

    if (scenario.RequireBudgetSaturation)
    {
        const std::size_t minimumFilledBudget = scenario.Settings.TriangleBudget > 2U
            ? scenario.Settings.TriangleBudget - 2U
            : scenario.Settings.TriangleBudget;
        for (BenchmarkFrameResult& frame : frames)
        {
            const bool budgetFilled = frame.TriangleCount >= minimumFilledBudget;
            frame.Passed = frame.Passed && budgetFilled;
            passed = passed && budgetFilled;
        }
    }

    if (scenario.Name == "incremental-emit" && frames.size() >= 3U &&
        (frames.front().AlgorithmName == "classic_cpu_roam" ||
         frames.front().AlgorithmName == "data_oriented_cpu_roam"))
    {
        const bool initializedOnce = frames[0].Stats.CpuMeshFullRebuildCount == 1U;
        const bool secondBuildStayedIncremental = frames[1].Stats.CpuMeshFullRebuildCount == 0U;
        const bool stableBuildReusedEverything =
            frames[2].Stats.CpuMeshFullRebuildCount == 0U &&
            frames[2].Stats.SplitCount == 0U &&
            frames[2].Stats.MergeCount == 0U &&
            frames[2].Stats.CpuMeshUpdatedTriangleCount == 0U &&
            frames[2].Stats.CpuMeshDirtyRangeCount == 0U &&
            frames[2].Stats.CpuMeshReusedTriangleCount == frames[2].Stats.ActiveTriangleCount;
        frames[0].Passed = frames[0].Passed && initializedOnce;
        frames[1].Passed = frames[1].Passed && secondBuildStayedIncremental;
        frames[2].Passed = frames[2].Passed && stableBuildReusedEverything;
        passed = passed && initializedOnce && secondBuildStayedIncremental && stableBuildReusedEverything;
    }

    return passed;
}

BenchmarkAlgorithmRun RunAlgorithm(
    BenchmarkAlgorithmSelection selection,
    const BenchmarkScenario& scenario,
    const Terrain::HeightMap& heightMap)
{
    BenchmarkAlgorithmRun run{};
    run.AlgorithmName = ToString(selection);

    std::unique_ptr<Algorithms::ITerrainLodAlgorithm> algorithm = CreateAlgorithm(selection);
    if (algorithm == nullptr)
    {
        // all 模式下 unavailable 会被打印为 SKIP
        // 显式选择该算法时 RunTerrainLodBenchmark 会返回失败
        return run;
    }

    run.Available = true;
    const Algorithms::TerrainLodAlgorithmInfo info = algorithm->Info();
    // 输出使用算法自报名称
    // 这样 CSV 不依赖命令行别名
    run.AlgorithmName = std::string{info.Name};
    run.Frames.reserve(scenario.CameraPath.size());

    for (std::size_t index = 0; index < scenario.CameraPath.size(); ++index)
    {
        // 每帧都重新构造 BuildInput
        // 防止算法修改输入 settings 后污染后续帧
        const BenchmarkCameraKeyframe& camera = scenario.CameraPath[index];
        Algorithms::TerrainLodBuildInput buildInput{};
        buildInput.HeightMap = &heightMap;
        buildInput.View = BuildBenchmarkView(camera);
        buildInput.Settings = scenario.Settings;

        Algorithms::TerrainLodRenderPacket renderPacket{};
        std::string errorMessage;
        Tools::PerformanceTimer buildTimer;
        const bool buildSucceeded = algorithm->BuildRenderData(buildInput, renderPacket, &errorMessage);
        const float buildWallMilliseconds = buildTimer.Stop();
        if (!errorMessage.empty())
        {
            // 错误信息不吞掉
            // benchmark 输出需要能定位失败算法和帧
            std::cerr << "[" << info.Name << "] " << errorMessage << '\n';
        }

        const Algorithms::TerrainLodStats stats = algorithm->Stats();
        BenchmarkFrameResult frame{};
        frame.AlgorithmName = std::string{info.Name};
        frame.ProfileName = scenario.Name;
        frame.CameraName = camera.Name;
        frame.FrameIndex = static_cast<int>(index);
        frame.TimeSeconds = camera.TimeSeconds;
        frame.CameraPosition = camera.Position;
        frame.HeightMapWidth = heightMap.Width();
        frame.HeightMapHeight = heightMap.Height();
        const Terrain::TerrainMeshData* cpuMesh = renderPacket.ResolveCpuMesh();
        frame.VertexCount = renderPacket.Mode == Algorithms::TerrainLodRenderMode::CpuMesh && cpuMesh != nullptr ?
            cpuMesh->Vertices.size() :
            renderPacket.ActiveTriangleCount * 3U;
        frame.IndexCount = renderPacket.IndexCount;
        frame.TriangleCount = stats.ActiveTriangleCount;
        frame.Stats = stats;
        // BuildWallMilliseconds 包括接口调用外层开销
        // Stats.CpuUpdateMilliseconds 则由算法自己报告
        frame.BuildWallMilliseconds = buildWallMilliseconds;
        const bool usesRoamBudget =
            selection == BenchmarkAlgorithmSelection::Classic ||
            selection == BenchmarkAlgorithmSelection::DataOriented;
        frame.Passed = ValidateFrame(scenario, renderPacket, stats, buildSucceeded) &&
            (!usesRoamBudget ||
             stats.ActiveTriangleCount <= scenario.Settings.TriangleBudget);
        if (selection == BenchmarkAlgorithmSelection::DataOriented)
        {
            // C1 契约：活动叶视图直接供最终输出使用，独立 Q_s 必须覆盖同一活动切分。
            frame.Passed = frame.Passed &&
                stats.PersistentSplitQueueSize == stats.ActiveTriangleCount &&
                stats.CpuFinalLeafCollectMilliseconds == 0.0F;
        }
        if (usesRoamBudget && camera.Name == "away" && !run.Frames.empty())
        {
            // 同一位置转向后背离地形，视锥感知应在单次 Build 中回收活动拓扑
            frame.Passed = frame.Passed && frame.TriangleCount < run.Frames.back().TriangleCount;
        }
        run.Frames.push_back(frame);
    }

    run.Passed = ValidateRunShape(scenario, run.Frames);
    return run;
}

float Median(std::vector<float> values)
{
    if (values.empty())
    {
        return 0.0F;
    }

    std::sort(values.begin(), values.end());
    // 当前样本量较小
    // 使用上中位数足够支撑 smoke 和 standard 摘要
    return values[values.size() / 2U];
}

float Percentile95(std::vector<float> values)
{
    if (values.empty())
    {
        return 0.0F;
    }

    std::sort(values.begin(), values.end());
    // p95 使用 ceiling 选择保守样本
    // 避免短路径下把最大 spike 过早平滑掉
    const std::size_t index = static_cast<std::size_t>(
        std::ceil(static_cast<float>(values.size()) * 0.95F) - 1.0F);
    return values[std::min(index, values.size() - 1U)];
}

void PrintRunSummary(const BenchmarkAlgorithmRun& run)
{
    if (!run.Available)
    {
        std::cout << "[SKIP] " << run.AlgorithmName << " unavailable";
        if (!run.UnavailableReason.empty())
        {
            std::cout << ": " << run.UnavailableReason;
        }
        std::cout << '\n';
        return;
    }

    std::vector<float> updateTimes;
    updateTimes.reserve(run.Frames.size());

    std::size_t minTriangles = 0U;
    std::size_t maxTriangles = 0U;
    if (!run.Frames.empty())
    {
        minTriangles = run.Frames.front().TriangleCount;
        maxTriangles = run.Frames.front().TriangleCount;
    }

    for (const BenchmarkFrameResult& frame : run.Frames)
    {
        updateTimes.push_back(frame.Stats.CpuUpdateMilliseconds);
        minTriangles = std::min(minTriangles, frame.TriangleCount);
        maxTriangles = std::max(maxTriangles, frame.TriangleCount);

        if (run.Frames.size() <= 8U)
        {
            // 短 profile 打印逐帧摘要
            // 长 profile 只输出聚合指标避免日志干扰性能阅读
            std::cout << (frame.Passed ? "[PASS] " : "[FAIL] ")
                      << run.AlgorithmName
                      << '/' << frame.CameraName
                      << " triangles=" << frame.TriangleCount
                      << " activeNodes=" << frame.Stats.ActiveNodeCount
                      << " split=" << frame.Stats.SplitCount
                      << " merge=" << frame.Stats.MergeCount
                      << " maxDepth=" << frame.Stats.MaxActiveDepth
                      << " updateMs=" << std::fixed << std::setprecision(3) << frame.Stats.CpuUpdateMilliseconds
                      << " wallMs=" << frame.BuildWallMilliseconds
                      << " invalid=" << (frame.Stats.TjunctionCount + frame.Stats.InvalidNeighborCount + frame.Stats.InvalidTopologyCount)
                      << '\n';
        }
    }

    std::cout << (run.Passed ? "[PASS] " : "[FAIL] ")
              << run.AlgorithmName
              << " frames=" << run.Frames.size()
              << " triangles=" << minTriangles << ".." << maxTriangles
              << " updateMsMedian=" << std::fixed << std::setprecision(3) << Median(updateTimes)
              << " updateMsP95=" << Percentile95(updateTimes)
              << '\n';
}

bool WriteCsv(
    const std::filesystem::path& csvPath,
    const BenchmarkScenario& scenario,
    const std::vector<BenchmarkAlgorithmRun>& runs)
{
    if (csvPath.empty())
    {
        // 没有传 csv 时 benchmark 只做控制台回归
        return true;
    }

    if (csvPath.has_parent_path())
    {
        std::filesystem::create_directories(csvPath.parent_path());
    }

    std::ofstream csv{csvPath};
    if (!csv.is_open())
    {
        std::cerr << "Failed to open benchmark CSV for writing: " << csvPath << '\n';
        return false;
    }

    csv << "profile,algorithm,frameIndex,timeSeconds,cameraName,cameraX,cameraY,cameraZ,"
           "heightMapWidth,heightMapHeight,vertexCount,indexCount,buildWallMilliseconds,";
    Experiment::WriteTerrainLodSettingsCsvHeader(csv);
    csv << ',';
    Experiment::WriteTerrainLodStatsCsvHeader(csv);
    csv << ",passed\n";

    for (const BenchmarkAlgorithmRun& run : runs)
    {
        if (!run.Available)
        {
            // CSV 只记录实际运行的算法
            // SKIP 已经在控制台摘要中呈现
            continue;
        }

        for (const BenchmarkFrameResult& frame : run.Frames)
        {
            // 字段顺序保持和表头一致
            // 新算法只要填 TerrainLodStats 就能复用同一 CSV
            csv << frame.ProfileName << ','
                << frame.AlgorithmName << ','
                << frame.FrameIndex << ','
                << frame.TimeSeconds << ','
                << frame.CameraName << ','
                << frame.CameraPosition.x << ','
                << frame.CameraPosition.y << ','
                << frame.CameraPosition.z << ','
                << frame.HeightMapWidth << ','
                << frame.HeightMapHeight << ','
                << frame.VertexCount << ','
                << frame.IndexCount << ','
                << frame.BuildWallMilliseconds << ',';
            Experiment::WriteTerrainLodSettingsCsvValues(csv, scenario.Settings);
            csv << ',';
            Experiment::WriteTerrainLodStatsCsvValues(csv, frame.Stats);
            csv << ',' << (frame.Passed ? 1 : 0)
                << '\n';
        }
    }

    std::cout << "Benchmark CSV written: " << csvPath << '\n';
    return true;
}

bool ParseAlgorithm(std::string_view value, BenchmarkAlgorithmSelection& outSelection)
{
    // 接受少量别名
    // 方便脚本里使用短名
    // 也兼容接口内部算法名
    if (value == "classic" || value == "classic_cpu_roam")
    {
        outSelection = BenchmarkAlgorithmSelection::Classic;
        return true;
    }

    if (value == "dod" || value == "data-oriented" || value == "data_oriented")
    {
        outSelection = BenchmarkAlgorithmSelection::DataOriented;
        return true;
    }

    if (value == "all")
    {
        outSelection = BenchmarkAlgorithmSelection::All;
        return true;
    }

    return false;
}

bool ParsePassPolicy(std::string_view value, BenchmarkPassPolicySelection& outSelection)
{
    if (value == "default")
    {
        outSelection = BenchmarkPassPolicySelection::Default;
        return true;
    }
    if (value == "serial-incremental" || value == "serial")
    {
        outSelection = BenchmarkPassPolicySelection::SerialIncremental;
        return true;
    }
    if (value == "maximum-parallel-incremental" ||
        value == "maximum-parallel" || value == "parallel")
    {
        outSelection = BenchmarkPassPolicySelection::MaximumParallelIncremental;
        return true;
    }
    if (value == "serial-full")
    {
        outSelection = BenchmarkPassPolicySelection::SerialFull;
        return true;
    }
    if (value == "maximum-parallel-full" || value == "parallel-full")
    {
        outSelection = BenchmarkPassPolicySelection::MaximumParallelFull;
        return true;
    }
    return false;
}

bool ParseProfile(std::string_view value, BenchmarkProfile& outProfile)
{
    // budget-reentry 专门覆盖硬预算满载后的原地转向
    if (value == "smoke")
    {
        outProfile = BenchmarkProfile::Smoke;
        return true;
    }

    if (value == "standard")
    {
        outProfile = BenchmarkProfile::Standard;
        return true;
    }

    if (value == "budget-reentry")
    {
        outProfile = BenchmarkProfile::BudgetReentry;
        return true;
    }

    if (value == "budget-saturation")
    {
        outProfile = BenchmarkProfile::BudgetSaturation;
        return true;
    }

    if (value == "incremental-emit")
    {
        outProfile = BenchmarkProfile::IncrementalEmit;
        return true;
    }

    if (value == "pass-trace-replay")
    {
        outProfile = BenchmarkProfile::PassTraceReplay;
        return true;
    }

    if (value == "pass-policy-replay")
    {
        outProfile = BenchmarkProfile::PassPolicyReplay;
        return true;
    }

    return false;
}
} // 匿名命名空间

int RunTerrainLodBenchmark(const BenchmarkOptions& options)
{
    BenchmarkScenario scenario = MakeScenario(options.Profile);
    ApplyPassPolicy(options.PassPolicy, scenario.Settings);
    ApplyTopologyExperimentSettings(options, scenario.Settings);

    Terrain::HeightMap heightMap;
    std::string errorMessage;
    if (!heightMap.LoadFromFile(scenario.HeightMapPath, &errorMessage))
    {
        // HeightMap 是所有算法共享输入
        // 加载失败时没有可比较的基准
        std::cerr << errorMessage << '\n';
        return 1;
    }

    std::cout << "Terrain LOD benchmark profile=" << scenario.Name
              << " algorithm=" << ToString(options.Algorithm)
              << " policy=" << ToString(options.PassPolicy)
              << " heightmap=" << scenario.HeightMapPath
              << " frames=" << scenario.CameraPath.size()
              << " maxDepth=" << scenario.Settings.MaxDepth
              << " screenSpaceSplitPixels=" << scenario.Settings.ScreenSpaceSplitThresholdPixels
              << " screenSpaceMergePixels=" << scenario.Settings.ScreenSpaceMergeThresholdPixels
              << " triangleBudget=" << scenario.Settings.TriangleBudget
              << " splitTopologyMinCandidates="
              << scenario.Settings.PassPolicy.SplitTopologyMinParallelCandidateCount
              << " mergeTopologyMinCandidates="
              << scenario.Settings.PassPolicy.MergeTopologyMinParallelCandidateCount
              << " parallelTopologyTargetBuild="
              << scenario.Settings.PassPolicy.ParallelTopologyTargetBuild
              << " parallelTopologyPhase="
              << Algorithms::ToString(scenario.Settings.PassPolicy.ParallelTopologyPhase)
              << '\n';

    std::vector<BenchmarkAlgorithmRun> runs;
    const std::vector<BenchmarkAlgorithmSelection> selections = ExpandAlgorithmSelection(options.Algorithm);
    runs.reserve(
        selections.size() *
        (options.Profile == BenchmarkProfile::PassPolicyReplay ? 4U : 1U));

    bool anyAvailable = false;
    bool allAvailablePassed = true;
    for (BenchmarkAlgorithmSelection selection : selections)
    {
        if (options.Profile == BenchmarkProfile::PassPolicyReplay)
        {
            BenchmarkScenario serialIncrementalScenario = scenario;
            BenchmarkScenario parallelIncrementalScenario = scenario;
            BenchmarkScenario serialFullScenario = scenario;
            BenchmarkScenario parallelFullScenario = scenario;
            ApplyPassPolicy(
                BenchmarkPassPolicySelection::SerialIncremental,
                serialIncrementalScenario.Settings);
            ApplyPassPolicy(
                BenchmarkPassPolicySelection::MaximumParallelIncremental,
                parallelIncrementalScenario.Settings);
            ApplyPassPolicy(BenchmarkPassPolicySelection::SerialFull, serialFullScenario.Settings);
            ApplyPassPolicy(
                BenchmarkPassPolicySelection::MaximumParallelFull,
                parallelFullScenario.Settings);

            BenchmarkAlgorithmRun serialIncrementalRun =
                RunAlgorithm(selection, serialIncrementalScenario, heightMap);
            BenchmarkAlgorithmRun parallelIncrementalRun =
                RunAlgorithm(selection, parallelIncrementalScenario, heightMap);
            BenchmarkAlgorithmRun serialFullRun = RunAlgorithm(selection, serialFullScenario, heightMap);
            BenchmarkAlgorithmRun parallelFullRun =
                RunAlgorithm(selection, parallelFullScenario, heightMap);
            const bool policyMatched =
                HasEquivalentPolicyResults(serialIncrementalRun, parallelIncrementalRun) &&
                HasEquivalentPolicyResults(serialIncrementalRun, serialFullRun) &&
                HasEquivalentPolicyResults(serialIncrementalRun, parallelFullRun) &&
                HasExpectedPolicyTrace(
                    serialIncrementalRun,
                    BenchmarkPassPolicySelection::SerialIncremental) &&
                HasExpectedPolicyTrace(
                    parallelIncrementalRun,
                    BenchmarkPassPolicySelection::MaximumParallelIncremental) &&
                HasExpectedPolicyTrace(serialFullRun, BenchmarkPassPolicySelection::SerialFull) &&
                HasExpectedPolicyTrace(
                    parallelFullRun,
                    BenchmarkPassPolicySelection::MaximumParallelFull);
            serialIncrementalRun.Passed = serialIncrementalRun.Passed && policyMatched;
            parallelIncrementalRun.Passed = parallelIncrementalRun.Passed && policyMatched;
            serialFullRun.Passed = serialFullRun.Passed && policyMatched;
            parallelFullRun.Passed = parallelFullRun.Passed && policyMatched;
            std::cout << (policyMatched ? "[PASS] " : "[FAIL] ")
                      << serialIncrementalRun.AlgorithmName
                      << " pass policy equivalence\n";

            RelabelPolicyRun(serialIncrementalRun, "serial-incremental");
            RelabelPolicyRun(parallelIncrementalRun, "maximum-parallel-incremental");
            RelabelPolicyRun(serialFullRun, "serial-full");
            RelabelPolicyRun(parallelFullRun, "maximum-parallel-full");
            PrintRunSummary(serialIncrementalRun);
            PrintRunSummary(parallelIncrementalRun);
            PrintRunSummary(serialFullRun);
            PrintRunSummary(parallelFullRun);
            anyAvailable = anyAvailable ||
                serialIncrementalRun.Available || parallelIncrementalRun.Available ||
                serialFullRun.Available || parallelFullRun.Available;
            allAvailablePassed = allAvailablePassed &&
                (!serialIncrementalRun.Available || serialIncrementalRun.Passed) &&
                (!parallelIncrementalRun.Available || parallelIncrementalRun.Passed) &&
                (!serialFullRun.Available || serialFullRun.Passed) &&
                (!parallelFullRun.Available || parallelFullRun.Passed);
            runs.push_back(std::move(serialIncrementalRun));
            runs.push_back(std::move(parallelIncrementalRun));
            runs.push_back(std::move(serialFullRun));
            runs.push_back(std::move(parallelFullRun));
            continue;
        }

        // allAvailablePassed 只统计实际运行的算法
        BenchmarkAlgorithmRun run = RunAlgorithm(selection, scenario, heightMap);
        if (scenario.RequireDeterministicReplay && run.Available)
        {
            const BenchmarkAlgorithmRun replay = RunAlgorithm(selection, scenario, heightMap);
            const bool replayMatched = HasEquivalentReplay(run, replay);
            run.Passed = run.Passed && replayMatched;
            std::cout << (replayMatched ? "[PASS] " : "[FAIL] ")
                      << run.AlgorithmName
                      << " deterministic replay hashes\n";
        }
        PrintRunSummary(run);
        anyAvailable = anyAvailable || run.Available;
        allAvailablePassed = allAvailablePassed && (!run.Available || run.Passed);
        runs.push_back(std::move(run));
    }

    const bool csvWritten = WriteCsv(options.CsvPath, scenario, runs);

    // 不能把没有实际运行任何算法的请求误判为成功。
    if (!anyAvailable)
    {
        // 显式选择未实现算法时需要失败
        // 否则 CI 会误把空跑当作通过
        std::cerr << "No requested benchmark algorithm is available.\n";
        return 1;
    }

    if (options.Algorithm != BenchmarkAlgorithmSelection::All && !runs.empty() && !runs.front().Available)
    {
        // 非 all 模式下 unavailable 不是 skip
        // 用户明确要求了该算法
        return 1;
    }

    const bool passed = allAvailablePassed && csvWritten;
    std::cout << (passed ? "Terrain LOD benchmark result: PASS\n" : "Terrain LOD benchmark result: FAIL\n");
    return passed ? 0 : 1;
}

int RunTerrainLodBenchmarkFromCommandLine(int argc, char** argv)
{
    BenchmarkOptions options{};

    for (int index = 1; index < argc; ++index)
    {
        const std::string_view argument{argv[index]};
        if (argument == "--benchmark")
        {
            // main 已经根据 --benchmark 分流
            // 这里允许重复看到该 flag
            continue;
        }

        if (argument == "--help" || argument == "-h")
        {
            // benchmark help 不启动窗口也不加载 HeightMap
            std::cout << BenchmarkUsage();
            return 0;
        }

        if (argument == "--algorithm" && index + 1 < argc)
        {
            // 所有带值参数都在消费后递增 index
            // 避免下一轮把值当成未知参数
            if (!ParseAlgorithm(argv[++index], options.Algorithm))
            {
                std::cerr << "Unknown benchmark algorithm: " << argv[index] << '\n';
                std::cerr << BenchmarkUsage();
                return 1;
            }
            continue;
        }

        if (argument == "--algorithm")
        {
            // 缺值错误需要在解析时返回
            // 不进入默认 benchmark
            std::cerr << "--algorithm requires a value.\n";
            std::cerr << BenchmarkUsage();
            return 1;
        }

        if (argument == "--profile" && index + 1 < argc)
        {
            // profile 控制场景和验收规则
            // 不允许静默 fallback 到 smoke
            if (!ParseProfile(argv[++index], options.Profile))
            {
                std::cerr << "Unknown benchmark profile: " << argv[index] << '\n';
                std::cerr << BenchmarkUsage();
                return 1;
            }
            continue;
        }

        if (argument == "--profile")
        {
            std::cerr << "--profile requires a value.\n";
            std::cerr << BenchmarkUsage();
            return 1;
        }

        if (argument == "--pass-policy" && index + 1 < argc)
        {
            if (!ParsePassPolicy(argv[++index], options.PassPolicy))
            {
                std::cerr << "Unknown benchmark pass policy: " << argv[index] << '\n';
                std::cerr << BenchmarkUsage();
                return 1;
            }
            continue;
        }

        if (argument == "--pass-policy")
        {
            std::cerr << "--pass-policy requires a value.\n";
            std::cerr << BenchmarkUsage();
            return 1;
        }

        if (argument == "--split-topology-min-candidates" && index + 1 < argc)
        {
            if (!ParseSize(argv[++index], options.SplitTopologyMinParallelCandidateCount))
            {
                std::cerr << "Invalid --split-topology-min-candidates value: " << argv[index] << '\n';
                return 1;
            }
            continue;
        }

        if (argument == "--merge-topology-min-candidates" && index + 1 < argc)
        {
            if (!ParseSize(argv[++index], options.MergeTopologyMinParallelCandidateCount))
            {
                std::cerr << "Invalid --merge-topology-min-candidates value: " << argv[index] << '\n';
                return 1;
            }
            continue;
        }

        if (argument == "--parallel-topology-target-build" && index + 1 < argc)
        {
            if (!ParseSize(argv[++index], options.ParallelTopologyTargetBuild))
            {
                std::cerr << "Invalid --parallel-topology-target-build value: " << argv[index] << '\n';
                return 1;
            }
            continue;
        }

        if (argument == "--parallel-topology-phase" && index + 1 < argc)
        {
            if (!ParseParallelTopologyPhase(argv[++index], options.ParallelTopologyPhase))
            {
                std::cerr << "Invalid --parallel-topology-phase value: " << argv[index] << '\n';
                return 1;
            }
            continue;
        }

        if (argument == "--split-topology-min-candidates" ||
            argument == "--merge-topology-min-candidates" ||
            argument == "--parallel-topology-target-build" ||
            argument == "--parallel-topology-phase")
        {
            std::cerr << argument << " requires a value.\n";
            return 1;
        }

        if (argument == "--csv" && index + 1 < argc)
        {
            // CSV 路径可以指向尚不存在的父目录
            // WriteCsv 会负责创建
            options.CsvPath = argv[++index];
            continue;
        }

        if (argument == "--csv")
        {
            std::cerr << "--csv requires a path.\n";
            std::cerr << BenchmarkUsage();
            return 1;
        }

        std::cerr << "Unknown benchmark argument: " << argument << '\n';
        std::cerr << BenchmarkUsage();
        return 1;
    }

    // 命令行解析只负责轻量参数
    // 实际场景构造和算法运行集中在 RunTerrainLodBenchmark
    return RunTerrainLodBenchmark(options);
}

std::string BenchmarkUsage()
{
    return "Usage: ParallelROAM --benchmark [--algorithm classic|dod|all] "
           "[--profile smoke|budget-reentry|budget-saturation|incremental-emit|pass-trace-replay|pass-policy-replay|standard] "
           "[--pass-policy default|serial-incremental|maximum-parallel-incremental|serial-full|maximum-parallel-full] "
           "[--split-topology-min-candidates count] [--merge-topology-min-candidates count] "
           "[--parallel-topology-target-build build] [--parallel-topology-phase both|split|merge] "
           "[--csv path]\n";
}
} // 命名空间 ParallelRoam::Benchmark
