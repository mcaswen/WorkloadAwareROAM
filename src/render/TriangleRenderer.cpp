#include "render/TriangleRenderer.h"

#include <glad/gl.h>

#include <array>
#include <cstddef>

namespace ParallelRoam::Render
{
namespace
{
// 顶点格式只保留位置和颜色
// 这个 renderer 只用于早期 OpenGL 闭环
struct Vertex
{
    glm::vec3 Position;
    glm::vec3 Color;
};

// terrain renderer 接入后仍保留
// 方便排查窗口或 shader 基础链路问题
// shader 内嵌在 cpp 中，terrain renderer 使用 assets/shaders 的资源加载
constexpr const char* VertexShaderSource = R"(
#version 410 core
layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec3 aColor;

uniform mat4 uView;
uniform mat4 uProjection;

out vec3 vColor;

void main()
{
    vColor = aColor;
    gl_Position = uProjection * uView * vec4(aPosition, 1.0);
}
)";

constexpr const char* FragmentShaderSource = R"(
#version 410 core
in vec3 vColor;
out vec4 FragColor;

void main()
{
    FragColor = vec4(vColor, 1.0);
}
)";
} // 匿名命名空间

TriangleRenderer::~TriangleRenderer()
{
    Shutdown();
}

bool TriangleRenderer::Initialize(std::string* errorMessage)
{
    // shader 初始化先于 buffer 创建，失败时不留下半初始化的 GL 对象
    if (!_shader.Load(VertexShaderSource, FragmentShaderSource, errorMessage))
    {
        return false;
    }

    // 三角形放在 XZ 平面上，用来模拟最小地面片
    const std::array<Vertex, 3> vertices{
        Vertex{glm::vec3{-3.0F, 0.0F, -2.0F}, glm::vec3{0.1F, 0.7F, 0.3F}},
        Vertex{glm::vec3{3.0F, 0.0F, -2.0F}, glm::vec3{0.2F, 0.5F, 0.95F}},
        Vertex{glm::vec3{0.0F, 0.0F, 3.0F}, glm::vec3{0.95F, 0.78F, 0.25F}},
    };

    glGenVertexArrays(1, &_vertexArrayId);
    glGenBuffers(1, &_vertexBufferId);

    glBindVertexArray(_vertexArrayId);
    glBindBuffer(GL_ARRAY_BUFFER, _vertexBufferId);

    // 顶点数据不会变化，因此使用 GL_STATIC_DRAW
    // 这里不走 TerrainMeshVertex
    // 避免最小 smoke 路径依赖后续 terrain 数据结构
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)),
        vertices.data(),
        GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(
        0,
        3,
        GL_FLOAT,
        GL_FALSE,
        static_cast<GLsizei>(sizeof(Vertex)),
        reinterpret_cast<const void*>(offsetof(Vertex, Position)));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1,
        3,
        GL_FLOAT,
        GL_FALSE,
        static_cast<GLsizei>(sizeof(Vertex)),
        reinterpret_cast<const void*>(offsetof(Vertex, Color)));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    return true;
}

void TriangleRenderer::Shutdown()
{
    // VBO 和 VAO 独立删除，便于后续 renderer 拆出更多 buffer
    // 删除后清零保证重复 Shutdown 安全
    if (_vertexBufferId != 0)
    {
        glDeleteBuffers(1, &_vertexBufferId);
        _vertexBufferId = 0;
    }

    if (_vertexArrayId != 0)
    {
        glDeleteVertexArrays(1, &_vertexArrayId);
        _vertexArrayId = 0;
    }

    _shader.Destroy();
}

void TriangleRenderer::Render(const TriangleRenderContext& context)
{
    // viewport 每帧使用 drawable 尺寸刷新，支持窗口拖拽和 HiDPI
    // 即使三角形本身静态
    // 输出尺寸仍必须跟随窗口变化
    glViewport(0, 0, context.DrawableWidth, context.DrawableHeight);
    glEnable(GL_DEPTH_TEST);

    _shader.Use();
    _shader.SetMat4("uView", context.View);
    _shader.SetMat4("uProjection", context.Projection);

    glBindVertexArray(_vertexArrayId);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}
} // 命名空间 ParallelRoam::Render
