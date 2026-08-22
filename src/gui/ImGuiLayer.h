#pragma once

#include "algorithms/ITerrainLodAlgorithm.h"

#include <SDL.h>
#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <string>

#if defined(PARALLEL_ROAM_GRAPHICS_API_D3D12)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <d3d12.h>
#include <dxgiformat.h>
#endif

namespace ParallelRoam::Gui
{
/// <summary>
/// ImGui OpenGL 后端初始化参数
/// </summary>
struct ImGuiOpenGlBackendConfig
{
    SDL_Window* Window{nullptr};
    SDL_GLContext Context{nullptr};
    const char* GlslVersion{nullptr};
};

#if defined(PARALLEL_ROAM_GRAPHICS_API_D3D12)
/// <summary>
/// ImGui D3D12 后端初始化参数
/// </summary>
struct ImGuiD3D12BackendConfig
{
    SDL_Window* Window{nullptr};
    ID3D12Device* Device{nullptr};
    ID3D12CommandQueue* CommandQueue{nullptr};
    ID3D12DescriptorHeap* SrvDescriptorHeap{nullptr};
    // 字体描述符由 D3D12GraphicsBackend 预留并保持稳定
    D3D12_CPU_DESCRIPTOR_HANDLE FontSrvCpuDescriptor{};
    D3D12_GPU_DESCRIPTOR_HANDLE FontSrvGpuDescriptor{};
    DXGI_FORMAT RenderTargetFormat{DXGI_FORMAT_R8G8B8A8_UNORM};
    DXGI_FORMAT DepthStencilFormat{DXGI_FORMAT_D32_FLOAT};
    int FramesInFlight{2};
};
#endif

/// <summary>
/// 当前 ImGui context 使用的渲染后端
/// </summary>
enum class ImGuiRenderBackend
{
    None,
    OpenGl,
    Direct3D12,
};

/// <summary>
/// 调试面板需要展示的单帧状态
/// </summary>
struct DebugOverlayData
{
    float FramesPerSecond{0.0F};
    float FrameTimeMilliseconds{0.0F};
    int WindowWidth{0};
    int WindowHeight{0};
    int DrawableWidth{0};
    int DrawableHeight{0};

    // VSync 状态属于运行时展示指标，帮助解释 Frame ms 是否被刷新率钳住
    bool VSyncEnabled{false};
    glm::vec3 CameraPosition{0.0F};
    float CameraYawDegrees{0.0F};
    float CameraPitchDegrees{0.0F};
    int HeightMapWidth{0};
    int HeightMapHeight{0};
    std::size_t VertexCount{0};
    std::size_t TriangleCount{0};
    int DrawCallCount{0};
    bool UseTerrainLod{false};
    Algorithms::TerrainLodAlgorithmId TerrainLodAlgorithm{Algorithms::TerrainLodAlgorithmId::ClassicCpuRoam};
    std::string TerrainLodStatusMessage;
    std::size_t RoamNodeCount{0};
    std::size_t RoamOriginalTriangleCount{0};
    std::size_t RoamSubdividedTriangleCount{0};
    std::size_t RoamRebuiltTriangleCount{0};

    // 下列统计直接来自统一 TerrainLodStats
    std::size_t RoamActiveSplitCount{0};
    std::size_t RoamSplitCount{0};
    std::size_t RoamForcedSplitCount{0};
    std::size_t RoamMergeCount{0};

    // 裂缝风险用于判断当前最大深度是否过低
    std::size_t RoamCrackRiskCount{0};

    // 约束传播次数越高表示 diamond split 触发越明显
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

    // 拓扑验证关闭时这两个值保持为 0
    std::size_t RoamTjunctionCount{0};
    std::size_t RoamInvalidNeighborCount{0};
    std::size_t RoamInvalidTopologyCount{0};

    // CPU 诊断字段来自统一算法 stats
    std::size_t RoamCpuWorkerCount{0};
    float RoamCpuUtilizationPercent{0.0F};

    // UI 直接展示算法层统计的各 pass 耗时
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
    int RoamMaxDepthSetting{0};
    int RoamMaxDepthReached{0};

