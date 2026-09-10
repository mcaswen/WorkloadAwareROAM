#include "algorithms/data_oriented_roam/DataOrientedRoamTerrainLodAlgorithm.h"
#include "algorithms/TerrainLodResultValidation.h"
#include "experiment/TerrainLodExperimentCsv.h"
#include "experiment/formal/FormalExperimentCamera.h"
#include "experiment/formal/FormalExperimentManifest.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace ParallelRoam;
using namespace Algorithms;
using namespace Experiment::Formal;
using Clock = std::chrono::steady_clock;

/// <summary>
/// 计时外检查网格属性与索引，并按三角形几何排序生成稳定身份
/// 不访问 DOD 内部状态；诊断关闭时也保留同等结果核查
/// </summary>
std::uint64_t ValidateGeometry(const TerrainLodRenderPacket& packet)
{
    const auto& mesh = *packet.ResolveCpuMesh();
    if (mesh.Indices.empty() || mesh.Indices.size() % 3U != 0U)
        throw std::runtime_error("网格索引不是完整三角形");
    std::vector<std::array<float, 27>> triangles;
    triangles.reserve(mesh.Indices.size() / 3U);
    for (std::size_t first = 0; first < mesh.Indices.size(); first += 3U)
    {
        std::array<float, 27> triangle{};
        for (std::size_t corner = 0; corner < 3U; ++corner)
        {
            const auto index = mesh.Indices[first + corner];
            if (index >= mesh.Vertices.size()) throw std::runtime_error("网格索引越界");
            const auto& vertex = mesh.Vertices[index];
            const std::array attributes{vertex.Position.x, vertex.Position.y, vertex.Position.z,
                vertex.Normal.x, vertex.Normal.y, vertex.Normal.z, vertex.TexCoord.x,
                vertex.TexCoord.y, vertex.Height};
            if (!std::all_of(attributes.begin(), attributes.end(), [](float value) { return std::isfinite(value); }))
                throw std::runtime_error("网格属性包含非有限值");
            std::copy(attributes.begin(), attributes.end(), triangle.begin() + static_cast<std::ptrdiff_t>(corner * 9U));
        }
        // 消除顶点循环起点差异，同时保留三角形绕序；反向绕序不会被误判为等价
        auto canonical = triangle;
        for (std::size_t rotation = 1; rotation < 3U; ++rotation)
        {
            auto rotated = triangle;
            std::rotate(rotated.begin(), rotated.begin() + static_cast<std::ptrdiff_t>(rotation * 9U), rotated.end());
            canonical = std::min(canonical, rotated);
        }
        triangles.push_back(canonical);
    }
    std::sort(triangles.begin(), triangles.end());
    std::uint64_t hash = TerrainLodHashOffset;
    AppendTerrainLodHash(hash, triangles.size());
    for (const auto& triangle : triangles)
        for (float value : triangle) AppendTerrainLodHash(hash, value == 0.0F ? 0.0F : value);
    return hash;
}

/// <summary>
/// 显式预设只覆盖执行动作，场景冻结的线程数量和并行门槛继续生效
/// </summary>
TerrainLodPassPolicy SelectPolicy(const std::string& name, const TerrainLodPassPolicy& frozen)
{
    auto result = frozen;
    TerrainLodPassPolicy actions;
    if (name == "serial-incremental") actions = MakeTerrainLodSerialIncrementalPolicy();
    else if (name == "maximum-parallel-incremental") actions = MakeTerrainLodMaximumParallelPolicy();
    else if (name != "default") throw std::runtime_error("未知执行策略");
    result.MergeScore = actions.MergeScore;
    result.SplitScore = actions.SplitScore;
    result.MergeTopology = actions.MergeTopology;
    result.SplitTopology = actions.SplitTopology;
    result.MeshEmit = actions.MeshEmit;
    result.CpuUpload = actions.CpuUpload;
    return result;
}

