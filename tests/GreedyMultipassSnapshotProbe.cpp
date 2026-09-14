#include "algorithms/RoamGeometry.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamPipeline.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamPassInput.h"
#include "benchmark/formal/FormalCpuInput.h"
#include "experiment/formal/FormalExperimentCamera.h"
#include "experiment/formal/FormalExperimentManifest.h"
#include "experiment/greedy_transactional_lod/TransactionalScalingProtocol.h"

#include <stb_image.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>

namespace
{
using namespace ParallelRoam;
namespace Dod = Algorithms::DataOrientedRoam;
namespace Formal = Experiment::Formal;
using Clock = std::chrono::steady_clock;

double Milliseconds(Clock::time_point started)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - started).count();
}

/// <summary>
/// 来源导出只持有当前几何，不持有队列、目标或来源状态引用
/// 参数坐标使用精确 dyadic 键，共享点不靠 epsilon 焊接
/// </summary>
struct Vertex
{
    float U, V, Height;
};

std::uint64_t VertexId(const glm::vec2& uv)
{
    // 位宽由当前来源深度上限决定；编码必须可逆，不能把近邻点量化成同一点
    constexpr double Scale = 1048576.0;
    const auto u = static_cast<std::uint64_t>(std::llround(uv.x * Scale));
    const auto v = static_cast<std::uint64_t>(std::llround(uv.y * Scale));
    if (u > 1048576U || v > 1048576U || static_cast<double>(u) / Scale != uv.x ||
        static_cast<double>(v) / Scale != uv.y)
        throw std::runtime_error("来源坐标不是支持的精确 dyadic 值");
    return (u << 21U) | v;
}

void Capture(const Dod::DataOrientedRoamState& state, const Formal::FormalScenario& scenario,
    std::uint32_t index, const std::filesystem::path& path)
{
    const auto started = Clock::now();
    // 完整阶段输入身份覆盖行为状态；导出前后必须相同
    const auto before = Dod::HashDataOrientedRoamPassInput(state, Algorithms::TerrainLodPassId::SplitTopology);
    std::map<std::uint64_t, Vertex> vertices;
    for (const auto node : state.ActiveLeafNodes)
    {
        // 节点缓存不代表活动网格，只遍历观察点仍有效的叶集合
        const auto& domain = state.Nodes.DomainAt(node);
        for (const auto uv : {domain.A, domain.B, domain.C})
        {
            const auto height = state.HeightMap->SampleBilinear(uv.x, uv.y) * state.HeightScale;
            const auto [it, inserted] = vertices.emplace(VertexId(uv), Vertex{uv.x, uv.y, height});
            if (!inserted && it->second.Height != height)
                throw std::runtime_error("共享点高度不一致");
        }
    }
    // 流式写出自己的几何值；不读取尚未同步的增量输出网格
    // 足够的小数位用于恢复原 float 值，避免文本舍入改变相机或几何输入
    std::ofstream output(path);
    output << std::setprecision(17) << "{\"scenario\":\"" << scenario.ScenarioId
        << "\",\"sampleIndex\":" << index << ",\"budget\":" << state.Settings.TriangleBudget
        << ",\"size\":" << state.TerrainSize << ",\"scale\":" << state.HeightScale
        << ",\"splitPixels\":" << state.Settings.SplitThreshold
        << ",\"width\":" << state.DrawableWidth << ",\"height\":" << state.DrawableHeight
        << ",\"inputHash\":\"" << before << "\",\"matrix\":[";
    for (glm::length_t row = 0; row < 4; ++row)
        // 输出显式采用行序，离线模块无需猜测 GLM 的列主序存储
        for (glm::length_t column = 0; column < 4; ++column)
            output << (row == 0 && column == 0 ? "" : ",") << state.ViewProjection[column][row];
    output << "],\"vertices\":[";
    bool first = true;
    for (const auto& [id, vertex] : vertices)
    {
        output << (first ? "" : ",") << '[' << id << ',' << vertex.U << ',' << vertex.V << ',' << vertex.Height << ']';
        first = false;
    }
    output << "],\"faces\":[";
    first = true;
    for (const auto node : state.ActiveLeafNodes)
    {
        const auto& d = state.Nodes.DomainAt(node);
        output << (first ? "" : ",") << '[' << state.Nodes.PathIdAt(node) << ','
            << VertexId(d.A) << ',' << VertexId(d.B) << ',' << VertexId(d.C) << ']';
        first = false;
    }
    output << "],\"captureMs\":" << Milliseconds(started) << "}";
    output.close();
    if (!output) throw std::runtime_error("快照写入失败");
    // 哈希诊断也有成本，外层捕获计时覆盖两次身份核对及文件关闭
    if (before != Dod::HashDataOrientedRoamPassInput(state, Algorithms::TerrainLodPassId::SplitTopology))
        throw std::runtime_error("导出修改了来源状态");
}

void WriteSource(const Formal::FormalScenario& scenario, const std::filesystem::path& path)
{
    int width = 0, height = 0, channels = 0;
    // 独立参考保留原 uint16 值，不能从 float HeightMap 反推原始资产
    std::unique_ptr<stbi_us, decltype(&stbi_image_free)> pixels(
        stbi_load_16(scenario.HeightMapPath.string().c_str(), &width, &height, &channels, 1), stbi_image_free);
    if (!pixels) throw std::runtime_error("原始高度读取失败");
    // 两个冻结资产均为灰度图，stb 将低位深资产扩展到同一 uint16 范围
    std::ofstream output(path);
    output << "{\"width\":" << width << ",\"height\":" << height << ",\"values\":[";
    for (int i = 0; i < width * height; ++i) output << (i ? "," : "") << pixels.get()[i];
    output << "]}";
    if (!output) throw std::runtime_error("原始高度写入失败");
}
}

