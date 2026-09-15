#include "experiment/infrastructure/ExperimentCase.h"

#include <boost/property_tree/json_parser.hpp>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <stdexcept>
#include <string_view>

namespace ParallelRoam::Experiment::Infrastructure
{
namespace
{
void Require(bool condition, std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}
bool OneOf(const std::string& value, std::initializer_list<std::string_view> choices)
{
    return std::find(choices.begin(), choices.end(), value) != choices.end();
}
bool Hash(const std::string& value)
{
    return value.size() == 64 && value.find_first_not_of("0123456789abcdef") == std::string::npos;
}
}

ExperimentCase ExperimentCase::LoadResolved(const std::filesystem::path& path)
{
    boost::property_tree::ptree data;
    boost::property_tree::read_json(path.string(), data);
    Require(data.get<int>("schemaVersion") == 1, "不支持实验配置版本");
    ExperimentCase value;
    value.Id = data.get<std::string>("id");
    value.TerrainId = data.get<std::string>("terrain");
    value.HeightMapPath = data.get<std::string>("heightMap");
    value.FileSha256 = data.get<std::string>("fileSha256");
    value.SampleSha256 = data.get<std::string>("sampleSha256");
    value.Algorithm = data.get<std::string>("algorithm");
    value.HeightPolicy = data.get<std::string>("heightPolicy");
    value.Camera = data.get<std::string>("camera");
    value.Material = data.get<std::string>("material");
    value.Backend = data.get<std::string>("backend","opengl");
    Require(OneOf(value.Backend, {"opengl","d3d12"}), "未知投影后端");
    value.Mode = data.get<std::string>("mode");
    value.Prefix = data.get<std::string>("prefix");
    value.Width = data.get<std::uint32_t>("width");
    value.Height = data.get<std::uint32_t>("height");
    value.Budget = data.get<std::uint32_t>("budget");
    value.Workers = data.get<std::uint32_t>("workers");
    value.MaxDepth = data.get<std::uint32_t>("maxDepth");
    value.ViewWidth = data.get<std::uint32_t>("viewWidth");
    value.ViewHeight = data.get<std::uint32_t>("viewHeight");
    value.TerrainSize = data.get<float>("terrainSize");
    value.HeightScale = data.get<float>("heightScale");
    value.SplitPixels = data.get<float>("splitPixels");

    value.MergePixels = data.get<float>("mergePixels",value.SplitPixels*.5F);
    value.CameraFile = data.get<std::string>("cameraFile","");
    value.MaterialFile = data.get<std::string>("materialFile","");
    value.SampleFnv64 = data.get<std::uint64_t>("sampleFnv64",0);
    value.CameraFnv64 = data.get<std::uint64_t>("cameraFnv64",0);
    value.MaterialTiling = data.get<float>("materialTiling",12);
    value.MaterialTint = data.get<float>("materialTint",.35F);
    value.CaptureStride = data.get<std::uint32_t>("captureStride",4);
    value.Warmup = data.get<std::uint32_t>("warmup",3);
    value.MaxFrames = data.get<std::uint32_t>("maxFrames",10000);
    Require(std::isfinite(value.MergePixels) && value.MergePixels>0 && value.MergePixels<=value.SplitPixels,
        "合并阈值无效");
    Require(value.CaptureStride>=1 && value.CaptureStride<=10000 && value.MaxFrames>=1 &&
        value.MaxFrames<=10000 && value.Warmup<=1000,"回放范围无效");

    // 入口拒绝未知策略；拼写错误不能静默变成默认算法或放宽质量规则
    Require(!value.Id.empty() && !value.TerrainId.empty(), "实验身份为空");
    Require(OneOf(value.Algorithm, {"classic", "dod", "transactional"}), "未知算法");
    Require(OneOf(value.HeightPolicy, {"fit", "immutable"}), "未知高度策略");
    Require(OneOf(value.Mode, {"timing", "visual", "quality", "profile"}), "未知采集模式");
    Require(OneOf(value.Prefix, {"fixed64", "scaled"}), "未知前缀策略");
    Require(!value.Camera.empty() && !value.Material.empty(), "路线或材质缺失");
    Require(Hash(value.FileSha256) && Hash(value.SampleSha256), "资产哈希格式错误");
    Require(std::filesystem::is_regular_file(value.HeightMapPath), "高度图文件不存在");

    // 实际像素数由加载器再次核对；配置上限阻止意外启动超出首版规模的任务
    Require(value.Width >= 2 && value.Width <= 1025 && value.Height == value.Width, "无效地形尺寸");
    Require(value.Budget >= 2 && value.Budget <= 200000, "无效三角形预算");
    Require(value.Workers >= 1 && value.Workers <= 64, "无效线程数量");
    Require(value.MaxDepth >= 1 && value.MaxDepth <= 24, "无效细分深度");
    Require(value.ViewWidth >= 64 && value.ViewWidth <= 7680 &&
            value.ViewHeight >= 64 && value.ViewHeight <= 4320, "无效视口");
    Require(std::isfinite(value.TerrainSize) && value.TerrainSize > 0 &&
            std::isfinite(value.HeightScale) && value.HeightScale > 0 &&
            std::isfinite(value.SplitPixels) && value.SplitPixels > 0, "无效几何尺度");
    return value;
}
}
