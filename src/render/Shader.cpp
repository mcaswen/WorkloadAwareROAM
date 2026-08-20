#include "render/Shader.h"

#include <glm/gtc/type_ptr.hpp>

#include <utility>

namespace ParallelRoam::Render
{
// Shader 封装的关键语义是失败不破坏旧 program
// Load 只有在新 program 编译和链接都成功后才替换 _programId
// 因此 renderer 可以在 shader 重载失败后继续使用旧状态
Shader::~Shader()
{
    Destroy();
}

bool Shader::Load(const char* vertexSource, const char* fragmentSource, std::string* errorMessage)
{
    // 顶点和片元 shader 分开编译，便于定位具体失败环节
    const unsigned int vertexShader = CompileShader(GL_VERTEX_SHADER, vertexSource, errorMessage);
    if (vertexShader == 0)
    {
        return false;
    }

    // fragment shader 失败时要清理已成功创建的 vertex shader
    const unsigned int fragmentShader = CompileShader(GL_FRAGMENT_SHADER, fragmentSource, errorMessage);
    if (fragmentShader == 0)
    {
        glDeleteShader(vertexShader);
        return false;
    }

    const unsigned int programId = glCreateProgram();
    // attach 后 shader object 生命周期仍由本函数负责
    // link 完成后无论成功失败都不再需要 shader object
    glAttachShader(programId, vertexShader);
    glAttachShader(programId, fragmentShader);
    glLinkProgram(programId);

    // shader object 链接进 program 后即可删除，program 会保留链接结果
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    int success = 0;
    glGetProgramiv(programId, GL_LINK_STATUS, &success);
    if (success == 0)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "OpenGL shader link failed:\n" + ReadProgramLog(programId);
        }
        glDeleteProgram(programId);
        return false;
    }

    // 新 program 链接成功后再替换旧 program，避免失败时破坏已有状态
    Destroy();
    _programId = programId;
    return true;
}

void Shader::Destroy()
{
    if (_programId != 0)
    {
        // Destroy 可重复调用
        // program id 清零后后续析构不会二次删除
        glDeleteProgram(_programId);
        _programId = 0;
    }
}

void Shader::Use() const
{
    glUseProgram(_programId);
}

void Shader::SetMat4(const char* uniformName, const glm::mat4& value) const
{
    // 缺失 uniform 可能是 shader 编译器优化导致，直接跳过即可
    const int location = glGetUniformLocation(_programId, uniformName);
    if (location >= 0)
    {
        glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(value));
    }
}

void Shader::SetVec3(const char* uniformName, const glm::vec3& value) const
{
    // SetMat4 已说明 uniform 被优化掉的情况
    // 其它 setter 保持同一容错语义
    const int location = glGetUniformLocation(_programId, uniformName);
    if (location >= 0)
    {
        glUniform3fv(location, 1, glm::value_ptr(value));
    }
}

void Shader::SetFloat(const char* uniformName, float value) const
{
    // uniform 查询放在 setter 内
    // 当前 shader 数量小
    // 先保持调用方简单
    const int location = glGetUniformLocation(_programId, uniformName);
    if (location >= 0)
    {
        glUniform1f(location, value);
    }
}

void Shader::SetInt(const char* uniformName, int value) const
{
    const int location = glGetUniformLocation(_programId, uniformName);
    if (location >= 0)
    {
        glUniform1i(location, value);
    }
}

unsigned int Shader::ProgramId() const
{
    return _programId;
}

unsigned int Shader::CompileShader(unsigned int shaderType, const char* source, std::string* errorMessage)
{
    // OpenGL 接收源码指针数组，这里只有单段内置 shader 字符串
    const unsigned int shaderId = glCreateShader(shaderType);
    glShaderSource(shaderId, 1, &source, nullptr);
    glCompileShader(shaderId);

    int success = 0;
    glGetShaderiv(shaderId, GL_COMPILE_STATUS, &success);
    if (success == 0)
    {
        // 编译失败的 shader id 不能泄漏
        // 错误日志在删除前读取
        if (errorMessage != nullptr)
        {
            *errorMessage = "OpenGL shader compile failed:\n" + ReadShaderLog(shaderId);
        }
        glDeleteShader(shaderId);
        return 0;
    }

    return shaderId;
}

std::string Shader::ReadShaderLog(unsigned int shaderId)
{
    // GL_INFO_LOG_LENGTH 包含结尾空字符，因此小于等于 1 代表没有有效日志
    int logLength = 0;
    glGetShaderiv(shaderId, GL_INFO_LOG_LENGTH, &logLength);

    if (logLength <= 1)
    {
        return {};
    }

    std::string log(static_cast<std::size_t>(logLength), '\0');
    // OpenGL 会写入尾部空字符
    // std::string 保留它不影响错误输出
    glGetShaderInfoLog(shaderId, logLength, nullptr, log.data());
    return log;
}

std::string Shader::ReadProgramLog(unsigned int programId)
{
    // program link log 和 shader compile log 分开读取，便于输出准确环节
    int logLength = 0;
    glGetProgramiv(programId, GL_INFO_LOG_LENGTH, &logLength);

    if (logLength <= 1)
    {
        return {};
    }

    std::string log(static_cast<std::size_t>(logLength), '\0');
    glGetProgramInfoLog(programId, logLength, nullptr, log.data());
    return log;
}
} // 命名空间 ParallelRoam::Render
