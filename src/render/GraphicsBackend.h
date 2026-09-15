#pragma once

#include "render/FrameCapture.h"
#include <optional>
#include <cstdint>
#include <memory>
#include <string>

struct SDL_Window;

namespace ParallelRoam::Gui
{
class ImGuiLayer;
}

namespace ParallelRoam::Render
{
/// <summary>
/// 构建配置可选择的图形 API
/// </summary>
enum class GraphicsApi
{
    OpenGl,
    Direct3D12,
};

/// <summary>
/// 设备原生计算能力；未查询的后端保留零值，算法自行解释运行要求
/// </summary>
struct GraphicsDeviceCapabilities
{
    std::uint32_t ShaderModelMajor{0U};
    std::uint32_t ShaderModelMinor{0U};
    bool SupportsShaderInt64{false};
    bool SupportsTypedResourceInt64Atomics{false};
};

/// <summary>
/// 应用主循环与具体图形 API 之间的生命周期边界
/// </summary>
class IGraphicsBackend
{
public:
    virtual ~IGraphicsBackend() = default;

    [[nodiscard]] virtual GraphicsApi Api() const = 0;
    [[nodiscard]] virtual const char* Name() const = 0;

    [[nodiscard]] virtual bool ConfigureWindow(std::string* errorMessage) = 0;
    [[nodiscard]] virtual std::uint32_t RequiredSdlWindowFlags() const = 0;

    [[nodiscard]] virtual bool Initialize(SDL_Window* window, std::string* errorMessage) = 0;
    [[nodiscard]] virtual bool InitializeImGui(Gui::ImGuiLayer& guiLayer, std::string* errorMessage) = 0;
    virtual void WaitForGpuIdle() = 0;
    virtual void Shutdown() = 0;

    virtual void BeginFrame() = 0;
    virtual void BeginImGuiFrame(Gui::ImGuiLayer& guiLayer) = 0;
    virtual void RenderImGui(Gui::ImGuiLayer& guiLayer) = 0;
    virtual void Present() = 0;
    // 只允许一个待处理请求；结果在Present完成后领取
    virtual bool RequestFrameCapture(std::uint64_t id) = 0;
    [[nodiscard]] virtual std::optional<FrameCapture> TakeFrameCapture() = 0;
    virtual void RefreshDrawableSize() = 0;

    [[nodiscard]] virtual bool SetVSyncEnabled(bool enabled) = 0;
    [[nodiscard]] virtual bool VSyncEnabled() const = 0;
    [[nodiscard]] virtual int DrawableWidth() const = 0;
    [[nodiscard]] virtual int DrawableHeight() const = 0;
    [[nodiscard]] virtual bool UsesZeroToOneDepth() const = 0;
    [[nodiscard]] virtual const std::string& AdapterName() const = 0;
    [[nodiscard]] virtual const std::string& VersionString() const = 0;
    [[nodiscard]] virtual const GraphicsDeviceCapabilities& GraphicsCapabilities() const
    {
        static const GraphicsDeviceCapabilities unavailable{};
        return unavailable;
    }
    [[nodiscard]] virtual float LastGpuFrameMilliseconds() const = 0;
    [[nodiscard]] virtual float LastGpuWaitMilliseconds() const = 0;
    [[nodiscard]] virtual bool IsValid() const = 0;
};

[[nodiscard]] std::unique_ptr<IGraphicsBackend> CreateConfiguredGraphicsBackend();
[[nodiscard]] const char* ConfiguredGraphicsApiName();
} // namespace ParallelRoam::Render
