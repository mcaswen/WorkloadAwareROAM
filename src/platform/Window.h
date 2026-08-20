#pragma once

#include <SDL.h>

#include <cstdint>
#include <string>

namespace ParallelRoam::Platform
{
/// <summary>
/// 只管理 SDL 生命周期和原生窗口，不持有图形 API 上下文
/// </summary>
class Window
{
public:
    Window() = default;
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool Initialize();
    bool Create(const std::string& title, int width, int height, std::uint32_t windowFlags);

    // 图形后端必须先释放仍在借用的原生窗口
    void Destroy();

    void ProcessEvent(const SDL_Event& event);
    void SetRelativeMouseMode(bool enabled);

    void RefreshSize();

    [[nodiscard]] SDL_Window* NativeWindow() const;
    [[nodiscard]] int Width() const;
    [[nodiscard]] int Height() const;
    [[nodiscard]] bool IsValid() const;

private:
    // SDL 资源由本类独占，图形后端只借用原生窗口指针
    SDL_Window* _window{nullptr};
    // 输入和 GUI 使用逻辑窗口尺寸而非渲染目标像素尺寸
    int _width{0};
    int _height{0};
    // 缓存系统实际接受的相对鼠标模式
    bool _relativeMouseMode{false};
    // 支持窗口创建失败后的幂等清理
    bool _sdlInitialized{false};
};
} // namespace ParallelRoam::Platform
