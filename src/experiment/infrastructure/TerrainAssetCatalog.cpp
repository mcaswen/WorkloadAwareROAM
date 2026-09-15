#include "experiment/infrastructure/TerrainAssetCatalog.h"

#include <boost/property_tree/json_parser.hpp>
#include <cmath>
#include <set>
#include <stdexcept>

namespace ParallelRoam::Experiment::Infrastructure
{
std::vector<TerrainAsset> LoadTerrainAssetCatalog(const std::filesystem::path& path)
{
    boost::property_tree::ptree data;
    boost::property_tree::read_json(path.string(), data);
    if (data.get<int>("schemaVersion") != 1) throw std::runtime_error("不支持资产目录版本");
    const auto root = std::filesystem::absolute(path).parent_path().parent_path().parent_path();
    std::set<std::string> identities;
    std::vector<TerrainAsset> result;
    for (const auto& entry : data.get_child("terrains"))
    {
        const auto& row = entry.second;
        TerrainAsset asset;
        asset.Id = row.get<std::string>("id");
        asset.Name = row.get<std::string>("name");
        asset.Path = root / row.get<std::string>("path");
        asset.SampleSha256 = row.get<std::string>("sampleSha256");
        asset.TerrainSize = row.get<float>("terrainSize");
        asset.HeightScale = row.get<float>("heightScale");
        asset.Resolution = row.get<int>("width");
        if (!identities.insert(asset.Id).second || asset.Id.empty() ||
            asset.Resolution < 2 || asset.Resolution > 1025 ||
            asset.Resolution != row.get<int>("height") ||
            !std::isfinite(asset.TerrainSize) || asset.TerrainSize <= 0 ||
            !std::isfinite(asset.HeightScale) || asset.HeightScale <= 0 ||
            !std::filesystem::is_regular_file(asset.Path))
            throw std::runtime_error("无效资产条目: " + asset.Id);
        result.push_back(std::move(asset));
    }
    return result;
}
}
