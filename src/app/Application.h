#pragma once

#include "app/CameraController.h"
#include "app/InputState.h"
#include "app/RuntimeBenchmark.h"
#include "gui/ImGuiLayer.h"
#include "platform/Window.h"
#include "render/GraphicsBackend.h"
#include "render/TerrainRenderer.h"
#include "tools/PerformanceTimer.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace ParallelRoam::App
{
struct RuntimeBenchmarkOverrides
{
    bool HasPath{false};
    Gui::TerrainPanelState::RuntimeBenchmarkPath Path{
        Gui::TerrainPanelState::RuntimeBenchmarkPath::Default};

    bool HasPassPolicy{false};
    Algorithms::TerrainLodPassPolicy PassPolicy{};

    bool HasHeightMapIndex{false};
    int HeightMapIndex{0};

    bool HasTerrainSize{false};
    float TerrainSize{30.0F};

    bool HasHeightScale{false};
    float HeightScale{4.0F};

    bool HasMaxDepth{false};
    int MaxDepth{14};

    bool HasScreenSpaceSplitThresholdPixels{false};
    float ScreenSpaceSplitThresholdPixels{4.0F};

    bool HasScreenSpaceMergeThresholdPixels{false};
    float ScreenSpaceMergeThresholdPixels{2.0F};

    bool HasSampleCount{false};
    std::size_t SampleCount{0};

    std::string Label;
};

/// <summary>
/// 协调平台层、输入层、渲染层和 GUI 层的应用主循环
/// </summary>
class Application
{
public:
    Application() = default;
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    void EnableAutomaticRuntimeBenchmark();
    void ConfigureRuntimeBenchmark(const RuntimeBenchmarkOverrides& overrides);
    bool Initialize();

    /// <summary>
    /// 运行应用主循环，maxFrameCount 大于 0 时用于 smoke test 固定帧退出
    /// </summary>
    int Run(int maxFrameCount = -1);

    void Shutdown();

private:
    /// <summary>
    /// 单帧时间数据，同时保留真实帧耗时和模拟用钳制时间
    /// </summary>
    struct FrameTiming
    {
        // RawDeltaSeconds 保留真实帧时间，用于性能显示
        float RawDeltaSeconds{0.0F};

        // ClampedDeltaSeconds 只用于相机和模拟，避免卡顿后瞬移
        float ClampedDeltaSeconds{0.0F};
    };

    /// <summary>
    /// benchmark 前后恢复用户相机姿态所需的最小快照
    /// </summary>
    struct CameraPose
    {
        // 只保存自由飞行相机恢复需要的公开状态
        glm::vec3 Position{0.0F};
        float YawDegrees{0.0F};
        float PitchDegrees{0.0F};
    };

    /// <summary>
    /// 运行时 benchmark 状态机，顺序驱动已实现的 ROAM 算法
    /// </summary>
    struct RuntimeBenchmarkState
    {
        // Active 为 true 时主循环由 benchmark 控制相机
        bool Active{false};

        // StartRequested 由 UI 设置，下一帧主循环再真正启动
        bool StartRequested{false};

        // 第一帧必须采样 t=0，不能先累加 delta
        bool HasPreparedFirstFrame{false};

        bool Failed{false};
        std::string FailureMessage;

        // 算法顺序固定，输出表格才能横向对齐
        std::vector<Algorithms::TerrainLodAlgorithmId> AlgorithmSequence;

        // Notes 记录构建、后端和可选算法 capability 信息
        std::vector<std::string> Notes;

        // Results 在整轮结束后一次性写成 Markdown 和 CSV
        std::vector<RuntimeBenchmarkAlgorithmResult> Results;

        // AlgorithmIndex 指向当前正在跑的算法
        std::size_t AlgorithmIndex{0};

        // ElapsedSeconds 只记录当前算法实际运行时长，不参与路径推进
        float ElapsedSeconds{0.0F};

        // 同一轮 benchmark 的所有算法使用相同路径采样点数
        std::size_t PathSampleCount{0};

        // PathSampleIndex 标识当前帧对应的离散相机姿态
        std::size_t PathSampleIndex{0};

        // StartPosition 是地形边中点上方的相机位置
        glm::vec3 StartPosition{0.0F};

        // EndPosition 是地形中心上方的相机位置
        glm::vec3 EndPosition{0.0F};

        // 朝向在单次路径内保持固定，避免中心点附近方向退化
        float YawDegrees{-90.0F};
        float PitchDegrees{-18.0F};
        Gui::TerrainPanelState::RuntimeBenchmarkPath Path{
            Gui::TerrainPanelState::RuntimeBenchmarkPath::Default};

        // benchmark 结束后恢复用户原本的面板参数
        Gui::TerrainPanelState PreviousTerrainPanelState;

        // benchmark 结束后恢复用户原本的观察视角
        CameraPose PreviousCameraPose;

        // LastMarkdownPath 用于 UI 告知用户最新表格位置
        std::filesystem::path LastMarkdownPath;

        // LastCsvPath 目前只输出到日志，后续可在 UI 增加入口
        std::filesystem::path LastCsvPath;
    };

    // 同时计算真实帧时间和模拟用帧时间
    [[nodiscard]] FrameTiming ComputeFrameTiming();

    // 集中处理 SDL 事件，保证 GUI、输入状态和窗口尺寸看到同一批事件
    void PollEvents();

    // 渲染 height map terrain 和调试面板
    void RenderFrame(const FrameTiming& frameTiming);

    // 应用 GUI 面板参数并把错误统一输出到 stderr
    void ApplyTerrainPanelSettings();

    // 应用不影响 terrain mesh 的窗口运行参数
    void ApplyWindowPanelSettings();
    void ApplyPendingRuntimeBenchmarkOverrides();

    // 根据 UI 选择加载不同 HeightMap，并重置 terrain LOD 状态
    void ApplyHeightMapSelection();

    // benchmark 生命周期由主循环推进，避免 UI 直接修改渲染器状态
    void StartRuntimeBenchmark();
    void BeginRuntimeBenchmarkAlgorithm();
    void PrepareRuntimeBenchmarkFrame(const FrameTiming& frameTiming);
    void CompleteRuntimeBenchmarkFrame();
    void RecordRuntimeBenchmarkSample(
        const FrameTiming& frameTiming,
        const Render::TerrainRenderStats& terrainStats,
        const glm::vec3& cameraPosition);
    void FinishRuntimeBenchmark();

    [[nodiscard]] std::string CurrentRuntimeBenchmarkAlgorithmName() const;
    [[nodiscard]] float RuntimeBenchmarkProgress() const;

    // 子系统按生命周期依赖顺序声明，析构和 Shutdown 更容易保持一致
    Platform::Window _window;
    std::unique_ptr<Render::IGraphicsBackend> _graphicsBackend;
    InputState _input;
    CameraController _camera;
    Render::TerrainRenderer _terrainRenderer;
    Gui::ImGuiLayer _guiLayer;
    Render::TerrainRenderSettings _terrainSettings;
    Gui::TerrainPanelState _terrainPanelState;
    RuntimeBenchmarkState _runtimeBenchmark;
    std::string _lastMeshUpdateError;
    Tools::PerformanceTimer _frameTimer;
    bool _initialized{false};
    bool _terrainLodSmokeTestEnabled{false};
    bool _terrainLodSmokeTestFailed{false};
    bool _automaticRuntimeBenchmarkEnabled{false};
    bool _automaticRuntimeBenchmarkCompleted{false};
    bool _automaticRuntimeBenchmarkFailed{false};
    bool _hasRuntimeBenchmarkOverrides{false};
    RuntimeBenchmarkOverrides _runtimeBenchmarkOverrides;
    float _framesPerSecond{0.0F};
    float _frameTimeMilliseconds{0.0F};
};
} // 命名空间 ParallelRoam::App
