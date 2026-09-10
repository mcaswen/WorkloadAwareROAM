#include "DataOrientedRoamExperimentTestSupport.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamQueues.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamStateOps.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamTopologyPlan.h"

#include <array>
#include <iostream>

using namespace ParallelRoam;
using namespace Tests;

namespace
{
DataOrientedRoamState MakePlanInput()
{
    DataOrientedRoamState state;
    state.Settings.MaxDepth = 4;
    state.Settings.EnableLocalConstraints = false;
    state.RemainingSerialSplitBudget = 4U;
    const std::array<float, 4U> scores{9.0F, 8.0F, 8.0F, 6.0F};
    for (std::size_t index = 0U; index < scores.size(); ++index)
    {
        const TriangleDomain domain{glm::vec2{0.0F}, glm::vec2{0.01F, 0.0F}, glm::vec2{0.0F, 0.01F}};
        const auto node = AddNode(state, domain, InvalidDataOrientedRoamNodeIndex, 0, 90U - index * 10U, 0U, 0U);
        const auto left = AddNode(state, domain, node, 1, 1000U + index * 2U, 0U, 0U);
        const auto right = AddNode(state, domain, node, 1, 1001U + index * 2U, 0U, 0U);
        state.Nodes.LeftChildren[node] = left;
        state.Nodes.RightChildren[node] = right;
        for (const auto member : {node, left, right})
        {
            state.Nodes.InteriorChunkIds[member] = static_cast<DataOrientedRoamChunkId>(index);
        }
        state.NodeMembership[node].ActiveLeafPosition = static_cast<DataOrientedRoamPosition>(index);
        state.NodeMembership[node].SplitQueuePosition = static_cast<DataOrientedRoamPosition>(index);
        state.ActiveLeafNodes.push_back(node);
        state.SplitQueue.push_back({scores[index], node});
    }
    return state;
}

void RequireEmptyPlan(const DataOrientedRoamSplitPlan& plan,
    const std::vector<DataOrientedRoamSplitCandidate>& expectedCandidates,
    std::size_t interiorCount, std::size_t boundaryCount)
{
    Require(plan.Candidates.size() == expectedCandidates.size(), "empty prefix lost candidate records");
    for (std::size_t index = 0U; index < expectedCandidates.size(); ++index)
    {
        const auto& actual = plan.Candidates[index];
        const auto& expected = expectedCandidates[index];
        Require(actual.Node == expected.Node && actual.Score == expected.Score && actual.Sequence == expected.Sequence,
            "empty prefix changed candidate order or frozen values");
    }
    Require(plan.InteriorCandidateCount == interiorCount && plan.BoundaryCandidateCount == boundaryCount &&
        plan.ScheduledCandidateCount == 0U && plan.NonEmptyChunkCount == 0U, "empty prefix classification or counts");
    Require(plan.Chunks.size() == DataOrientedRoamTopologyChunkGridSize * DataOrientedRoamTopologyChunkGridSize,
        "empty prefix must retain the full chunk grid");
    for (const auto& chunk : plan.Chunks)
        Require(chunk.empty(), "empty prefix retained scheduled work");
}

void CheckOrderingAndReadonly()
{
    auto state = MakePlanInput();
    const std::vector<DataOrientedRoamSplitCandidate> shuffled{{8.0F, 0U, 3U}, {6.0F, 1U, 9U},
        {8.0F, 2U, 6U}, {9.0F, 3U, 0U}};
    const StateSnapshot before{state};
    const auto plan = PlanDataOrientedRoamSplitTopology(state, shuffled);
    Require(before == StateSnapshot{state}, "planning mutated source storage or state");
    const std::array<DataOrientedRoamNodeIndex, 4U> expected{0U, 6U, 3U, 9U};
    for (std::size_t index = 0U; index < expected.size(); ++index)
    {
        Require(plan.Candidates[index].Node == expected[index] && TopPersistentSplitQueueNode(state) == expected[index],
            "planner and real heap disagree on score/PathId order");
        RemovePersistentSplitQueueNode(state, expected[index]);
    }
    Require(plan.ScheduledCandidateCount == 4U && plan.NonEmptyChunkCount == 4U, "full safe prefix");
    Require(plan.InteriorCandidateCount == 4U && plan.BoundaryCandidateCount == 0U, "full safe classification");
    const std::array<DataOrientedRoamSplitCandidate, 4U> expectedChunks{{
        {9.0F, 3U, 0U}, {8.0F, 0U, 3U}, {8.0F, 2U, 6U}, {6.0F, 1U, 9U}}};
    for (std::size_t index = 0U; index < plan.Chunks.size(); ++index)
    {
        const auto& chunk = plan.Chunks[index];
        Require(chunk.size() == (index < expectedChunks.size() ? 1U : 0U), "full prefix chunk membership");
        if (!chunk.empty())
        {
            const auto& expectedChunk = expectedChunks[index];
            Require(chunk.front().Node == expectedChunk.Node && chunk.front().Score == expectedChunk.Score &&
                chunk.front().Sequence == expectedChunk.Sequence, "chunk changed frozen candidate values");
        }
    }
}

void CheckPrefixAndBudget()
{
    auto state = MakePlanInput();
    state.Nodes.InteriorChunkIds[0U] = InvalidDataOrientedRoamChunkId;
    const StateSnapshot unsafeBefore{state};
    auto plan = PlanDataOrientedRoamSplitTopology(state);
    const std::vector<DataOrientedRoamSplitCandidate> expected{{9.0F, 0U, 0U}, {8.0F, 2U, 6U},
        {8.0F, 1U, 3U}, {6.0F, 3U, 9U}};
    RequireEmptyPlan(plan, expected, 3U, 1U);
    Require(unsafeBefore == StateSnapshot{state}, "unsafe-head planning mutated source");
    auto middle = MakePlanInput();
    middle.Nodes.InteriorChunkIds[6U] = InvalidDataOrientedRoamChunkId;
    plan = PlanDataOrientedRoamSplitTopology(middle);
    Require(plan.ScheduledCandidateCount == 1U && plan.Chunks[0].front().Node == 0U &&
        plan.InteriorCandidateCount == 3U && plan.BoundaryCandidateCount == 1U,
        "a safe suffix cannot reopen a stopped prefix");
    auto budgetInput = MakePlanInput();
    for (const std::size_t budget : {0U, 1U, 3U, 4U, 5U})
    {
        budgetInput.RemainingSerialSplitBudget = budget;
        const StateSnapshot before{budgetInput};
        plan = PlanDataOrientedRoamSplitTopology(budgetInput);
        Require(plan.ScheduledCandidateCount == std::min<std::size_t>(budget, 4U) &&
            plan.InteriorCandidateCount == 4U && before == StateSnapshot{budgetInput}, "budget limit and readonly classification");
        if (budget == 0U)
            RequireEmptyPlan(plan, expected, 4U, 0U);
    }
}

void CheckSerialStopAndMergePriority()
{
    auto state = MakePlanInput();
    state.Settings.SplitThreshold = 10.0F;
    // 队列仍有条目，但资格过滤后没有候选；空计划也必须保留完整块形状
    const StateSnapshot emptyBefore{state};
    RequireEmptyPlan(PlanDataOrientedRoamSplitTopology(state), {}, 0U, 0U);
    Require(emptyBefore == StateSnapshot{state}, "empty-candidate planning mutated source");
    state.PreviousSplitPaths.insert(state.Nodes.PathIdAt(6U));
    const auto filtered = PlanDataOrientedRoamSplitTopology(state);
    Require(filtered.Candidates.size() == 1U && filtered.Candidates.front().Node == 6U &&
        filtered.ScheduledCandidateCount == 0U, "filtered hysteresis candidate cannot bypass a serial stop");
    // 同分停止项也按路径顺序比较，不能用快照的 Sequence 判断是否可越过
    auto ties = MakePlanInput();
    ties.Settings.SplitThreshold = 10.0F;
    ties.SplitQueue = {{8.0F, 6U}, {8.0F, 3U}, {8.0F, 0U}, {6.0F, 9U}};
    for (std::size_t index = 0U; index < ties.SplitQueue.size(); ++index)
        ties.NodeMembership[ties.SplitQueue[index].Node].SplitQueuePosition = static_cast<DataOrientedRoamPosition>(index);
    ties.PreviousSplitPaths.insert(ties.Nodes.PathIdAt(3U));
    Require(PlanDataOrientedRoamSplitTopology(ties).ScheduledCandidateCount == 0U,
        "higher-priority equal-score stop blocks a later eligible node");
    ties.PreviousSplitPaths.insert(ties.Nodes.PathIdAt(6U));
    Require(PlanDataOrientedRoamSplitTopology(ties).ScheduledCandidateCount == 2U,
        "eligible equal-score nodes before the first stop form a prefix");
    auto mergeInput = MakePlanInput();
    mergeInput.MergeQueue.push_back({mergeInput.Settings.MergeThreshold - 1.0F, 1U});
    const StateSnapshot mergeBefore{mergeInput};
    RequireEmptyPlan(PlanDataOrientedRoamSplitTopology(mergeInput),
        {{9.0F, 0U, 0U}, {8.0F, 2U, 6U}, {8.0F, 1U, 3U}, {6.0F, 3U, 9U}}, 4U, 0U);
    Require(mergeBefore == StateSnapshot{mergeInput}, "merge-first planning mutated source");
    mergeInput.MergeQueue.front().Score = mergeInput.Settings.MergeThreshold;
    Require(PlanDataOrientedRoamSplitTopology(mergeInput).ScheduledCandidateCount == 4U,
        "the serial merge-before-split comparison is strict");
}
}

int main()
{
    try
    {
        CheckOrderingAndReadonly();
        CheckPrefixAndBudget();
        CheckSerialStopAndMergePriority();
        std::cout << "Split priority prefix, stop boundaries and complete classification verified\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
