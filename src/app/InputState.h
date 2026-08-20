#pragma once

#include <SDL.h>

#include <array>
#include <cstddef>

namespace ParallelRoam::App
{
/// <summary>
/// 由 SDL 事件转换得到的逐帧输入快照
/// </summary>
class InputState
{
public:
    void BeginFrame();

    void HandleEvent(const SDL_Event& event);
    void SetWindowSize(int width, int height);

    [[nodiscard]] bool IsQuitRequested() const;
    [[nodiscard]] bool IsKeyDown(SDL_Scancode scancode) const;
    [[nodiscard]] bool IsRightMouseDown() const;
    [[nodiscard]] float MouseDeltaX() const;
    [[nodiscard]] float MouseDeltaY() const;
    [[nodiscard]] int WindowWidth() const;
    [[nodiscard]] int WindowHeight() const;

private:
    // SDL scancode 可直接作为数组索引，避免每帧查 map
    static constexpr std::size_t KeyCount = static_cast<std::size_t>(SDL_NUM_SCANCODES);

    // 输入层只保存当前快照，不直接触发业务逻辑
    std::array<bool, KeyCount> _keys{};
    bool _quitRequested{false};
    bool _rightMouseDown{false};
    float _mouseDeltaX{0.0F};
    float _mouseDeltaY{0.0F};
    int _windowWidth{0};
    int _windowHeight{0};
};
} // 命名空间 ParallelRoam::App
