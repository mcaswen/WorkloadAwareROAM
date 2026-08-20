#pragma once

#include "algorithms/ITerrainLodAlgorithm.h"
#if defined(PARALLEL_ROAM_GRAPHICS_API_OPENGL)
#include "render/Shader.h"
#endif
#include "terrain/HeightMap.h"
#include "terrain/TerrainMeshBuilder.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace ParallelRoam::Render
{
class IGraphicsBackend;
#if defined(PARALLEL_ROAM_GRAPHICS_API_D3D12)
struct D3D12TerrainRendererState;
#endif

/// <summary>
/// terrain shader 的调试着色模式
/// </summary>
enum class TerrainDebugColorMode
{
    Lit = 0,
    LodState = 1,
};

/// <summary>
/// 单帧渲染上下文，承载相机矩阵、投影深度约定和当前 drawable 尺寸
/// </summary>
struct RenderContext
{
    glm::mat4 View{1.0F};
    glm::mat4 Projection{1.0F};
    glm::vec3 CameraPosition{0.0F};
    glm::vec3 CameraForward{0.0F, 0.0F, -1.0F};
    int DrawableWidth{1};
    int DrawableHeight{1};
    // 投影矩阵必须与后端的 NDC 深度范围保持一致
    bool UsesZeroToOneDepth{false};
};

/// <summary>
/// terrain renderer 的可编辑运行参数，来自 GUI 面板并驱动 mesh 重建或 shader uniform 更新
/// </summary>
struct TerrainRenderSettings
{
    float TerrainSize{30.0F};
    float HeightScale{4.0F};
    bool Wireframe{false};
    TerrainDebugColorMode DebugColorMode{TerrainDebugColorMode::Lit};
    float DebugOverlayStrength{0.85F};
    bool UseTerrainLod{true};
    // UseTerrainLod 为 false 时算法 id 保留上次选择
    Algorithms::TerrainLodAlgorithmId TerrainLodAlgorithm{Algorithms::TerrainLodAlgorithmId::ClassicCpuRoam};
    int RoamMaxDepth{14};

    // Classic 和 DOD 共享像素误差阈值与活动 leaf 预算。
    float RoamScreenSpaceSplitThresholdPixels{4.0F};
    float RoamScreenSpaceMergeThresholdPixels{2.0F};
    std::size_t RoamTriangleBudget{20000U};
    // DOD 专属实验开关；其他算法忽略该字段。
    bool RoamEnableParallelSplit{true};

    // 局部约束只做 baseNeighbor forced split，不执行全局 repair
    bool RoamEnableLocalConstraints{true};

    // 拓扑验证会触发全局扫描，只用于 debug
    bool RoamEnableTopologyValidation{false};

    // 光照参数只影响表现，不触发 terrain mesh 重建
    glm::vec3 LightDirection{-0.45F, -1.0F, -0.35F};
    glm::vec3 LightColor{1.0F, 0.96F, 0.88F};
    float AmbientStrength{0.28F};
    float DiffuseStrength{0.85F};
    float SpecularStrength{0.18F};
};

/// <summary>
/// terrain renderer 汇总给 GUI 的渲染规模、ROAM 拓扑和各 pass 耗时统计
/// </summary>
struct TerrainRenderStats
{
    // 输入资源和实际绘制规模用于保证 benchmark 样本可追溯
    std::filesystem::path HeightMapPath;
    int HeightMapWidth{0};
    int HeightMapHeight{0};
    std::size_t VertexCount{0};
    std::size_t TriangleCount{0};

    // 当前渲染器仍然保持单 draw call 提交 terrain
    int DrawCallCount{0};
    float TerrainSize{0.0F};
    float HeightScale{0.0F};
    bool UseTerrainLod{false};
    Algorithms::TerrainLodAlgorithmId TerrainLodAlgorithm{Algorithms::TerrainLodAlgorithmId::ClassicCpuRoam};
    std::string TerrainLodStatusMessage;

    // setting 字段来自 UI 快照，不与算法实际达到的状态混用
    int RoamMaxDepthSetting{0};
    float RoamScreenSpaceSplitThresholdPixels{0.0F};
    float RoamScreenSpaceMergeThresholdPixels{0.0F};
    std::size_t RoamTriangleBudgetSetting{0U};
    bool RoamParallelSplitEnabled{true};
    std::size_t RoamNodeCount{0};

    // 三类活动叶用于区分保留、细分和本帧重建的几何来源
    std::size_t RoamOriginalTriangleCount{0};
    std::size_t RoamSubdividedTriangleCount{0};
    std::size_t RoamRebuiltTriangleCount{0};

    // 提交计数区分误差驱动和兼容链驱动，用于分析拓扑维护成本
    std::size_t RoamActiveSplitCount{0};
    std::size_t RoamSplitCount{0};
    std::size_t RoamForcedSplitCount{0};
    std::size_t RoamMergeCount{0};
    std::size_t RoamCrackRiskCount{0};
    std::size_t RoamConstraintPassCount{0};
    std::size_t RoamCandidatePeakCount{0};
    std::size_t RoamPersistentSplitQueueSize{0};
    std::size_t RoamPersistentMergeQueueSize{0};
    std::size_t RoamQueueCrossoverCount{0};
    std::size_t RoamQueueMembershipUpdateCount{0};
    std::size_t RoamCpuMeshFullRebuildCount{0};
    std::size_t RoamCpuMeshUpdatedTriangleCount{0};
    std::size_t RoamCpuMeshReusedTriangleCount{0};
    std::size_t RoamCpuMeshDirtyRangeCount{0};
    std::size_t RoamRejectedSplitCount{0};
    std::size_t RoamBudgetRejectedSplitCount{0};
    std::size_t RoamRejectedMergeCount{0};

    // 下面三项只在可选全局验证开启时更新
    std::size_t RoamTjunctionCount{0};
    std::size_t RoamInvalidNeighborCount{0};
    std::size_t RoamInvalidTopologyCount{0};

    std::size_t RoamCpuWorkerCount{0};
    float RoamCpuUtilizationPercent{0.0F};

    // 时间桶覆盖算法、同步和渲染边界，不能相加后当作独立阶段总和
    float RoamTotalMilliseconds{0.0F};
    float RoamUpdateMilliseconds{0.0F};
    float RoamCpuPrepareMilliseconds{0.0F};
    float RoamCpuMergeCandidateMarkMilliseconds{0.0F};
    float RoamCpuMergeTopologyMilliseconds{0.0F};
    float RoamCpuSplitTopologyChunkBuildMilliseconds{0.0F};
    float RoamCpuSplitTopologyQueueInvalidationMilliseconds{0.0F};
    float RoamCpuSplitTopologyParallelCommitMilliseconds{0.0F};
    float RoamCpuSplitTopologyResultMergeMilliseconds{0.0F};
    float RoamCpuSplitTopologyIndexQueueRefreshMilliseconds{0.0F};
    float RoamCpuSplitTopologySerialConvergenceMilliseconds{0.0F};
    float RoamCpuMergeTopologyChunkBuildMilliseconds{0.0F};
    float RoamCpuMergeTopologyQueueInvalidationMilliseconds{0.0F};
    float RoamCpuMergeTopologyParallelCommitMilliseconds{0.0F};
    float RoamCpuMergeTopologyResultMergeMilliseconds{0.0F};
    float RoamCpuMergeTopologyIndexQueueRefreshMilliseconds{0.0F};
    float RoamCpuMergeTopologySerialConvergenceMilliseconds{0.0F};
    float RoamCpuBudgetLeafCollectMilliseconds{0.0F};
    float RoamCpuErrorEvalMilliseconds{0.0F};
    float RoamCpuSplitCandidateMarkMilliseconds{0.0F};
    float RoamCpuSplitTopologyMilliseconds{0.0F};
    float RoamCpuFinalLeafCollectMilliseconds{0.0F};
    float RoamCpuMeshEmitMilliseconds{0.0F};
    float RoamCpuFinalizeMilliseconds{0.0F};
    float RoamCpuUploadMilliseconds{0.0F};
    float RoamSplitMilliseconds{0.0F};
    float RoamMergeMilliseconds{0.0F};
    float RoamEmitMilliseconds{0.0F};
    float RoamValidateMilliseconds{0.0F};
    float RoamFrameFenceWaitMilliseconds{0.0F};
    float RoamRenderMilliseconds{0.0F};
    std::size_t RoamCpuGpuUploadBytes{0};
    std::size_t RoamCpuGpuReadbackBytes{0};

    int RoamMaxDepthReached{0};
};

/// <summary>
/// 统一调度规则网格、CPU LOD 和 GPU LOD 的资源更新与绘制
/// </summary>
class TerrainRenderer
{
public:
    TerrainRenderer();
    ~TerrainRenderer();

    TerrainRenderer(const TerrainRenderer&) = delete;
    TerrainRenderer& operator=(const TerrainRenderer&) = delete;

    bool Initialize(
        IGraphicsBackend& graphicsBackend,
        const std::filesystem::path& heightMapPath,
        const std::filesystem::path& texturePath,
        const TerrainRenderSettings& settings,
        std::string* errorMessage);

    bool ApplySettings(const TerrainRenderSettings& settings, std::string* errorMessage);
    bool LoadHeightMap(const std::filesystem::path& heightMapPath, std::string* errorMessage);
    bool UpdateForView(const RenderContext& context, std::string* errorMessage);

    // benchmark 可绕过普通相机位移缓存，要求下一帧重新构建 mesh
    void RequestMeshRebuild();

    // 切换 benchmark 算法时清掉持久 ROAM 拓扑和上一帧统计
    void ResetTerrainLodAlgorithm();
    void Shutdown();
    void Render(const RenderContext& context);

    [[nodiscard]] TerrainRenderStats Stats() const;
    [[nodiscard]] const std::filesystem::path& HeightMapPath() const;
    [[nodiscard]] const std::filesystem::path& TexturePath() const;

private:
    bool RebuildMesh(std::string* errorMessage);

    // baseline 路径，便于和 ROAM 视觉对照
    bool RebuildRegularGrid(std::string* errorMessage);

    // Terrain LOD 路径会随相机位置动态更新
    bool RebuildTerrainLod(const RenderContext& context, std::string* errorMessage);
    bool UploadMesh(std::string* errorMessage);
    bool UploadMeshData(
        const Terrain::TerrainMeshData& meshData,
        bool fullUpload,
        const std::vector<Algorithms::TerrainLodCpuMeshUpdateRange>& updateRanges,
        std::string* errorMessage);
#if defined(PARALLEL_ROAM_GRAPHICS_API_OPENGL)
    bool ConfigureTerrainVertexArray(
        unsigned int vertexBufferId,
        unsigned int indexBufferId,
        std::string* errorMessage);
#endif
    bool LoadTexture(const std::filesystem::path& texturePath, std::string* errorMessage);
    [[nodiscard]] bool HasDrawableTerrain() const;

#if defined(PARALLEL_ROAM_GRAPHICS_API_OPENGL)
    Shader _shader;
#elif defined(PARALLEL_ROAM_GRAPHICS_API_D3D12)
    std::unique_ptr<D3D12TerrainRendererState> _d3d12State;
#endif
    // 后端只借用，LOD 算法实例和 CPU 数据由 renderer 持有
    IGraphicsBackend* _graphicsBackend{nullptr};
    Terrain::HeightMap _heightMap;
    Terrain::TerrainMeshData _meshData;
    // Classic/DOD incremental emit 的 mesh 由算法持有，生命周期到下一次 Build/Reset。
    const Terrain::TerrainMeshData* _borrowedCpuMeshData{nullptr};
    std::unique_ptr<Algorithms::ITerrainLodAlgorithm> _terrainLodAlgorithm;
    Algorithms::TerrainLodStats _terrainLodStats;
    std::string _terrainLodStatusMessage;
    float _terrainLodTotalMilliseconds{0.0F};
    float _terrainLodCpuUploadMilliseconds{0.0F};
    TerrainRenderSettings _settings;
    std::filesystem::path _heightMapPath;
    std::filesystem::path _texturePath;
    RenderContext _lastRenderContext{};
    RenderContext _lastRoamBuildContext{};
#if defined(PARALLEL_ROAM_GRAPHICS_API_OPENGL)
    // CPU mesh 缓冲由 renderer 拥有。
    unsigned int _vertexArrayId{0};
    unsigned int _vertexBufferId{0};
    unsigned int _indexBufferId{0};
    std::size_t _vertexBufferCapacityBytes{0};
    std::size_t _indexBufferCapacityBytes{0};
    unsigned int _textureId{0};
#endif
    Algorithms::TerrainLodRenderMode _renderMode{Algorithms::TerrainLodRenderMode::CpuMesh};
    std::size_t _drawVertexCount{0};
    std::size_t _drawIndexCount{0};
    std::size_t _drawTriangleCount{0};
    bool _initialized{false};
    bool _meshDirty{true};
    bool _hasRoamBuildView{false};
};
} // namespace ParallelRoam::Render