/// <summary>
/// 每次进程从空算法状态重放完整冻结轨迹，构建、外围验证和输出拥有独立计时边界
/// </summary>
void Run(const std::filesystem::path& root, const std::filesystem::path& scenariosPath,
    const std::filesystem::path& camerasPath, const std::string& scenarioId,
    const std::string& policy, const std::string& mode, const std::filesystem::path& outputPath)
{
    if (mode != "diagnostics-on" && mode != "diagnostics-off") throw std::runtime_error("未知诊断模式");
    const auto scenarios = LoadScenarioManifest(scenariosPath, root);
    const auto cameras = LoadCameraManifest(camerasPath, scenarios);
    const auto selected = std::find_if(scenarios.begin(), scenarios.end(),
        [&scenarioId](const auto& value) { return value.ScenarioId == scenarioId; });
    if (selected == scenarios.end()) throw std::runtime_error("场景编号不在清单中");
    const auto& scenario = *selected;
    Terrain::HeightMap heightMap;
    std::string error;
    if (!heightMap.LoadFromFile(scenario.HeightMapPath, &error)) throw std::runtime_error(error);
    TerrainLodSettings settings = scenario.Settings;
    settings.PassPolicy = SelectPolicy(policy, settings.PassPolicy);
    settings.EnablePassEvidence = mode == "diagnostics-on";
    settings.EnableTopologyValidation = false;
    settings.EnableTopologyPairEvidence = false;
    Algorithms::DataOrientedRoam::DataOrientedRoamTerrainLodAlgorithm implementation;
    ITerrainLodAlgorithm& algorithm = implementation;
    // 目录由运行器独占，探针拒绝覆盖已有结果；资产摘要由同一运行器另行核查
    if (std::filesystem::exists(outputPath)) throw std::runtime_error("输出 CSV 已存在");
    std::ofstream output(outputPath);
    if (!output) throw std::runtime_error("无法打开输出 CSV");
    output << std::setprecision(9);
    output << "probeProtocolVersion,diagnosticsMode,scenarioId,frameIndex,cameraPoseHash,viewInputHash,"
        "probeBuildInputHash,probeGeometryHash,probeValidationMilliseconds,buildWallMilliseconds,";
    Experiment::WriteTerrainLodSettingsCsvHeader(output);
    output << ',';
    Experiment::WriteTerrainLodStatsCsvHeader(output);
    output << ",passed\n";
    for (const auto& camera : cameras)
    {
        if (camera.ScenarioId != scenarioId) continue;
        TerrainLodBuildInput input{&heightMap, BuildCameraView(camera), settings};
        TerrainLodRenderPacket packet;
        const auto started = Clock::now();
        const bool built = algorithm.BuildRenderData(input, packet, &error);
        const auto stopped = Clock::now();
        if (!built) throw std::runtime_error(error);
        const auto& stats = algorithm.Stats();
        // 先验证资源契约再读取借用网格，下一次构建前完成所有校验和序列化
        if (!ValidateTerrainLodResult(stats, packet, settings.EnablePassEvidence).Passed)
            throw std::runtime_error("公共算法结果校验失败");
        const auto geometryHash = ValidateGeometry(packet);
        const auto inputHash = HashTerrainLodBuildInput(input);
        const auto validated = Clock::now();
        output << "1," << mode << ',' << scenarioId << ',' << camera.SampleIndex << ','
            << camera.CameraPoseHash << ',' << camera.ViewInputHash << ',' << inputHash << ',' << geometryHash << ','
            << std::chrono::duration<double, std::milli>(validated - stopped).count() << ','
            << std::chrono::duration<double, std::milli>(stopped - started).count() << ',';
        Experiment::WriteTerrainLodSettingsCsvValues(output, settings);
        output << ',';
        Experiment::WriteTerrainLodStatsCsvValues(output, stats);
        output << ",true\n";
        if (!output) throw std::runtime_error("写入 CSV 失败");
    }
    output.close();
    if (!output) throw std::runtime_error("关闭 CSV 失败");
}
}

int main(int argc, char** argv)
{
    try
    {
        if (argc != 8) throw std::runtime_error("参数：资产根目录 场景清单 相机清单 场景编号 执行策略 诊断模式 输出CSV");
        Run(argv[1], argv[2], argv[3], argv[4], argv[5], argv[6], argv[7]);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
