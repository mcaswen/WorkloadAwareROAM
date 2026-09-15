#include "gui/ImGuiLayer.h"
#include "algorithms/TerrainLodAlgorithmRegistry.h"

#include <imgui.h>
#if defined(PARALLEL_ROAM_GRAPHICS_API_OPENGL)
#include <imgui_impl_opengl3.h>
#elif defined(PARALLEL_ROAM_GRAPHICS_API_D3D12)
#include <imgui_impl_dx12.h>
#endif
#include <imgui_impl_sdl2.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>

namespace ParallelRoam::Gui
{
namespace
{
// GUI 层只编辑配置和展示统计
// 它不直接触碰 renderer buffer 或算法节点
constexpr float PanelMinWidth = 330.0F;
constexpr float PanelMaxWidth = 390.0F;

// DrawDebugOverlay 通过返回 changed 通知 Application 应用新设置
// 这样 UI 控件和渲染/算法边界保持单向数据流
constexpr float MetricValueOffset = 132.0F;
constexpr float PerformanceOverlayWidth = 360.0F;

void ApplyEditorStyle()
{
    // 样式在初始化时集中设置
    // 避免每帧重复写 ImGuiStyle
    // 也让截图和录屏的 UI 外观稳定
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2{16.0F, 14.0F};
    style.FramePadding = ImVec2{10.0F, 6.0F};
    style.ItemSpacing = ImVec2{10.0F, 9.0F};
    style.ItemInnerSpacing = ImVec2{8.0F, 6.0F};
    style.ScrollbarSize = 11.0F;
    style.WindowBorderSize = 0.0F;
    style.ChildBorderSize = 0.0F;
    style.PopupBorderSize = 1.0F;
    style.FrameBorderSize = 0.0F;
    style.WindowRounding = 0.0F;
    style.ChildRounding = 0.0F;
    style.FrameRounding = 4.0F;
    style.GrabRounding = 4.0F;
    style.TabRounding = 4.0F;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = ImVec4{0.88F, 0.91F, 0.90F, 1.0F};
    colors[ImGuiCol_TextDisabled] = ImVec4{0.47F, 0.52F, 0.52F, 1.0F};
    colors[ImGuiCol_WindowBg] = ImVec4{0.07F, 0.085F, 0.09F, 0.98F};
    colors[ImGuiCol_ChildBg] = ImVec4{0.085F, 0.105F, 0.11F, 1.0F};
    colors[ImGuiCol_PopupBg] = ImVec4{0.07F, 0.085F, 0.09F, 1.0F};
    colors[ImGuiCol_Border] = ImVec4{0.20F, 0.25F, 0.25F, 1.0F};
    colors[ImGuiCol_FrameBg] = ImVec4{0.12F, 0.15F, 0.155F, 1.0F};
    colors[ImGuiCol_FrameBgHovered] = ImVec4{0.16F, 0.20F, 0.205F, 1.0F};
    colors[ImGuiCol_FrameBgActive] = ImVec4{0.18F, 0.25F, 0.25F, 1.0F};
    colors[ImGuiCol_TitleBg] = ImVec4{0.07F, 0.085F, 0.09F, 1.0F};
    colors[ImGuiCol_TitleBgActive] = ImVec4{0.07F, 0.085F, 0.09F, 1.0F};
    colors[ImGuiCol_Header] = ImVec4{0.10F, 0.14F, 0.145F, 1.0F};
    colors[ImGuiCol_HeaderHovered] = ImVec4{0.16F, 0.23F, 0.23F, 1.0F};
    colors[ImGuiCol_HeaderActive] = ImVec4{0.13F, 0.18F, 0.18F, 1.0F};
    colors[ImGuiCol_Button] = ImVec4{0.12F, 0.18F, 0.18F, 1.0F};
    colors[ImGuiCol_ButtonHovered] = ImVec4{0.18F, 0.27F, 0.27F, 1.0F};
    colors[ImGuiCol_ButtonActive] = ImVec4{0.11F, 0.34F, 0.31F, 1.0F};
    colors[ImGuiCol_CheckMark] = ImVec4{0.24F, 0.72F, 0.61F, 1.0F};
    colors[ImGuiCol_SliderGrab] = ImVec4{0.24F, 0.72F, 0.61F, 1.0F};
    colors[ImGuiCol_SliderGrabActive] = ImVec4{0.34F, 0.86F, 0.72F, 1.0F};
    colors[ImGuiCol_Separator] = ImVec4{0.18F, 0.24F, 0.24F, 1.0F};
    colors[ImGuiCol_ResizeGrip] = ImVec4{0.0F, 0.0F, 0.0F, 0.0F};
    colors[ImGuiCol_ScrollbarBg] = ImVec4{0.06F, 0.075F, 0.08F, 1.0F};
    colors[ImGuiCol_ScrollbarGrab] = ImVec4{0.18F, 0.24F, 0.24F, 1.0F};
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4{0.24F, 0.32F, 0.32F, 1.0F};
}

void LoadChineseFont()
{
    ImGuiIO& io = ImGui::GetIO();
    const std::array<std::filesystem::path, 6> fontCandidates{
        std::filesystem::path{"assets/fonts/NotoSansSC-Regular.otf"},
        std::filesystem::path{"assets/fonts/NotoSansSC-Regular.ttf"},
        std::filesystem::path{"assets/fonts/SourceHanSansSC-Regular.otf"},
        std::filesystem::path{"assets/fonts/SourceHanSansSC-Regular.ttf"},
        std::filesystem::path{"assets/fonts/AlibabaPuHuiTi-3-55-Regular.ttf"},
        std::filesystem::path{"assets/fonts/AlibabaPuHuiTi-Regular.ttf"},
    };

    // 中文字体必须显式载入 CJK glyph range，默认 ImGui 字体不覆盖中文
    for (const std::filesystem::path& fontPath : fontCandidates)
    {
        if (!std::filesystem::exists(fontPath))
        {
            continue;
        }

        ImFontConfig fontConfig{};
        // CJK 字体边缘在小字号下容易糊
        // 适度 oversample 提升调试面板可读性
        fontConfig.OversampleH = 3;
        fontConfig.OversampleV = 2;
        fontConfig.PixelSnapH = false;

        if (io.Fonts->AddFontFromFileTTF(
                fontPath.string().c_str(),
                17.0F,
                &fontConfig,
                io.Fonts->GetGlyphRangesChineseFull()) != nullptr)
        {
            std::printf("ImGui font loaded: %s\n", fontPath.string().c_str());
            return;
        }
    }

    std::fprintf(stderr, "Chinese font not found; using the ImGui default font.\n");
    io.Fonts->AddFontDefault();
}

void DrawSectionHeader(const char* label)
{
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.24F, 0.72F, 0.61F, 1.0F});
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::Separator();
}

