#include "DataOrientedRoamExperimentTestSupport.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamPipeline.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamPassInput.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamMeshPlan.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamTopologyPlan.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamWorkloadProbe.h"

#include <iostream>
#include <limits>

using namespace ParallelRoam;
using namespace Tests;

namespace
{
void CheckTopologyPlans()
{
    DataOrientedRoamState state;
    auto& nodes = state.Nodes;
    nodes.Domains.resize(12U);
    nodes.Depths.assign(12U, 0);
    nodes.LeftChildren.assign(12U, InvalidDataOrientedRoamNodeIndex);
    nodes.RightChildren.assign(12U, InvalidDataOrientedRoamNodeIndex);
    nodes.BaseNeighbors.assign(12U, InvalidDataOrientedRoamNodeIndex);
    nodes.LeftNeighbors = nodes.RightNeighbors = nodes.BaseNeighbors;
    nodes.IsSplits.assign(12U, 0U);
    nodes.InteriorChunkIds = {0U, 0U, 0U, 0U, 0U, 0U, 1U, 1U, 1U, 1U, 1U, 1U};
    nodes.PathIds = {90U, 80U, 2U, 3U, 4U, 5U, 70U, 7U, 8U, 9U, 10U, 11U};
    nodes.LeftChildren[0] = 2U; nodes.RightChildren[0] = 3U;
    nodes.LeftChildren[1] = 4U; nodes.RightChildren[1] = 5U;
    nodes.LeftChildren[6] = 8U; nodes.RightChildren[6] = 9U;
    state.Settings.MaxDepth = 4;
    state.Settings.EnableLocalConstraints = false;
    state.RemainingSerialSplitBudget = 1U;
    const std::vector<DataOrientedRoamSplitCandidate> splits{{8.0F, 2U, 0U}, {8.0F, 1U, 1U}, {10.0F, 0U, 6U}};
    state.SplitQueue = {{10.0F, 6U}, {8.0F, 1U}, {8.0F, 0U}};
    const StateSnapshot before{state};
    const auto limited = PlanDataOrientedRoamSplitTopology(state, splits);
    Require(before == StateSnapshot{state}, "topology planning changed source");
    Require(limited.InteriorCandidateCount == 3U && limited.BoundaryCandidateCount == 0U &&
        limited.ScheduledCandidateCount == 1U && limited.NonEmptyChunkCount == 1U &&
        limited.Chunks[1][0].Node == 6U && limited.Candidates[1].Node == 1U, "classification before budget and score order");
    state.RemainingSerialSplitBudget = 3U;
    const auto full = PlanDataOrientedRoamSplitTopology(state, splits);
    Require(full.ScheduledCandidateCount == 3U && full.NonEmptyChunkCount == 2U, "multiple nonempty chunks");
    nodes.LeftChildren[0] = InvalidDataOrientedRoamNodeIndex;
    nodes.Depths[1] = 4;
    nodes.BaseNeighbors[6] = 7U;
    state.Settings.EnableLocalConstraints = true;
    const auto boundary = PlanDataOrientedRoamSplitTopology(state, splits);
    Require(boundary.BoundaryCandidateCount == 3U && boundary.InteriorCandidateCount == 0U,
        "missing reusable children, depth limit and forced neighbor remain serial");

    nodes.LeftChildren[0] = 2U;
    nodes.BaseNeighbors[6] = InvalidDataOrientedRoamNodeIndex;
    nodes.IsSplits[0] = nodes.IsSplits[1] = nodes.IsSplits[6] = 1U;
    const std::vector<DataOrientedRoamMergeCandidate> merges{{1.0F, 0U}, {1.0F, 1U}, {0.5F, 6U}};
    const auto merged = PlanDataOrientedRoamMergeTopology(state, merges);
    Require(merged.Candidates[0].Node == 6U && merged.Candidates[1].Node == 1U &&
        merged.InteriorCandidateCount == 3U && merged.NonEmptyChunkCount == 2U, "merge score/path ordering");
    nodes.InteriorChunkIds[0] = InvalidDataOrientedRoamChunkId;
    nodes.IsSplits[4] = 1U;
    const auto mixed = PlanDataOrientedRoamMergeTopology(state, merges);
    Require(mixed.InteriorCandidateCount == 1U && mixed.BoundaryCandidateCount == 2U &&
        mixed.ScheduledCandidateCount == 1U, "merge boundary and non-leaf children");
}

void CheckMeshTransitions()
{
    // 独立小拓扑使槽位顺序预期可手算而无需复制生产算法
    DataOrientedRoamNodePool nodes;
    nodes.Domains.resize(6U);
    nodes.LeftChildren = {2U, 4U, 0U, 0U, 0U, 0U};
    nodes.RightChildren = {3U, 5U, 0U, 0U, 0U, 0U};
    nodes.IsSplits.assign(6U, 0U);
    nodes.ActivatedBuildIds.assign(6U, 0U);
    nodes.MergeBuildIds.assign(6U, 0U);
    std::vector<DataOrientedRoamNodeIndex> active{0U, 1U};
    DataOrientedRoamMeshMetadata mesh;
    BeginDataOrientedRoamMeshPlan(mesh, false);
    Require(ApplyDataOrientedRoamMeshPlan({nodes, active, 1U}, mesh), "root initialization");
    FinalizeDataOrientedRoamMeshPlan({nodes, active, 1U}, mesh);
    Require(mesh.SlotOwners == active && mesh.DirtySlots == active &&
        mesh.UpdateRanges.size() == 1U && mesh.UpdateRanges[0].TriangleCount == 2U, "root ranges");

    BeginDataOrientedRoamMeshPlan(mesh, false);
    nodes.IsSplits[0] = 1U;
    active = {2U, 1U, 3U};
    mesh.TopologyEdits = {{DataOrientedRoamMeshTopologyEditType::Split, 0U}};
    Require(!ApplyDataOrientedRoamMeshPlan({nodes, active, 2U}, mesh), "split without rebuild");
    FinalizeDataOrientedRoamMeshPlan({nodes, active, 2U}, mesh);
    Require(mesh.SlotOwners == active && mesh.DirtySlots == std::vector<DataOrientedRoamPosition>{0U, 2U}
        && mesh.UpdateRanges.size() == 2U, "left inherits and right appends");

    // 强制让保留的左子节点占尾槽以覆盖合并的防覆盖分支
    std::swap(mesh.SlotOwners[0], mesh.SlotOwners[2]);
    mesh.NodeSlots[2] = 2U;
    mesh.NodeSlots[3] = 0U;
    BeginDataOrientedRoamMeshPlan(mesh, false);
    nodes.IsSplits[0] = 0U;
    active = {0U, 1U};
    mesh.TopologyEdits = {{DataOrientedRoamMeshTopologyEditType::Merge, 0U}};
    Require(!ApplyDataOrientedRoamMeshPlan({nodes, active, 3U}, mesh), "tail child merge");
    Require(mesh.SlotOwners == active && mesh.NodeSlots[0] == 0U, "parent survives tail deletion");

    BeginDataOrientedRoamMeshPlan(mesh, false);
    mesh.TopologyEdits = {{DataOrientedRoamMeshTopologyEditType::Split, 0U},
        {DataOrientedRoamMeshTopologyEditType::Split, 1U},
        {DataOrientedRoamMeshTopologyEditType::Merge, 0U}};
    nodes.IsSplits[1] = 1U;
    active = {0U, 4U, 5U};
    Require(!ApplyDataOrientedRoamMeshPlan({nodes, active, 4U}, mesh) && mesh.SlotOwners == active,
        "sequential edits retain input order");
    BeginDataOrientedRoamMeshPlan(mesh, false);
    mesh.DebugTransitionLeaves = {0U};
    mesh.DirtySlots = {99U, 1U, 1U};
    Require(!ApplyDataOrientedRoamMeshPlan({nodes, active, 5U}, mesh) &&
        mesh.DirtySlots == std::vector<DataOrientedRoamPosition>{0U, 1U}, "debug aging and stale dirty slots");
    FinalizeDataOrientedRoamMeshPlan({nodes, active, 5U}, mesh);
    Require(mesh.UpdateRanges.size() == 1U && mesh.UpdateRanges[0].TriangleCount == 2U, "coalesced range");

    BeginDataOrientedRoamMeshPlan(mesh, false);
    mesh.TopologyEdits = {{DataOrientedRoamMeshTopologyEditType::Merge, 5U}};
    Require(ApplyDataOrientedRoamMeshPlan({nodes, active, 6U}, mesh) && mesh.SlotOwners == active,
        "inconsistent edits fall back to final leaves");
    mesh.Generation = std::numeric_limits<std::uint64_t>::max();
    BeginDataOrientedRoamMeshPlan(mesh, false);
    Require(mesh.Generation == 1U && std::all_of(mesh.SlotDirtyGenerations.begin(),
        mesh.SlotDirtyGenerations.end(), [](auto value) { return value == 0U; }), "generation wrap");
    Require(!ApplyDataOrientedRoamMeshPlan({nodes, active, 7U}, mesh) && mesh.DirtySlots.empty(), "no work");
    active.clear();
    BeginDataOrientedRoamMeshPlan(mesh, true);
    Require(ApplyDataOrientedRoamMeshPlan({nodes, active, 8U}, mesh), "empty initialization");
    FinalizeDataOrientedRoamMeshPlan({nodes, active, 8U}, mesh);
    Require(mesh.DirtySlots.empty() && mesh.UpdateRanges.empty(), "empty plan");
}

void CheckIdentity(const DataOrientedRoamState& source, TerrainLodPassId pass)
{
    const auto original = HashDataOrientedRoamPassInput(source, pass);
    DataOrientedRoamState clone{source};
    clone.Stats.EmitMilliseconds += 1000.0F;
    clone.Settings.PassPolicy = MakeTerrainLodMaximumSafeParallelFullOutputPolicy();
    Require(HashDataOrientedRoamPassInput(clone, pass) == original, "address timing and strategy excluded");
    const auto changed = [&](const auto& mutate) {
        DataOrientedRoamState state{source};
        mutate(state);
        Require(HashDataOrientedRoamPassInput(state, pass) != original, "meaningful input mutation detected");
    };
    changed([](auto& state) { ++state.RemainingSerialSplitBudget; });
    changed([](auto& state) { ++state.BuildSequence; });
    changed([](auto& state) { state.ViewProjection[0][0] += 1.0F; });
    changed([](auto& state) { state.PreviousSplitPaths.insert(999999999U); });
    changed([](auto& state) { ++state.IncrementalMesh.Metadata.Generation; });
    changed([](auto& state) { ++state.NodeMembership[0].MergeQueuePosition; });
    changed([](auto& state) { state.Nodes.ScreenErrors[0] += 1.0F; });
    if (source.SplitQueue.size() > 1U)
        changed([](auto& state) { std::swap(state.SplitQueue[0], state.SplitQueue[1]); });
    if (!source.IncrementalMesh.Data.Vertices.empty())
        changed([](auto& state) { state.IncrementalMesh.Data.Vertices[0].Position.x += 1.0F; });
}
}

