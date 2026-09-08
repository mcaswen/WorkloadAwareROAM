#include "algorithms/ITerrainLodAlgorithm.h"
#include "algorithms/TerrainLodResultValidation.h"
#include "algorithms/TerrainLodView.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamMeshEmit.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamParallel.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamPipeline.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamQueues.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamTerrainLodAlgorithm.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamValidation.h"

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{
using namespace ParallelRoam;
using namespace Algorithms;
using namespace Algorithms::DataOrientedRoam;

void Require(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error{message};
}

constexpr std::array<std::size_t TerrainLodPassPolicy::*, 3> Minimums{
    &TerrainLodPassPolicy::MergeScoreMinParallelEntryCount,
    &TerrainLodPassPolicy::SplitScoreMinParallelEntryCount,
    &TerrainLodPassPolicy::MeshEmitMinParallelTriangleCount};

void CheckPolicyAndCounts()
{
    for (const auto& policy : {TerrainLodPassPolicy{}, MakeTerrainLodSerialIncrementalPolicy(),
            MakeTerrainLodMaximumSafeParallelIncrementalPolicy(), MakeTerrainLodSerialFullOutputPolicy(),
            MakeTerrainLodMaximumSafeParallelFullOutputPolicy(), MakeTerrainLodFixedSerialPolicy(),
            MakeTerrainLodMaximumParallelPolicy(), MakeTerrainLodSerialFullPolicy()})
    {
        for (const auto member : Minimums)
            Require(policy.*member == 256U, "preset minimum");
        Require(policy.SplitTopologyMinParallelCandidateCount == 32U &&
            policy.MergeTopologyMinParallelCandidateCount == 160U, "topology defaults");
    }
    TerrainLodBuildInput input{};
    const auto originalHash = HashTerrainLodBuildInput(input);
    for (const auto member : Minimums)
    {
        auto changed = input;
        changed.Settings.PassPolicy.*member = 256U;
        Require(HashTerrainLodBuildInput(changed) == originalHash, "explicit defaults identity");
        changed.Settings.PassPolicy.*member = 0U;
        Require(HashTerrainLodBuildInput(changed) != originalHash, "minimum in input identity");
        Require(changed.Settings.PassPolicy != input.Settings.PassPolicy, "policy equality");
    }

    // 表中预期写死，独立覆盖下限两侧和单项工作，避免测试只是重写解析实现
    struct CountCase { std::size_t Items, Minimum, ExpectedAtEight; };
    for (const auto c : std::array<CountCase, 12>{{
            {0U, 0U, 0U}, {0U, 256U, 0U}, {1U, 0U, 1U}, {2U, 0U, 2U},
            {255U, 0U, 8U}, {2U, 256U, 1U}, {255U, 256U, 1U},
            {256U, 256U, 8U}, {257U, 256U, 8U}, {255U, 257U, 1U},
            {256U, 257U, 1U}, {257U, 257U, 8U}}})
    {
        Require(ResolveDataOrientedRoamWorkerCount(c.Items, 8U, c.Minimum) == c.ExpectedAtEight,
            "eight-worker boundary");
        Require(ResolveDataOrientedRoamWorkerCount(c.Items, 2U, c.Minimum) ==
            std::min(c.ExpectedAtEight, std::size_t{2U}), "two-worker boundary");
        Require(ResolveDataOrientedRoamWorkerCount(c.Items, 1U, c.Minimum) ==
            (c.Items == 0U ? 0U : 1U), "explicit single worker");
    }
    Require(ResolveDataOrientedRoamWorkerCount(255U, 8U,
        std::numeric_limits<std::size_t>::max()) == 1U, "maximum minimum");
    Require(ResolveDataOrientedRoamWorkerCount(100U, 17U, 0U) == 17U, "explicit above eight");
    const auto automatic = ResolveDataOrientedRoamWorkerCount(257U, 0U, 0U);
    const auto hint = std::thread::hardware_concurrency();
    Require(automatic == (hint == 0U ? 1U : std::min(std::size_t{8U}, std::size_t{hint})),
        "automatic hardware hint");

    DataOrientedRoamState empty;
    empty.Settings.PassPolicy = MakeTerrainLodMaximumSafeParallelIncrementalPolicy();
    empty.Settings.PassPolicy.MergeScoreMinParallelEntryCount = 0U;
    empty.Settings.PassPolicy.SplitScoreMinParallelEntryCount = 0U;
    RefreshPersistentMergeQueuePriorities(empty);
    RefreshPersistentSplitQueuePriorities(empty);
    Require(empty.Stats.MergeCandidateMarkWorkerCount == 0U &&
        empty.Stats.SplitCandidateMarkWorkerCount == 0U, "empty score phases");
}

void CheckThreadDispatch()
{
    DataOrientedRoamThreadPool pool;
    DataOrientedRoamState state;
    state.ThreadPool = &pool;
    std::mutex mutex;
    std::condition_variable ready;
    std::set<std::thread::id> ids;
    std::array<bool, 2> visited{};
    bool timedOut = false;
    RunDataOrientedRoamWorkers(state, 2U, [&](std::size_t index) {
        std::unique_lock lock{mutex};
        ids.insert(std::this_thread::get_id());
        visited[index] = true;
        ready.notify_all();
        // 两个任务互相等待进入，避免极短任务恰好被一个线程全部领取
        if (!ready.wait_for(lock, std::chrono::seconds{5}, [&] { return ids.size() == 2U; }))
            timedOut = true;
    });
    Require(!timedOut && ids.size() == 2U && visited[0] && visited[1] &&
        !ids.contains(std::this_thread::get_id()), "real pool execution");
    std::cout << "Controlled dispatch: distinct OS threads=" << ids.size() << '\n';
    state.ThreadPool = nullptr;
    ids.clear();
    RunDataOrientedRoamWorkers(state, 8U, [&](std::size_t) { ids.insert(std::this_thread::get_id()); });
    Require(ids.size() == 1U && ids.contains(std::this_thread::get_id()), "no-pool fallback");
    bool called = false;
    RunDataOrientedRoamWorkers(state, 0U, [&](std::size_t) { called = true; });
    Require(!called, "zero work dispatch");
}

TerrainLodViewInput MakeView()
{
    const glm::vec3 eye{0.0F, 12.0F, 25.0F};
    const glm::vec3 target{0.0F};
    return BuildTerrainLodViewInput(
        glm::lookAt(eye, target, glm::vec3{0.0F, 1.0F, 0.0F}),
        glm::perspective(glm::radians(60.0F), 16.0F / 9.0F, 0.1F, 200.0F),
        eye, glm::normalize(target - eye), 1280U, 720U, false);
}

template <typename Entries>
auto ScoresByNode(const Entries& entries)
{
    std::map<DataOrientedRoamNodeIndex, float> scores;
    for (const auto& entry : entries)
        Require(scores.emplace(entry.Node, entry.Score).second, "duplicate queue member");
    return scores;
}

void CheckScores(const DataOrientedRoamState& source, bool merge)
{
    const auto count = merge ? source.MergeQueue.size() : source.SplitQueue.size();
    Require(count >= 2U && count < 256U, "small score fixture coverage");
    const auto refresh = merge ? RefreshPersistentMergeQueuePriorities : RefreshPersistentSplitQueuePriorities;
    DataOrientedRoamState reference{source};
    refresh(reference);
    const auto expected = merge ? ScoresByNode(reference.MergeQueue) : ScoresByNode(reference.SplitQueue);
    for (std::size_t minimum : {0U, 256U})
    {
        for (std::size_t workers : {1U, 2U, 8U})
        {
            for (const auto action : {TerrainLodScoreRefreshAction::SerialRefresh,
                    TerrainLodScoreRefreshAction::ParallelRefresh, TerrainLodScoreRefreshAction::Automatic})
            {
                DataOrientedRoamState state{source};
                auto& policy = state.Settings.PassPolicy;
                policy.*Minimums[merge ? 0U : 1U] = minimum;
                (merge ? policy.MergeScore : policy.SplitScore) = action;
                (merge ? policy.MergeScoreWorkerCount : policy.SplitScoreWorkerCount) = workers;
                refresh(state);
                const auto effective = merge
                    ? state.Stats.MergeCandidateMarkWorkerCount : state.Stats.SplitCandidateMarkWorkerCount;
                const auto expectedWorkers = action == TerrainLodScoreRefreshAction::SerialRefresh ||
                    minimum == 256U ? 1U : std::min(workers, count);
                Require(effective == expectedWorkers, "score lower bound/action");
                Require((merge ? ScoresByNode(state.MergeQueue) : ScoresByNode(state.SplitQueue)) == expected,
                    "per-node score and membership equality");
                Require(state.ActiveLeafNodes == source.ActiveLeafNodes &&
                    CountPersistentQueueInvariantViolations(state) == 0U, "score invariants");
                Require(state.Settings.PassPolicy.*Minimums[merge ? 1U : 0U] == 256U &&
                    state.Settings.PassPolicy.MeshEmitMinParallelTriangleCount == 256U,
                    "independent score minimum");
            }
        }
    }
    std::cout << (merge ? "Merge" : "Split") << " score fixture entries=" << count << '\n';
}

void RequireMeshEqual(const Terrain::TerrainMeshData& left, const DataOrientedRoamState& source)
{
    const auto& right = source.IncrementalMesh.Data;
    Require(left.Indices == right.Indices && left.Vertices.size() == right.Vertices.size(), "mesh layout");
    for (std::size_t index = 0U; index < left.Vertices.size(); ++index)
    {
        const auto& a = left.Vertices[index];
        const auto& b = right.Vertices[index];
        Require(a.Position == b.Position && a.Normal == b.Normal && a.TexCoord == b.TexCoord &&
            a.Height == b.Height && a.DebugColor == b.DebugColor && a.DebugHighlight == b.DebugHighlight,
            "mesh vertex values");
    }
    std::vector<std::uint64_t> paths;
    for (const auto node : source.IncrementalMesh.SlotOwners)
        paths.push_back(source.Nodes.PathIdAt(node));
    Require(HashTerrainLodNormalizedMesh(left, paths) == HashTerrainLodNormalizedMesh(right, paths),
        "normalized mesh");
}

void PrepareDirtyMesh(DataOrientedRoamState& state, bool dirty)
{
    BeginIncrementalMeshUpdate(state, false);
    // 有效拓扑保持不变，只请求重写两个已存在槽位；重复项应在提交前归一化
    state.IncrementalMesh.DebugTransitionLeaves.clear();
    if (dirty)
    {
        state.IncrementalMesh.DirtySlots = {0U, 1U, 1U};
        state.IncrementalMesh.Data.Vertices[0].Position.x = -9999.0F;
        state.IncrementalMesh.Data.Vertices[3].Position.x = -9999.0F;
    }
}

void CheckMesh(const DataOrientedRoamState& source)
{
    Require(source.IncrementalMesh.SlotOwners.size() >= 2U, "mesh fixture coverage");
    for (std::size_t minimum : {0U, 256U})
    {
        for (std::size_t workers : {1U, 2U, 8U})
        {
            for (const auto action : {TerrainLodMeshEmitAction::SerialDirty,
                    TerrainLodMeshEmitAction::ParallelDirty, TerrainLodMeshEmitAction::Automatic})
            {
                DataOrientedRoamState state{source};
                PrepareDirtyMesh(state, true);
                state.Settings.PassPolicy.MeshEmit = action;
                state.Settings.PassPolicy.MeshEmitWorkerCount = workers;
                state.Settings.PassPolicy.MeshEmitMinParallelTriangleCount = minimum;
                ApplyIncrementalMeshUpdates(state);
                const auto expected = action == TerrainLodMeshEmitAction::SerialDirty ||
                    minimum == 256U ? 1U : std::min(workers, std::size_t{2U});
                Require(state.Stats.EmitWorkerCount == expected &&
                    state.IncrementalMesh.DirtySlots.size() == 2U, "dirty work rather than active mesh");
                RequireMeshEqual(state.IncrementalMesh.Data, source);
                FinalizeIncrementalMeshUpdate(state);
                ValidateIncrementalMesh(state);
                Require(state.Stats.InvalidTopologyCount == 0U, "mesh topology invariant");
                Require(state.Settings.PassPolicy.MergeScoreMinParallelEntryCount == 256U &&
                    state.Settings.PassPolicy.SplitScoreMinParallelEntryCount == 256U, "independent mesh minimum");
            }
        }
    }

    DataOrientedRoamState emptyDirty{source};
    PrepareDirtyMesh(emptyDirty, false);
    emptyDirty.Settings.PassPolicy.MeshEmit = TerrainLodMeshEmitAction::ParallelDirty;
    emptyDirty.Settings.PassPolicy.MeshEmitMinParallelTriangleCount = 0U;
    ApplyIncrementalMeshUpdates(emptyDirty);
    Require(emptyDirty.Stats.EmitWorkerCount == 0U, "no dirty work");

    DataOrientedRoamState full{source};
    PrepareDirtyMesh(full, false);
    full.Settings.PassPolicy.MeshEmit = TerrainLodMeshEmitAction::SerialFull;
    full.Settings.PassPolicy.MeshEmitWorkerCount = 8U;
    full.Settings.PassPolicy.MeshEmitMinParallelTriangleCount = 0U;
    for (auto& vertex : full.IncrementalMesh.Data.Vertices)
        vertex.Position.x = -9999.0F;
    ApplyIncrementalMeshUpdates(full);
    Require(full.Stats.EmitWorkerCount == 1U, "serial full ignores dirty set");
    RequireMeshEqual(full.IncrementalMesh.Data, source);
    std::cout << "Mesh fixture active=" << source.IncrementalMesh.SlotOwners.size() << ", dirty=2\n";
}

void CheckPublicMapping(const Terrain::HeightMap& heightMap, const TerrainLodViewInput& view)
{
    for (std::size_t phase = 0U; phase < Minimums.size(); ++phase)
    {
        DataOrientedRoamTerrainLodAlgorithm parallel;
        DataOrientedRoamTerrainLodAlgorithm fallback;
        TerrainLodBuildInput input{};
        input.HeightMap = &heightMap;
        input.View = view;
        input.Settings.TerrainSize = 30.0F;
        input.Settings.HeightScale = 4.0F;
        input.Settings.MaxDepth = 10;
        input.Settings.TriangleBudget = 128U;
        input.Settings.ScreenSpaceSplitThresholdPixels = 0.2F;
        input.Settings.ScreenSpaceMergeThresholdPixels = 0.1F;
        input.Settings.EnablePassEvidence = true;
        input.Settings.EnableTopologyValidation = true;
        input.Settings.PassPolicy = MakeTerrainLodMaximumSafeParallelIncrementalPolicy();
        input.Settings.PassPolicy.MergeTopology = TerrainLodTopologyAction::SerialImmediate;
        input.Settings.PassPolicy.SplitTopology = TerrainLodTopologyAction::SerialImmediate;
        input.Settings.PassPolicy.MergeScoreWorkerCount = 2U;
        input.Settings.PassPolicy.SplitScoreWorkerCount = 2U;
        input.Settings.PassPolicy.MeshEmitWorkerCount = 2U;
        bool observed = false;
        for (int frame = 0; frame < 3; ++frame)
        {
            TerrainLodRenderPacket parallelPacket, fallbackPacket;
            std::string error;
            input.Settings.PassPolicy.*Minimums[phase] = 0U;
            Require(parallel.BuildRenderData(input, parallelPacket, &error), "public parallel build: " + error);
            input.Settings.PassPolicy.*Minimums[phase] = 256U;
            Require(fallback.BuildRenderData(input, fallbackPacket, &error), "public fallback build: " + error);
            Require(ValidateTerrainLodResult(parallel.Stats(), parallelPacket, true).Passed &&
                ValidateTerrainLodResult(fallback.Stats(), fallbackPacket, true).Passed,
                "public result/resource contract");
            const auto pass = phase == 0U ? TerrainLodPassId::MergeScore
                : (phase == 1U ? TerrainLodPassId::SplitScore : TerrainLodPassId::MeshEmit);
            const auto index = static_cast<std::size_t>(pass);
            const auto& trace = parallel.Stats().PassTraces[index];
            const auto& baselineTrace = fallback.Stats().PassTraces[index];
            observed = observed || (trace.EffectiveWorkerCount == 2U && baselineTrace.EffectiveWorkerCount == 1U);
            Require(parallel.Stats().NormalizedMeshHash == fallback.Stats().NormalizedMeshHash &&
                parallel.Stats().ActiveTriangleCount == fallback.Stats().ActiveTriangleCount, "public result equivalence");
        }
        Require(observed, "public-to-DOD minimum must affect actual execution");
    }
}
} // 匿名命名空间