void DrawMetricRow(const char* label, const char* value)
{
    // metric 使用固定 value 偏移
    // 长 label 不会挤压右侧数值列
    ImGui::TextDisabled("%s", label);
    ImGui::SameLine(MetricValueOffset);
    ImGui::TextUnformatted(value);
}

void DrawMetricInt(const char* label, int value)
{
    char buffer[64]{};
    std::snprintf(buffer, sizeof(buffer), "%d", value);
    DrawMetricRow(label, buffer);
}

void DrawMetricSize(const char* label, std::size_t value)
{
    char buffer[64]{};
    std::snprintf(buffer, sizeof(buffer), "%zu", value);
    DrawMetricRow(label, buffer);
}

void DrawMetricFloat(const char* label, float value, const char* format)
{
    char buffer[64]{};
    std::snprintf(buffer, sizeof(buffer), format, static_cast<double>(value));
    DrawMetricRow(label, buffer);
}

int TerrainModeIndex(bool useTerrainLod, Algorithms::TerrainLodAlgorithmId algorithmId)
{
    if (!useTerrainLod)
    {
        // mode index 0 保留规则网格 baseline
        return 0;
    }

    if (algorithmId == Algorithms::TerrainLodAlgorithmId::TransactionalCpuLod) return 3;
    if (algorithmId == Algorithms::TerrainLodAlgorithmId::Cbt2024)
    {
        return 4;
    }
    if (algorithmId == Algorithms::TerrainLodAlgorithmId::DataOrientedCpuRoam)
    {
        return 2;
    }

    return 1;
}

Algorithms::TerrainLodAlgorithmId TerrainAlgorithmFromModeIndex(int modeIndex)
{
    if (modeIndex == 3) return Algorithms::TerrainLodAlgorithmId::TransactionalCpuLod;
    if (modeIndex == 4)
    {
        return Algorithms::TerrainLodAlgorithmId::Cbt2024;
    }
    if (modeIndex == 2)
    {
        // DOD 仍走 CPU mesh 输出路径
        return Algorithms::TerrainLodAlgorithmId::DataOrientedCpuRoam;
    }

    return Algorithms::TerrainLodAlgorithmId::ClassicCpuRoam;
}

const char* TerrainModeName(bool useTerrainLod, Algorithms::TerrainLodAlgorithmId algorithmId)
{
    if (!useTerrainLod)
    {
        // 面板显示以 renderer 实际运行的模式为准
        return "规则网格";
    }

    if (algorithmId == Algorithms::TerrainLodAlgorithmId::DataOrientedCpuRoam)
    {
        return "Data-Oriented CPU ROAM";
    }

    return Algorithms::TerrainLodAlgorithmDisplayName(algorithmId).data();
}

void DrawDebugColorLegend()
{
    // legend 与 TerrainMeshVertex 的 debug color 语义对应
    // 用户打开 LOD 着色后能直接解释当前颜色
    const std::array<std::pair<const char*, ImVec4>, 3> legendItems{
        std::pair<const char*, ImVec4>{"原始", ImVec4{0.28F, 0.34F, 0.30F, 1.0F}},
        std::pair<const char*, ImVec4>{"细分", ImVec4{0.08F, 0.72F, 0.62F, 1.0F}},
        std::pair<const char*, ImVec4>{"重建", ImVec4{1.0F, 0.58F, 0.12F, 1.0F}},
    };

    for (const auto& [label, color] : legendItems)
    {
        ImGui::ColorButton(label, color, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop, ImVec2{14.0F, 14.0F});
        ImGui::SameLine();
        ImGui::TextUnformatted(label);
        ImGui::SameLine();
    }

    ImGui::NewLine();
}

