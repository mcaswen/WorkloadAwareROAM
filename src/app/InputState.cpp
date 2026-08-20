#include "app/InputState.h"

namespace ParallelRoam::App
{
// InputState 是帧快照
// 它不派发业务命令
// Application 每帧读取快照后再决定相机和窗口行为
void InputState::BeginFrame()
{
    // 鼠标移动量是事件累积值，每帧开始前归零
    _mouseDeltaX = 0.0F;
    _mouseDeltaY = 0.0F;
}

void InputState::HandleEvent(const SDL_Event& event)
{
    // SDL event 在同一帧内按到达顺序合并
    // 最终状态由按键数组和鼠标增量表达
    switch (event.type)
    {
    case SDL_QUIT:
        _quitRequested = true;
        break;
    case SDL_KEYDOWN:
        // 忽略按键自动重复，避免按住键时产生无意义的重复状态写入
        if (event.key.repeat == 0)
        {
            const auto index = static_cast<std::size_t>(event.key.keysym.scancode);
            if (index < _keys.size())
            {
                _keys[index] = true;
            }
        }
        break;
    case SDL_KEYUP:
    {
        // keyup 不检查 repeat
        // SDL 的 repeat 只对 keydown 有意义
        const auto index = static_cast<std::size_t>(event.key.keysym.scancode);
        if (index < _keys.size())
        {
            _keys[index] = false;
        }
        break;
    }
    case SDL_MOUSEMOTION:
        // 同一帧可能有多个 motion 事件，需要累积成一份帧快照
        _mouseDeltaX += static_cast<float>(event.motion.xrel);
        _mouseDeltaY += static_cast<float>(event.motion.yrel);
        break;
    case SDL_MOUSEBUTTONDOWN:
        if (event.button.button == SDL_BUTTON_RIGHT)
        {
            _rightMouseDown = true;
        }
        break;
    case SDL_MOUSEBUTTONUP:
        if (event.button.button == SDL_BUTTON_RIGHT)
        {
            _rightMouseDown = false;
        }
        break;
    case SDL_WINDOWEVENT:
        // RESIZED 和 SIZE_CHANGED 在不同平台触发时机略有差异，这里统一处理
        if (event.window.event == SDL_WINDOWEVENT_RESIZED || event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
        {
            SetWindowSize(event.window.data1, event.window.data2);
        }
        break;
    default:
        break;
    }
}

void InputState::SetWindowSize(int width, int height)
{
    // 保存逻辑尺寸
    // drawable 尺寸由 Window 单独提供给 renderer
    _windowWidth = width;
    _windowHeight = height;
}

bool InputState::IsQuitRequested() const
{
    return _quitRequested;
}

bool InputState::IsKeyDown(SDL_Scancode scancode) const
{
    // 外部传入的 scancode 理论上来自 SDL，但边界检查能隔离异常输入
    const auto index = static_cast<std::size_t>(scancode);
    return index < _keys.size() && _keys[index];
}

bool InputState::IsRightMouseDown() const
{
    return _rightMouseDown;
}

float InputState::MouseDeltaX() const
{
    return _mouseDeltaX;
}

float InputState::MouseDeltaY() const
{
    return _mouseDeltaY;
}

int InputState::WindowWidth() const
{
    return _windowWidth;
}

int InputState::WindowHeight() const
{
    return _windowHeight;
}
} // 命名空间 ParallelRoam::App
