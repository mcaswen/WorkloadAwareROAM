#include "experiment/roam_materialization/MaterializationDodBridge.h"
#include "experiment/roam_materialization/MaterializationReference.h"
#include "experiment/roam_materialization/MaterializationPatch.h"
#include "experiment/roam_materialization/MaterializationValidation.h"
#include "experiment/roam_materialization/MaterializationExecutor.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamPipeline.h"
#include "algorithms/TerrainLodView.h"
#include "benchmark/formal/FormalCpuInput.h"
#include "experiment/formal/FormalExperimentCamera.h"
#include "experiment/formal/FormalExperimentManifest.h"
#include "experiment/ExperimentCsvCodec.h"
#include "tools/PerformanceTimer.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>

using namespace ParallelRoam;
using namespace Experiment::RoamMaterialization;

namespace
{
/// <summary>
/// 每个案例拥有共同旧状态；来源恢复与认证先完成，重复计量只复制同一输入
/// </summary>
struct Case
{
    std::string Name;
    DodTargetTask Task;
    double SourceMs{0}, CaptureMs{0}, SeedMs{0}, CertifyMs{0};
    CertifiedTarget Target;
};

using Arguments = std::map<std::string, std::string>;

Arguments Parse(int argc, char** argv)
{
    const std::set<std::string> keys{"--mode", "--method", "--cases", "--asset-root",
        "--scenario-manifest", "--camera-manifest", "--warmups", "--repeats", "--output", "--workers", "--case"};
    Arguments args;
    for (int i = 1; i < argc; i += 2)
    {
        if (i + 1 >= argc || !keys.contains(argv[i]) || !args.emplace(argv[i], argv[i + 1]).second)
            throw std::invalid_argument("未知、重复或无值参数");
    }
    args.try_emplace("--workers", "1");
    args.try_emplace("--case", "all");
    // 压力输入由独立冻结协议定义，不借用或绕过轨迹 A 的清单验证
    const bool stress = args.contains("--cases") && args.at("--cases") == "mpr-stress-01";
    if (stress)
    {
        if (args.contains("--scenario-manifest") || args.contains("--camera-manifest"))
            throw std::invalid_argument("压力协议不接受轨迹 A 清单");
        args.emplace("--scenario-manifest", ""); args.emplace("--camera-manifest", "");
    }
    if (args.size() != keys.size()) throw std::invalid_argument("缺少测量参数");
    if ((args.at("--mode") != "audit" && args.at("--mode") != "measure") ||
        (args.at("--method") != "reference-serial" && args.at("--method") != "materialize-serial" &&
            args.at("--method") != "materialize-parallel") ||
        (!stress && args.at("--cases") != "mpr-01")) throw std::invalid_argument("方法或案例协议不受支持");
    const auto& workers = args.at("--workers");
    if ((workers != "1" && workers != "2" && workers != "4") ||
        ((args.at("--method") == "materialize-parallel") != (workers != "1")))
        throw std::invalid_argument("方法与线程数不匹配");
    // 重复数量属于预注册协议，不在看到波动后临时扩大
    if (args.at("--warmups") != "5" || args.at("--repeats") != "30")
        throw std::invalid_argument("本协议固定五次热身、三十次计量");
    if (std::filesystem::exists(args.at("--output"))) throw std::invalid_argument("拒绝覆盖已有输出");
    return args;
}

// 只复用旧环绕的几何公式，CPU 视图单独冻结，不冒充历史 GUI 的逐位输入
Experiment::Formal::CameraSample StressCamera(std::uint32_t index, std::uint32_t count)
{
    Experiment::Formal::CameraSample camera;
    camera.TrajectoryId = "budget-orbit"; camera.SampleIndex = index;
    const float t = static_cast<float>(index) / static_cast<float>(count - 1);
    const float angle = t * 6.28318530718F;
    camera.Position = {std::cos(angle) * 58.0F, 20.0F + std::sin(angle * 2.0F) * 3.0F,
        std::sin(angle) * 58.0F};
    camera.Target = {std::cos(angle + 0.55F) * 10.0F, 4.0F, std::sin(angle + 0.55F) * 10.0F};
    return camera;
}

std::vector<Case> PrepareStress(const Arguments& args)
{
    using namespace Algorithms::DataOrientedRoam;
    using namespace Experiment::Formal;
    Terrain::HeightMap height;
    std::string error;
    if (!height.LoadFromFile(std::filesystem::path(args.at("--asset-root")) /
        "assets/heightmaps/Hm_Terrain_Peking_513.png", &error)) throw std::runtime_error(error);
    if (height.Width() != 547 || height.Height() != 547) throw std::runtime_error("压力资产尺寸不匹配");
    DataOrientedRoamSettings settings;
    settings.MaxDepth = 20; settings.TriangleBudget = 200000;
    settings.SplitThreshold = 0.25F; settings.MergeThreshold = 0.10F;
    settings.PassPolicy = Algorithms::MakeTerrainLodSerialIncrementalPolicy();
    settings.EnablePassEvidence = settings.EnableTopologyValidation = true;
    // 来源预热单独持有流水线，销毁后重新从干净拓扑推进固定前缀
    {
        DataOrientedRoamPipeline warmup;
        for (std::uint32_t i = 0; i < 4; ++i)
            static_cast<void>(warmup.Build(height, 80.0F, 12.0F, BuildCameraView(StressCamera(i, 4)), settings));
    }
    const auto cameraPath = std::filesystem::path(args.at("--output")).parent_path() / "stress-cameras.csv";
    if (std::filesystem::exists(cameraPath)) throw std::runtime_error("拒绝覆盖压力姿态记录");
    std::ofstream cameras(cameraPath);
    Experiment::WriteExperimentCsvRow(cameras, {"sampleIndex", "x", "y", "z", "targetX", "targetY", "targetZ",
        "viewProjectionHash"});
    // 全部姿态落盘；只有前缀参与来源恢复，计时对象仍是指定的一次目标应用
    for (std::uint32_t i = 0; i < 64; ++i)
    {
        const auto camera = StressCamera(i, 64);
        const auto view = BuildCameraView(camera);
        std::uint64_t hash = Algorithms::TerrainLodHashOffset;
        for (glm::length_t column = 0; column < 4; ++column)
            for (glm::length_t row = 0; row < 4; ++row)
                Algorithms::AppendTerrainLodHash(hash, view.ViewProjection[column][row]);
        Experiment::ExperimentCsvRow row{std::to_string(i)};
        for (const auto value : {camera.Position.x, camera.Position.y, camera.Position.z,
            camera.Target.x, camera.Target.y, camera.Target.z}) row.push_back(Experiment::FormatExperimentCsvDouble(value));
        row.push_back(std::to_string(hash)); Experiment::WriteExperimentCsvRow(cameras, row);
    }
    if (!cameras) throw std::runtime_error("压力姿态记录失败");
    Case item;
    item.Name = "peking547-budget-orbit64-b200000-sample14";
    DataOrientedRoamPipeline pipeline;
    Tools::PerformanceTimer trajectory;
    for (std::uint32_t i = 0; i <= 14; ++i)
        static_cast<void>(pipeline.BuildWithPassObserver(height, 80.0F, 12.0F,
            BuildCameraView(StressCamera(i, 64)), settings, [&](const auto& source, Algorithms::TerrainLodPassId pass) {
                if (i != 14 || pass != Algorithms::TerrainLodPassId::SplitTopology) return;
                if (item.Task.Initial) throw std::runtime_error("压力输入重复捕获");
                Tools::PerformanceTimer capture;
                item.Task = MaterializationDodBridge::Capture(source); item.CaptureMs = capture.Stop();
            }));
    item.SourceMs = trajectory.Stop();
    if (!item.Task.Initial) throw std::runtime_error("压力输入未捕获");
    Tools::PerformanceTimer certify;
    item.Target = MaterializationValidation::Certify(*item.Task.Initial, item.Task.Request);
    item.CertifyMs = certify.Stop();
    std::vector<Case> cases;
    cases.push_back(std::move(item));
    return cases;
}

std::vector<Case> Prepare(const Arguments& args)
{
    if (args.at("--cases") == "mpr-stress-01") return PrepareStress(args);
    using namespace Experiment::Formal;
    using namespace Algorithms::DataOrientedRoam;
    const auto scenarios = LoadScenarioManifest(args.at("--scenario-manifest"), args.at("--asset-root"),
        {"test129-a-b4096", "peking547-a-b20000"});
    const auto cameras = LoadCameraManifest(args.at("--camera-manifest"), scenarios);
    std::vector<Case> cases;
    for (const auto& scenario : scenarios)
    {
        Case item;
        item.Name = scenario.ScenarioId + "-sample14";
        Tools::PerformanceTimer trajectory;
        Terrain::HeightMap height;
        std::string error;
        if (!height.LoadFromFile(scenario.HeightMapPath, &error)) throw std::runtime_error(error);
        DataOrientedRoamPipeline pipeline;
        const auto settings = Benchmark::Formal::MakeCpuSourceSettings(scenario);
        for (const auto& camera : cameras)
        {
            if (camera.ScenarioId != scenario.ScenarioId || camera.SampleIndex > 14) continue;
            static_cast<void>(pipeline.BuildWithPassObserver(height, scenario.Settings.TerrainSize,
                scenario.Settings.HeightScale, BuildCameraView(camera), settings,
                [&](const auto& source, Algorithms::TerrainLodPassId pass) {
                    if (camera.SampleIndex != 14 || pass != Algorithms::TerrainLodPassId::SplitTopology) return;
                    if (item.Task.Initial) throw std::runtime_error("冻结自然输入重复");
                    Tools::PerformanceTimer capture;
                    item.Task = MaterializationDodBridge::Capture(source);
                    item.CaptureMs = capture.Stop();
                }));
        }
        item.SourceMs = trajectory.Stop();
        if (!item.Task.Initial) throw std::runtime_error("未恢复指定自然输入");
        cases.push_back(std::move(item));
    }

    const auto source = std::find_if(cases.begin(), cases.end(), [](const auto& c) {
        return c.Name == "test129-a-b4096-sample14";
    });
    if (source == cases.end()) throw std::runtime_error("缺少解析夹具的冻结环境");
    const auto environment = source->Task.Environment;
    const auto sourceHash = source->Task.SourceHash;
    for (const int depth : {8, 12})
    {
        Tools::PerformanceTimer seedTimer;
        MaterializationState seed(environment, 20000);
        EventSet uniform;
        for (const auto root : {RootA, RootB})
            for (int level = 0; level < depth; ++level)
                for (NodeId offset = 0; offset < (NodeId{1} << level); ++offset)
                    uniform.insert((root << level) + offset);
        MaterializationReference::Apply(seed, MaterializationValidation::Certify(seed,
            MaterializationValidation::Difference(seed, uniform)));
        seed.AdvanceEpoch(); seed.ConsumePending();
        const auto seedMs = seedTimer.Stop();
        for (const std::size_t count : {1U, 16U, 128U})
        {
            Case item;
            item.Name = "uniform-d" + std::to_string(depth) + "-diamonds" + std::to_string(count);
            Tools::PerformanceTimer restore;
            item.Task.Initial = std::make_unique<MaterializationState>(seed);
            item.SeedMs = seedMs + restore.Stop();
            item.Task.Environment = environment; item.Task.SourceHash = sourceHash;
            Tools::PerformanceTimer difference;
            EventSet target = uniform;
            std::size_t selected = 0;
            // 仅按稳定身份选完整内部菱形，不能根据评分或时间更换位置
            for (const auto id : seed.Leaves())
            {
                const auto members = seed.Members(id);
                if (members.size() != 2 || members.front() != id) continue;
                target.insert(members.begin(), members.end());
                if (++selected == count) break;
            }
            if (selected != count) throw std::runtime_error("固定解析菱形数量不足");
            item.Task.Request = MaterializationValidation::Difference(seed, std::move(target));
            item.Task.DifferenceMs = difference.Stop();
            cases.push_back(std::move(item));
        }
    }
    if (cases.size() != 8) throw std::runtime_error("案例矩阵不完整");
    for (auto& item : cases)
    {
        Tools::PerformanceTimer timer;
        item.Target = MaterializationValidation::Certify(*item.Task.Initial, item.Task.Request);
        item.CertifyMs = timer.Stop();
    }
    return cases;
}

void Apply(const std::string& method, MaterializationState& state,
    const CertifiedTarget& target, WorkCounters* counters, const MaterializationExecution& execution)
{
    if (method == "reference-serial") MaterializationReference::Apply(state, target, counters);
    else MaterializationPatch::Apply(state, target, counters, execution);
}

Experiment::ExperimentCsvRow Header()
{
    return {"schema", "case", "sourceHash", "mode", "method", "workers", "repeat", "status",
        "cachedN", "initialLeaves", "targetLeaves", "H", "added", "removed", "oldPending", "finalPending",
        "finalRecords", "heldSlots", "R", "A", "restoreMs", "transactionMs", "consumeMs", "epochMs",
        "sourceInclusiveMs", "captureInclusiveMs", "seedMs", "environmentMs", "importMs", "discoveryCopyMs",
        "discoveryMs", "differenceMs", "certifyMs", "staticBytes", "sourceRecords",
        "F", "C", "eventQueries", "recordQueries", "scoreEvaluations", "splitRechecks", "mergeRechecks",
        "queueWrites", "recordsCreated", "cachedEnsureCalls", "neighborWrites", "slotAllocations", "slotReuses",
        "pendingWrites", "primitiveGroups", "preparedEdges", "fullScanItems", "supportMs", "recordsMs",
        "connectMs", "maintenanceMs", "consumeItems", "epochItems", "executorInitMs",
        "allocationMs", "descriptorMs", "stateMaintenanceMs", "descriptorItems", "scratchPayloadBytes", "recordPatches",
        "staticItems", "staticDispatches", "staticChunkItems", "staticThreadIds", "staticDistinctThreads", "staticWallMs",
        "derivedItems", "derivedDispatches", "derivedChunkItems", "derivedThreadIds", "derivedDistinctThreads", "derivedWallMs",
        "writeItems", "writeDispatches", "writeChunkItems", "writeThreadIds", "writeDistinctThreads", "writeWallMs",
        "nodeIndexProbes", "localSearches", "scratchTreeElements", "scratchSortItems",
        "queueMemberInserts", "queueMemberErases", "queueOrderInserts", "queueOrderErases",
        "queueUnchangedRefreshes", "queueAbsentRefreshes", "resultValidation"};
}

void Measure(const Case& item, const Arguments& args, std::ostream& output,
    const MaterializationExecution& execution, double executorInitMs)
{
    const bool audit = args.at("--mode") == "audit";
    const auto& initial = *item.Task.Initial;
    auto expected = initial;
    MaterializationReference::Apply(expected, item.Target);
    const auto finalLeaves = MaterializationValidation::EnumerateLeaves(item.Task.Request.Target);
    std::size_t removedLeaves = 0, addedLeaves = 0;
    for (const auto id : initial.Leaves()) if (!finalLeaves.contains(id)) ++removedLeaves;
    for (const auto id : finalLeaves) if (!initial.Leaf(id)) ++addedLeaves;
    for (int repeat = audit ? 0 : -5; repeat < (audit ? 1 : 30); ++repeat)
    {
        Tools::PerformanceTimer restore;
        auto state = initial;
        const double restoreMs = restore.Stop();
        WorkCounters work;
        Tools::PerformanceTimer transaction;
        Apply(args.at("--method"), state, item.Target, audit ? &work : nullptr, execution);
        const double transactionMs = transaction.Stop();
        // 固定输入重复计量不重复全域审计，诊断及末次结果核对完整投影和续接
        const bool validate = audit || repeat == 29;
        if (validate) MaterializationValidation::Compare(expected, state);
        if (audit && (work.FullScanItems != 0 || (args.at("--method") != "reference-serial" &&
            (work.PrimitiveGroups != 0 || work.LeafSupport > 3 * (item.Target.Added().size() +
                item.Target.Removed().size()) || work.MergeSupport > 2 * (item.Target.Added().size() +
                    item.Target.Removed().size()))))) throw std::runtime_error("局部路径费用超出契约");
        const auto pending = state.Pending().size(), records = state.Nodes().size(), slots = state.Slots().size();
        WorkCounters consumeWork, epochWork;
        double consumeMs = 0, epochMs = 0;
        if (validate)
        {
            Tools::PerformanceTimer consumer;
            state.ConsumePending(audit ? &consumeWork : nullptr);
            consumeMs = consumer.Stop();
            Tools::PerformanceTimer epoch;
            state.AdvanceEpoch(audit ? &epochWork : nullptr);
            epochMs = epoch.Stop();
            expected.ConsumePending(); expected.AdvanceEpoch();
            MaterializationValidation::Compare(expected, state);
        }
        if (repeat < 0) continue;
        Experiment::ExperimentCsvRow row{"3", item.Name, std::to_string(item.Task.SourceHash),
            args.at("--mode"), args.at("--method"), args.at("--workers"), std::to_string(repeat),
            validate ? "valid" : "measured"};
        const auto number = [&](auto value) { row.push_back(std::to_string(value)); };
        number(initial.Nodes().size()); number(initial.Leaves().size()); number(finalLeaves.size());
        number(initial.Environment().MaxDepth); number(item.Target.Added().size()); number(item.Target.Removed().size());
        number(initial.Pending().size()); number(pending); number(records); number(slots);
        number(removedLeaves); number(addedLeaves);
        for (const auto value : {restoreMs, transactionMs}) row.push_back(Experiment::FormatExperimentCsvDouble(value));
        // 未执行的外围步骤留空，不能伪造为零成本或声称每个计量结果均已单独验证
        for (const auto value : {consumeMs, epochMs})
            row.push_back(validate ? Experiment::FormatExperimentCsvDouble(value) : "");
        for (const auto value : {item.SourceMs, item.CaptureMs,
            item.SeedMs, item.Task.EnvironmentMs, item.Task.ImportMs, item.Task.DiscoveryCopyMs,
            item.Task.DiscoveryMs, item.Task.DifferenceMs, item.CertifyMs})
            row.push_back(Experiment::FormatExperimentCsvDouble(value));
        number(item.Task.StaticBytes); number(item.Task.SourceRecords);
        for (const auto value : {work.LeafSupport, work.MergeSupport, work.EventQueries, work.RecordQueries,
            work.ScoreEvaluations, work.SplitRechecks, work.MergeRechecks, work.QueueWrites, work.RecordsCreated,
            work.RecordsReused, work.NeighborWrites, work.SlotAllocations, work.SlotReuses, work.PendingWrites,
            work.PrimitiveGroups, work.PreparedEdges, work.FullScanItems}) number(value);
        for (const auto value : {work.SupportMs, work.RecordsMs, work.ConnectMs, work.MaintenanceMs})
            row.push_back(Experiment::FormatExperimentCsvDouble(value));
        row.push_back(validate ? std::to_string(consumeWork.FullScanItems) : "");
        row.push_back(validate ? std::to_string(epochWork.FullScanItems) : "");
        for (const auto value : {executorInitMs, work.AllocationMs, work.DescriptorMs, work.StateMaintenanceMs})
            row.push_back(Experiment::FormatExperimentCsvDouble(value));
        number(work.DescriptorItems); number(work.ScratchPayloadBytes); number(work.RecordPatches);
        for (const auto& phase : work.Phases)
        {
            number(phase.Items); number(phase.Dispatches);
            std::ostringstream chunks, threads;
            for (std::size_t i = 0; i < phase.Threads.size(); ++i)
            {
                if (i != 0) { chunks << ';'; threads << ';'; }
                chunks << phase.ChunkItems[i]; threads << phase.Threads[i];
            }
            row.push_back(chunks.str()); row.push_back(threads.str());
            number(std::set(phase.Threads.begin(), phase.Threads.end()).size());
            row.push_back(Experiment::FormatExperimentCsvDouble(phase.WallMs));
        }
        for (const auto value : {work.NodeIndexProbes, work.LocalSearches, work.ScratchTreeElements,
            work.ScratchSortItems, work.QueueMemberInserts, work.QueueMemberErases, work.QueueOrderInserts,
            work.QueueOrderErases, work.QueueUnchangedRefreshes, work.QueueAbsentRefreshes}) number(value);
        row.push_back(validate ? "complete-continuation" : "not-evaluated");
        if (row.size() != Header().size()) throw std::logic_error("成本行字段数量错误");
        Experiment::WriteExperimentCsvRow(output, row);
        if (!output) throw std::runtime_error("结果写入失败");
    }
    std::cout << item.Name << " 完成\n";
}
}

