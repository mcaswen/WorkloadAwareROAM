#include "platform/Window.h"

#include <iostream>

namespace ParallelRoam::Platform
{
Window::~Window()
{
    Destroy();
}

bool Window::Initialize()
{
    if (_sdlInitialized)
    {
        return true;
    }

    // SDL main ready 在部分平台会影响 SDL 初始化入口行为
    SDL_SetMainReady();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_EVENTS) != 0)
    {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
        return false;
    }

    _sdlInitialized = true;
    return true;
}

bool Window::Create(const std::string& title, int width, int height, std::uint32_t windowFlags)
{
    if (!_sdlInitialized && !Initialize())
    {
        return false;
    }

    if (_window != nullptr)
    {
        std::cerr << "SDL window is already created.\n";
        return false;
    }

    _window = SDL_CreateWindow(
        title.c_str(),
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        width,
        height,
        windowFlags);

    if (_window == nullptr)
    {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << '\n';
        Destroy();
        return false;
    }

    RefreshSize();
    return true;
}

void Window::Destroy()
{
    // 先退出相对鼠标模式，避免窗口销毁后系统光标仍被捕获
    SetRelativeMouseMode(false);

    if (_window != nullptr)
    {
        SDL_DestroyWindow(_window);
        _window = nullptr;
    }

    if (_sdlInitialized)
    {
        SDL_Quit();
        _sdlInitialized = false;
    }

    _width = 0;
    _height = 0;
}

void Window::ProcessEvent(const SDL_Event& event)
{
    // RESIZED 和 SIZE_CHANGED 在不同平台触发时机不同
    // 两者统一刷新 cached 尺寸
    if (event.type == SDL_WINDOWEVENT &&
        (event.window.event == SDL_WINDOWEVENT_RESIZED || event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED))
    {
        RefreshSize();
    }
}

void Window::SetRelativeMouseMode(bool enabled)
{
    // 避免每帧重复调用 SDL_SetRelativeMouseMode 导致平台层抖动
    if (_relativeMouseMode == enabled)
    {
        return;
    }

    if (SDL_SetRelativeMouseMode(enabled ? SDL_TRUE : SDL_FALSE) == 0)
    {
        // 只有 SDL 调用成功后才更新缓存
        // 防止缓存状态和系统状态分叉
        _relativeMouseMode = enabled;
    }
    else
    {
        std::cerr << "SDL_SetRelativeMouseMode failed: " << SDL_GetError() << '\n';
    }
}

void Window::RefreshSize()
{
    if (_window == nullptr)
    {
        return;
    }

    SDL_GetWindowSize(_window, &_width, &_height);
}

SDL_Window* Window::NativeWindow() const
{
    return _window;
}

int Window::Width() const
{
    return _width;
}

int Window::Height() const
{
    return _height;
}

bool Window::IsValid() const
{
    return _window != nullptr;
}
} // namespace ParallelRoam::Platform