int main()
{
    try
    {
        CheckTopologyPlans();
        CheckMeshTransitions();
        DataOrientedRoamState empty;
        for (const auto pass : {TerrainLodPassId::MergeScore, TerrainLodPassId::SplitScore,
            TerrainLodPassId::MergeTopology, TerrainLodPassId::SplitTopology, TerrainLodPassId::MeshEmit})
        {
            empty.Settings.TriangleBudget = 0U;
            empty.Settings.MaxDepth = 0;
            const auto work = ProbeDataOrientedRoamPassWorkload(empty, pass);
            Require(work.PrimaryWorkValue == 0.0F && std::all_of(work.AuxiliaryFeatures.begin(),
                work.AuxiliaryFeatures.end(), [](auto value) { return value == 0.0F; }), "zero denominators");
        }
        Terrain::HeightMap height;
        std::string error;
        Require(height.LoadFromFile("assets/heightmaps/Hm_Terrain_Test_129.pgm", &error), error);
        DataOrientedRoamSettings settings;
        settings.MaxDepth = 10;
        settings.TriangleBudget = 2048U;
        settings.PassPolicy = MakeTerrainLodSerialIncrementalPolicy();
        settings.EnableTopologyValidation = true;
        DataOrientedRoamPipeline pipeline;
        std::size_t observed = 0U;
        for (std::size_t frame = 0U; frame < 12U; ++frame)
        {
            DataOrientedRoamPassWorkload meshWork;
            (void)pipeline.BuildWithPassObserver(height, 32.0F, 8.0F, ExperimentView(frame), settings,
                [&](const auto& state, auto pass) {
                    const StateSnapshot before{state};
                    const auto work = ProbeDataOrientedRoamPassWorkload(state, pass);
                    CheckIdentity(state, pass);
                    Require(before == StateSnapshot{state}, "probe/hash changed source storage or fields");
                    Require(work.PreActiveTriangleCount == state.ActiveLeafNodes.size(), "boundary active count");
                    if (pass == TerrainLodPassId::MeshEmit)
                        meshWork = work;
                    ++observed;
                });
            Require(meshWork.PlanningDirtyTriangleCount == pipeline.Stats().MeshUpdatedTriangleCount &&
                meshWork.PlanningDirtyRangeCount == pipeline.Stats().MeshDirtyRangeCount, "predicted mesh writes/ranges");
            Require(pipeline.Stats().InvalidTopologyCount == 0U, "production remains valid");
        }
        Require(observed == 60U, "all physical boundaries inspected");
        std::cout << "Read-only probes, identities and mesh transitions verified\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