int main(int argc, char** argv)
{
    try
    {
        const auto args = Parse(argc, argv);
        const auto outputPath = std::filesystem::path(args.at("--output"));
        if (!outputPath.parent_path().empty()) std::filesystem::create_directories(outputPath.parent_path());
        std::ofstream output(outputPath);
        if (!output) throw std::runtime_error("无法创建输出");
        Experiment::WriteExperimentCsvRow(output, Header());
        // 先准备完整矩阵；来源失败时保留空表和非零退出，不把少量完成行当成功报告
        const auto cases = Prepare(args);
        if (args.at("--case") != "all" && std::none_of(cases.begin(), cases.end(), [&](const auto& item) {
            return item.Name == args.at("--case");
        })) throw std::invalid_argument("未知冻结案例");
        std::unique_ptr<MaterializationExecutor> executor;
        MaterializationExecution execution;
        double executorInitMs = 0;
        if (args.at("--workers") != "1")
        {
            Tools::PerformanceTimer timer;
            executor = std::make_unique<MaterializationExecutor>(static_cast<std::size_t>(std::stoul(args.at("--workers"))));
            execution = executor->Execution();
            executorInitMs = timer.Stop();
        }
        for (const auto& item : cases)
            if (args.at("--case") == "all" || item.Name == args.at("--case"))
                Measure(item, args, output, execution, executorInitMs);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n'; return 1;
    }
}
