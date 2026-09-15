#include "render/OpenGlFrameCapture.h"
#include "render/OpenGlGraphicsBackend.h"

#include "gui/ImGuiLayer.h"

#include <glad/gl.h>

#include <iostream>
#include <string>

namespace ParallelRoam::Render
{
namespace
{
GLADapiproc LoadOpenGlProc(const char* name)
{
    // SDL 返回平台函数指针，GLAD 通过统一回调加载入口
    return reinterpret_cast<GLADapiproc>(SDL_GL_GetProcAddress(name));
}

const char* ImGuiGlslVersion()
{
    // 主分支 CPU terrain 路径统一使用 OpenGL 4.1 core。
    return "#version 410";
}

bool SetOpenGlAttribute(SDL_GLattr attribute, int value, const char* name, std::string* errorMessage)
{
    // 窗口创建前集中检查属性设置，避免得到配置不完整的 context
    if (SDL_GL_SetAttribute(attribute, value) == 0)
    {
        return true;
    }

    if (errorMessage != nullptr)
    {
        *errorMessage = std::string{"SDL_GL_SetAttribute failed for "} + name + ": " + SDL_GetError();
    }
    return false;
}
} // namespace

OpenGlGraphicsBackend::~OpenGlGraphicsBackend()
{
    Shutdown();
}

GraphicsApi OpenGlGraphicsBackend::Api() const
{
    return GraphicsApi::OpenGl;
}

const char* OpenGlGraphicsBackend::Name() const
{
    return "OpenGL";
}

bool OpenGlGraphicsBackend::ConfigureWindow(std::string* errorMessage)
{
    // 深度和 stencil 随默认 framebuffer 一次创建
    if (!SetOpenGlAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE, "profile", errorMessage) ||
        !SetOpenGlAttribute(SDL_GL_DOUBLEBUFFER, 1, "double buffer", errorMessage) ||
        !SetOpenGlAttribute(SDL_GL_DEPTH_SIZE, 24, "depth size", errorMessage) ||
        !SetOpenGlAttribute(SDL_GL_STENCIL_SIZE, 8, "stencil size", errorMessage))
    {
        return false;
    }

#if defined(__APPLE__)
    // Apple 要求 core context 同时声明 forward compatible
    return SetOpenGlAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4, "major version", errorMessage) &&
           SetOpenGlAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1, "minor version", errorMessage) &&
           SetOpenGlAttribute(
               SDL_GL_CONTEXT_FLAGS,
               SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG,
               "context flags",
               errorMessage);
#else
    // CPU terrain 路径与内置 shader 只要求 OpenGL 4.1 core。
    return SetOpenGlAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4, "major version", errorMessage) &&
           SetOpenGlAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1, "minor version", errorMessage) &&
           SetOpenGlAttribute(SDL_GL_CONTEXT_FLAGS, 0, "context flags", errorMessage);
#endif
}

std::uint32_t OpenGlGraphicsBackend::RequiredSdlWindowFlags() const
{
    return SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
}

bool OpenGlGraphicsBackend::Initialize(SDL_Window* window, std::string* errorMessage)
{
    // Window 保留所有权，本后端只在自身生命周期内借用
    if (window == nullptr)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "OpenGL backend requires a valid SDL window";
        }
        return false;
    }

    _window = window;
    _context = SDL_GL_CreateContext(_window);
    if (_context == nullptr)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = std::string{"SDL_GL_CreateContext failed: "} + SDL_GetError();
        }
        Shutdown();
        return false;
    }

    // GLAD 加载和后续所有 GL 调用依赖当前线程已绑定 context
    if (SDL_GL_MakeCurrent(_window, _context) != 0)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = std::string{"SDL_GL_MakeCurrent failed: "} + SDL_GetError();
        }
        Shutdown();
        return false;
    }

    // 函数加载失败后不能创建任何渲染资源
    const int version = gladLoadGL(LoadOpenGlProc);
    if (version == 0)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "gladLoadGL failed";
        }
        Shutdown();
        return false;
    }

    std::cout << "Graphics backend: OpenGL\n";
    std::cout << "OpenGL loaded: " << GLAD_VERSION_MAJOR(version) << '.' << GLAD_VERSION_MINOR(version) << '\n';
    std::cout << "OpenGL renderer: " << glGetString(GL_RENDERER) << '\n';
    std::cout << "OpenGL version: " << glGetString(GL_VERSION) << '\n';

    // 缓存字符串以供 benchmark 使用，避免 context 销毁后访问 GL 返回指针
    const auto* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const auto* versionString = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    _adapterName = renderer != nullptr ? renderer : "Unknown OpenGL adapter";
    _versionString = versionString != nullptr ? versionString : "Unknown OpenGL version";

    // 默认关闭 VSync，让性能基准反映真实吞吐而非刷新率上限
    if (!SetVSyncEnabled(false))
    {
        std::cerr << "OpenGL backend could not disable VSync during initialization.\n";
    }
    RefreshDrawableSize();
    return true;
}