void DrawBenchmarkStatusOverlay(const DebugOverlayData& data)
{
    if (!data.BenchmarkRunning)
    {
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();
    // 顶部居中提示不参与输入，避免干扰 benchmark 过程中的鼠标状态
    constexpr float OverlayWidth = 460.0F;
    constexpr float OverlayHeight = 70.0F;
    ImGui::SetNextWindowPos(ImVec2{io.DisplaySize.x * 0.5F, 22.0F}, ImGuiCond_Always, ImVec2{0.5F, 0.0F});
    ImGui::SetNextWindowSize(ImVec2{OverlayWidth, OverlayHeight}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.88F);

    constexpr ImGuiWindowFlags OverlayFlags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoInputs;

    ImGui::Begin("Runtime Benchmark Status", nullptr, OverlayFlags);
    ImGui::Text("正在应用%s算法进行性能测试", data.BenchmarkAlgorithmName.c_str());
    ImGui::ProgressBar(std::clamp(data.BenchmarkProgress, 0.0F, 1.0F), ImVec2{-1.0F, 8.0F}, "");
    ImGui::End();
}

void DrawPerformanceModeToggle(bool& detailedMode)
{
    ImGui::TextUnformatted("性能");
    ImGui::SameLine();
    // 模式切换只影响 overlay 展示密度，不改变 renderer 参数
    // 因此它不参与 DrawDebugOverlay 的 changed 返回值
    if (ImGui::RadioButton("简略", !detailedMode))
    {
        detailedMode = false;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("详细", detailedMode))
    {
        detailedMode = true;
    }
}

void DrawCompactPerformanceMetrics(const DebugOverlayData& data)
{
    // 简略模式保留判断卡顿和规模变化最常用的指标
    DrawMetricFloat("FPS", data.FramesPerSecond, "%.1f");
    DrawMetricFloat("Frame ms", data.FrameTimeMilliseconds, "%.2f");
    DrawMetricRow("VSync", data.VSyncEnabled ? "开启" : "关闭");
    DrawMetricRow("模式", TerrainModeName(data.UseTerrainLod, data.TerrainLodAlgorithm));
    DrawMetricSize("三角形数", data.TriangleCount);
    if (!data.Transactional) DrawMetricSize("节点数", data.RoamNodeCount);
    DrawMetricFloat("LOD total ms", data.RoamTotalMilliseconds, "%.2f");
    if (data.Transactional) DrawMetricRow("CPU 实际参与 / 占用", "未测");
    else
    {
        DrawMetricSize("CPU Worker", data.RoamCpuWorkerCount);
        DrawMetricFloat("CPU 占用", data.RoamCpuUtilizationPercent, "%.1f%%");
    }
    if (!data.TerrainLodStatusMessage.empty())
    {
        DrawMetricRow("LOD 状态", "不可用");
        ImGui::TextWrapped("%s", data.TerrainLodStatusMessage.c_str());
    }
}

void DrawDetailedPerformanceMetrics(const DebugOverlayData& data)
{
    char cameraBuffer[96]{};
    // 详细模式把相机和 drawable 信息放到同一个性能面板
    std::snprintf(
        cameraBuffer,
        sizeof(cameraBuffer),
        "%.1f, %.1f, %.1f",
        static_cast<double>(data.CameraPosition.x),
        static_cast<double>(data.CameraPosition.y),
        static_cast<double>(data.CameraPosition.z));

    DrawSectionHeader("运行");
    DrawMetricInt("Draw Call", data.DrawCallCount);
    DrawMetricInt("窗口宽度", data.WindowWidth);
    DrawMetricInt("窗口高度", data.WindowHeight);
    DrawMetricInt("Drawable W", data.DrawableWidth);
    DrawMetricInt("Drawable H", data.DrawableHeight);
    DrawMetricRow("相机位置", cameraBuffer);
    DrawMetricFloat("Yaw", data.CameraYawDegrees, "%.1f");
    DrawMetricFloat("Pitch", data.CameraPitchDegrees, "%.1f");

    DrawSectionHeader("地形");
    DrawMetricInt("高度图宽", data.HeightMapWidth);
    DrawMetricInt("高度图高", data.HeightMapHeight);
    DrawMetricSize("顶点数", data.VertexCount);
    DrawMetricSize("三角形数", data.TriangleCount);
    DrawMetricRow("模式", TerrainModeName(data.UseTerrainLod, data.TerrainLodAlgorithm));

    DrawSectionHeader("ROAM");
    DrawMetricSize("原始三角", data.RoamOriginalTriangleCount);
    DrawMetricSize("细分三角", data.RoamSubdividedTriangleCount);
    DrawMetricSize("重建三角", data.RoamRebuiltTriangleCount);
    DrawMetricSize("活跃 Split", data.RoamActiveSplitCount);
    DrawMetricSize("本帧 Split", data.RoamSplitCount);
    DrawMetricSize("强制 Split", data.RoamForcedSplitCount);
    DrawMetricSize("Merge 数", data.RoamMergeCount);
    DrawMetricSize("约束传播", data.RoamConstraintPassCount);
    DrawMetricSize("队列峰值", data.RoamCandidatePeakCount);
    if (data.TerrainLodAlgorithm == Algorithms::TerrainLodAlgorithmId::ClassicCpuRoam ||
        data.TerrainLodAlgorithm == Algorithms::TerrainLodAlgorithmId::DataOrientedCpuRoam)
    {
        DrawMetricSize("持久 Qs", data.RoamPersistentSplitQueueSize);
        DrawMetricSize("持久 Qm", data.RoamPersistentMergeQueueSize);
        DrawMetricSize("队列交换", data.RoamQueueCrossoverCount);
        DrawMetricSize("队列局部更新", data.RoamQueueMembershipUpdateCount);
    }
    if (data.TerrainLodAlgorithm == Algorithms::TerrainLodAlgorithmId::ClassicCpuRoam)
    {
        DrawMetricSize("Mesh 全量重建", data.RoamCpuMeshFullRebuildCount);
        DrawMetricSize("Mesh 更新三角形", data.RoamCpuMeshUpdatedTriangleCount);
        DrawMetricSize("Mesh 复用三角形", data.RoamCpuMeshReusedTriangleCount);
        DrawMetricSize("Mesh 更新区间", data.RoamCpuMeshDirtyRangeCount);
    }
    DrawMetricSize("Split 拒绝", data.RoamRejectedSplitCount);
    DrawMetricSize("预算拒绝", data.RoamBudgetRejectedSplitCount);
    DrawMetricSize("Merge 拒绝", data.RoamRejectedMergeCount);
    DrawMetricSize("T-junction", data.RoamTjunctionCount);
    DrawMetricSize("邻接错误", data.RoamInvalidNeighborCount);
    DrawMetricSize("拓扑错误", data.RoamInvalidTopologyCount);
    DrawMetricFloat("LOD total ms", data.RoamTotalMilliseconds, "%.2f");
    DrawMetricFloat("CPU update ms", data.RoamUpdateMilliseconds, "%.2f");
    DrawMetricFloat("CPU prepare ms", data.RoamCpuPrepareMilliseconds, "%.2f");
    DrawMetricFloat("CPU merge mark ms", data.RoamCpuMergeCandidateMarkMilliseconds, "%.2f");
    DrawMetricFloat("CPU merge topology ms", data.RoamCpuMergeTopologyMilliseconds, "%.2f");
    if (data.TerrainLodAlgorithm == Algorithms::TerrainLodAlgorithmId::DataOrientedCpuRoam)
    {
        DrawMetricFloat("DOD merge 分桶 ms", data.RoamCpuMergeTopologyChunkBuildMilliseconds, "%.2f");
        DrawMetricFloat("DOD merge 队列失效 ms", data.RoamCpuMergeTopologyQueueInvalidationMilliseconds, "%.2f");
        DrawMetricFloat("DOD merge 并行提交 ms", data.RoamCpuMergeTopologyParallelCommitMilliseconds, "%.2f");
        DrawMetricFloat("DOD merge 结果汇总 ms", data.RoamCpuMergeTopologyResultMergeMilliseconds, "%.2f");
        DrawMetricFloat("DOD merge 索引/队列刷新 ms", data.RoamCpuMergeTopologyIndexQueueRefreshMilliseconds, "%.2f");
        DrawMetricFloat("DOD merge 串行收敛 ms", data.RoamCpuMergeTopologySerialConvergenceMilliseconds, "%.2f");
    }
    DrawMetricFloat("CPU budget collect ms", data.RoamCpuBudgetLeafCollectMilliseconds, "%.2f");
    DrawMetricFloat("CPU error eval ms", data.RoamCpuErrorEvalMilliseconds, "%.2f");
    DrawMetricFloat("CPU split scan/mark ms", data.RoamCpuSplitCandidateMarkMilliseconds, "%.2f");
    DrawMetricFloat("CPU split topology ms", data.RoamCpuSplitTopologyMilliseconds, "%.2f");
    if (data.TerrainLodAlgorithm == Algorithms::TerrainLodAlgorithmId::DataOrientedCpuRoam)
    {
        DrawMetricFloat("DOD split 分桶 ms", data.RoamCpuSplitTopologyChunkBuildMilliseconds, "%.2f");
        DrawMetricFloat("DOD split 队列失效 ms", data.RoamCpuSplitTopologyQueueInvalidationMilliseconds, "%.2f");
        DrawMetricFloat("DOD split 并行提交 ms", data.RoamCpuSplitTopologyParallelCommitMilliseconds, "%.2f");
        DrawMetricFloat("DOD split 结果汇总 ms", data.RoamCpuSplitTopologyResultMergeMilliseconds, "%.2f");
        DrawMetricFloat("DOD split 索引/队列刷新 ms", data.RoamCpuSplitTopologyIndexQueueRefreshMilliseconds, "%.2f");
    }
    DrawMetricFloat("Split 串行收敛 ms", data.RoamCpuSplitTopologySerialConvergenceMilliseconds, "%.2f");
    DrawMetricFloat("CPU final collect ms", data.RoamCpuFinalLeafCollectMilliseconds, "%.2f");
    DrawMetricFloat("CPU mesh emit ms", data.RoamCpuMeshEmitMilliseconds, "%.2f");
    DrawMetricFloat("CPU finalize ms", data.RoamCpuFinalizeMilliseconds, "%.2f");
    DrawMetricFloat("CPU upload ms", data.RoamCpuUploadMilliseconds, "%.2f");
    DrawMetricFloat("Split ms", data.RoamSplitMilliseconds, "%.2f");
    DrawMetricFloat("Merge ms", data.RoamMergeMilliseconds, "%.2f");
    DrawMetricFloat("Emit ms", data.RoamEmitMilliseconds, "%.2f");
    DrawMetricFloat("Validate ms", data.RoamValidateMilliseconds, "%.2f");
    DrawMetricFloat("Frame fence wait ms", data.RoamFrameFenceWaitMilliseconds, "%.2f");
    DrawMetricSize("上传 B", data.RoamCpuGpuUploadBytes);
    DrawMetricSize("回读 B", data.RoamCpuGpuReadbackBytes);
    DrawMetricInt("设置深度", data.RoamMaxDepthSetting);
    DrawMetricInt("实际深度", data.RoamMaxDepthReached);
    if (!data.TerrainLodStatusMessage.empty())
    {
        DrawSectionHeader("LOD 状态");
        ImGui::TextWrapped("%s", data.TerrainLodStatusMessage.c_str());
    }
}

void DrawPerformanceOverlay(const DebugOverlayData& data, bool& detailedMode)
{
    constexpr ImGuiWindowFlags OverlayFlags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings;

    // 性能 overlay 放在左上角，避免和右侧交互面板抢空间
    const ImGuiIO& io = ImGui::GetIO();
    const float maxOverlayHeight = std::max(180.0F, io.DisplaySize.y - 32.0F);
    ImGui::SetNextWindowPos(ImVec2{16.0F, 16.0F}, ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(
        ImVec2{PerformanceOverlayWidth, 0.0F},
        ImVec2{PerformanceOverlayWidth, maxOverlayHeight});
    ImGui::SetNextWindowBgAlpha(0.88F);
    ImGui::Begin("Performance Overlay", nullptr, OverlayFlags);

    DrawPerformanceModeToggle(detailedMode);
    ImGui::Separator();
    DrawCompactPerformanceMetrics(data);

    if (detailedMode)
    {
        // 详细模式继续展开 ROAM 拓扑和 pass 级耗时
        if (data.Transactional)
        {
            const auto& t = *data.Transactional;
            DrawSectionHeader("事务阶段");
            DrawMetricSize("配置线程（非实测参与）", t.RequestedWorkers);
            DrawMetricSize("接收 / 回收交换", t.Exchanges);
            DrawMetricSize("自由额度细化", t.FreeExecuted);
            DrawMetricSize("配对检查", t.PairChecks);
            DrawMetricSize("冲突", t.Conflicts);
            DrawMetricFloat("相机刷新", static_cast<float>(t.ViewMilliseconds), "%.3f ms");
            DrawMetricFloat("接收认证", static_cast<float>(t.ReceiverMilliseconds), "%.3f ms");
            DrawMetricFloat("回收认证", static_cast<float>(t.DonorMilliseconds), "%.3f ms");
            DrawMetricFloat("预留", static_cast<float>(t.ReservationMilliseconds), "%.3f ms");
            DrawMetricFloat("样本续接", static_cast<float>(t.SampleRepairMilliseconds), "%.3f ms");
            DrawMetricFloat("冷启动", static_cast<float>(t.SeedMilliseconds + t.InitializeMilliseconds), "%.3f ms");
            ImGui::TextUnformatted("旧五阶段与实际线程利用率：不适用 / 未测");
            if (!data.TerrainLodStatusMessage.empty()) ImGui::TextWrapped("%s", data.TerrainLodStatusMessage.c_str());
        }
        else if (data.Cbt)
        {
            const auto& cbt = *data.Cbt;
            DrawSectionHeader("CBT GPU（延迟样本）");
            DrawMetricSize("拓扑代", cbt.TopologyGeneration);
            DrawMetricSize("计数样本代", cbt.ClassificationSampleGeneration);
            DrawMetricSize("GPU 计时样本代", cbt.GpuTimingSampleGeneration);
            DrawMetricSize("资源代", cbt.ResourceGeneration);
            DrawMetricSize("动态槽占用", cbt.ActiveDynamicSlotCount);
            DrawMetricSize("动态槽剩余", cbt.RemainingDynamicSlotCount);
            DrawMetricSize("故障重建", cbt.FaultRecoveryCount);
            DrawMetricFloat("GPU compute", cbt.GpuStageSumMilliseconds, "%.3f ms");
            DrawMetricFloat("GPU draw", cbt.GpuStageMilliseconds[
                static_cast<std::size_t>(Algorithms::TerrainLodCbtGpuStage::TerrainRender)], "%.3f ms");
            ImGui::TextUnformatted("CPU 提交、GPU compute 与 draw 独立计量；池容量不是三角形硬预算");
        }
        else
        {
            DrawDetailedPerformanceMetrics(data);
        }
        if (!data.LastBenchmarkOutputPath.empty())
        {
            DrawSectionHeader("Benchmark");
            ImGui::TextWrapped("%s", data.LastBenchmarkOutputPath.c_str());
        }
    }

    ImGui::End();
}
} // 匿名命名空间

#if defined(PARALLEL_ROAM_GRAPHICS_API_OPENGL)
bool ImGuiLayer::Initialize(const ImGuiOpenGlBackendConfig& config)
{
    if (_initialized || config.Window == nullptr || config.Context == nullptr || config.GlslVersion == nullptr)
    {
        return false;
    }

    // ImGui context 必须先创建，backend 初始化会访问当前 context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    LoadChineseFont();
    // 键盘导航先打开，后续增加 GUI 控件时不用再改初始化路径
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    ApplyEditorStyle();

    // SDL2 backend 负责事件和输入状态桥接
    // 失败时必须销毁 context
    // 否则后续重新初始化会留下 ImGui 全局状态
    if (!ImGui_ImplSDL2_InitForOpenGL(config.Window, config.Context))
    {
        ImGui::DestroyContext();
        return false;
    }

    // OpenGL3 backend 负责生成 GUI draw call
    // SDL backend 已经成功时
    // OpenGL backend 失败必须按相反顺序清理
    if (!ImGui_ImplOpenGL3_Init(config.GlslVersion))
    {
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    _renderBackend = ImGuiRenderBackend::OpenGl;
    _initialized = true;
    return true;
}
#endif

#if defined(PARALLEL_ROAM_GRAPHICS_API_D3D12)
bool ImGuiLayer::Initialize(const ImGuiD3D12BackendConfig& config)
{
    // D3D12 后端必须同时提供设备 队列 可见堆和稳定字体句柄
    if (_initialized || config.Window == nullptr || config.Device == nullptr ||
        config.CommandQueue == nullptr || config.SrvDescriptorHeap == nullptr ||
        config.FontSrvCpuDescriptor.ptr == 0 || config.FontSrvGpuDescriptor.ptr == 0)
    {
        return false;
    }

    // context 创建早于平台和渲染后端初始化
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    LoadChineseFont();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ApplyEditorStyle();

    // SDL 平台后端在 D3D12 下只负责窗口事件和输入
    if (!ImGui_ImplSDL2_InitForD3D(config.Window))
    {
        ImGui::DestroyContext();
        return false;
    }

    // 回调触发前先缓存后端分配的固定描述符句柄
    _fontSrvCpuDescriptor = config.FontSrvCpuDescriptor;
    _fontSrvGpuDescriptor = config.FontSrvGpuDescriptor;
    _fontDescriptorInUse = false;

    // ImGui 不创建独立描述符堆，绘制时与地形共享后端可见堆
    ImGui_ImplDX12_InitInfo initInfo{};
    initInfo.Device = config.Device;
    initInfo.CommandQueue = config.CommandQueue;
    initInfo.NumFramesInFlight = config.FramesInFlight;
    initInfo.RTVFormat = config.RenderTargetFormat;
    initInfo.DSVFormat = config.DepthStencilFormat;
    initInfo.SrvDescriptorHeap = config.SrvDescriptorHeap;
    initInfo.UserData = this;
    // 当前只允许 ImGui 占用预留字体槽位，不开放通用描述符分配
    initInfo.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* info,
                                       D3D12_CPU_DESCRIPTOR_HANDLE* cpuHandle,
                                       D3D12_GPU_DESCRIPTOR_HANDLE* gpuHandle) {
        auto* layer = static_cast<ImGuiLayer*>(info->UserData);
        *cpuHandle = layer->_fontSrvCpuDescriptor;
        *gpuHandle = layer->_fontSrvGpuDescriptor;
        // 状态位用于验证 ImGui 已请求并接管字体描述符
        layer->_fontDescriptorInUse = true;
    };
    // 实际槽位由 D3D12GraphicsBackend 在 ImGui Shutdown 后归还
    initInfo.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo* info,
                                      D3D12_CPU_DESCRIPTOR_HANDLE,
                                      D3D12_GPU_DESCRIPTOR_HANDLE) {
        static_cast<ImGuiLayer*>(info->UserData)->_fontDescriptorInUse = false;
    };

    // 渲染后端失败时按逆序关闭已成功的平台后端和 context
    if (!ImGui_ImplDX12_Init(&initInfo))
    {
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    // 只有两个后端都成功后才发布已初始化状态
    _renderBackend = ImGuiRenderBackend::Direct3D12;
    _initialized = true;
    return true;
}
#endif

void ImGuiLayer::Shutdown()
{
    // Shutdown 可能被 Application 析构重复触发，因此需要状态保护
    if (!_initialized)
    {
        return;
    }

    // 必须调用与初始化类型匹配的渲染后端清理函数
    if (_renderBackend == ImGuiRenderBackend::OpenGl)
    {
#if defined(PARALLEL_ROAM_GRAPHICS_API_OPENGL)
        ImGui_ImplOpenGL3_Shutdown();
#endif
    }
#if defined(PARALLEL_ROAM_GRAPHICS_API_D3D12)
    else if (_renderBackend == ImGuiRenderBackend::Direct3D12)
    {
        ImGui_ImplDX12_Shutdown();
    }
#endif
    // 平台后端晚于渲染后端关闭并早于 context 销毁
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    _renderBackend = ImGuiRenderBackend::None;
    _initialized = false;
}

void ImGuiLayer::ProcessEvent(const SDL_Event& event)
{
    ImGui_ImplSDL2_ProcessEvent(&event);
}

void ImGuiLayer::BeginFrame()
{
    if (!_initialized)
    {
        return;
    }

    // backend new frame 必须早于 ImGui::NewFrame
    // 渲染后端先准备每帧状态，随后平台后端更新输入
    if (_renderBackend == ImGuiRenderBackend::OpenGl)
    {
#if defined(PARALLEL_ROAM_GRAPHICS_API_OPENGL)
        ImGui_ImplOpenGL3_NewFrame();
#endif
    }
#if defined(PARALLEL_ROAM_GRAPHICS_API_D3D12)
    else if (_renderBackend == ImGuiRenderBackend::Direct3D12)
    {
        ImGui_ImplDX12_NewFrame();
    }
#endif
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
}

bool ImGuiLayer::DrawDebugOverlay(const DebugOverlayData& data, TerrainPanelState& terrainState)
{
    bool changed = false;
    const ImGuiIO& io = ImGui::GetIO();
    DrawBenchmarkStatusOverlay(data);
    DrawPerformanceOverlay(data, _performanceOverlayDetailed);

    // 面板宽度跟随窗口
    // 同时限制最小和最大值
    // 避免宽屏上占用过多 terrain 视野
    const float panelWidth = std::clamp(io.DisplaySize.x * 0.25F, PanelMinWidth, PanelMaxWidth);
    const float panelX = std::max(0.0F, io.DisplaySize.x - panelWidth);

    // 右侧固定 inspector 面板避免遮挡左侧视野，也不随鼠标误拖动
    ImGui::SetNextWindowPos(ImVec2{panelX, 0.0F}, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2{panelWidth, io.DisplaySize.y}, ImGuiCond_Always);

    constexpr ImGuiWindowFlags PanelFlags =
        // inspector 固定为工具面板
        // 不保存 ImGui ini 状态
        // 每次启动都出现在相同位置
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("Parallel ROAM Inspector", nullptr, PanelFlags);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.92F, 0.96F, 0.94F, 1.0F});
    ImGui::TextUnformatted("Parallel ROAM");
    ImGui::PopStyleColor();
    // 右侧面板只保留会改变运行状态的交互控件
    ImGui::TextDisabled("地形与算法交互控制");

    DrawSectionHeader("性能测试");
    if (data.BenchmarkRunning)
    {
        ImGui::BeginDisabled();
    }
    const char* benchmarkPathItems[] = {"默认选项路径", "极限压力路径"};
    int benchmarkPathIndex =
        terrainState.BenchmarkPath == TerrainPanelState::RuntimeBenchmarkPath::BudgetSaturation ? 1 : 0;
    if (ImGui::Combo("Benchmark 路径", &benchmarkPathIndex, benchmarkPathItems, 2))
    {
        terrainState.BenchmarkPath = benchmarkPathIndex == 1
            ? TerrainPanelState::RuntimeBenchmarkPath::BudgetSaturation
            : TerrainPanelState::RuntimeBenchmarkPath::Default;
    }
    if (ImGui::Button("开始 Benchmark", ImVec2{-1.0F, 0.0F}))
    {
        // 按钮只写请求位，Application 下一帧统一启动状态机
        terrainState.StartBenchmarkRequested = true;
    }
    // VSync 只影响 swap interval，不应该触发 terrain renderer 重建
    ImGui::Checkbox("垂直同步限帧", &terrainState.VSyncEnabled);
    if (data.BenchmarkRunning)
    {
        ImGui::EndDisabled();
    }

    DrawSectionHeader("地形");

    const bool lockControls = data.BenchmarkRunning;
    if (lockControls)
    {
        // 测试期间锁定参数，确保同一轮内两种算法输入一致
        // 性能 overlay 仍可切换简略/详细，方便观察测试过程
        ImGui::BeginDisabled();
    }

    const char* heightMapItems[] = {"测试地形 129", "Peking 513"};
    // 下拉框 index 是 Application 高度图资源表的稳定键
    int heightMapIndex = std::clamp(terrainState.HeightMapIndex, 0, 1);
    if (ImGui::Combo("高度图", &heightMapIndex, heightMapItems, 2))
    {
        // 高度图切换由 Application 处理，不进入 terrain settings changed
        // 这样换图不会被误判为普通参数微调
        terrainState.HeightMapIndex = heightMapIndex;
    }
    changed |= ImGui::Checkbox("线框模式", &terrainState.Wireframe);
    int terrainModeIndex = TerrainModeIndex(terrainState.UseTerrainLod, terrainState.TerrainLodAlgorithm);
    const char* terrainModeItems[] = {"规则网格", "Classic CPU ROAM", "Data-Oriented CPU ROAM",
        "Transactional CPU LOD（实验）", "CBT 2024（GPU 参考）"};
    if (ImGui::BeginCombo("LOD 算法", terrainModeItems[terrainModeIndex]))
    {
        for (int index = 0; index < 5; ++index)
        {
            const auto id = TerrainAlgorithmFromModeIndex(index);
            const bool available = index == 0 || Algorithms::IsTerrainLodAlgorithmAvailable(id);
            const bool deviceSupported = index != 4 || terrainState.CbtUnavailableReason.empty();
            ImGui::BeginDisabled(!available || !deviceSupported);
            if (ImGui::Selectable(terrainModeItems[index], index == terrainModeIndex))
            {
                changed = true;
                terrainState.UseTerrainLod = index != 0;
                terrainState.TerrainLodAlgorithm = id;
            }
            ImGui::EndDisabled();
        }
        ImGui::EndCombo();
    }
    if (!terrainState.CbtUnavailableReason.empty())
    {
        ImGui::TextWrapped("%s", terrainState.CbtUnavailableReason.c_str());
    }
    const char* debugColorModes[] = {"关闭", "LOD 状态"};
    changed |= ImGui::Combo("调试着色", &terrainState.DebugColorMode, debugColorModes, 2);
    changed |= ImGui::SliderFloat("着色强度", &terrainState.DebugOverlayStrength, 0.0F, 1.0F, "%.2f");
    if (terrainState.DebugColorMode == 1)
    {
        DrawDebugColorLegend();
    }
    changed |= ImGui::SliderFloat("地形尺寸", &terrainState.TerrainSize, 6.0F, 80.0F, "%.1f");
    changed |= ImGui::SliderFloat("高度缩放", &terrainState.HeightScale, 0.0F, 12.0F, "%.2f");

    const bool transactional = terrainState.TerrainLodAlgorithm == Algorithms::TerrainLodAlgorithmId::TransactionalCpuLod;
    const bool cbt = terrainState.TerrainLodAlgorithm == Algorithms::TerrainLodAlgorithmId::Cbt2024;
    if (cbt)
    {
        DrawSectionHeader("CBT GPU 参考");
        changed |= ImGui::SliderFloat("目标面积 (px²)", &terrainState.Cbt.TriangleAreaPixels,
            0.1F, 128.0F, "%.3f", ImGuiSliderFlags_Logarithmic);
        constexpr std::array capacities{
            Algorithms::TerrainLodCbtCapacity::Capacity128K,
            Algorithms::TerrainLodCbtCapacity::Capacity256K,
            Algorithms::TerrainLodCbtCapacity::Capacity512K,
            Algorithms::TerrainLodCbtCapacity::Capacity1M};
        const char* capacityNames[] = {"128K", "256K", "512K", "1M"};
        int capacityIndex = 0;
        for (int index = 0; index < 4; ++index)
        {
            if (capacities[static_cast<std::size_t>(index)] == terrainState.Cbt.Capacity)
            {
                capacityIndex = index;
            }
        }
        if (ImGui::Combo("动态槽容量", &capacityIndex, capacityNames, 4))
        {
            terrainState.Cbt.Capacity = capacities[static_cast<std::size_t>(capacityIndex)];
            changed = true;
        }
        int validation = static_cast<int>(terrainState.Cbt.ValidationMode);
        const char* validationModes[] = {"关闭", "延迟验证", "阻塞诊断"};
        if (ImGui::Combo("CBT 验证", &validation, validationModes, 3))
        {
            terrainState.Cbt.ValidationMode = static_cast<Algorithms::TerrainLodCbtValidationMode>(validation);
            changed = true;
        }
        int geometry = static_cast<int>(terrainState.Cbt.GeometryMode);
        const char* geometryModes[] = {"增量几何", "全量诊断"};
        if (ImGui::Combo("几何更新", &geometry, geometryModes, 2))
        {
            terrainState.Cbt.GeometryMode = static_cast<Algorithms::TerrainLodCbtGeometryMode>(geometry);
            changed = true;
        }
        changed |= ImGui::Checkbox("暂停更新", &terrainState.CbtPaused);
        if (ImGui::Button("执行一轮"))
        {
            terrainState.LodStepRequested = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("重新初始化"))
        {
            terrainState.LodResetRequested = true;
        }
        ImGui::TextWrapped("容量限制动态槽池，不代表 ROAM 三角形预算；面积驱动独立细分。");
    }
    if (transactional)
    {
        DrawSectionHeader("事务化 LOD（实验）");
        int workers = static_cast<int>(terrainState.Transactional.WorkerCount);
        int prefix = static_cast<int>(terrainState.Transactional.PrefixLimit);
        int donors = static_cast<int>(terrainState.Transactional.DonorLimit);
        changed |= ImGui::SliderInt("线程", &workers, 1, 32);
        changed |= ImGui::SliderInt("需求前缀", &prefix, 1, 640);
        changed |= ImGui::SliderInt("回收前缀", &donors, 1, 640);
        terrainState.Transactional.WorkerCount = static_cast<std::size_t>(workers);
        terrainState.Transactional.PrefixLimit = static_cast<std::size_t>(prefix);
        terrainState.Transactional.DonorLimit = static_cast<std::size_t>(donors);
        changed |= ImGui::Checkbox("暂停更新", &terrainState.TransactionalPaused);
        if (ImGui::Button("执行一批")) terrainState.LodStepRequested = true;
        ImGui::SameLine();
        if (ImGui::Button("重新初始化")) terrainState.LodResetRequested = true;
        changed |= ImGui::SliderInt("三角形预算", &terrainState.RoamTriangleBudget, 2, 200000);
        ImGui::TextWrapped("预算、线程或前缀修改会重新初始化；当前视图认证不保证连续视图质量。");
    }
    DrawSectionHeader(cbt ? "层次深度" : (transactional ? "初始种子设置" : "ROAM"));
    ImGui::BeginDisabled(transactional || cbt);
    changed |= ImGui::Checkbox("局部约束", &terrainState.RoamEnableLocalConstraints);
    changed |= ImGui::Checkbox("拓扑验证", &terrainState.RoamEnableTopologyValidation);
    if (terrainState.TerrainLodAlgorithm == Algorithms::TerrainLodAlgorithmId::DataOrientedCpuRoam)
    {
        changed |= ImGui::Checkbox("并行 Split", &terrainState.RoamEnableParallelSplit);
    }
    ImGui::EndDisabled();
    changed |= ImGui::SliderInt("最大深度", &terrainState.RoamMaxDepth, 1, 20);
    if (terrainState.TerrainLodAlgorithm == Algorithms::TerrainLodAlgorithmId::ClassicCpuRoam ||
        terrainState.TerrainLodAlgorithm == Algorithms::TerrainLodAlgorithmId::DataOrientedCpuRoam)
    {
        changed |= ImGui::SliderFloat(
            "Split 误差 (px)",
            &terrainState.RoamScreenSpaceSplitThresholdPixels,
            0.25F,
            32.0F,
            "%.2f");
        changed |= ImGui::SliderFloat(
            "Merge 误差 (px)",
            &terrainState.RoamScreenSpaceMergeThresholdPixels,
            0.1F,
            32.0F,
            "%.2f");
        changed |= ImGui::SliderInt("Triangle Budget", &terrainState.RoamTriangleBudget, 2, 200000);
        terrainState.RoamScreenSpaceMergeThresholdPixels = std::min(
            terrainState.RoamScreenSpaceMergeThresholdPixels,
            terrainState.RoamScreenSpaceSplitThresholdPixels);
    }

    DrawSectionHeader("光照");
    changed |= ImGui::SliderFloat3("方向", &terrainState.LightDirection.x, -1.0F, 1.0F, "%.2f");
    changed |= ImGui::ColorEdit3("颜色", &terrainState.LightColor.x, ImGuiColorEditFlags_NoInputs);
    changed |= ImGui::SliderFloat("环境光", &terrainState.AmbientStrength, 0.0F, 1.0F, "%.2f");
    changed |= ImGui::SliderFloat("漫反射", &terrainState.DiffuseStrength, 0.0F, 2.0F, "%.2f");
    changed |= ImGui::SliderFloat("高光", &terrainState.SpecularStrength, 0.0F, 1.0F, "%.2f");
    if (lockControls)
    {
        ImGui::EndDisabled();
    }
    ImGui::End();

    return changed;
}

void ImGuiLayer::EndFrame(void* nativeCommandList)
{
    if (!_initialized)
    {
        return;
    }

    // ImGui::Render 只生成 draw data，真正绘制由选定图形后端完成
    ImGui::Render();
    if (_renderBackend == ImGuiRenderBackend::OpenGl)
    {
#if defined(PARALLEL_ROAM_GRAPHICS_API_OPENGL)
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#endif
    }
#if defined(PARALLEL_ROAM_GRAPHICS_API_D3D12)
    else if (_renderBackend == ImGuiRenderBackend::Direct3D12 && nativeCommandList != nullptr)
    {
        ImGui_ImplDX12_RenderDrawData(
            ImGui::GetDrawData(),
            static_cast<ID3D12GraphicsCommandList*>(nativeCommandList));
    }
#else
    (void)nativeCommandList;
#endif
}
} // 命名空间 ParallelRoam::Gui
