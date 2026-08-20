#pragma once

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
/// 标识当前 terrain LOD 算法实现，供 UI、benchmark 和日志输出使用
/// </summary>
enum class TerrainLodAlgorithmId
{
    ClassicCpuRoam,
    DataOrientedCpuRoam,
    Count,
};

/// <summary>
/// terrain LOD 算法的展示名称和简短描述
/// </summary>
struct TerrainLodAlgorithmInfo
{
    TerrainLodAlgorithmId Id{TerrainLodAlgorithmId::ClassicCpuRoam};
    std::string_view Name;
    std::string_view DisplayName;
    std::string_view Description;
};

/// <summary>
/// 描述某个 terrain LOD 算法当前可输出的渲染路径和拓扑能力
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
/// terrain LOD 算法共享的运行参数，benchmark 和 renderer 使用同一套字段做公平对比
/// </summary>
struct TerrainLodSettings
{
    float TerrainSize{30.0F};
    float HeightScale{4.0F};
    int MaxDepth{14};
    // Classic 和 DOD 共享像素单位的迟滞阈值。
    float ScreenSpaceSplitThresholdPixels{4.0F};
    float ScreenSpaceMergeThresholdPixels{2.0F};
    // 两种 CPU ROAM 路径共享活动 leaf triangle 硬上限。
    std::size_t TriangleBudget{20000U};
    // 仅供 DOD 选择是否执行 chunk 并行 Split 预提交；评分并行不受影响。
    bool EnableParallelSplit{true};
    bool EnableLocalConstraints{true};
    bool EnableTopologyValidation{false};
};

/// <summary>
/// 视锥平面在 TerrainLodViewInput 中的固定顺序
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
/// renderer 与视点相关 LOD 算法共享的只读视图数据
/// </summary>
struct TerrainLodViewInput
{
    glm::mat4 View{1.0F};
    glm::mat4 Projection{1.0F};
    glm::mat4 ViewProjection{1.0F};
    glm::vec3 CameraPosition{0.0F};
    glm::vec3 CameraForward{0.0F, 0.0F, -1.0F};
    std::array<glm::vec4, static_cast<std::size_t>(TerrainLodFrustumPlane::Count)> FrustumPlanes{}; // 向内法线且内侧平面值非负
    std::uint32_t DrawableWidth{1U};
    std::uint32_t DrawableHeight{1U};
};

/// <summary>
/// 单帧 LOD 构建输入，固定高度图、视图和统一算法参数
/// </summary>
struct TerrainLodBuildInput
{
    const Terrain::HeightMap* HeightMap{nullptr};
    TerrainLodViewInput View;
    TerrainLodSettings Settings;
};

/// <summary>
/// 算法渲染输出模式，区分 CPU mesh 和无几何的调试路径
/// </summary>
enum class TerrainLodRenderMode
{
    CpuMesh,
    DebugOnly,
};

/// <summary>
/// CPU mesh 的一段连续更新范围；vertex/index 范围可以独立为空。
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
/// 算法输出给 renderer 或 benchmark 的统一渲染数据包
/// </summary>
struct TerrainLodRenderPacket
{
    TerrainLodRenderMode Mode{TerrainLodRenderMode::CpuMesh};
    Terrain::TerrainMeshData CpuMesh;
    // 增量 CPU 算法可借用其持久 mesh，避免每帧复制完整数组。
    const Terrain::TerrainMeshData* BorrowedCpuMesh{nullptr};
    std::vector<TerrainLodCpuMeshUpdateRange> CpuMeshUpdateRanges;
    TerrainLodCpuMeshLifetime CpuMeshLifetime{TerrainLodCpuMeshLifetime::OwnedByPacket};
    bool CpuMeshRequiresFullUpload{true};
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
    /// 校验拥有式或借用式 CPU mesh 是否满足当前渲染模式
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
/// Classic 和 Data-Oriented 版本共享的统计字段，用于 UI 展示、回归测试和 CSV 输出
/// </summary>
struct TerrainLodStats
{
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
    // Classic 和 DOD 填充持久 topology queue diagnostics
    std::size_t PersistentSplitQueueSize{0};
    std::size_t PersistentMergeQueueSize{0};
    std::size_t QueueCrossoverCount{0};
    std::size_t QueueMembershipUpdateCount{0};
    // Classic 和 DOD incremental emit 统计；其他算法保持为零。
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
    // CpuWorkerCount 表示本次 CPU LOD build 的实际并行宽度
    std::size_t CpuWorkerCount{0};
    // DOD chunk topology commit 诊断；其他算法保持为零
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
    float CpuUpdateMilliseconds{0.0F};
    // CpuUtilizationPercent 按单核 100% 口径记录进程 CPU 占用
    float CpuUtilizationPercent{0.0F};
    float CpuPrepareMilliseconds{0.0F};
    float CpuMergeCandidateMarkMilliseconds{0.0F};
    float CpuMergeTopologyMilliseconds{0.0F};
    // split/merge topology detail fields 是可选的子阶段诊断
    // Classic 和 DOD 共享 serial convergence，parallel-only 字段在 Classic 中保持为 0
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
    float CpuSplitCandidateMarkMilliseconds{0.0F};
    float CpuSplitTopologyMilliseconds{0.0F};
    float CpuFinalLeafCollectMilliseconds{0.0F};
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
/// 地形 LOD 算法的统一边界，所有 CPU 和 GPU 实现都通过它接入 renderer 和 benchmark
/// </summary>
class ITerrainLodAlgorithm
{
public:
    virtual ~ITerrainLodAlgorithm() = default;

    [[nodiscard]] virtual TerrainLodAlgorithmInfo Info() const = 0;
    [[nodiscard]] virtual TerrainLodAlgorithmCapabilities Capabilities() const = 0;

    /// <summary>
    /// 根据固定输入构建当前帧统一渲染包，失败时通过 errorMessage 暴露可诊断原因
    /// </summary>
    [[nodiscard]] virtual bool BuildRenderData(
        const TerrainLodBuildInput& input,
        TerrainLodRenderPacket& outPacket,
        std::string* errorMessage) = 0;

    [[nodiscard]] virtual const TerrainLodStats& Stats() const = 0;
    virtual void Reset() = 0;
};
} // 命名空间 ParallelRoam::Algorithms
