#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace ParallelRoam::Experiment::Infrastructure
{
/// <summary>
/// 资产目录中的运行描述，只保存来源引用与地形尺度
/// 图片内容不常驻本记录，实际样本由HeightMap持有
/// </summary>
struct TerrainAsset
{
    std::string Id;
    std::string Name;
    std::filesystem::path Path;
    std::string SampleSha256;
    float TerrainSize{};
    float HeightScale{};
    int Resolution{};
};

[[nodiscard]] std::vector<TerrainAsset> LoadTerrainAssetCatalog(const std::filesystem::path& path);
}
