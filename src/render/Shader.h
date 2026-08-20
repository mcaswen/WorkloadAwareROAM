#pragma once

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <string>

namespace ParallelRoam::Render
{
/// <summary>
/// OpenGL shader program 的小型 RAII 封装
/// </summary>
class Shader
{
public:
    Shader() = default;
    ~Shader();

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    bool Load(const char* vertexSource, const char* fragmentSource, std::string* errorMessage);
    void Destroy();
    void Use() const;
    void SetMat4(const char* uniformName, const glm::mat4& value) const;
    void SetVec3(const char* uniformName, const glm::vec3& value) const;
    void SetFloat(const char* uniformName, float value) const;
    void SetInt(const char* uniformName, int value) const;

    [[nodiscard]] unsigned int ProgramId() const;

private:
    // shader 编译失败时保留 OpenGL info log，避免只得到一个失败码
    static unsigned int CompileShader(unsigned int shaderType, const char* source, std::string* errorMessage);
    static std::string ReadShaderLog(unsigned int shaderId);
    static std::string ReadProgramLog(unsigned int programId);

    unsigned int _programId{0};
};
} // 命名空间 ParallelRoam::Render
