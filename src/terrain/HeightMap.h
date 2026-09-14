#pragma once

#include <filesystem>
#include <cstdint>
#include <string>
#include <vector>

namespace ParallelRoam::Terrain
{
/// <summary>
/// 保存归一化高度值的灰度 Height Map
/// </summary>
class HeightMap
{
public:
    bool LoadFromFile(const std::filesystem::path& filePath, std::string* errorMessage);

    /// <summary>
    /// 读取像素高度，坐标越界时会 clamp 到边界
    /// </summary>
    [[nodiscard]] float SamplePixel(int x, int y) const;

    /// <summary>
    /// 以归一化 UV 读取双线性高度
    /// </summary>
    [[nodiscard]] float SampleBilinear(float u, float v) const;

    [[nodiscard]] int Width() const;
    [[nodiscard]] int Height() const;
    [[nodiscard]] bool IsValid() const;
    [[nodiscard]] const std::filesystem::path& SourcePath() const;

    /// <summary>
    /// 原始整数与归一化采样来自同一次成功加载；引用有效至下次加载或销毁
    /// </summary>
    [[nodiscard]] const std::vector<std::uint16_t>& RawSamples() const { return _rawSamples; }
    [[nodiscard]] std::uint64_t SourceRevision() const { return _sourceRevision; }

private:
    std::vector<float> _heightValues;
    std::vector<std::uint16_t> _rawSamples;
    std::uint64_t _sourceRevision{};
    std::filesystem::path _sourcePath;
    int _width{0};
    int _height{0};
};
} // 命名空间 ParallelRoam::Terrain