int main(int argc, char** argv)
{
    try
    {
        if (argc != 4) throw std::runtime_error("参数：资产根目录 baseline/capture 输出目录");
        const std::filesystem::path root{argv[1]}, directory{argv[3]};
        const std::string mode{argv[2]};
        using Scaling=Experiment::GreedyTransactionalLod::TransactionalScalingProtocol;
        const bool scaling=mode.starts_with("scaling-");
        const auto budget=scaling ? Scaling::Budget("peking547-sve-orbit64-b"+mode.substr(8)) : std::nullopt;
        if (mode != "baseline" && mode != "capture" && !budget) throw std::runtime_error("模式非法");
        if (std::filesystem::exists(directory)) throw std::runtime_error("拒绝覆盖已有输出");
        // 资产与相机清单须使用相同场景子集，保留原清单的全部姿态校验
        const auto scenarios = scaling ? std::vector{Scaling::Scenario(root,*budget,4)} :
            Formal::LoadScenarioManifest(root / "docs/parallel-roam/cpu-pilot-scenarios-v1.csv",
                root, {"test129-a-b4096", "peking547-a-b20000"});
        auto cameras = scaling ? std::vector<Formal::CameraSample>{} : Formal::LoadCameraManifest(
            root / "benchmark-output/roam-materialization/mpr-01/input-freeze/inputs/camera-samples.csv", scenarios);
        if (scaling)
            for (std::uint32_t index=0;index<=18;++index)
            { auto camera=Scaling::Camera(index);camera.ScenarioId=scenarios.front().ScenarioId;cameras.push_back(camera); }
        std::filesystem::create_directories(directory);
        if (scaling)
        {
            std::ofstream manifest(directory/"cameras.json");manifest<<std::setprecision(17)<<'[';
            for (std::size_t i=0;i<cameras.size();++i)
            {
                const auto& camera=cameras[i];const auto view=Formal::BuildCameraView(camera);
                manifest<<(i ? "," : "")<<"{\"sampleIndex\":"<<camera.SampleIndex<<",\"position\":["
                    <<camera.Position.x<<','<<camera.Position.y<<','<<camera.Position.z<<"],\"target\":["
                    <<camera.Target.x<<','<<camera.Target.y<<','<<camera.Target.z<<"],\"matrix\":[";
                for (glm::length_t row=0;row<4;++row)
                    for (glm::length_t column=0;column<4;++column)
                        manifest<<(row || column ? "," : "")<<view.ViewProjection[column][row];
                manifest<<"]}";
            }
            manifest<<']';if (!manifest) throw std::runtime_error("压力相机清单写入失败");
        }
        std::ofstream times(directory / "source-times.csv");
        times << "scenario,mode,buildMs,captureMs,finalInputHash\n" << std::setprecision(17);
        for (const auto& scenario : scenarios)
        {
            Terrain::HeightMap heightMap;
            std::string error;
            if (!heightMap.LoadFromFile(scenario.HeightMapPath, &error)) throw std::runtime_error(error);
            if (mode == "capture" || scaling) WriteSource(scenario, directory / (scenario.ScenarioId + "-source.json"));
            Dod::DataOrientedRoamPipeline pipeline;
            // 每场景独立恢复持续状态；不以复制旧结果替代原轨迹前缀
            auto settings = Benchmark::Formal::MakeCpuSourceSettings(scenario);
            if (scaling)
            {
                settings.PassPolicy=scenario.Settings.PassPolicy;
                settings.EnablePassEvidence=settings.EnableTopologyValidation=false;
                settings.EnableTopologyPairEvidence=false;
            }
            double captureMs = 0;
            const auto started = Clock::now();
            for (const auto& camera : cameras)
            {
                if (camera.ScenarioId != scenario.ScenarioId || camera.SampleIndex > 46) continue;
                if (scaling)
                {
                    if (camera.SampleIndex>14) continue;
                    // 完整 CPU 更新后再导出，与家族基线的公共帧边界对应
                    static_cast<void>(pipeline.Build(heightMap,80.0F,12.0F,Formal::BuildCameraView(camera),settings));
                    if (camera.SampleIndex==14)
                    {
                        const auto timer=Clock::now();
                        Capture(pipeline.State(),scenario,14,directory/(scenario.ScenarioId+"-14.json"));
                        captureMs+=Milliseconds(timer);
                    }
                    continue;
                }
                // 原管线只恢复固定来源前缀；回调不执行任何新目标发现
                static_cast<void>(pipeline.BuildWithPassObserver(heightMap, scenario.Settings.TerrainSize,
                    scenario.Settings.HeightScale, Formal::BuildCameraView(camera), settings,
                    [&](const auto& state, Algorithms::TerrainLodPassId pass) {
                        if (mode != "capture" || pass != Algorithms::TerrainLodPassId::SplitTopology ||
                            (camera.SampleIndex != 14 && camera.SampleIndex != 46)) return;
                        const auto timer = Clock::now();
                        Capture(state, scenario, camera.SampleIndex, directory /
                            (scenario.ScenarioId + "-" + std::to_string(camera.SampleIndex) + ".json"));
                        captureMs += Milliseconds(timer);
                    }));
            }
            const auto elapsed = Milliseconds(started);
            // 比较完整来源结果，确认观察入口没有扰动后续原执行
            const auto hash = Dod::HashDataOrientedRoamPassInput(pipeline.State(), Algorithms::TerrainLodPassId::SplitTopology);
            times << scenario.ScenarioId << ',' << mode << ',' << elapsed << ',' << captureMs << ',' << hash << '\n';
        }
        if (!times) throw std::runtime_error("来源计时写入失败");
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
