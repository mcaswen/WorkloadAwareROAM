#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace ParallelRoam::Experiment::Infrastructure
{
/// <summary>
/// 保存只影响观察方式的漫反射预设
/// 材质身份与地形、算法种子分别记录
/// </summary>
struct MaterialPreset
{
    std::string Id, Name;
    std::filesystem::path Path;
    float Tiling{12.0F};
    float HeightTint{0.35F};
};

[[nodiscard]] std::vector<MaterialPreset> LoadMaterialCatalog(const std::filesystem::path& path);
}
