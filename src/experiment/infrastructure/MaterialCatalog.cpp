#include "experiment/infrastructure/MaterialCatalog.h"
#include <boost/property_tree/json_parser.hpp>
#include <cmath>
#include <set>
#include <stdexcept>

namespace ParallelRoam::Experiment::Infrastructure
{
std::vector<MaterialPreset> LoadMaterialCatalog(const std::filesystem::path& path)
{
    boost::property_tree::ptree tree;
    boost::property_tree::read_json(path.string(), tree);
    if (tree.get<int>("schemaVersion") != 1) throw std::runtime_error("材质目录版本错误");
    std::vector<MaterialPreset> result;
    std::set<std::string> ids;
    const auto root = std::filesystem::absolute(path).parent_path().parent_path().parent_path();
    for (const auto& item : tree.get_child("materials"))
    {
        const auto& value = item.second;
        MaterialPreset preset{value.get<std::string>("id"), value.get<std::string>("name"),
            root / value.get<std::string>("path"), value.get<float>("tiling"), value.get<float>("heightTint")};
        if (preset.Id.empty() || !ids.insert(preset.Id).second ||
            !std::isfinite(preset.Tiling) || preset.Tiling <= 0 || preset.Tiling > 128 ||
            !std::isfinite(preset.HeightTint) || preset.HeightTint < 0 || preset.HeightTint > 1 ||
            !std::filesystem::is_regular_file(preset.Path))
            throw std::runtime_error("材质预设无效: " + preset.Id);
        result.push_back(std::move(preset));
    }
    return result;
}
}
