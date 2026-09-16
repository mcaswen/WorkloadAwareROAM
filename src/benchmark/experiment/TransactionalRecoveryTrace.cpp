#include "benchmark/experiment/TransactionalRecoveryTrace.h"
#include "benchmark/experiment/ExperimentReplay.h"
#include "algorithms/TerrainLodView.h"
#include "algorithms/greedy_transactional_lod/TransactionalSeedBuilder.h"
#include "algorithms/greedy_transactional_lod/TransactionalReservation.h"
#include "algorithms/greedy_transactional_lod/TransactionalStateInvariant.h"
#include "experiment/greedy_transactional_lod/TransactionalQualityProvenance.h"
#include "experiment/greedy_transactional_lod/TransactionalBoundaryAudit.h"
#include "experiment/greedy_transactional_lod/TransactionalExchangeQualityAudit.h"
#include "experiment/mesh_quality/PlatformMeshArtifact.h"
#include "tools/CpuTaskExecutor.h"

#include <cmath>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>

namespace ParallelRoam::Benchmark::Experiment
{
namespace
{
using namespace Algorithms::GreedyTransactionalLod;
using Audit = ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalQualityProvenance;

struct WitnessGroup
{
    std::size_t SourceFrame{};
    std::size_t ReturnFrame{};
    std::vector<Point> Points;
};

/// <summary>
/// 见证表只保存冻结位置和诊断投影帧，不承担新的场景或算法设置
/// 无效输入在创建输出和构造种子之前拒绝
/// </summary>
std::vector<WitnessGroup> ReadWitnesses(const std::filesystem::path& path, std::size_t frameCount)
{
    std::ifstream stream(path);
    if (!stream)
    {
        throw std::runtime_error("无法打开见证表");
    }
    std::vector<WitnessGroup> groups;
    std::set<std::tuple<std::size_t, double, double>> unique;
    std::size_t count = 0;
    std::string line;
    while (std::getline(stream, line))
    {
        std::istringstream row(line);
        std::size_t sourceFrame{}, returnFrame{}, ordinal{};
        Point point;
        std::string extra;
        if (!(row >> sourceFrame >> returnFrame >> ordinal >> point.U >> point.V) || row >> extra ||
            sourceFrame >= frameCount || returnFrame >= frameCount ||
            !std::isfinite(point.U) || !std::isfinite(point.V) || point.U < 0 || point.U > 1 ||
            point.V < 0 || point.V > 1 || !unique.emplace(sourceFrame, point.U, point.V).second || ++count > 6)
        {
            throw std::runtime_error("见证表须包含至多六个有效且不重复的位置");
        }
        auto group = std::find_if(groups.begin(), groups.end(), [&](const auto& item) { return item.SourceFrame == sourceFrame; });
        if (group == groups.end())
        {
            groups.push_back({sourceFrame, returnFrame, {point}});
        }
        else
        {
            if (group->ReturnFrame != returnFrame)
            {
                throw std::runtime_error("同组见证的返回投影帧不一致");
            }
            group->Points.push_back(point);
        }
    }
    if (count == 0)
    {
        throw std::runtime_error("见证表不能为空");
    }
    return groups;
}

Algorithms::TerrainLodViewInput View(const ReplayInput& input, std::size_t frame)
{
    const auto& camera = input.Cameras.at(frame);
    const bool zero = input.Case.Backend == "d3d12";
    return Algorithms::BuildTerrainLodViewInput(camera.View, zero ? camera.ProjectionZo : camera.ProjectionNo,
        camera.Position, camera.Forward, camera.Width, camera.Height, zero);
}

struct BoundaryGeometry
{
    std::map<Identity, Point> Vertices;
    std::set<Edge> Edges;
    bool operator==(const BoundaryGeometry&) const = default;
};

BoundaryGeometry Boundary(const TransactionalState& state)
{
    BoundaryGeometry result;
    for (const auto& [edge, association] : state.Edges())
    {
        if (association.Count != 1)
        {
            continue;
        }
        result.Edges.insert(edge);
        for (const auto id : edge)
        {
            if (!state.IsBoundary(id))
            {
                throw std::runtime_error("边界关联与持久 boundary 标记不同");
            }
            result.Vertices.emplace(id, state.Vertex(id).Geometry);
        }
    }
    return result;
}

/// <summary>
/// 只按获批单侧事务更新期望边界，随后独立扫描实际关联进行比对
/// 旧点坐标与高度必须保留，其他事务不能隐式改变外接口
/// </summary>
void AdvanceBoundary(const TransactionalState& state, const CertifiedBatch& batch, BoundaryGeometry& expected)
{
    for (const auto& exchange : batch.Exchanges)
    {
        if (exchange.Kind != ExchangeKind::BoundaryRefinement)
        {
            continue;
        }
        const auto& proposal = exchange.Receiver;
        const auto& point = proposal.Points.at(proposal.NewVertex);
        const auto& face = state.Face(proposal.Root).Vertices;
        bool found = false;
        for (std::size_t index = 0; index < face.size(); ++index)
        {
            const auto edge = EdgeKey(face[index], face[(index + 1) % face.size()]);
            if (!expected.Edges.contains(edge))
            {
                continue;
            }
            const auto& a = expected.Vertices.at(edge[0]);
            const auto& b = expected.Vertices.at(edge[1]);
            if (point.U == (a.U + b.U) * .5 && point.V == (a.V + b.V) * .5)
            {
                expected.Edges.erase(edge);
                expected.Edges.insert(EdgeKey(edge[0], proposal.NewVertex));
                expected.Edges.insert(EdgeKey(proposal.NewVertex, edge[1]));
                expected.Vertices.emplace(proposal.NewVertex, point);
                found = true;
                break;
            }
        }
        if (!found)
        {
            throw std::runtime_error("获批单侧事务没有可继承的旧边界");
        }
    }
}
}

int RunTransactionalRecoveryTrace(int argc, char** argv)
{
    try
    {
        const bool boundaryAudit = argc == 6 && std::string_view(argv[5]) == "--boundary-audit";
        const bool selectedPriorities = argc == 7 && std::string_view(argv[5]) == "--priority-frames";
        const bool exchangeQuality = argc == 7 && std::string_view(argv[5]) == "--exchange-quality-frames";
        const bool priorityAudit = selectedPriorities || (argc == 6 && std::string_view(argv[5]) == "--priority-audit");
        if (argc != 5 && !boundaryAudit && !priorityAudit && !exchangeQuality)
        {
            throw std::runtime_error("用法: --recovery-trace RESOLVED WITNESSES OUTPUT [--boundary-audit|--priority-audit|--priority-frames I,J|--exchange-quality-frames I,J]");
        }
        const auto input = LoadReplayInput(argv[2]);
        const auto& settings = input.Settings;
        if (input.Case.Algorithm != "transactional" || !settings.Transactional.PreserveSurvivingHeights ||
            !settings.Transactional.EnableFlipRecovery)
        {
            throw std::runtime_error("本追溯只接受冻结旧点且启用翻边恢复的 Transactional 输入");
        }
        const auto groups = ReadWitnesses(argv[3], input.Cameras.size());
        std::set<std::size_t> priorityFrames;
        if (selectedPriorities || exchangeQuality)
        {
            // 全根快照显式限为两帧，避免诊断意外扩成逐帧全量记录
            std::istringstream list(argv[6]);
            std::string token;
            while (std::getline(list, token, ','))
            {
                if (token.empty() || token.find_first_not_of("0123456789") != std::string::npos)
                {
                    throw std::invalid_argument("诊断帧列表无效");
                }
                const auto frame = std::stoull(token);
                if (frame >= input.Cameras.size() || !priorityFrames.insert(frame).second || priorityFrames.size() > 2)
                {
                    throw std::invalid_argument("诊断帧超出范围或重复");
                }
            }
            if (priorityFrames.empty())
            {
                throw std::invalid_argument("诊断帧列表为空");
            }
        }
        else
        {
            for (const auto& group : groups)
            {
                priorityFrames.insert(group.SourceFrame);
            }
        }
        const std::filesystem::path output(argv[4]);
        if (std::filesystem::exists(output))
        {
            throw std::runtime_error("拒绝覆盖质量追溯输出");
        }
        std::filesystem::create_directories(output);
        std::ofstream metadata(output / "policy.json");
        metadata << "{\"receiverOrder\":\"" << input.Case.ReceiverOrder << "\",\"priorityField\":\"composite\"}\n";
        Algorithms::TerrainLodBuildInput task;
        task.HeightMap = &input.Source;
        task.Settings = settings;
        task.View = View(input, 0);
        auto seed = TransactionalSeedBuilder::Build(task);
        Tools::CpuTaskExecutor executor(settings.Transactional.WorkerCount);
        const TransactionalExecution execution{settings.Transactional.WorkerCount, false,
            [&](auto count, const auto& function) { executor.Dispatch(count, function); }};
        TransactionalPipeline pipeline(std::move(seed), execution);
        TransactionalStateInvariant::Validate(pipeline.State());
        WorkLedger initialization;
        pipeline.Initialize(initialization);
        const auto boundary = Boundary(pipeline.State());
        auto expectedBoundary = boundary;
        std::ofstream boundaryFile(output / "boundary.csv");
        boundaryFile.exceptions(std::ios::badbit | std::ios::failbit);
        boundaryFile << std::setprecision(17) << "a,b,au,av,ah,bu,bv,bh\n";
        for (const auto& edge : boundary.Edges)
        {
            const auto& a = boundary.Vertices.at(edge[0]);
            const auto& b = boundary.Vertices.at(edge[1]);
            boundaryFile << edge[0] << ',' << edge[1] << ',' << a.U << ',' << a.V << ',' << a.Height
                << ',' << b.U << ',' << b.V << ',' << b.Height << '\n';
        }
        std::vector<std::unique_ptr<Audit>> audits;
        for (const auto& group : groups)
        {
            const auto directory = output / ("frame-" + std::to_string(group.SourceFrame));
            std::filesystem::create_directory(directory);
            task.View = View(input, group.SourceFrame);
            const auto future = TransactionalSeedBuilder::ConfigurationFor(task);
            task.View = View(input, group.ReturnFrame);
            const auto returned = TransactionalSeedBuilder::ConfigurationFor(task);
            audits.push_back(std::make_unique<Audit>(directory, future, returned, group.Points));
            audits.back()->Seed(pipeline.State(), pipeline.Samples());
        }
        std::ofstream frames(output / "frames.csv");
        frames.exceptions(std::ios::badbit | std::ios::failbit);
        frames << "frame,hash,faces,raw,examined,receivers,need,feasible,exchanges,free,pairs,conflicts,donorReuse,"
            "flipTriggered,flipAttempts,flipCertified,flipConflicts,flips,touches,boundaryUnchanged\n";
        std::ofstream budget(output / "boundary-refinement.csv");
        budget.exceptions(std::ios::badbit | std::ios::failbit);
        budget << "frame,attempts,certified,resolutionRejected,conflicts,freeExecuted,pairedExecuted,"
            "assignedFaces,consumedFreeFaces,unusedFaces,releasedFaces,netFaceChange,boundaryVertices\n";
        std::ofstream priorityTimes;
        if (priorityAudit)
        {
            priorityTimes.open(output / "priority-audit.csv");
            priorityTimes.exceptions(std::ios::badbit | std::ios::failbit);
            priorityTimes << std::setprecision(17) << "frame,seconds\n";
        }
        for (std::size_t frame = 0; frame < input.Cameras.size(); ++frame)
        {
            WorkLedger work;
            work.Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(180);
            task.View = View(input, frame);
            pipeline.SetView(TransactionalSeedBuilder::ConfigurationFor(task), work);
            if (priorityAudit && priorityFrames.contains(frame))
            {
                // 诊断快照取当前视图发布后、批次决策前，费用独立于生产账本
                const auto started = std::chrono::steady_clock::now();
                Audit::WritePrioritySnapshot(output / ("priority-" + std::to_string(frame) + ".csv"),
                    pipeline.State(), pipeline.Samples());
                priorityTimes << frame << ',' <<
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count() << '\n';
            }
            // 私有边界提案只读取机会开始状态，不改变原批次及边界保持断言
            if (boundaryAudit)
            {
                for (const auto& group : groups)
                {
                    if (frame == group.SourceFrame)
                    {
                        ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalBoundaryAudit::Run(
                            pipeline.State(), pipeline.Samples(), frame, group.Points,
                            output / ("boundary-audit-" + std::to_string(frame) + ".jsonl"));
                    }
                }
            }
            const auto batch = TransactionalReservation::Plan(pipeline.State(), pipeline.Samples(), work, execution);
            if (exchangeQuality && priorityFrames.contains(frame))
            {
                // 批次仍按原顺序发布；审计器不能筛选或改变任何生产提案
                ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalExchangeQualityAudit::Capture(
                    pipeline.State(), pipeline.Samples(), batch, frame, output);
            }
            for (const auto& audit : audits)
            {
                audit->Before(frame, pipeline.State(), pipeline.Samples(), batch);
            }
            const auto oldCount = pipeline.State().FaceCount();
            AdvanceBoundary(pipeline.State(), batch, expectedBoundary);
            std::int64_t actualDifference = 0;
            for (const auto& exchange : batch.Exchanges)
            {
                actualDifference += static_cast<std::int64_t>(exchange.Receiver.Faces.size()) -
                    static_cast<std::int64_t>(exchange.Receiver.Support.size());
                if (exchange.HasDonor)
                {
                    actualDifference += static_cast<std::int64_t>(exchange.Donor.Faces.size()) -
                        static_cast<std::int64_t>(exchange.Donor.Support.size());
                }
            }
            pipeline.Apply(batch, work);
            static_cast<void>(pipeline.ConsumeMesh());
            if (static_cast<std::int64_t>(pipeline.State().FaceCount()) != static_cast<std::int64_t>(oldCount) + actualDifference ||
                actualDifference != batch.NetFaceChange ||
                batch.AssignedFaces > settings.TriangleBudget - oldCount ||
                batch.ConsumedFreeFaces + batch.UnusedFaces != batch.AssignedFaces ||
                batch.Exchanges.size() != batch.Executed + batch.FreeExecuted + batch.FlipExecuted ||
                pipeline.State().FaceCount() > settings.TriangleBudget)
            {
                throw std::runtime_error("批次预算或事务计数不同");
            }
            if (Boundary(pipeline.State()) != expectedBoundary)
            {
                throw std::runtime_error("实际边界不符合已批准单侧细分，其余旧点或接口发生变化");
            }
            for (const auto& audit : audits)
            {
                audit->After(frame, pipeline.State(), pipeline.Samples());
            }
            frames << frame << ',' << ValidateReplayMesh(pipeline.Mesh(), input) << ',' << pipeline.State().FaceCount()
                << ',' << batch.Raw << ',' << batch.Examined << ',' << batch.Receivers << ',' << batch.Need
                << ',' << batch.Feasible << ',' << batch.Executed << ',' << batch.FreeExecuted << ',' << work.PairChecks
                << ',' << work.Conflicts << ',' << work.DonorReuse << ',' << work.FlipTriggered << ',' << work.FlipAttempts
                << ',' << work.FlipCertified << ',' << work.FlipConflicts << ',' << batch.FlipExecuted
                << ',' << work.SampleTouches << ',' << (expectedBoundary == boundary) << '\n';
            budget << frame << ',' << work.BoundaryAttempts << ',' << work.BoundaryCertified << ','
                << work.BoundaryResolutionRejected << ',' << work.BoundaryConflicts << ',' << batch.BoundaryFreeExecuted << ','
                << batch.BoundaryPairedExecuted << ',' << batch.AssignedFaces << ',' << batch.ConsumedFreeFaces << ','
                << batch.UnusedFaces << ',' << batch.ReleasedFaces << ',' << batch.NetFaceChange << ','
                << expectedBoundary.Vertices.size() << '\n';
            frames.flush();
        }
        TransactionalStateInvariant::Validate(pipeline.State());
        std::cout << "frames=" << input.Cameras.size() << " boundaryEdges=" << boundary.Edges.size() << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
}
