#include "experiment/formal/FormalExperimentManifest.h"

#include "experiment/ExperimentCsvCodec.h"
#include "experiment/formal/FormalExperimentCamera.h"
#include "terrain/HeightMap.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>

namespace ParallelRoam::Experiment::Formal
{
namespace
{
const ExperimentCsvRow ScenarioHeader{
    "schemaVersion", "dataPurpose", "scenarioId", "terrainId", "heightMapPath", "terrainSize",
    "heightScale", "maxDepth", "splitPixels", "mergePixels", "triangleBudget", "trajectoryId",
    "analysisSplit", "sampleCount", "drawableWidth", "drawableHeight", "fovDegrees", "nearPlane",
    "farPlane", "localConstraints", "mergeScoreParallelMinimum", "splitScoreParallelMinimum",
    "splitTopologyParallelMinimum", "mergeTopologyParallelMinimum", "meshParallelMinimum",
    "serialWorkerCount", "parallelWorkerCount", "passWarmupCount", "passMeasuredRepeatCount",
    "heightMapWidth", "heightMapHeight", "heightMapSha256"};
const ExperimentCsvRow CameraHeader{
    "schemaVersion", "dataPurpose", "scenarioId", "terrainId", "trajectoryId", "sampleIndex",
    "positionX", "positionY", "positionZ", "targetX", "targetY", "targetZ", "upX", "upY", "upZ",
    "fovDegrees", "nearPlane", "farPlane", "drawableWidth", "drawableHeight", "depthConvention",
    "cameraPoseHash", "viewMatrixHash", "projectionNoHash", "viewInputHash"};
const ExperimentCsvRow TargetHeader{
    "schemaVersion", "dataPurpose", "scenarioId", "passId", "sampleIndex", "selectionStratum",
    "selectionRank", "selectionSeed", "primaryWorkValue", "featureVector", "replayInputHash",
    "selectionFeatureHash", "analysisSplit", "cameraPoseHash", "viewInputHash"};

void Require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

ExperimentCsvTable ReadTable(const std::filesystem::path& path, const ExperimentCsvRow& header)
{
    std::ifstream input{path, std::ios::binary};
    Require(input.is_open(), "Cannot open manifest: " + path.string());
    auto table = ReadExperimentCsv(input, path.string());
    RequireExperimentCsvHeader(table, header);
    Require(!table.Rows.empty(), "Empty manifest: " + path.string());
    return table;
}

// 字段游标只负责把当前 schema 的一行转换为值，不访问文件或解释实验协议
class RowReader
{
public:
    explicit RowReader(const ExperimentCsvRow& row) : _row{row}
    {
        Require(Unsigned() == CpuPilotSchemaVersion && Text() == "exploratory",
            "Unsupported CPU pilot schemaVersion or dataPurpose");
    }
    std::string Text() { return _row.at(_index++); }
    std::uint64_t Unsigned() { return ParseExperimentCsvUnsigned(Text()); }
    std::uint32_t Count()
    {
        const auto value = Unsigned();
        Require(value <= std::numeric_limits<std::uint32_t>::max(), "Manifest integer overflow");
        return static_cast<std::uint32_t>(value);
    }
    float Real() { return ParseExperimentCsvFloat(Text()); }
    glm::vec3 Vector() { const float x = Real(); const float y = Real(); return {x, y, Real()}; }

private:
    const ExperimentCsvRow& _row;
    std::size_t _index{0};
};

void ValidateScenario(const FormalScenario& scenario)
{
    const auto& settings = scenario.Settings;
    const bool small = scenario.TerrainId == "test129";
    Require(small || scenario.TerrainId == "peking547", "Unknown terrainId");
    const auto budget = settings.TriangleBudget;
    Require(small ? (budget == 512 || budget == 4096 || budget == 20000)
                  : (budget == 20000 || budget == 80000 || budget == 200000), "Invalid protocol budget");
    Require(scenario.ScenarioId == scenario.TerrainId + "-a-b" + std::to_string(budget), "Invalid scenarioId");
    Require(scenario.TrajectoryId == "A" && scenario.AnalysisSplit == "train", "Only trajectory A/train is enabled");
    Require(settings.TerrainSize == (small ? 30.0F : 80.0F) &&
        settings.HeightScale == (small ? 4.0F : 12.0F) && settings.MaxDepth == (small ? 14 : 20) &&
        settings.ScreenSpaceSplitThresholdPixels == (small ? 4.0F : 0.25F) &&
        settings.ScreenSpaceMergeThresholdPixels == (small ? 2.0F : 0.10F), "Terrain protocol mismatch");
    Require(scenario.SampleCount == 64 && scenario.DrawableWidth == 1280 && scenario.DrawableHeight == 720 &&
        scenario.FovDegrees == 60.0F && scenario.NearPlane == 0.1F && scenario.FarPlane == 500.0F,
        "Camera protocol mismatch");
    const auto& policy = settings.PassPolicy;
    Require(settings.EnableLocalConstraints && policy.MergeScoreMinParallelEntryCount == 0 &&
        policy.SplitScoreMinParallelEntryCount == 0 && policy.MergeTopologyMinParallelCandidateCount == 0 &&
        policy.SplitTopologyMinParallelCandidateCount == 0 && policy.MeshEmitMinParallelTriangleCount == 0 &&
        scenario.SerialWorkerCount == 1 && scenario.ParallelWorkerCount == 8 &&
        scenario.PassWarmupCount == 5 && scenario.PassMeasuredRepeatCount == 30, "CPU protocol mismatch");
    const std::string expectedHash = small
        ? "88e7688c5ee298e4df16a250ae37e82c1e48ae8e66c973facfcc0eb208d6448c"
        : "1b084bacf08e3cb67afc3744d98cc80c6dfb7d55280d93d02a3f93294e121031";
    Require(!scenario.HeightMapPath.empty() && scenario.HeightMapWidth == (small ? 129U : 547U) &&
        scenario.HeightMapHeight == scenario.HeightMapWidth && scenario.HeightMapSha256 == expectedHash,
        "Height map identity declaration mismatch");
}

FormalScenario ParseScenario(const ExperimentCsvRow& row)
{
    RowReader reader{row};
    FormalScenario scenario;
    scenario.ScenarioId = reader.Text();
    scenario.TerrainId = reader.Text();
    scenario.HeightMapPath = reader.Text();
    auto& settings = scenario.Settings;
    settings.TerrainSize = reader.Real();
    settings.HeightScale = reader.Real();
    const auto depth = reader.Count();
    Require(depth <= 30, "Invalid maximum depth");
    settings.MaxDepth = static_cast<int>(depth);
    settings.ScreenSpaceSplitThresholdPixels = reader.Real();
    settings.ScreenSpaceMergeThresholdPixels = reader.Real();
    settings.TriangleBudget = reader.Count();
    scenario.TrajectoryId = reader.Text();
    scenario.AnalysisSplit = reader.Text();
    scenario.SampleCount = reader.Count();
    scenario.DrawableWidth = reader.Count();
    scenario.DrawableHeight = reader.Count();
    scenario.FovDegrees = reader.Real();
    scenario.NearPlane = reader.Real();
    scenario.FarPlane = reader.Real();
    Require(reader.Text() == "true", "localConstraints must be true");
    settings.PassPolicy = Algorithms::MakeTerrainLodSerialIncrementalPolicy();
    settings.PassPolicy.MergeScoreMinParallelEntryCount = reader.Count();
    settings.PassPolicy.SplitScoreMinParallelEntryCount = reader.Count();
    settings.PassPolicy.SplitTopologyMinParallelCandidateCount = reader.Count();
    settings.PassPolicy.MergeTopologyMinParallelCandidateCount = reader.Count();
    settings.PassPolicy.MeshEmitMinParallelTriangleCount = reader.Count();
    scenario.SerialWorkerCount = reader.Count();
    scenario.ParallelWorkerCount = reader.Count();
    scenario.PassWarmupCount = reader.Count();
    scenario.PassMeasuredRepeatCount = reader.Count();
    scenario.HeightMapWidth = reader.Count();
    scenario.HeightMapHeight = reader.Count();
    scenario.HeightMapSha256 = reader.Text();
    ValidateScenario(scenario);
    // 来源流水线采用固定串行增量策略；计时副本的诊断/线程由执行阶段决定
    settings.EnableParallelSplit = false;
    settings.EnablePassEvidence = true;
    settings.EnableTopologyValidation = true;
    settings.PassPolicy.MergeScoreWorkerCount = scenario.SerialWorkerCount;
    settings.PassPolicy.SplitScoreWorkerCount = scenario.SerialWorkerCount;
    settings.PassPolicy.MergeTopologyWorkerCount = scenario.SerialWorkerCount;
    settings.PassPolicy.SplitTopologyWorkerCount = scenario.SerialWorkerCount;
    settings.PassPolicy.MeshEmitWorkerCount = scenario.SerialWorkerCount;
    return scenario;
}

std::map<std::string, const FormalScenario*> IndexScenarios(const std::vector<FormalScenario>& scenarios)
{
    std::map<std::string, const FormalScenario*> result;
    for (const auto& scenario : scenarios)
    {
        ValidateScenario(scenario);
        Require(result.emplace(scenario.ScenarioId, &scenario).second, "Duplicate scenario input");
    }
    Require(!result.empty(), "No selected scenarios");
    return result;
}

Algorithms::TerrainLodPassId ParsePass(const std::string& name)
{
    for (const auto pass : CpuPilotPassIds)
    {
        if (Algorithms::ToString(pass) == name) return pass;
    }
    throw std::runtime_error("Unsupported CPU passId: " + name);
}

void ValidateFeatureVector(std::string_view features)
{
    Require(!features.empty(), "Missing selection feature vector");
    for (;;)
    {
        const auto end = features.find(';');
        (void)ParseExperimentCsvFloat(features.substr(0, end));
        if (end == std::string_view::npos) return;
        features.remove_prefix(end + 1);
    }
}
} // 匿名命名空间

std::vector<FormalScenario> LoadScenarioManifest(const std::filesystem::path& path,
    const std::filesystem::path& assetRoot, const std::vector<std::string>& selectedIds)
{
    const auto table = ReadTable(path, ScenarioHeader);
    std::set<std::string> requested{selectedIds.begin(), selectedIds.end()};
    Require(requested.size() == selectedIds.size(), "Duplicate selected scenarioId");
    std::set<std::string> found;
    std::vector<FormalScenario> scenarios;
    for (std::size_t index = 0; index < table.Rows.size(); ++index)
    {
        try
        {
            auto scenario = ParseScenario(table.Rows[index]);
            Require(found.insert(scenario.ScenarioId).second, "Duplicate scenarioId");
            if (!requested.empty() && !requested.contains(scenario.ScenarioId)) continue;
            scenario.HeightMapPath = std::filesystem::absolute(assetRoot / scenario.HeightMapPath).lexically_normal();
            Terrain::HeightMap heightMap;
            std::string error;
            Require(heightMap.LoadFromFile(scenario.HeightMapPath, &error), error);
            Require(heightMap.Width() == static_cast<int>(scenario.HeightMapWidth) &&
                heightMap.Height() == static_cast<int>(scenario.HeightMapHeight), "Decoded height map dimensions mismatch");
            scenarios.push_back(std::move(scenario));
        }
        catch (const std::exception& error)
        {
            throw std::runtime_error(path.string() + ": record " + std::to_string(index + 2) + ": " + error.what());
        }
    }
    for (const auto& id : requested) Require(found.contains(id), "Unknown selected scenarioId: " + id);
    Require(!scenarios.empty(), "No selected scenarios");
    std::sort(scenarios.begin(), scenarios.end(), [](const auto& left, const auto& right) {
        return left.ScenarioId < right.ScenarioId;
    });
    return scenarios;
}

std::vector<CameraSample> LoadCameraManifest(const std::filesystem::path& path,
    const std::vector<FormalScenario>& scenarios)
{
    const auto table = ReadTable(path, CameraHeader);
    const auto indexed = IndexScenarios(scenarios);
    std::set<std::pair<std::string, std::uint32_t>> found;
    std::vector<CameraSample> samples;
    for (const auto& row : table.Rows)
    {
        RowReader reader{row};
        CameraSample sample;
        sample.ScenarioId = reader.Text();
        sample.TerrainId = reader.Text();
        sample.TrajectoryId = reader.Text();
        sample.SampleIndex = reader.Count();
        sample.Position = reader.Vector();
        sample.Target = reader.Vector();
        sample.Up = reader.Vector();
        sample.FovDegrees = reader.Real();
        sample.NearPlane = reader.Real();
        sample.FarPlane = reader.Real();
        sample.DrawableWidth = reader.Count();
        sample.DrawableHeight = reader.Count();
        Require(reader.Text() == "NO", "Only NO camera depth is enabled");
        sample.CameraPoseHash = reader.Unsigned();
        sample.ViewMatrixHash = reader.Unsigned();
        sample.ProjectionNoHash = reader.Unsigned();
        sample.ViewInputHash = reader.Unsigned();
        Require(indexed.contains(sample.ScenarioId), "Unknown camera scenarioId");
        Require(found.emplace(sample.ScenarioId, sample.SampleIndex).second, "Duplicate camera sample");
        ValidateCameraSample(sample, *indexed.at(sample.ScenarioId));
        samples.push_back(std::move(sample));
    }
    Require(samples.size() == scenarios.size() * CpuPilotSampleCount, "Incomplete camera manifest");
    std::sort(samples.begin(), samples.end(), [](const auto& left, const auto& right) {
        return std::tie(left.ScenarioId, left.SampleIndex) < std::tie(right.ScenarioId, right.SampleIndex);
    });
    return samples;
}

std::vector<TargetStateRef> LoadTargetManifest(const std::filesystem::path& path,
    const std::vector<FormalScenario>& scenarios, const std::vector<CameraSample>& cameras)
{
    const auto table = ReadTable(path, TargetHeader);
    const auto indexed = IndexScenarios(scenarios);
    std::map<std::pair<std::string, std::uint32_t>, const CameraSample*> cameraIndex;
    // 目标只引用部分采样点，但来源轨迹必须完整，不能用目标子集代替冻结相机输入
    for (const auto& camera : cameras)
    {
        Require(indexed.contains(camera.ScenarioId), "Unknown target camera scenario");
        ValidateCameraSample(camera, *indexed.at(camera.ScenarioId));
        Require(cameraIndex.emplace(std::make_pair(camera.ScenarioId, camera.SampleIndex), &camera).second,
            "Duplicate target camera");
    }
    // 编号范围与唯一性已逐项验证，总数相等才能保证每场景的 64 个点无缺失
    Require(cameraIndex.size() == scenarios.size() * CpuPilotSampleCount, "Incomplete target camera inputs");
    std::set<std::tuple<std::string, Algorithms::TerrainLodPassId, std::uint32_t>> keys;
    std::map<std::pair<std::string, Algorithms::TerrainLodPassId>, std::set<std::uint32_t>> ranks;
    std::vector<TargetStateRef> targets;
    for (const auto& row : table.Rows)
    {
        RowReader reader{row};
        TargetStateRef target;
        target.ScenarioId = reader.Text();
        target.PassId = ParsePass(reader.Text());
        target.SampleIndex = reader.Count();
        target.SelectionStratum = reader.Text();
        target.SelectionRank = reader.Count();
        target.SelectionSeed = reader.Count();
        target.PrimaryWorkValue = reader.Real();
        target.FeatureVector = reader.Text();
        target.ReplayInputHash = reader.Unsigned();
        target.SelectionFeatureHash = reader.Unsigned();
        target.AnalysisSplit = reader.Text();
        target.CameraPoseHash = reader.Unsigned();
        target.ViewInputHash = reader.Unsigned();
        // 采样点 0 包含从根三角形建立节点池和网格的首次成本，不进入冻结阶段目标选择
        Require(target.SampleIndex > 0 && target.SampleIndex < CpuPilotSampleCount &&
            target.SelectionRank < 8 && target.SelectionSeed == 20260830 && target.PrimaryWorkValue > 0 &&
            target.ReplayInputHash != 0 && target.SelectionFeatureHash != 0 && target.AnalysisSplit == "train",
            "Invalid target index, selection or identity");
        Require(target.SelectionStratum == "low" || target.SelectionStratum == "middle" ||
            target.SelectionStratum == "high" || target.SelectionStratum == "coverage", "Invalid selection stratum");
        ValidateFeatureVector(target.FeatureVector);
        const auto camera = cameraIndex.find({target.ScenarioId, target.SampleIndex});
        Require(camera != cameraIndex.end(), "Target camera reference missing");
        // 此处只绑定冻结相机身份，重放和选择特征哈希仍须由实际算法状态重建核对
        Require(target.CameraPoseHash == camera->second->CameraPoseHash &&
            target.ViewInputHash == camera->second->ViewInputHash, "Target camera identity mismatch");
        Require(keys.emplace(target.ScenarioId, target.PassId, target.SampleIndex).second, "Duplicate target state");
        Require(ranks[{target.ScenarioId, target.PassId}].insert(target.SelectionRank).second,
            "Duplicate target selection rank");
        targets.push_back(std::move(target));
    }
    for (const auto& [key, group] : ranks)
    {
        (void)key;
        // 集合已保证序号唯一；最小值为 0 且最大值加一等于数量即可证明序号连续
        Require(*group.begin() == 0 && *group.rbegin() + 1 == group.size(), "Incomplete target selection ranks");
    }
    std::sort(targets.begin(), targets.end(), [](const auto& left, const auto& right) {
        return std::tie(left.ScenarioId, left.PassId, left.SelectionRank) <
            std::tie(right.ScenarioId, right.PassId, right.SelectionRank);
    });
    return targets;
}

void WriteScenarioManifest(std::ostream& output, const std::vector<FormalScenario>& scenarios)
{
    WriteExperimentCsvRow(output, ScenarioHeader);
    for (const auto& scenario : scenarios)
    {
        ValidateScenario(scenario);
        const auto& settings = scenario.Settings;
        const auto& policy = settings.PassPolicy;
        WriteExperimentCsvRow(output, {"1", "exploratory", scenario.ScenarioId, scenario.TerrainId,
            scenario.HeightMapPath.generic_string(), FormatExperimentCsvFloat(settings.TerrainSize),
            FormatExperimentCsvFloat(settings.HeightScale), std::to_string(settings.MaxDepth),
            FormatExperimentCsvFloat(settings.ScreenSpaceSplitThresholdPixels),
            FormatExperimentCsvFloat(settings.ScreenSpaceMergeThresholdPixels), std::to_string(settings.TriangleBudget),
            scenario.TrajectoryId, scenario.AnalysisSplit, std::to_string(scenario.SampleCount),
            std::to_string(scenario.DrawableWidth), std::to_string(scenario.DrawableHeight),
            FormatExperimentCsvFloat(scenario.FovDegrees), FormatExperimentCsvFloat(scenario.NearPlane),
            FormatExperimentCsvFloat(scenario.FarPlane), "true",
            std::to_string(policy.MergeScoreMinParallelEntryCount), std::to_string(policy.SplitScoreMinParallelEntryCount),
            std::to_string(policy.SplitTopologyMinParallelCandidateCount), std::to_string(policy.MergeTopologyMinParallelCandidateCount),
            std::to_string(policy.MeshEmitMinParallelTriangleCount), std::to_string(scenario.SerialWorkerCount),
            std::to_string(scenario.ParallelWorkerCount), std::to_string(scenario.PassWarmupCount),
            std::to_string(scenario.PassMeasuredRepeatCount), std::to_string(scenario.HeightMapWidth),
            std::to_string(scenario.HeightMapHeight), scenario.HeightMapSha256});
    }
}

void WriteCameraManifest(std::ostream& output, const std::vector<CameraSample>& cameras)
{
    WriteExperimentCsvRow(output, CameraHeader);
    for (const auto& sample : cameras)
    {
        WriteExperimentCsvRow(output, {"1", "exploratory", sample.ScenarioId, sample.TerrainId, sample.TrajectoryId,
            std::to_string(sample.SampleIndex), FormatExperimentCsvFloat(sample.Position.x),
            FormatExperimentCsvFloat(sample.Position.y), FormatExperimentCsvFloat(sample.Position.z),
            FormatExperimentCsvFloat(sample.Target.x), FormatExperimentCsvFloat(sample.Target.y),
            FormatExperimentCsvFloat(sample.Target.z), FormatExperimentCsvFloat(sample.Up.x),
            FormatExperimentCsvFloat(sample.Up.y), FormatExperimentCsvFloat(sample.Up.z),
            FormatExperimentCsvFloat(sample.FovDegrees), FormatExperimentCsvFloat(sample.NearPlane),
            FormatExperimentCsvFloat(sample.FarPlane), std::to_string(sample.DrawableWidth),
            std::to_string(sample.DrawableHeight), "NO", std::to_string(sample.CameraPoseHash),
            std::to_string(sample.ViewMatrixHash), std::to_string(sample.ProjectionNoHash), std::to_string(sample.ViewInputHash)});
    }
}

void WriteTargetManifest(std::ostream& output, const std::vector<TargetStateRef>& targets)
{
    WriteExperimentCsvRow(output, TargetHeader);
    for (const auto& target : targets)
    {
        WriteExperimentCsvRow(output, {"1", "exploratory", target.ScenarioId, std::string{Algorithms::ToString(target.PassId)},
            std::to_string(target.SampleIndex), target.SelectionStratum, std::to_string(target.SelectionRank),
            std::to_string(target.SelectionSeed), FormatExperimentCsvFloat(target.PrimaryWorkValue), target.FeatureVector,
            std::to_string(target.ReplayInputHash), std::to_string(target.SelectionFeatureHash), target.AnalysisSplit,
            std::to_string(target.CameraPoseHash), std::to_string(target.ViewInputHash)});
    }
}
} // 命名空间 ParallelRoam::Experiment::Formal
