#include "app/Application.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>

namespace ParallelRoam::App
{
namespace
{
const std::array<std::filesystem::path, 2> HeightMapPaths{
    // 资源表顺序必须和 ImGui 高度图下拉框保持一致
    std::filesystem::path{"assets/heightmaps/Hm_Terrain_Test_129.pgm"},
    // Peking 513 用于更大输入规模下观察 ROAM 行为
    std::filesystem::path{"assets/heightmaps/Hm_Terrain_Peking_513.png"},
};

float SmoothStep(float value)
{
    // SmoothStep 让相机起停速度连续，避免 benchmark 首尾突变
    const float t = std::clamp(value, 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
}

std::string BuildConfigurationName()
{
#if defined(PARALLEL_ROAM_BUILD_CONFIG)
    return PARALLEL_ROAM_BUILD_CONFIG;
#else
    return "Unknown";
#endif
}

std::string PassPolicyDisplayName(const Algorithms::TerrainLodPassPolicy& policy)
{
    if (policy == Algorithms::TerrainLodPassPolicy{})
    {
        return "默认自动策略";
    }
    if (policy == Algorithms::MakeTerrainLodSerialIncrementalPolicy())
    {
        return "固定串行 + 增量输出";
    }
    if (policy == Algorithms::MakeTerrainLodMaximumSafeParallelIncrementalPolicy())
    {
        return "最大安全并行 + 增量输出";
    }
    if (policy == Algorithms::MakeTerrainLodSerialFullOutputPolicy())
    {
        return "固定串行 + 全量输出";
    }
    if (policy == Algorithms::MakeTerrainLodMaximumSafeParallelFullOutputPolicy())
    {
        return "最大安全并行 + 全量输出";
    }
    return "自定义阶段策略";
}

std::pair<float, float> ComputeYawPitchForLookAt(const glm::vec3& position, const glm::vec3& target)
{
    // 目标点与相机重合时返回默认姿态，避免 atan2/asin 输入退化
    const glm::vec3 direction = target - position;
    const float lengthSquared = glm::dot(direction, direction);
    if (lengthSquared <= 0.000001F)
    {
        return {-90.0F, -18.0F};
    }

    const glm::vec3 normalizedDirection = glm::normalize(direction);
    // CameraController 的 yaw 约定是 -90 度看向 -Z
    const float yawDegrees = glm::degrees(std::atan2(normalizedDirection.z, normalizedDirection.x));
    const float pitchDegrees = glm::degrees(std::asin(std::clamp(normalizedDirection.y, -1.0F, 1.0F)));
    return {yawDegrees, pitchDegrees};
}

struct BudgetSaturationCameraPose
{
    glm::vec3 Position{0.0F};
    glm::vec3 Target{0.0F};
};

BudgetSaturationCameraPose ComputeBudgetSaturationCameraPose(float normalizedTime)
{
    const float t = std::clamp(normalizedTime, 0.0F, 1.0F);
    const float angle = t * 6.28318530718F;
    return BudgetSaturationCameraPose{
        glm::vec3{
            std::cos(angle) * 58.0F,
            20.0F + std::sin(angle * 2.0F) * 3.0F,
            std::sin(angle) * 58.0F,
        },
        glm::vec3{
            std::cos(angle + 0.55F) * 10.0F,
            4.0F,
            std::sin(angle + 0.55F) * 10.0F,
        }};
}

// benchmark 路径是有限的相机姿态序列；压力路径的单点成本更高，因此使用较少采样点
constexpr std::size_t DefaultRuntimeBenchmarkSampleCount = 600;
constexpr std::size_t BudgetSaturationRuntimeBenchmarkSampleCount = 64;
constexpr std::size_t DefaultRuntimeBenchmarkWarmupSampleCount = 32;
constexpr std::size_t BudgetSaturationRuntimeBenchmarkWarmupSampleCount = 4;

Algorithms::TerrainLodSettings ToTerrainLodSettings(const Gui::TerrainPanelState& state)
{
    Algorithms::TerrainLodSettings settings{};
    settings.TerrainSize = state.TerrainSize;
    settings.HeightScale = state.HeightScale;
    settings.MaxDepth = state.RoamMaxDepth;
    settings.ScreenSpaceSplitThresholdPixels = state.RoamScreenSpaceSplitThresholdPixels;
    settings.ScreenSpaceMergeThresholdPixels = state.RoamScreenSpaceMergeThresholdPixels;
    settings.TriangleBudget = static_cast<std::size_t>(std::max(state.RoamTriangleBudget, 2));
    settings.EnableParallelSplit = state.RoamEnableParallelSplit;
    settings.PassPolicy = state.RoamPassPolicy;
    settings.EnableLocalConstraints = state.RoamEnableLocalConstraints;
    settings.EnableTopologyValidation = state.RoamEnableTopologyValidation;
    settings.EnablePassEvidence = true;
    return settings;
}

Render::TerrainRenderSettings ToRenderSettings(const Gui::TerrainPanelState& state)
{
    Render::TerrainRenderSettings settings{};
    settings.TerrainSize = state.TerrainSize;
    settings.HeightScale = state.HeightScale;
    settings.Wireframe = state.Wireframe;
    settings.DebugColorMode = static_cast<Render::TerrainDebugColorMode>(std::clamp(state.DebugColorMode, 0, 1));
    settings.DebugOverlayStrength = std::clamp(state.DebugOverlayStrength, 0.0F, 1.0F);
    settings.UseTerrainLod = state.UseTerrainLod;
    settings.TerrainLodAlgorithm = state.TerrainLodAlgorithm;
    settings.RoamMaxDepth = state.RoamMaxDepth;
    settings.RoamScreenSpaceSplitThresholdPixels = state.RoamScreenSpaceSplitThresholdPixels;
    settings.RoamScreenSpaceMergeThresholdPixels = state.RoamScreenSpaceMergeThresholdPixels;
    settings.RoamTriangleBudget = static_cast<std::size_t>(std::max(state.RoamTriangleBudget, 2));
    settings.RoamEnableParallelSplit = state.RoamEnableParallelSplit;
    settings.RoamPassPolicy = state.RoamPassPolicy;
    settings.RoamEnableLocalConstraints = state.RoamEnableLocalConstraints;
    settings.RoamEnableTopologyValidation = state.RoamEnableTopologyValidation;
    settings.LightDirection = state.LightDirection;
    settings.LightColor = state.LightColor;
    settings.AmbientStrength = state.AmbientStrength;
    settings.DiffuseStrength = state.DiffuseStrength;
    settings.SpecularStrength = state.SpecularStrength;
    return settings;
}
} // 匿名命名空间

Application::~Application()
{
    Shutdown();
}

void Application::EnableAutomaticRuntimeBenchmark()
{
    _automaticRuntimeBenchmarkEnabled = true;
}

void Application::ConfigureRuntimeBenchmark(const RuntimeBenchmarkOverrides& overrides)
{
    _runtimeBenchmarkOverrides = overrides;
    _hasRuntimeBenchmarkOverrides = true;
    ApplyPendingRuntimeBenchmarkOverrides();
}

bool Application::Initialize()
{
    // 图形后端由构建配置唯一选择并先于窗口属性配置创建
    _graphicsBackend = Render::CreateConfiguredGraphicsBackend();
    if (_graphicsBackend == nullptr)
    {
        std::cerr << "Configured graphics backend is not implemented: "
                  << Render::ConfiguredGraphicsApiName() << '\n';
        return false;
    }

    // SDL 必须先初始化，后端才能设置窗口属性或查询平台句柄
    if (!_window.Initialize())
    {
        return false;
    }

    // ConfigureWindow 位于 SDL 初始化和窗口创建之间
    std::string graphicsError;
    if (!_graphicsBackend->ConfigureWindow(&graphicsError))
    {
        std::cerr << graphicsError << '\n';
        Shutdown();
        return false;
    }

    // 后端决定窗口是否携带 OpenGL 标志或只作为原生交换链目标
    if (!_window.Create(
            "Parallel ROAM",
            1280,
            720,
            _graphicsBackend->RequiredSdlWindowFlags()))
    {
        Shutdown();
        return false;
    }

    // 后端只借用窗口，Shutdown 顺序必须早于 Window::Destroy
    if (!_graphicsBackend->Initialize(_window.NativeWindow(), &graphicsError))
    {
        std::cerr << graphicsError << '\n';
        Shutdown();
        return false;
    }

    // 输入使用逻辑窗口尺寸，渲染尺寸由后端单独维护
    _input.SetWindowSize(_window.Width(), _window.Height());
    _terrainPanelState.VSyncEnabled = _graphicsBackend->VSyncEnabled();
    _terrainPanelState.HeightMapIndex = 0;
    ApplyPendingRuntimeBenchmarkOverrides();

    _terrainSettings = ToRenderSettings(_terrainPanelState);

    // 渲染器加载 Height Map、地表纹理和内置 shader
    std::string rendererError;
    if (!_terrainRenderer.Initialize(
            *_graphicsBackend,
            HeightMapPaths[static_cast<std::size_t>(_terrainPanelState.HeightMapIndex)],
            std::filesystem::path{"assets/textures/Tex_Terrain_Debug_Diffuse.ppm"},
            _terrainSettings,
            &rendererError))
    {
        std::cerr << rendererError << '\n';
        Shutdown();
        return false;
    }

    // ImGui 依赖已建立的图形设备和 renderer 使用后的稳定资源布局
    if (!_graphicsBackend->InitializeImGui(_guiLayer, &graphicsError))
    {
        std::cerr << graphicsError << '\n';
        Shutdown();
        return false;
    }

    // 初始化完成后重置时钟，避免资源加载耗时进入首帧 delta
    _frameTimer.Restart();
    _initialized = true;
    return true;
}

int Application::Run(int maxFrameCount)
{
    if (!_initialized)
    {
        return 1;
    }

    int frameCount = 0;
    if (_automaticRuntimeBenchmarkEnabled)
    {
        StartRuntimeBenchmark();
    }

    while (!_input.IsQuitRequested())
    {
        const FrameTiming frameTiming = ComputeFrameTiming();

        // 鼠标位移是逐帧增量，必须在轮询事件前清零
        _input.BeginFrame();
        PollEvents();

        if (_input.IsKeyDown(SDL_SCANCODE_ESCAPE))
        {
            break;
        }

        if (_runtimeBenchmark.StartRequested && !_runtimeBenchmark.Active)
        {
            // UI 事件在 RenderFrame 里产生，下一轮主循环再启动测试更稳定
            _runtimeBenchmark.StartRequested = false;
            StartRuntimeBenchmark();
        }

        // benchmark 接管相机时不捕获鼠标，避免测试中途被用户输入污染
        _window.SetRelativeMouseMode(!_runtimeBenchmark.Active && _input.IsRightMouseDown());
        if (_runtimeBenchmark.Active)
        {
            PrepareRuntimeBenchmarkFrame(frameTiming);
        }
        else
        {
            _camera.Update(_input, frameTiming.ClampedDeltaSeconds);
        }

        // RenderFrame 记录场景和 GUI，Present 统一关闭并提交后端帧
        RenderFrame(frameTiming);
        _graphicsBackend->Present();
        CompleteRuntimeBenchmarkFrame();
        if (_automaticRuntimeBenchmarkEnabled && _automaticRuntimeBenchmarkCompleted)
        {
            break;
        }

        // smoke test 通过固定帧数退出，便于自动验证窗口和图形后端
        ++frameCount;
        if (maxFrameCount > 0 && frameCount >= maxFrameCount)
        {
            break;
        }
    }

    const bool automaticBenchmarkIncomplete =
        _automaticRuntimeBenchmarkEnabled && !_automaticRuntimeBenchmarkCompleted;
    const int exitCode =
        (_terrainLodSmokeTestFailed || _automaticRuntimeBenchmarkFailed || automaticBenchmarkIncomplete) ? 1 : 0;
    Shutdown();
    return exitCode;
}

void Application::Shutdown()
{
    // 所有子系统的 Shutdown 都允许重复调用
    // SDL 可能已初始化但窗口创建失败，因此不能按窗口状态提前返回
    if (_graphicsBackend != nullptr && _graphicsBackend->IsValid())
    {
        // GUI 和 terrain renderer 持有 GPU 资源，销毁前必须完成所有在途帧
        _graphicsBackend->WaitForGpuIdle();
    }
    // 依赖顺序为 GUI renderer 图形后端 SDL 窗口
    _guiLayer.Shutdown();
    _terrainRenderer.Shutdown();
    if (_graphicsBackend != nullptr)
    {
        _graphicsBackend->Shutdown();
        _graphicsBackend.reset();
    }
    _window.Destroy();
    _initialized = false;
}

Application::FrameTiming Application::ComputeFrameTiming()
{
    // 调试断点或窗口拖拽会造成异常大 delta，需要限制相机单帧位移
    constexpr float MaxDeltaSeconds = 0.1F;
    const float rawDeltaSeconds = std::max(_frameTimer.Restart() * 0.001F, 0.0F);

    FrameTiming frameTiming{};
    frameTiming.RawDeltaSeconds = rawDeltaSeconds;
    frameTiming.ClampedDeltaSeconds = std::min(rawDeltaSeconds, MaxDeltaSeconds);
    return frameTiming;
}

void Application::PollEvents()
{
    SDL_Event event{};
    while (SDL_PollEvent(&event) != 0)
    {
        // 三个层级消费同一事件，后续可在这里接入事件总线
        _guiLayer.ProcessEvent(event);
        _input.HandleEvent(event);
        _window.ProcessEvent(event);
    }
}

void Application::RenderFrame(const FrameTiming& frameTiming)
{
    _window.RefreshSize();
    _graphicsBackend->RefreshDrawableSize();

    // HiDPI 下 drawable 尺寸可能大于窗口逻辑尺寸，viewport 必须使用 drawable
    const int drawableWidth = std::max(_graphicsBackend->DrawableWidth(), 1);
    const int drawableHeight = std::max(_graphicsBackend->DrawableHeight(), 1);
    const float aspectRatio = static_cast<float>(drawableWidth) / static_cast<float>(drawableHeight);

    // FPS 和帧时间必须使用 raw delta，不能受相机 delta clamp 影响
    if (frameTiming.RawDeltaSeconds > 0.0F)
    {
        _framesPerSecond = 1.0F / frameTiming.RawDeltaSeconds;
        _frameTimeMilliseconds = frameTiming.RawDeltaSeconds * 1000.0F;
    }

    _graphicsBackend->BeginFrame();

    _graphicsBackend->BeginImGuiFrame(_guiLayer);

    const glm::vec3 cameraPosition = _camera.Position();
    Render::RenderContext renderContext{};
    renderContext.View = _camera.GetViewMatrix();
    renderContext.Projection = _graphicsBackend->UsesZeroToOneDepth()
        ? glm::perspectiveRH_ZO(glm::radians(60.0F), aspectRatio, 0.05F, 1000.0F)
        : glm::perspectiveRH_NO(glm::radians(60.0F), aspectRatio, 0.05F, 1000.0F);
    renderContext.CameraPosition = cameraPosition;
    renderContext.CameraForward = _camera.Forward();
    renderContext.DrawableWidth = drawableWidth;
    renderContext.DrawableHeight = drawableHeight;
    renderContext.UsesZeroToOneDepth = _graphicsBackend->UsesZeroToOneDepth();
    if (_terrainLodSmokeTestEnabled)
    {
        _terrainRenderer.RequestMeshRebuild();
    }

    std::string meshUpdateError;
    if (!_terrainRenderer.UpdateForView(renderContext, &meshUpdateError))
    {
        if (_runtimeBenchmark.Active && !_runtimeBenchmark.Failed)
        {
            _runtimeBenchmark.Failed = true;
            _runtimeBenchmark.FailureMessage =
                meshUpdateError.empty() ? "Terrain rebuild failed during runtime benchmark" : meshUpdateError;
        }
        _terrainLodSmokeTestFailed = _terrainLodSmokeTestEnabled;
        if (meshUpdateError != _lastMeshUpdateError)
        {
            std::cerr << meshUpdateError << '\n';
            _lastMeshUpdateError = meshUpdateError;
        }
    }
    else
    {
        _lastMeshUpdateError.clear();
    }

    const Render::TerrainRenderStats terrainStats = _terrainRenderer.Stats();
    Gui::DebugOverlayData debugData{};
    debugData.FramesPerSecond = _framesPerSecond;
    debugData.FrameTimeMilliseconds = _frameTimeMilliseconds;
    debugData.WindowWidth = _window.Width();
    debugData.WindowHeight = _window.Height();
    debugData.DrawableWidth = drawableWidth;
    debugData.DrawableHeight = drawableHeight;
    debugData.VSyncEnabled = _terrainPanelState.VSyncEnabled;
    debugData.CameraPosition = cameraPosition;
    debugData.CameraYawDegrees = _camera.YawDegrees();
    debugData.CameraPitchDegrees = _camera.PitchDegrees();
    debugData.HeightMapWidth = terrainStats.HeightMapWidth;
    debugData.HeightMapHeight = terrainStats.HeightMapHeight;
    debugData.VertexCount = terrainStats.VertexCount;
    debugData.TriangleCount = terrainStats.TriangleCount;
    debugData.DrawCallCount = terrainStats.DrawCallCount;
    debugData.UseTerrainLod = terrainStats.UseTerrainLod;
    debugData.TerrainLodAlgorithm = terrainStats.TerrainLodAlgorithm;
    debugData.TerrainLodStatusMessage = terrainStats.TerrainLodStatusMessage;
    debugData.RoamNodeCount = terrainStats.RoamNodeCount;
    debugData.RoamOriginalTriangleCount = terrainStats.RoamOriginalTriangleCount;
    debugData.RoamSubdividedTriangleCount = terrainStats.RoamSubdividedTriangleCount;
    debugData.RoamRebuiltTriangleCount = terrainStats.RoamRebuiltTriangleCount;
    debugData.RoamActiveSplitCount = terrainStats.RoamActiveSplitCount;
    debugData.RoamSplitCount = terrainStats.RoamSplitCount;
    debugData.RoamForcedSplitCount = terrainStats.RoamForcedSplitCount;
    debugData.RoamMergeCount = terrainStats.RoamMergeCount;
    debugData.RoamCrackRiskCount = terrainStats.RoamCrackRiskCount;
    debugData.RoamConstraintPassCount = terrainStats.RoamConstraintPassCount;
    debugData.RoamCandidatePeakCount = terrainStats.RoamCandidatePeakCount;
    debugData.RoamPersistentSplitQueueSize = terrainStats.RoamPersistentSplitQueueSize;
    debugData.RoamPersistentMergeQueueSize = terrainStats.RoamPersistentMergeQueueSize;
    debugData.RoamQueueCrossoverCount = terrainStats.RoamQueueCrossoverCount;
    debugData.RoamQueueMembershipUpdateCount = terrainStats.RoamQueueMembershipUpdateCount;
    debugData.RoamCpuMeshFullRebuildCount = terrainStats.RoamCpuMeshFullRebuildCount;
    debugData.RoamCpuMeshUpdatedTriangleCount = terrainStats.RoamCpuMeshUpdatedTriangleCount;
    debugData.RoamCpuMeshReusedTriangleCount = terrainStats.RoamCpuMeshReusedTriangleCount;
    debugData.RoamCpuMeshDirtyRangeCount = terrainStats.RoamCpuMeshDirtyRangeCount;
    debugData.RoamRejectedSplitCount = terrainStats.RoamRejectedSplitCount;
    debugData.RoamBudgetRejectedSplitCount = terrainStats.RoamBudgetRejectedSplitCount;
    debugData.RoamRejectedMergeCount = terrainStats.RoamRejectedMergeCount;
    debugData.RoamTjunctionCount = terrainStats.RoamTjunctionCount;
    debugData.RoamInvalidNeighborCount = terrainStats.RoamInvalidNeighborCount;
    debugData.RoamInvalidTopologyCount = terrainStats.RoamInvalidTopologyCount;
    debugData.RoamCpuWorkerCount = terrainStats.RoamCpuWorkerCount;
    debugData.RoamCpuUtilizationPercent = terrainStats.RoamCpuUtilizationPercent;
    debugData.RoamTotalMilliseconds = terrainStats.RoamTotalMilliseconds;
    debugData.RoamUpdateMilliseconds = terrainStats.RoamUpdateMilliseconds;
    debugData.RoamCpuPrepareMilliseconds = terrainStats.RoamCpuPrepareMilliseconds;
    debugData.RoamCpuMergeCandidateMarkMilliseconds = terrainStats.RoamCpuMergeCandidateMarkMilliseconds;
    debugData.RoamCpuMergeTopologyMilliseconds = terrainStats.RoamCpuMergeTopologyMilliseconds;
    debugData.RoamCpuSplitTopologyChunkBuildMilliseconds =
        terrainStats.RoamCpuSplitTopologyChunkBuildMilliseconds;
    debugData.RoamCpuSplitTopologyQueueInvalidationMilliseconds =
        terrainStats.RoamCpuSplitTopologyQueueInvalidationMilliseconds;
    debugData.RoamCpuSplitTopologyParallelCommitMilliseconds =
        terrainStats.RoamCpuSplitTopologyParallelCommitMilliseconds;
    debugData.RoamCpuSplitTopologyResultMergeMilliseconds =
        terrainStats.RoamCpuSplitTopologyResultMergeMilliseconds;
    debugData.RoamCpuSplitTopologyIndexQueueRefreshMilliseconds =
        terrainStats.RoamCpuSplitTopologyIndexQueueRefreshMilliseconds;
    debugData.RoamCpuSplitTopologySerialConvergenceMilliseconds =
        terrainStats.RoamCpuSplitTopologySerialConvergenceMilliseconds;
    debugData.RoamCpuMergeTopologyChunkBuildMilliseconds =
        terrainStats.RoamCpuMergeTopologyChunkBuildMilliseconds;
    debugData.RoamCpuMergeTopologyQueueInvalidationMilliseconds =
        terrainStats.RoamCpuMergeTopologyQueueInvalidationMilliseconds;
    debugData.RoamCpuMergeTopologyParallelCommitMilliseconds =
        terrainStats.RoamCpuMergeTopologyParallelCommitMilliseconds;
    debugData.RoamCpuMergeTopologyResultMergeMilliseconds =
        terrainStats.RoamCpuMergeTopologyResultMergeMilliseconds;
    debugData.RoamCpuMergeTopologyIndexQueueRefreshMilliseconds =
        terrainStats.RoamCpuMergeTopologyIndexQueueRefreshMilliseconds;
    debugData.RoamCpuMergeTopologySerialConvergenceMilliseconds =
        terrainStats.RoamCpuMergeTopologySerialConvergenceMilliseconds;
    debugData.RoamCpuBudgetLeafCollectMilliseconds = terrainStats.RoamCpuBudgetLeafCollectMilliseconds;
    debugData.RoamCpuErrorEvalMilliseconds = terrainStats.RoamCpuErrorEvalMilliseconds;
    debugData.RoamCpuSplitCandidateMarkMilliseconds = terrainStats.RoamCpuSplitCandidateMarkMilliseconds;
    debugData.RoamCpuSplitTopologyMilliseconds = terrainStats.RoamCpuSplitTopologyMilliseconds;
    debugData.RoamCpuFinalLeafCollectMilliseconds = terrainStats.RoamCpuFinalLeafCollectMilliseconds;
    debugData.RoamCpuMeshEmitMilliseconds = terrainStats.RoamCpuMeshEmitMilliseconds;
    debugData.RoamCpuFinalizeMilliseconds = terrainStats.RoamCpuFinalizeMilliseconds;
    debugData.RoamCpuUploadMilliseconds = terrainStats.RoamCpuUploadMilliseconds;
    debugData.RoamSplitMilliseconds = terrainStats.RoamSplitMilliseconds;
    debugData.RoamMergeMilliseconds = terrainStats.RoamMergeMilliseconds;
    debugData.RoamEmitMilliseconds = terrainStats.RoamEmitMilliseconds;
    debugData.RoamValidateMilliseconds = terrainStats.RoamValidateMilliseconds;
    debugData.RoamFrameFenceWaitMilliseconds = terrainStats.RoamFrameFenceWaitMilliseconds;
    debugData.RoamRenderMilliseconds = terrainStats.RoamRenderMilliseconds;
    debugData.RoamCpuGpuUploadBytes = terrainStats.RoamCpuGpuUploadBytes;
    debugData.RoamCpuGpuReadbackBytes = terrainStats.RoamCpuGpuReadbackBytes;
    debugData.RoamMaxDepthSetting = terrainStats.RoamMaxDepthSetting;
    debugData.RoamMaxDepthReached = terrainStats.RoamMaxDepthReached;
    // benchmark 状态走 DebugOverlayData，GUI 不直接读取 Application 成员
    debugData.BenchmarkRunning = _runtimeBenchmark.Active;
    debugData.BenchmarkAlgorithmName = CurrentRuntimeBenchmarkAlgorithmName();
    debugData.BenchmarkProgress = RuntimeBenchmarkProgress();
    if (!_runtimeBenchmark.LastMarkdownPath.empty())
    {
        debugData.LastBenchmarkOutputPath = _runtimeBenchmark.LastMarkdownPath.string();
    }

    RecordRuntimeBenchmarkSample(frameTiming, terrainStats, cameraPosition);

    const bool previousVSyncEnabled = _terrainPanelState.VSyncEnabled;
    const int previousHeightMapIndex = _terrainPanelState.HeightMapIndex;
    if (_guiLayer.DrawDebugOverlay(debugData, _terrainPanelState))
    {
        ApplyTerrainPanelSettings();
    }

    if (previousVSyncEnabled != _terrainPanelState.VSyncEnabled)
    {
        // VSync 改变只更新窗口 swap interval，不触发 mesh 或算法重建
        ApplyWindowPanelSettings();
    }

    if (previousHeightMapIndex != _terrainPanelState.HeightMapIndex)
    {
        ApplyHeightMapSelection();
    }

    if (_terrainPanelState.StartBenchmarkRequested)
    {
        // 按钮请求被消费后立刻清零，避免下一帧重复启动
        _runtimeBenchmark.StartRequested = true;
        _terrainPanelState.StartBenchmarkRequested = false;
    }

    // terrain renderer 消费相机矩阵和 UI 参数，不直接处理输入事件
    _terrainRenderer.Render(renderContext);
    _graphicsBackend->RenderImGui(_guiLayer);
}

void Application::ApplyTerrainPanelSettings()
{
    _terrainSettings = ToRenderSettings(_terrainPanelState);
    // 结果哈希和队列全量检查只在运行时基准期间开启
    _terrainSettings.RoamEnablePassEvidence = _runtimeBenchmark.Active;

    std::string settingsError;
    if (!_terrainRenderer.ApplySettings(_terrainSettings, &settingsError))
    {
        std::cerr << settingsError << '\n';
    }
}

void Application::ApplyWindowPanelSettings()
{
    if (!_graphicsBackend->SetVSyncEnabled(_terrainPanelState.VSyncEnabled))
    {
        _terrainPanelState.VSyncEnabled = _graphicsBackend->VSyncEnabled();
    }
}

void Application::ApplyPendingRuntimeBenchmarkOverrides()
{
    if (!_hasRuntimeBenchmarkOverrides)
    {
        return;
    }

    const RuntimeBenchmarkOverrides& overrides = _runtimeBenchmarkOverrides;
    if (overrides.HasPath)
    {
        _terrainPanelState.BenchmarkPath = overrides.Path;
    }

    if (overrides.HasPassPolicy)
    {
        _terrainPanelState.RoamPassPolicy = overrides.PassPolicy;
    }

    if (overrides.HasSplitTopologyMinParallelCandidateCount)
    {
        _terrainPanelState.RoamPassPolicy.SplitTopologyMinParallelCandidateCount =
            overrides.SplitTopologyMinParallelCandidateCount;
    }

    if (overrides.HasMergeTopologyMinParallelCandidateCount)
    {
        _terrainPanelState.RoamPassPolicy.MergeTopologyMinParallelCandidateCount =
            overrides.MergeTopologyMinParallelCandidateCount;
    }

    if (overrides.HasParallelTopologyTargetBuild)
    {
        _terrainPanelState.RoamPassPolicy.ParallelTopologyTargetBuild =
            overrides.ParallelTopologyTargetBuild;
    }

    if (overrides.HasParallelTopologyPhase)
    {
        _terrainPanelState.RoamPassPolicy.ParallelTopologyPhase = overrides.ParallelTopologyPhase;
    }

    if (overrides.HasHeightMapIndex)
    {
        _terrainPanelState.HeightMapIndex =
            std::clamp(overrides.HeightMapIndex, 0, static_cast<int>(HeightMapPaths.size()) - 1);
    }

    if (overrides.HasTerrainSize)
    {
        _terrainPanelState.TerrainSize = std::clamp(overrides.TerrainSize, 6.0F, 80.0F);
    }

    if (overrides.HasHeightScale)
    {
        _terrainPanelState.HeightScale = std::clamp(overrides.HeightScale, 0.0F, 12.0F);
    }

    if (overrides.HasMaxDepth)
    {
        _terrainPanelState.RoamMaxDepth = std::clamp(overrides.MaxDepth, 1, 20);
    }

    if (overrides.HasScreenSpaceSplitThresholdPixels)
    {
        _terrainPanelState.RoamScreenSpaceSplitThresholdPixels =
            std::clamp(overrides.ScreenSpaceSplitThresholdPixels, 0.25F, 32.0F);
    }

    if (overrides.HasScreenSpaceMergeThresholdPixels)
    {
        _terrainPanelState.RoamScreenSpaceMergeThresholdPixels =
            std::clamp(overrides.ScreenSpaceMergeThresholdPixels, 0.1F, 32.0F);
    }

    _terrainPanelState.RoamScreenSpaceMergeThresholdPixels = std::min(
        _terrainPanelState.RoamScreenSpaceMergeThresholdPixels,
        _terrainPanelState.RoamScreenSpaceSplitThresholdPixels);

    if (_initialized)
    {
        ApplyTerrainPanelSettings();
        ApplyHeightMapSelection();
    }
}

void Application::ApplyHeightMapSelection()
{
    // UI index 先钳制到资源表范围，防止未来增删选项时越界
    const int selectedIndex = std::clamp(
        _terrainPanelState.HeightMapIndex,
        0,
        static_cast<int>(HeightMapPaths.size()) - 1);
    _terrainPanelState.HeightMapIndex = selectedIndex;
    const std::filesystem::path& heightMapPath = HeightMapPaths[static_cast<std::size_t>(selectedIndex)];

    std::string heightMapError;
    if (!_terrainRenderer.LoadHeightMap(heightMapPath, &heightMapError))
    {
        std::cerr << heightMapError << '\n';
        // 加载失败时把 UI 回滚到 renderer 当前实际持有的高度图
        const auto currentPath = _terrainRenderer.HeightMapPath();
        const auto match = std::find_if(
            HeightMapPaths.begin(),
            HeightMapPaths.end(),
            [&currentPath](const std::filesystem::path& optionPath) {
                return optionPath == currentPath;
            });
        if (match != HeightMapPaths.end())
        {
            _terrainPanelState.HeightMapIndex = static_cast<int>(std::distance(HeightMapPaths.begin(), match));
        }
    }
}

void Application::StartRuntimeBenchmark()
{
    // 每轮 benchmark 都重新生成结果，保留上一次输出路径供 UI 展示
    _runtimeBenchmark.Active = true;
    _runtimeBenchmark.HasPreparedFirstFrame = false;
    _runtimeBenchmark.WarmingUp = false;
    _runtimeBenchmark.WarmupSampleIndex = 0U;
    _runtimeBenchmark.AlgorithmIndex = 0;
    _runtimeBenchmark.ElapsedSeconds = 0.0F;
    _runtimeBenchmark.PathSampleIndex = 0;
    _runtimeBenchmark.Failed = false;
    _runtimeBenchmark.FailureMessage.clear();
    _runtimeBenchmark.Results.clear();
    _runtimeBenchmark.Notes.clear();
    _runtimeBenchmark.Path = _terrainPanelState.BenchmarkPath;
    _runtimeBenchmark.PathSampleCount =
        _runtimeBenchmark.Path == Gui::TerrainPanelState::RuntimeBenchmarkPath::BudgetSaturation
        ? BudgetSaturationRuntimeBenchmarkSampleCount
        : DefaultRuntimeBenchmarkSampleCount;
    if (_runtimeBenchmarkOverrides.HasSampleCount)
    {
        _runtimeBenchmark.PathSampleCount = std::max(
            _runtimeBenchmarkOverrides.SampleCount,
            static_cast<std::size_t>(2));
    }
    _runtimeBenchmark.WarmupSampleCount =
        _runtimeBenchmark.Path == Gui::TerrainPanelState::RuntimeBenchmarkPath::BudgetSaturation
        ? BudgetSaturationRuntimeBenchmarkWarmupSampleCount
        : DefaultRuntimeBenchmarkWarmupSampleCount;
    if (_runtimeBenchmarkOverrides.HasWarmupSampleCount)
    {
        _runtimeBenchmark.WarmupSampleCount = _runtimeBenchmarkOverrides.WarmupSampleCount;
    }
    _runtimeBenchmark.Notes.push_back("构建配置：" + BuildConfigurationName());
    _runtimeBenchmark.Notes.push_back("图形后端：" + std::string{_graphicsBackend->Name()});
    _runtimeBenchmark.Notes.push_back(
        "图形适配器：" + _graphicsBackend->AdapterName() + " (" + _graphicsBackend->VersionString() + ")");
    _runtimeBenchmark.Notes.push_back(
        "阶段策略：" + PassPolicyDisplayName(_terrainPanelState.RoamPassPolicy));
    _runtimeBenchmark.Notes.push_back(
        "并行拓扑候选阈值：细分 " +
        std::to_string(_terrainPanelState.RoamPassPolicy.SplitTopologyMinParallelCandidateCount) +
        "，合并 " +
        std::to_string(_terrainPanelState.RoamPassPolicy.MergeTopologyMinParallelCandidateCount));
    _runtimeBenchmark.Notes.push_back(
        "并行拓扑限定：更新 " +
        std::to_string(_terrainPanelState.RoamPassPolicy.ParallelTopologyTargetBuild) +
        "，阶段 " +
        std::string{Algorithms::ToString(_terrainPanelState.RoamPassPolicy.ParallelTopologyPhase)} +
        "；更新 0 表示每次更新均允许");
    if (!_runtimeBenchmarkOverrides.Label.empty())
    {
        _runtimeBenchmark.Notes.push_back("Benchmark 标签：" + _runtimeBenchmarkOverrides.Label);
    }
    _runtimeBenchmark.AlgorithmSequence = {
        Algorithms::TerrainLodAlgorithmId::ClassicCpuRoam,
        Algorithms::TerrainLodAlgorithmId::DataOrientedCpuRoam,
    };
    const std::size_t algorithmCount = _runtimeBenchmark.AlgorithmSequence.size();
    _runtimeBenchmark.AlgorithmOrderRotation = algorithmCount == 0U
        ? 0U
        : (_runtimeBenchmarkOverrides.HasAlgorithmOrderRotation
            ? _runtimeBenchmarkOverrides.AlgorithmOrderRotation
            : _runtimeBenchmarkRunCount) % algorithmCount;
    if (_runtimeBenchmark.AlgorithmOrderRotation != 0U)
    {
        std::rotate(
            _runtimeBenchmark.AlgorithmSequence.begin(),
            _runtimeBenchmark.AlgorithmSequence.begin() +
                static_cast<std::ptrdiff_t>(_runtimeBenchmark.AlgorithmOrderRotation),
            _runtimeBenchmark.AlgorithmSequence.end());
    }
    ++_runtimeBenchmarkRunCount;
    std::string algorithmOrder;
    for (const Algorithms::TerrainLodAlgorithmId algorithmId : _runtimeBenchmark.AlgorithmSequence)
    {
        if (!algorithmOrder.empty())
        {
            algorithmOrder += " -> ";
        }
        algorithmOrder += RuntimeBenchmarkAlgorithmDisplayName(algorithmId);
    }
    _runtimeBenchmark.Notes.push_back(
        "算法顺序：" + algorithmOrder + "；轮换偏移 " +
        std::to_string(_runtimeBenchmark.AlgorithmOrderRotation));
    _runtimeBenchmark.Notes.push_back(
        "独立预热：每种算法 " + std::to_string(_runtimeBenchmark.WarmupSampleCount) +
        " 个不计入结果的采样点，预热后重置拓扑再开始记录");
    _runtimeBenchmark.PreviousTerrainPanelState = _terrainPanelState;
    _runtimeBenchmark.PreviousTerrainPanelState.StartBenchmarkRequested = false;
    _runtimeBenchmark.PreviousCameraPose = CameraPose{
        _camera.Position(),
        _camera.YawDegrees(),
        _camera.PitchDegrees(),
    };

    if (_runtimeBenchmark.Path == Gui::TerrainPanelState::RuntimeBenchmarkPath::BudgetSaturation)
    {
        // 压力路径使用已验证能吃满 200000 三角形的固定场景参数。
        _terrainPanelState.HeightMapIndex = 1;
        _terrainPanelState.TerrainSize = 80.0F;
        _terrainPanelState.HeightScale = 12.0F;
        _terrainPanelState.RoamMaxDepth = 20;
        _terrainPanelState.RoamScreenSpaceSplitThresholdPixels = 0.25F;
        _terrainPanelState.RoamScreenSpaceMergeThresholdPixels = 0.10F;
        _terrainPanelState.RoamTriangleBudget = 200000;
        ApplyHeightMapSelection();
        _runtimeBenchmark.Notes.push_back("Benchmark 路径：极限压力路径");
    }
    else
    {
        _runtimeBenchmark.Notes.push_back("Benchmark 路径：默认选项路径");
    }

    _runtimeBenchmark.Notes.push_back(
        "路径采样点数：" + std::to_string(_runtimeBenchmark.PathSampleCount) +
        "；每种算法按相同 sampleIndex 执行完整路径");

    _terrainPanelState.VSyncEnabled = false;
    ApplyWindowPanelSettings();
    _runtimeBenchmark.Notes.push_back(
        _terrainPanelState.VSyncEnabled ?
            "VSync：已启用（关闭请求未被接受）" :
            "VSync：基准测试期间已关闭");

    BeginRuntimeBenchmarkAlgorithm();
}

void Application::BeginRuntimeBenchmarkAlgorithm()
{
    if (_runtimeBenchmark.AlgorithmIndex >= _runtimeBenchmark.AlgorithmSequence.size())
    {
        FinishRuntimeBenchmark();
        return;
    }

    const Algorithms::TerrainLodAlgorithmId algorithmId =
        _runtimeBenchmark.AlgorithmSequence[_runtimeBenchmark.AlgorithmIndex];

    RuntimeBenchmarkAlgorithmResult result{};
    result.AlgorithmId = algorithmId;
    result.AlgorithmName = RuntimeBenchmarkAlgorithmDisplayName(algorithmId);
    result.Settings = ToTerrainLodSettings(_terrainPanelState);
    result.WarmupSampleCount = _runtimeBenchmark.WarmupSampleCount;
    result.ExecutionOrderIndex = _runtimeBenchmark.AlgorithmIndex;
    result.AlgorithmOrderRotation = _runtimeBenchmark.AlgorithmOrderRotation;
    result.Samples.reserve(_runtimeBenchmark.PathSampleCount);
    _runtimeBenchmark.Results.push_back(std::move(result));

    if (_runtimeBenchmark.Path == Gui::TerrainPanelState::RuntimeBenchmarkPath::BudgetSaturation)
    {
        const BudgetSaturationCameraPose pose = ComputeBudgetSaturationCameraPose(0.0F);
        _runtimeBenchmark.StartPosition = pose.Position;
        _runtimeBenchmark.EndPosition = pose.Target;
    }
    else
    {
        const float halfTerrainSize = _terrainPanelState.TerrainSize * 0.5F;
        const float cameraHeight = std::max(3.0F, _terrainPanelState.HeightScale * 1.5F);
        // Z+ 边中点到中心的路径便于和固定朝向一起解释
        _runtimeBenchmark.StartPosition = glm::vec3{0.0F, cameraHeight, halfTerrainSize};
        _runtimeBenchmark.EndPosition = glm::vec3{0.0F, cameraHeight, 0.0F};
    }
    const glm::vec3 initialLookAtTarget =
        _runtimeBenchmark.Path == Gui::TerrainPanelState::RuntimeBenchmarkPath::BudgetSaturation
        ? _runtimeBenchmark.EndPosition
        : glm::vec3{0.0F, 0.0F, 0.0F};
    const auto [yawDegrees, pitchDegrees] =
        ComputeYawPitchForLookAt(_runtimeBenchmark.StartPosition, initialLookAtTarget);
    _runtimeBenchmark.YawDegrees = yawDegrees;
    _runtimeBenchmark.PitchDegrees = pitchDegrees;
    _runtimeBenchmark.ElapsedSeconds = 0.0F;
    _runtimeBenchmark.PathSampleIndex = 0;
    _runtimeBenchmark.HasPreparedFirstFrame = false;
    _runtimeBenchmark.WarmingUp = _runtimeBenchmark.WarmupSampleCount > 0U;
    _runtimeBenchmark.WarmupSampleIndex = 0U;

    _terrainPanelState.UseTerrainLod = true;
    _terrainPanelState.TerrainLodAlgorithm = algorithmId;
    _terrainPanelState.StartBenchmarkRequested = false;
    // ApplySettings 先切换算法，再 reset 可保证下一帧从干净拓扑开始
    ApplyTerrainPanelSettings();
    _terrainRenderer.ResetTerrainLodAlgorithm();
    _terrainRenderer.RequestMeshRebuild();
    _camera.SetPose(_runtimeBenchmark.StartPosition, _runtimeBenchmark.YawDegrees, _runtimeBenchmark.PitchDegrees);
}

void Application::PrepareRuntimeBenchmarkFrame(const FrameTiming& frameTiming)
{
    if (!_runtimeBenchmark.Active)
    {
        return;
    }

    if (_runtimeBenchmark.HasPreparedFirstFrame)
    {
        // 第一帧从 0 开始记录；后续帧只累计实际墙钟时间
        const float deltaSeconds = std::max(frameTiming.RawDeltaSeconds, 0.0F);
        _runtimeBenchmark.ElapsedSeconds += deltaSeconds;
    }
    else
    {
        _runtimeBenchmark.HasPreparedFirstFrame = true;
    }

    const std::size_t currentSampleCount = _runtimeBenchmark.WarmingUp
        ? _runtimeBenchmark.WarmupSampleCount
        : _runtimeBenchmark.PathSampleCount;
    const std::size_t currentSampleIndex = _runtimeBenchmark.WarmingUp
        ? _runtimeBenchmark.WarmupSampleIndex
        : _runtimeBenchmark.PathSampleIndex;
    const float t = currentSampleCount <= 1U
        ? 0.0F
        : static_cast<float>(currentSampleIndex) /
            static_cast<float>(currentSampleCount - 1U);
    if (_runtimeBenchmark.Path == Gui::TerrainPanelState::RuntimeBenchmarkPath::BudgetSaturation)
    {
        const BudgetSaturationCameraPose pose = ComputeBudgetSaturationCameraPose(t);
        const auto [yawDegrees, pitchDegrees] = ComputeYawPitchForLookAt(pose.Position, pose.Target);
        _camera.SetPose(pose.Position, yawDegrees, pitchDegrees);
    }
    else
    {
        // 只平滑位置，不旋转相机，保证每个算法看到同一条视点路径
        const glm::vec3 cameraPosition =
            glm::mix(_runtimeBenchmark.StartPosition, _runtimeBenchmark.EndPosition, SmoothStep(t));
        _camera.SetPose(cameraPosition, _runtimeBenchmark.YawDegrees, _runtimeBenchmark.PitchDegrees);
    }
    _terrainRenderer.RequestMeshRebuild();
}

void Application::CompleteRuntimeBenchmarkFrame()
{
    if (!_runtimeBenchmark.Active)
    {
        return;
    }

    if (_runtimeBenchmark.Failed)
    {
        FinishRuntimeBenchmark();
        return;
    }

    if (_runtimeBenchmark.WarmingUp)
    {
        if (_runtimeBenchmark.WarmupSampleIndex + 1U < _runtimeBenchmark.WarmupSampleCount)
        {
            ++_runtimeBenchmark.WarmupSampleIndex;
            return;
        }

        // 预热只保留代码、分配器和图形管线的热状态，正式样本从干净拓扑重新开始
        _runtimeBenchmark.WarmingUp = false;
        _runtimeBenchmark.WarmupSampleIndex = 0U;
        _runtimeBenchmark.PathSampleIndex = 0U;
        _runtimeBenchmark.ElapsedSeconds = 0.0F;
        _runtimeBenchmark.HasPreparedFirstFrame = false;
        ApplyTerrainPanelSettings();
        _terrainRenderer.ResetTerrainLodAlgorithm();
        _terrainRenderer.RequestMeshRebuild();
        _camera.SetPose(
            _runtimeBenchmark.StartPosition,
            _runtimeBenchmark.YawDegrees,
            _runtimeBenchmark.PitchDegrees);
        return;
    }

    if (_runtimeBenchmark.PathSampleCount == 0U)
    {
        _runtimeBenchmark.Failed = true;
        _runtimeBenchmark.FailureMessage = "Runtime benchmark path has no sample points";
        FinishRuntimeBenchmark();
        return;
    }

    if (_runtimeBenchmark.PathSampleIndex + 1U < _runtimeBenchmark.PathSampleCount)
    {
        ++_runtimeBenchmark.PathSampleIndex;
        return;
    }

    ++_runtimeBenchmark.AlgorithmIndex;
    if (_runtimeBenchmark.AlgorithmIndex < _runtimeBenchmark.AlgorithmSequence.size())
    {
        // 当前算法走完全部采样点后，从同一路径起点切到下一个算法
        BeginRuntimeBenchmarkAlgorithm();
        return;
    }

    FinishRuntimeBenchmark();
}

void Application::RecordRuntimeBenchmarkSample(
    const FrameTiming& frameTiming,
    const Render::TerrainRenderStats& terrainStats,
    const glm::vec3& cameraPosition)
{
    if (!_runtimeBenchmark.Active || _runtimeBenchmark.WarmingUp ||
        _runtimeBenchmark.Failed || _runtimeBenchmark.Results.empty())
    {
        return;
    }

    RuntimeBenchmarkSample sample{};
    sample.BuildConfiguration = BuildConfigurationName();
    sample.GraphicsBackend = _graphicsBackend->Name();
    sample.GraphicsAdapter = _graphicsBackend->AdapterName();
    sample.GraphicsVersion = _graphicsBackend->VersionString();
    sample.VSyncEnabled = _terrainPanelState.VSyncEnabled;
    sample.PathSampleIndex = _runtimeBenchmark.PathSampleIndex;
    sample.PathSampleCount = _runtimeBenchmark.PathSampleCount;
    sample.PathProgress = _runtimeBenchmark.PathSampleCount <= 1U
        ? 0.0F
        : static_cast<float>(_runtimeBenchmark.PathSampleIndex) /
            static_cast<float>(_runtimeBenchmark.PathSampleCount - 1U);
    sample.TimeSeconds = _runtimeBenchmark.ElapsedSeconds;
    sample.CameraPosition = cameraPosition;
    // RawDeltaSeconds 是真实帧耗时，ClampedDeltaSeconds 只适合模拟
    sample.FrameMilliseconds = frameTiming.RawDeltaSeconds * 1000.0F;
    sample.Stats = terrainStats;
    _runtimeBenchmark.Results.back().Samples.push_back(sample);
}

void Application::FinishRuntimeBenchmark()
{
    bool reportSucceeded = false;
    if (_runtimeBenchmark.Failed)
    {
        std::cerr << "Runtime benchmark aborted: " << _runtimeBenchmark.FailureMessage << '\n';
    }
    else
    {
        try
        {
            const RuntimeBenchmarkReportPaths paths =
                WriteRuntimeBenchmarkReport(_runtimeBenchmark.Results, _runtimeBenchmark.Notes);
            // 输出路径留在状态里，下一帧 UI 可以继续展示给用户
            _runtimeBenchmark.LastMarkdownPath = paths.MarkdownPath;
            _runtimeBenchmark.LastCsvPath = paths.CsvPath;
            std::cout << "Runtime benchmark report: " << paths.MarkdownPath << '\n';
            std::cout << "Runtime benchmark csv: " << paths.CsvPath << '\n';
            reportSucceeded = true;
        }
        catch (const std::exception& exception)
        {
            std::cerr << exception.what() << '\n';
        }
    }

    const Gui::TerrainPanelState previousTerrainPanelState = _runtimeBenchmark.PreviousTerrainPanelState;
    const CameraPose previousCameraPose = _runtimeBenchmark.PreviousCameraPose;
    // 先退出 Active，再恢复 UI 状态，避免 DrawDebugOverlay 继续锁定控件
    _runtimeBenchmark.Active = false;
    _runtimeBenchmark.HasPreparedFirstFrame = false;
    _runtimeBenchmark.WarmingUp = false;
    _runtimeBenchmark.WarmupSampleIndex = 0U;
    _runtimeBenchmark.ElapsedSeconds = 0.0F;
    _runtimeBenchmark.PathSampleIndex = 0;

    _terrainPanelState = previousTerrainPanelState;
    _terrainPanelState.StartBenchmarkRequested = false;
    ApplyWindowPanelSettings();
    _camera.SetPose(previousCameraPose.Position, previousCameraPose.YawDegrees, previousCameraPose.PitchDegrees);
    // 压力路径会临时切换 HeightMap；恢复面板状态时必须同步恢复 renderer 实际资源。
    ApplyHeightMapSelection();
    // 恢复设置后强制重建一次，防止画面停留在 benchmark 的算法 mesh
    ApplyTerrainPanelSettings();
    _terrainRenderer.ResetTerrainLodAlgorithm();
    _terrainRenderer.RequestMeshRebuild();

    if (_automaticRuntimeBenchmarkEnabled)
    {
        _automaticRuntimeBenchmarkCompleted = true;
        _automaticRuntimeBenchmarkFailed = !reportSucceeded;
    }
}

std::string Application::CurrentRuntimeBenchmarkAlgorithmName() const
{
    if (!_runtimeBenchmark.Active || _runtimeBenchmark.AlgorithmIndex >= _runtimeBenchmark.AlgorithmSequence.size())
    {
        return {};
    }

    std::string name = RuntimeBenchmarkAlgorithmDisplayName(
        _runtimeBenchmark.AlgorithmSequence[_runtimeBenchmark.AlgorithmIndex]);
    if (_runtimeBenchmark.WarmingUp)
    {
        name += "（预热）";
    }
    return name;
}

float Application::RuntimeBenchmarkProgress() const
{
    if (!_runtimeBenchmark.Active || _runtimeBenchmark.AlgorithmSequence.empty())
    {
        // 非运行状态下 UI 进度条保持空值
        return 0.0F;
    }

    // 预热和正式采样都计入进度，但只有正式采样写入报告
    const std::size_t localTotal =
        _runtimeBenchmark.WarmupSampleCount + _runtimeBenchmark.PathSampleCount;
    const std::size_t localCompleted = _runtimeBenchmark.WarmingUp
        ? _runtimeBenchmark.WarmupSampleIndex + 1U
        : _runtimeBenchmark.WarmupSampleCount + _runtimeBenchmark.PathSampleIndex + 1U;
    const float localProgress = localTotal == 0U
        ? 0.0F
        : std::clamp(
            static_cast<float>(localCompleted) / static_cast<float>(localTotal),
            0.0F,
            1.0F);
    const float completedAlgorithms = static_cast<float>(_runtimeBenchmark.AlgorithmIndex);
    // AlgorithmSequence 非空已在函数入口确认
    const float algorithmCount = static_cast<float>(_runtimeBenchmark.AlgorithmSequence.size());
    return (completedAlgorithms + localProgress) / algorithmCount;
}
} // 命名空间 ParallelRoam::App
