#pragma once

#include "render/Shader.h"

#include <glm/glm.hpp>

#include <string>

namespace ParallelRoam::Render
{
/// <summary>
/// 单帧渲染上下文，承载相机矩阵和当前 drawable 尺寸
/// </summary>
struct TriangleRenderContext
{
    glm::mat4 View{1.0F};
    glm::mat4 Projection{1.0F};
    int DrawableWidth{1};
    int DrawableHeight{1};
};

/// <summary>
/// 用于验证 OpenGL 闭环的最小地形占位渲染器
/// </summary>
class TriangleRenderer
{
public:
    TriangleRenderer() = default;
    ~TriangleRenderer();

    TriangleRenderer(const TriangleRenderer&) = delete;
    TriangleRenderer& operator=(const TriangleRenderer&) = delete;

    bool Initialize(std::string* errorMessage);
    void Shutdown();
    void Render(const TriangleRenderContext& context);

private:
    // 这里保留验证渲染链路的最小状态
    Shader _shader;
    unsigned int _vertexArrayId{0};
    unsigned int _vertexBufferId{0};
};
} // 命名空间 ParallelRoam::Render