int main()
{
    try
    {
        CheckPolicyAndCounts();
        CheckThreadDispatch();
        Terrain::HeightMap heightMap;
        std::string error;
        Require(heightMap.LoadFromFile("assets/heightmaps/Hm_Terrain_Test_129.pgm", &error), error);
        const auto view = MakeView();
        DataOrientedRoamSettings settings{};
        settings.MaxDepth = 10;
        settings.TriangleBudget = 128U;
        settings.SplitThreshold = 0.2F;
        settings.MergeThreshold = 0.1F;
        settings.PassPolicy = MakeTerrainLodSerialIncrementalPolicy();
        settings.EnableTopologyValidation = true;
        settings.EnablePassEvidence = true;
        // 源 Pipeline 与高度图存活到全部副本结束；副本复用池但从不并发运行
        DataOrientedRoamPipeline pipeline;
        const auto& mesh = pipeline.Build(heightMap, 30.0F, 4.0F, view, settings);
        Require(!mesh.Indices.empty(), "valid source mesh");
        const auto sourceHash = HashTerrainLodMesh(mesh);
        const auto sourceMerge = ScoresByNode(pipeline.State().MergeQueue);
        const auto sourceSplit = ScoresByNode(pipeline.State().SplitQueue);
        Require(CountPersistentQueueInvariantViolations(pipeline.State()) == 0U, "source queue invariant");
        DataOrientedRoamState copy{pipeline.State()};
        Require(copy.Settings.PassPolicy == settings.PassPolicy && copy.ThreadPool == pipeline.State().ThreadPool,
            "whole policy and borrowed pool copy");
        copy.Settings.PassPolicy.MergeScoreMinParallelEntryCount = 0U;
        copy.Settings.PassPolicy.SplitScoreMinParallelEntryCount = 255U;
        copy.Settings.PassPolicy.MeshEmitMinParallelTriangleCount = 257U;
        const DataOrientedRoamState explicitCopy{copy};
        Require(explicitCopy.Settings.PassPolicy == copy.Settings.PassPolicy &&
            explicitCopy.ThreadPool == copy.ThreadPool, "copy preserves explicit minimums");
        CheckScores(pipeline.State(), true);
        CheckScores(pipeline.State(), false);
        CheckMesh(pipeline.State());
        Require(HashTerrainLodMesh(mesh) == sourceHash &&
            ScoresByNode(pipeline.State().MergeQueue) == sourceMerge &&
            ScoresByNode(pipeline.State().SplitQueue) == sourceSplit, "source remains untouched");

        settings.TriangleBudget = 512U;
        DataOrientedRoamPipeline large;
        (void)large.Build(heightMap, 30.0F, 4.0F, view, settings);
        Require(large.State().ActiveLeafNodes.size() > 256U, "large active mesh fixture");
        CheckMesh(large.State());
        CheckPublicMapping(heightMap, view);
        std::cout << "DOD pass policy contracts passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
