#pragma once

#include "algorithms/TerrainLodPassTrace.h"
#include "terrain/HeightMap.h"
#include "terrain/TerrainMeshBuilder.h"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ParallelRoam::Algorithms
{
/// <summary>
/// 为每种地形 LOD 实现提供稳定编号，供界面选择、基准测试和日志记录使用
/// </summary>
enum class TerrainLodAlgorithmId
{
    ClassicCpuRoam,
    DataOrientedCpuRoam,
    Count,
};

/// <summary>
/// 保存地形 LOD 实现的程序名称、界面名称和用途说明
/// </summary>
struct TerrainLodAlgorithmInfo
{
    TerrainLodAlgorithmId Id{TerrainLodAlgorithmId::ClassicCpuRoam};
    std::string_view Name;
    std::string_view DisplayName;
    std::string_view Description;
};

/// <summary>
/// 说明实现支持哪些输出方式和拓扑操作，调用方据此启用相应功能
/// </summary>
struct TerrainLodAlgorithmCapabilities
{
    bool SupportsCpuMeshOutput{false};
    bool SupportsSplit{false};
    bool SupportsMerge{false};
    bool SupportsCrackFix{false};
    bool SupportsTopologyValidation{false};
};

/// <summary>
/// 所有地形 LOD 实现共用的运行参数，确保基准测试和渲染使用相同配置
/// </summary>
struct TerrainLodSettings
{
    float TerrainSize{30.0F};
    float HeightScale{4.0F};
    int MaxDepth{14};
    // 细分与合并使用不同的像素阈值，避免误差在临界值附近时反复切换
    float ScreenSpaceSplitThresholdPixels{4.0F};
    float ScreenSpaceMergeThresholdPixels{2.0F};
    // 限制当前活动叶三角形数量，防止细分超过 CPU 网格预算
    std::size_t TriangleBudget{20000U};
    // 控制 DOD 是否先把能够独立处理的细分分给多个线程，候选评分是否并行由另一项设置决定
    bool EnableParallelSplit{true};
    TerrainLodPassPolicy PassPolicy{};
    bool EnableLocalConstraints{true};
    bool EnableTopologyValidation{false};
    // 基准测试开启后保存结果哈希并检查持久队列，普通交互帧默认关闭全量证据扫描
    bool EnablePassEvidence{false};
};

/// <summary>
/// 规定视锥平面在 TerrainLodViewInput 数组中的固定顺序
/// </summary>
enum class TerrainLodFrustumPlane
{
    Left,
    Right,
    Bottom,
    Top,
    Near,
    Far,
    Count,
};

/// <summary>
/// 汇总渲染器和 LOD 算法共用的相机、投影及视锥数据
/// </summary>
struct TerrainLodViewInput
{
    glm::mat4 View{1.0F};
    glm::mat4 Projection{1.0F};
    glm::mat4 ViewProjection{1.0F};
    glm::vec3 CameraPosition{0.0F};
    glm::vec3 CameraForward{0.0F, 0.0F, -1.0F};
    std::array<glm::vec4, static_cast<std::size_t>(TerrainLodFrustumPlane::Count)> FrustumPlanes{}; // 法线朝向视锥内部，内部点的平面值非负
    std::uint32_t DrawableWidth{1U};
    std::uint32_t DrawableHeight{1U};
};

/// <summary>
/// 一次 LOD 更新所需的完整只读输入
/// </summary>
struct TerrainLodBuildInput
{
    const Terrain::HeightMap* HeightMap{nullptr};
    TerrainLodViewInput View;
    TerrainLodSettings Settings;
};

/// <summary>
/// 为固定高度图、相机和算法设置生成可重放输入编号
/// </summary>
[[nodiscard]] inline std::uint64_t HashTerrainLodBuildInput(const TerrainLodBuildInput& input)
{
    std::uint64_t hash = TerrainLodHashOffset;
    if (input.HeightMap != nullptr)
    {
        const std::string sourcePath = input.HeightMap->SourcePath().generic_string();
        AppendTerrainLodHash(hash, std::string_view{sourcePath});
        AppendTerrainLodHash(hash, input.HeightMap->Width());
        AppendTerrainLodHash(hash, input.HeightMap->Height());
    }
    for (int column = 0; column < 4; ++column)
    {
        for (int row = 0; row < 4; ++row)
        {
            AppendTerrainLodHash(hash, input.View.ViewProjection[column][row]);
        }
    }
    for (const glm::vec4& plane : input.View.FrustumPlanes)
    {
        AppendTerrainLodHash(hash, plane.x);
        AppendTerrainLodHash(hash, plane.y);
        AppendTerrainLodHash(hash, plane.z);
        AppendTerrainLodHash(hash, plane.w);
    }
    AppendTerrainLodHash(hash, input.View.DrawableWidth);
    AppendTerrainLodHash(hash, input.View.DrawableHeight);
    AppendTerrainLodHash(hash, input.Settings.TerrainSize);
    AppendTerrainLodHash(hash, input.Settings.HeightScale);
    AppendTerrainLodHash(hash, input.Settings.MaxDepth);
    AppendTerrainLodHash(hash, input.Settings.ScreenSpaceSplitThresholdPixels);
    AppendTerrainLodHash(hash, input.Settings.ScreenSpaceMergeThresholdPixels);
    AppendTerrainLodHash(hash, input.Settings.TriangleBudget);
    AppendTerrainLodHash(hash, input.Settings.EnableParallelSplit);
    AppendTerrainLodHash(hash, input.Settings.EnableLocalConstraints);
    AppendTerrainLodHash(hash, input.Settings.PassPolicy.MergeScore);
    AppendTerrainLodHash(hash, input.Settings.PassPolicy.SplitScore);
    AppendTerrainLodHash(hash, input.Settings.PassPolicy.MergeTopology);
    AppendTerrainLodHash(hash, input.Settings.PassPolicy.SplitTopology);
    AppendTerrainLodHash(hash, input.Settings.PassPolicy.MeshEmit);
    AppendTerrainLodHash(hash, input.Settings.PassPolicy.CpuUpload);
    AppendTerrainLodHash(hash, input.Settings.PassPolicy.MergeScoreWorkerCount);
    AppendTerrainLodHash(hash, input.Settings.PassPolicy.SplitScoreWorkerCount);
    AppendTerrainLodHash(hash, input.Settings.PassPolicy.MergeTopologyWorkerCount);
    AppendTerrainLodHash(hash, input.Settings.PassPolicy.SplitTopologyWorkerCount);
    AppendTerrainLodHash(hash, input.Settings.PassPolicy.MeshEmitWorkerCount);
    AppendTerrainLodHash(hash, input.Settings.PassPolicy.SplitTopologyMinParallelCandidateCount);
    AppendTerrainLodHash(hash, input.Settings.PassPolicy.MergeTopologyMinParallelCandidateCount);
    AppendTerrainLodHash(hash, input.Settings.PassPolicy.ParallelTopologyTargetBuild);
    AppendTerrainLodHash(hash, input.Settings.PassPolicy.ParallelTopologyPhase);
    return hash;
}

/// <summary>
/// 区分可直接渲染的 CPU 网格输出和只提供状态的调试输出
/// </summary>
enum class TerrainLodRenderMode
{
    CpuMesh,
    DebugOnly,
};

/// <summary>
/// 描述 CPU 网格中需要重新上传的一段连续顶点和索引范围；两者可以独立为空
/// </summary>
struct TerrainLodCpuMeshUpdateRange
{
    std::size_t FirstVertex{0};
    std::size_t VertexCount{0};
    std::size_t FirstIndex{0};
    std::size_t IndexCount{0};
};

enum class TerrainLodCpuMeshLifetime
{
    OwnedByPacket,
    UntilNextBuildOrReset,
};

/// <summary>
/// LOD 算法交给渲染器和基准测试的统一结果
/// </summary>
struct TerrainLodRenderPacket
{
    TerrainLodRenderMode Mode{TerrainLodRenderMode::CpuMesh};
    Terrain::TerrainMeshData CpuMesh;
    // 增量算法可以直接返回内部长期保留的网格引用，避免每帧复制全部顶点和索引
    const Terrain::TerrainMeshData* BorrowedCpuMesh{nullptr};
    std::vector<TerrainLodCpuMeshUpdateRange> CpuMeshUpdateRanges;
    TerrainLodCpuMeshLifetime CpuMeshLifetime{TerrainLodCpuMeshLifetime::OwnedByPacket};
    bool CpuMeshRequiresFullUpload{true};
    TerrainLodCpuUploadAction CpuUploadAction{TerrainLodCpuUploadAction::Automatic};
    std::uint64_t CpuMeshGeneration{0};
    std::string StatusMessage;
    std::size_t ActiveLeafCount{0};
    std::size_t ActiveTriangleCount{0};
    std::size_t IndexCount{0};

    [[nodiscard]] const Terrain::TerrainMeshData* ResolveCpuMesh() const
    {
        return BorrowedCpuMesh != nullptr ? BorrowedCpuMesh : &CpuMesh;
    }

    /// <summary>
    /// 检查当前结果是否提供了与渲染模式匹配且可安全访问的 CPU 网格
    /// </summary>
    [[nodiscard]] bool HasConsistentResourceContract() const
    {
        if (Mode == TerrainLodRenderMode::CpuMesh || Mode == TerrainLodRenderMode::DebugOnly)
        {
            const bool hasBorrowedCpuMesh = BorrowedCpuMesh != nullptr;
            const Terrain::TerrainMeshData* cpuMesh = ResolveCpuMesh();
            bool hasValidCpuMeshContract = true;
            if (Mode == TerrainLodRenderMode::CpuMesh)
            {
                hasValidCpuMeshContract = cpuMesh != nullptr &&
                    !cpuMesh->Vertices.empty() && !cpuMesh->Indices.empty();
                if (hasBorrowedCpuMesh)
                {
                    hasValidCpuMeshContract = hasValidCpuMeshContract &&
                        CpuMesh.Vertices.empty() && CpuMesh.Indices.empty() &&
                        CpuMeshLifetime == TerrainLodCpuMeshLifetime::UntilNextBuildOrReset &&
                        CpuMeshGeneration > 0U;
                    for (const TerrainLodCpuMeshUpdateRange& range : CpuMeshUpdateRanges)
                    {
                        hasValidCpuMeshContract = hasValidCpuMeshContract &&
                            range.FirstVertex <= cpuMesh->Vertices.size() &&
                            range.VertexCount <= cpuMesh->Vertices.size() - range.FirstVertex &&
                            range.FirstIndex <= cpuMesh->Indices.size() &&
                            range.IndexCount <= cpuMesh->Indices.size() - range.FirstIndex;
                    }
                }
                else
                {
                    hasValidCpuMeshContract = hasValidCpuMeshContract &&
                        CpuMeshLifetime == TerrainLodCpuMeshLifetime::OwnedByPacket &&
                        CpuMeshRequiresFullUpload && CpuMeshGeneration == 0U &&
                        CpuMeshUpdateRanges.empty();
                }
            }
            return hasValidCpuMeshContract;
        }

        return false;
    }
};

/// <summary>
/// 汇总 Classic 与 DOD 共用的运行统计，供界面、回归测试和 CSV 报告读取
/// </summary>
struct TerrainLodStats
{
    // 阶段记录只描述当前真实实现，不会改变算法选择和执行顺序
    TerrainLodPassTraceArray PassTraces{MakeTerrainLodPassTraces()};
    // 证据字段用于固定轨迹重放，关闭 EnablePassEvidence 时保持为零
    std::uint64_t BuildSequence{0U};
    std::uint64_t ReplayInputHash{0U};
    std::uint64_t TopologyHash{0U};
    std::uint64_t ActiveLeafHash{0U};
    std::uint64_t MeshHash{0U};
    std::uint64_t NormalizedMeshHash{0U};
    std::size_t TriangleBudget{0U};
    std::size_t BudgetViolationCount{0U};
    std::size_t QueueInvariantViolationCount{0U};
    std::size_t ResourceValidationFailureCount{0U};
    float PassEvidenceMilliseconds{0.0F};
    std::size_t ActiveTriangleCount{0};
    std::size_t ActiveNodeCount{0};
    std::size_t OriginalTriangleCount{0};
    std::size_t SubdividedTriangleCount{0};
    std::size_t RebuiltTriangleCount{0};
    std::size_t ActiveSplitCount{0};
    std::size_t SplitCount{0};
    std::size_t ForcedSplitCount{0};
    std::size_t MergeCount{0};
    std::size_t CrackRiskCount{0};
    std::size_t ConstraintPassCount{0};
    std::size_t CandidatePeakCount{0};
    // 记录两种 CPU ROAM 实现中跨帧保留的细分/合并队列规模和维护次数
    std::size_t PersistentSplitQueueSize{0};
    std::size_t PersistentMergeQueueSize{0};
    std::size_t QueueCrossoverCount{0};
    std::size_t QueueMembershipUpdateCount{0};
    // 记录增量更新网格时的完整重建、复用和重新上传区间，不支持该功能的实现保持为零
    std::size_t CpuMeshFullRebuildCount{0};
    std::size_t CpuMeshUpdatedTriangleCount{0};
    std::size_t CpuMeshReusedTriangleCount{0};
    std::size_t CpuMeshDirtyRangeCount{0};
    std::size_t RejectedSplitCount{0};
    std::size_t BudgetRejectedSplitCount{0};
    std::size_t RejectedMergeCount{0};
    std::size_t TjunctionCount{0};
    std::size_t InvalidNeighborCount{0};
    std::size_t InvalidTopologyCount{0};
    std::size_t CpuGpuUploadBytes{0};
    std::size_t CpuGpuReadbackBytes{0};
    // CpuWorkerCount 是本次 CPU LOD 更新实际使用的线程数量，而不是线程池总容量
    std::size_t CpuWorkerCount{0};
    // 记录 DOD 按分块修改拓扑时的候选规模、非空分块和实际线程数量
    std::size_t TopologyCommitMinCandidateCount{0};
    std::size_t SplitTopologyCommitMinCandidateCount{0};
    std::size_t MergeTopologyCommitMinCandidateCount{0};
    std::size_t SplitTopologyCandidateCount{0};
    std::size_t SplitTopologyNonEmptyChunkCount{0};
    std::size_t SplitTopologyCommitWorkerCount{0};
    std::size_t ParallelSplitCommitCount{0};
    std::size_t MergeTopologyCandidateCount{0};
    std::size_t MergeTopologyNonEmptyChunkCount{0};
    std::size_t MergeTopologyCommitWorkerCount{0};
    std::size_t ParallelMergeCommitCount{0};
    // 区分可以交给独立分块处理的候选和必须由主线程处理的跨分块候选
    std::size_t InteriorSplitCandidateCount{0};
    std::size_t BoundarySplitCandidateCount{0};
    std::size_t InteriorMergeCandidateCount{0};
    std::size_t BoundaryMergeCandidateCount{0};
    float CpuUpdateMilliseconds{0.0F};
    // CpuUtilizationPercent 以单个逻辑核心满载为 100%，多线程运行时可以超过 100%
    float CpuUtilizationPercent{0.0F};
    float CpuPrepareMilliseconds{0.0F};
    // 对应合并评分阶段，只包含当前 Q_m 条目的评分刷新与建堆
    float CpuMergeCandidateMarkMilliseconds{0.0F};
    float CpuMergeTopologyMilliseconds{0.0F};
    // 以下字段分别记录细分和合并的准备、并行处理及后续串行处理耗时
    // Classic 只填写主线程反复处理候选直到队列稳定的耗时，DOD 专用并行字段保持为零
    float CpuSplitTopologyChunkBuildMilliseconds{0.0F};
    float CpuSplitTopologyQueueInvalidationMilliseconds{0.0F};
    float CpuSplitTopologyParallelCommitMilliseconds{0.0F};
    float CpuSplitTopologyResultMergeMilliseconds{0.0F};
    float CpuSplitTopologyIndexQueueRefreshMilliseconds{0.0F};
    float CpuSplitTopologySerialConvergenceMilliseconds{0.0F};
    float CpuMergeTopologyChunkBuildMilliseconds{0.0F};
    float CpuMergeTopologyQueueInvalidationMilliseconds{0.0F};
    float CpuMergeTopologyParallelCommitMilliseconds{0.0F};
    float CpuMergeTopologyResultMergeMilliseconds{0.0F};
    float CpuMergeTopologyIndexQueueRefreshMilliseconds{0.0F};
    float CpuMergeTopologySerialConvergenceMilliseconds{0.0F};
    float CpuBudgetLeafCollectMilliseconds{0.0F};
    float CpuErrorEvalMilliseconds{0.0F};
    // 对应细分扫描与评分阶段，只包含当前 Q_s 条目的评分刷新与建堆
    float CpuSplitCandidateMarkMilliseconds{0.0F};
    float CpuSplitTopologyMilliseconds{0.0F};
    float CpuFinalLeafCollectMilliseconds{0.0F};
    // 只记录 CPU 网格提交，不包含后续图形缓冲上传
    float CpuMeshEmitMilliseconds{0.0F};
    float CpuFinalizeMilliseconds{0.0F};
    float CpuUploadMilliseconds{0.0F};
    float RenderMilliseconds{0.0F};
    float SplitMilliseconds{0.0F};
    float MergeMilliseconds{0.0F};
    float EmitMilliseconds{0.0F};
    float ValidateMilliseconds{0.0F};
    int MaxActiveDepth{0};
};

/// <summary>
/// 地形 LOD 的统一接口，使不同 CPU/GPU 实现能够接入同一渲染和基准测试流程
/// </summary>
class ITerrainLodAlgorithm
{
public:
    virtual ~ITerrainLodAlgorithm() = default;

    [[nodiscard]] virtual TerrainLodAlgorithmInfo Info() const = 0;
    [[nodiscard]] virtual TerrainLodAlgorithmCapabilities Capabilities() const = 0;

    /// <summary>
    /// 根据输入更新当前帧 LOD 并生成渲染结果；失败时通过 errorMessage 返回可定位的原因
    /// </summary>
    [[nodiscard]] virtual bool BuildRenderData(
        const TerrainLodBuildInput& input,
        TerrainLodRenderPacket& outPacket,
        std::string* errorMessage) = 0;

    [[nodiscard]] virtual const TerrainLodStats& Stats() const = 0;
    virtual void Reset() = 0;
};
} // 命名空间 ParallelRoam::Algorithms