    // 运行时 benchmark 状态用于绘制顶部提示和输出路径
    bool BenchmarkRunning{false};
    std::string BenchmarkAlgorithmName;
    float BenchmarkProgress{0.0F};
    std::string LastBenchmarkOutputPath;
};

/// <summary>
/// GUI 可编辑的 terrain 渲染参数
/// </summary>
struct TerrainPanelState
{
    enum class RuntimeBenchmarkPath
    {
        Default,
        BudgetSaturation,
    };

    float TerrainSize{30.0F};
    float HeightScale{4.0F};
    bool Wireframe{false};

    // VSync 只影响 swap interval，不进入 TerrainRenderSettings
    bool VSyncEnabled{false};
    int HeightMapIndex{0};
    int DebugColorMode{0};
    float DebugOverlayStrength{0.85F};
    bool UseTerrainLod{true};
    Algorithms::TerrainLodAlgorithmId TerrainLodAlgorithm{Algorithms::TerrainLodAlgorithmId::ClassicCpuRoam};
    int RoamMaxDepth{14};

    // 两种 ROAM 使用同一组像素双阈值减少相机移动时的抖动。
    float RoamScreenSpaceSplitThresholdPixels{4.0F};
    float RoamScreenSpaceMergeThresholdPixels{2.0F};
    int RoamTriangleBudget{20000};

    // 只控制 DOD 的 chunk Split 拓扑提交，不关闭并行误差评分。
    bool RoamEnableParallelSplit{false};
    Algorithms::TerrainLodPassPolicy RoamPassPolicy{};

    // 局部约束是 ROAM 消除裂缝的默认路径
    bool RoamEnableLocalConstraints{true};

    // 拓扑验证会全局扫描 active leaf，只在 debug 时打开
    bool RoamEnableTopologyValidation{false};

    // 光照字段保持在面板状态中，便于和 terrain 参数一起提交
    glm::vec3 LightDirection{-0.45F, -1.0F, -0.35F};
    glm::vec3 LightColor{1.0F, 0.96F, 0.88F};
    float AmbientStrength{0.28F};
    float DiffuseStrength{0.85F};
    float SpecularStrength{0.18F};

    // UI 按钮只提交请求，Application 在下一帧开始可控相机路径
    bool StartBenchmarkRequested{false};
    RuntimeBenchmarkPath BenchmarkPath{RuntimeBenchmarkPath::Default};
};

/// <summary>
/// 面向运行时诊断的 Dear ImGui 集成层
/// </summary>
class ImGuiLayer
{
public:
    bool Initialize(const ImGuiOpenGlBackendConfig& config);
#if defined(PARALLEL_ROAM_GRAPHICS_API_D3D12)
    bool Initialize(const ImGuiD3D12BackendConfig& config);
#endif

    void Shutdown();
    void ProcessEvent(const SDL_Event& event);
    void BeginFrame();

    /// <summary>
    /// 返回 true 表示 terrain 面板参数发生变化，调用方需要重新应用 renderer 设置
    /// </summary>
    [[nodiscard]] bool DrawDebugOverlay(const DebugOverlayData& data, TerrainPanelState& terrainState);

    void EndFrame(void* nativeCommandList = nullptr);

private:
    // 防止未初始化或重复 Shutdown 时调用 backend 清理接口
    bool _initialized{false};
    // Shutdown 和 EndFrame 依赖此值选择匹配的后端接口
    ImGuiRenderBackend _renderBackend{ImGuiRenderBackend::None};

    // 性能 overlay 的详细模式属于 GUI 展示偏好，不触发 renderer 设置更新
    bool _performanceOverlayDetailed{false};
#if defined(PARALLEL_ROAM_GRAPHICS_API_D3D12)
    // 字体描述符由图形后端预留，ImGui 回调只借用固定句柄
    D3D12_CPU_DESCRIPTOR_HANDLE _fontSrvCpuDescriptor{};
    D3D12_GPU_DESCRIPTOR_HANDLE _fontSrvGpuDescriptor{};
    // 记录 ImGui 是否已经接管固定字体槽位
    bool _fontDescriptorInUse{false};
#endif
};
} // 命名空间 ParallelRoam::Gui
