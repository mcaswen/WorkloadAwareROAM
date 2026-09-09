#include "experiment/ExperimentCsvCodec.h"
#include "experiment/formal/FormalExperimentCamera.h"
#include "experiment/formal/FormalExperimentCsv.h"
#include "experiment/formal/FormalExperimentManifest.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace
{
using namespace ParallelRoam::Experiment;
using namespace ParallelRoam::Experiment::Formal;
namespace Algorithms = ParallelRoam::Algorithms;

void Expect(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

template <typename Action>
void Reject(Action action, const char* message)
{
    try { action(); }
    catch (const std::runtime_error&) { return; }
    throw std::runtime_error(message);
}

ExperimentCsvTable ReadText(const std::string& text)
{
    std::istringstream input{text};
    return ReadExperimentCsv(input, "test");
}

void Set(ExperimentCsvTable& table, const std::string& name, const std::string& value)
{
    const auto column = std::find(table.Header.begin(), table.Header.end(), name);
    Expect(column != table.Header.end(), "Missing test column: " + name);
    table.Rows.front()[static_cast<std::size_t>(column - table.Header.begin())] = value;
}

void WriteTable(const std::filesystem::path& path, const ExperimentCsvTable& table)
{
    std::ofstream output{path, std::ios::binary};
    WriteExperimentCsvRow(output, table.Header);
    for (const auto& row : table.Rows) WriteExperimentCsvRow(output, row);
}

template <typename Writer>
ExperimentCsvTable Capture(Writer writer)
{
    std::ostringstream output;
    writer(output);
    return ReadText(output.str());
}

void CheckCodec()
{
    const ExperimentCsvRow fields{"comma,value", "two\"quotes", "line\r\nnext", "中文", ""};
    std::ostringstream output;
    WriteExperimentCsvRow(output, {"a", "b", "c", "d", "e"});
    WriteExperimentCsvRow(output, fields);
    Expect(ReadText(output.str()).Rows.front() == fields, "CSV escaping round trip mismatch");
    Expect(ReadText("\xEF\xBB\xBF" "a,b\r\n1,2\r\n").Rows.size() == 1, "BOM/CRLF not supported");
    for (const std::string bad : {"", "a,a\n1,2\n", "a,b\n1\n", "a\n\"unclosed", "a\nx\"q\n",
        "a\n\"x\"tail\n", "a\rx", "a,b\n\n", "a\n\xC0\xAF\n", "a\n\xED\xA0\x80\n", "a\n\xF4\x90\x80\x80\n"})
        Reject([&] { (void)ReadText(bad); }, "Malformed CSV accepted");
    Expect(ReadText("a\n\n").Rows.front() == ExperimentCsvRow{""}, "An empty CSV field is a valid lexical value");
    for (const std::string bad : {"", "-1", "+1", "1x", "1.5", "18446744073709551616", " 1"})
        Reject([&] { (void)ParseExperimentCsvUnsigned(bad); }, "Invalid unsigned integer accepted");
    for (const std::string bad : {"nan", "inf", "-inf", "1e999", "1x", " 1", "", "1e-999"})
        Reject([&] { (void)ParseExperimentCsvFloat(bad); }, "Invalid float accepted");
    Expect(ParseExperimentCsvUnsigned("18446744073709551615") == UINT64_MAX, "Hash precision lost");
}

void CheckRecords()
{
    CpuDiscoveryRecord discovery;
    discovery.ScenarioId = "test129-a-b512";
    discovery.Status = CpuRecordStatus::Failed;
    discovery.Failure = "missing, input\nsecond line";
    const auto table = Capture([&](auto& output) {
        WriteCpuDiscoveryCsvHeader(output); WriteCpuDiscoveryCsvRow(output, discovery);
    });
    Expect(table.Rows.front()[0] == "2" && table.Rows.front()[1] == "exploratory" && table.Rows.front()[11] == discovery.Failure &&
        table.Rows.front()[10] == "failed", "Failed discovery evidence changed");
    discovery.Status = CpuRecordStatus::Valid;
    Reject([&] { std::ostringstream output; WriteCpuDiscoveryCsvRow(output, discovery); }, "Missing input evidence accepted");
    CpuPairRecord pair;
    pair.RunId = "process-fixture";
    pair.ScenarioId = "test129-a-b512";
    pair.TerrainId = "test129";
    pair.AnalysisSplit = "train";
    pair.SelectionSeed = 20260830U;
    pair.SelectorVersion = pair.PassInputVersion = 1U;
    pair.SelectionStratum = "low";
    pair.CameraPoseHash = pair.ViewInputHash = pair.ReplayInputHash = pair.SelectionFeatureHash = 1U;
    pair.PrimaryWorkValue = 2.0F;
    pair.FeatureVector = "2";
    pair.SampleIndex = 1;
    pair.AbsoluteBlockIndex = pair.RepeatIndex = 1U;
    pair.IsWarmup = true;
    pair.BlockOrder = "BA";
    pair.OrderIndex = 0;
    pair.RequestedAction = Algorithms::TerrainLodPassAction::ParallelFullRefresh;
    pair.ActualAction = Algorithms::TerrainLodPassAction::SerialFullRefresh;
    pair.RequestedWorkerCount = 8;
    pair.ActualWorkerCount = 1;
    pair.Fallback = Algorithms::TerrainLodPassFallbackReason::BelowParallelThreshold;
    pair.ExecutionPath = "caller_thread";
    pair.FallbackDetail = "below_parallel_threshold";
    pair.Status = CpuRecordStatus::Failed;
    pair.Failure = "result mismatch";
    std::stringstream pairStream;
    WriteCpuPairCsvHeader(pairStream);
    WriteCpuPairCsvRow(pairStream, pair);
    const auto pairs = ReadCpuPairCsv(pairStream, "failed-pair-fixture");
    Expect(pairs.front().BlockOrder == "BA" && pairs.front().RequestedWorkerCount == 8U &&
        pairs.front().ActualWorkerCount == 1U && pairs.front().Status == CpuRecordStatus::Failed,
        "Pair order, workers or failure state lost");
    pair.Status = CpuRecordStatus::Valid;
    Reject([&] { std::ostringstream output; WriteCpuPairCsvRow(output, pair); }, "Invalid result labeled valid");
    InputPreparationSummary summary;
    summary.ExpectedCameraCount = 64;
    summary.CameraCount = 63;
    const auto incomplete = Capture([&](auto& output) { WriteInputPreparationSummary(output, summary); });
    Expect(incomplete.Rows.front()[2] == "preparing" && incomplete.Rows.front()[4] == "64" &&
        incomplete.Rows.front()[5] == "63" && incomplete.Rows.front()[8] == "external_verification_required",
        "Incomplete summary falsely completed");
}

void CheckManifests(const std::filesystem::path& root, const std::filesystem::path& output)
{
    const auto manifest = root / "docs/parallel-roam/cpu-pilot-scenarios-v1.csv";
    const auto scenarios = LoadScenarioManifest(manifest, root);
    Expect(scenarios.size() == 6, "Pilot scene count mismatch");
    const auto selected = LoadScenarioManifest(manifest, root, {"test129-a-b512"});
    Expect(selected.size() == 1 && selected.front().Settings.TriangleBudget == 512, "Scenario filter failed");
    Reject([&] { (void)LoadScenarioManifest(manifest, root, {"missing"}); }, "Missing ID accepted");
    Reject([&] { (void)LoadScenarioManifest(manifest, root, {"test129-a-b512", "test129-a-b512"}); }, "Duplicate ID accepted");
    const auto sceneTable = Capture([&](auto& stream) { WriteScenarioManifest(stream, selected); });
    const auto scenePath = output / "scenes.csv";
    for (const auto& [column, value] : std::vector<std::pair<std::string, std::string>>{
        {"schemaVersion", "2"}, {"dataPurpose", "formal"}, {"sampleCount", "63"}, {"trajectoryId", "C"},
        {"analysisSplit", "holdout"}, {"triangleBudget", "513"}, {"terrainSize", "31"}, {"nearPlane", "0.05"},
        {"parallelWorkerCount", "0"}, {"heightMapWidth", "513"}, {"heightMapSha256", std::string(64, '0')},
        {"heightMapPath", (root / "assets/heightmaps/Hm_Terrain_Peking_513.png").generic_string()}})
    {
        auto changed = sceneTable;
        Set(changed, column, value);
        WriteTable(scenePath, changed);
        Reject([&] { (void)LoadScenarioManifest(scenePath, root); }, "Invalid scene or actual dimensions accepted");
    }
    auto duplicated = sceneTable;
    duplicated.Rows.push_back(duplicated.Rows.front());
    WriteTable(scenePath, duplicated);
    Reject([&] { (void)LoadScenarioManifest(scenePath, root); }, "Duplicate scenario accepted");
    const auto cameras = GenerateCameraSamples(selected);
    const auto cameraTable = Capture([&](auto& stream) { WriteCameraManifest(stream, cameras); });
    const auto cameraPath = output / "cameras.csv";
    auto shuffled = cameraTable;
    std::reverse(shuffled.Rows.begin(), shuffled.Rows.end());
    WriteTable(cameraPath, shuffled);
    Expect(LoadCameraManifest(cameraPath, selected) == cameras, "Frozen cameras do not round trip in canonical order");
    for (const auto& [column, value] : std::vector<std::pair<std::string, std::string>>{
        {"sampleIndex", "64"}, {"positionX", "0"}, {"depthConvention", "ZO"}, {"viewInputHash", "1"},
        {"scenarioId", "peking547-a-b20000"}, {"projectionNoHash", "0"}, {"fovDegrees", "nan"}})
    {
        auto changed = cameraTable;
        Set(changed, column, value);
        WriteTable(cameraPath, changed);
        Reject([&] { (void)LoadCameraManifest(cameraPath, selected); }, "Corrupted camera accepted");
    }
    auto missing = cameraTable;
    missing.Rows.pop_back();
    WriteTable(cameraPath, missing);
    Reject([&] { (void)LoadCameraManifest(cameraPath, selected); }, "Missing camera accepted");
    missing = cameraTable;
    missing.Rows.back() = missing.Rows.front();
    WriteTable(cameraPath, missing);
    Reject([&] { (void)LoadCameraManifest(cameraPath, selected); }, "Duplicate camera accepted");
    std::vector<TargetStateRef> targets;
    for (const auto pass : CpuPilotPassIds)
    {
        TargetStateRef target;
        target.ScenarioId = selected.front().ScenarioId;
        target.PassId = pass;
        target.SampleIndex = 1;
        target.SelectionStratum = "low";
        target.PrimaryWorkValue = 4;
        target.FeatureVector = "4;0.5;2";
        target.ReplayInputHash = UINT64_MAX;
        target.SelectionFeatureHash = 2;
        target.CameraPoseHash = cameras[1].CameraPoseHash;
        target.ViewInputHash = cameras[1].ViewInputHash;
        targets.push_back(target);
    }
    const auto targetTable = Capture([&](auto& stream) { WriteTargetManifest(stream, targets); });
    const auto targetPath = output / "targets.csv";
    WriteTable(targetPath, targetTable);
    Expect(LoadTargetManifest(targetPath, selected, cameras).size() == 5, "Five CPU target references rejected");
    for (const auto& [column, value] : std::vector<std::pair<std::string, std::string>>{
        {"passId", "cpuUpload"}, {"passId", "cpu-upload"}, {"sampleIndex", "0"}, {"sampleIndex", "64"},
        {"selectionRank", "1"}, {"selectionRank", "8"}, {"selectionSeed", "1"}, {"primaryWorkValue", "0"},
        {"featureVector", "1;nan"}, {"featureVector", "1;"}, {"replayInputHash", "0"}, {"cameraPoseHash", "1"},
        {"viewInputHash", "1"}, {"selectionStratum", "winner"}, {"analysisSplit", "holdout"}})
    {
        auto changed = targetTable;
        Set(changed, column, value);
        WriteTable(targetPath, changed);
        Reject([&] { (void)LoadTargetManifest(targetPath, selected, cameras); }, "Invalid target relation accepted");
    }
    auto changed = targetTable;
    changed.Rows.push_back(changed.Rows.front());
    WriteTable(targetPath, changed);
    Reject([&] { (void)LoadTargetManifest(targetPath, selected, cameras); }, "Duplicate target accepted");
    changed.Rows.clear();
    WriteTable(targetPath, changed);
    Reject([&] { (void)LoadTargetManifest(targetPath, selected, cameras); }, "Empty target selection accepted");
}
} // 匿名命名空间

int main(int argc, char** argv)
{
    try
    {
        Expect(argc == 3, "Source and output directories required");
        const auto output = std::filesystem::path{argv[2]} /
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        std::filesystem::create_directories(output);
        CheckCodec();
        CheckRecords();
        CheckManifests(argv[1], output);
        std::cout << "Manifest protocol, frozen references, malformed inputs and record evidence verified.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