bool OpenGlGraphicsBackend::InitializeImGui(Gui::ImGuiLayer& guiLayer, std::string* errorMessage)
{
    // ImGui 配置只借用窗口和 context，不转移所有权
    const Gui::ImGuiOpenGlBackendConfig config{
        .Window = _window,
        .Context = _context,
        .GlslVersion = ImGuiGlslVersion(),
    };
    if (guiLayer.Initialize(config))
    {
        return true;
    }

    if (errorMessage != nullptr)
    {
        *errorMessage = "Dear ImGui OpenGL backend initialization failed";
    }
    return false;
}

void OpenGlGraphicsBackend::WaitForGpuIdle()
{
    // OpenGL 没有本后端管理的显式 fence，销毁前使用全队列等待
    if (_context != nullptr)
    {
        glFinish();
    }
}

void OpenGlGraphicsBackend::Shutdown()
{
    _captureRequest.reset();
    _captureResult.reset();
    // SDL 窗口由 Window 拥有，context 必须先于窗口释放
    if (_context != nullptr)
    {
        SDL_GL_DeleteContext(_context);
        _context = nullptr;
    }

    _window = nullptr;
    _drawableWidth = 0;
    _drawableHeight = 0;
    _swapInterval = -1;
    _adapterName.clear();
    _versionString.clear();
}

void OpenGlGraphicsBackend::BeginFrame()
{
    // 默认 framebuffer 每帧由后端统一清除，地形渲染器只提交几何
    glClearColor(0.035F, 0.045F, 0.055F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void OpenGlGraphicsBackend::BeginImGuiFrame(Gui::ImGuiLayer& guiLayer)
{
    guiLayer.BeginFrame();
}

void OpenGlGraphicsBackend::RenderImGui(Gui::ImGuiLayer& guiLayer)
{
    guiLayer.EndFrame();
}

bool OpenGlGraphicsBackend::RequestFrameCapture(std::uint64_t id)
{
    if (!IsValid() || _captureRequest || _captureResult) return false;
    _captureRequest = id;
    return true;
}
std::optional<FrameCapture> OpenGlGraphicsBackend::TakeFrameCapture()
{
    auto result = std::move(_captureResult);
    _captureResult.reset();
    return result;
}

void OpenGlGraphicsBackend::Present()
{
    if (_window != nullptr)
    {
        if (_captureRequest)
        {
            _captureResult = CaptureOpenGlBackBuffer(*_captureRequest, _drawableWidth, _drawableHeight);
            _captureRequest.reset();
        }
        SDL_GL_SwapWindow(_window);
    }
}

void OpenGlGraphicsBackend::RefreshDrawableSize()
{
    if (_window == nullptr)
    {
        _drawableWidth = 0;
        _drawableHeight = 0;
        return;
    }

    // 逻辑窗口尺寸由 Window 管理，此处只保存渲染目标像素尺寸
    SDL_GL_GetDrawableSize(_window, &_drawableWidth, &_drawableHeight);
}

bool OpenGlGraphicsBackend::SetVSyncEnabled(bool enabled)
{
    const int requestedInterval = enabled ? 1 : 0;
    // 避免重复调用驱动交换间隔接口
    if (_swapInterval == requestedInterval)
    {
        return true;
    }

    // 失败时回读真实状态，保证 UI 不展示未应用的请求值
    if (SDL_GL_SetSwapInterval(requestedInterval) != 0)
    {
        std::cerr << "SDL_GL_SetSwapInterval failed: " << SDL_GetError() << '\n';
        _swapInterval = SDL_GL_GetSwapInterval();
        return false;
    }

    _swapInterval = requestedInterval;
    return true;
}

bool OpenGlGraphicsBackend::VSyncEnabled() const
{
    return _swapInterval > 0;
}

int OpenGlGraphicsBackend::DrawableWidth() const
{
    return _drawableWidth;
}

int OpenGlGraphicsBackend::DrawableHeight() const
{
    return _drawableHeight;
}

bool OpenGlGraphicsBackend::UsesZeroToOneDepth() const
{
    return false;
}

const std::string& OpenGlGraphicsBackend::AdapterName() const
{
    return _adapterName;
}

const std::string& OpenGlGraphicsBackend::VersionString() const
{
    return _versionString;
}

float OpenGlGraphicsBackend::LastGpuFrameMilliseconds() const
{
    // OpenGL 路径沿用算法自身计时，后端不额外插入整帧查询
    return 0.0F;
}

float OpenGlGraphicsBackend::LastGpuWaitMilliseconds() const
{
    // OpenGL 帧循环没有显式逐帧 fence 等待统计
    return 0.0F;
}

bool OpenGlGraphicsBackend::IsValid() const
{
    return _window != nullptr && _context != nullptr;
}
} // namespace ParallelRoam::Render
