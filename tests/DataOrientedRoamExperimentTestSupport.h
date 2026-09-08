#pragma once

#include "algorithms/ITerrainLodAlgorithm.h"
#include "algorithms/TerrainLodView.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cstring>
#include <stdexcept>
#include <string>

namespace ParallelRoam::Tests
{
using namespace Algorithms;
using namespace Algorithms::DataOrientedRoam;

inline void Require(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error{message};
}

inline TerrainLodViewInput ExperimentView(std::size_t frame)
{
    const glm::vec3 eye = frame % 6U < 3U ? glm::vec3{0.0F, 12.0F, 25.0F}
        : glm::vec3{80.0F, 90.0F, 140.0F};
    return BuildTerrainLodViewInput(glm::lookAt(eye, glm::vec3{0.0F}, glm::vec3{0.0F, 1.0F, 0.0F}),
        glm::perspective(glm::radians(60.0F), 16.0F / 9.0F, 0.1F, 500.0F),
        eye, glm::normalize(-eye), 1280U, 720U, false);
}

/// <summary>
/// 源状态前后逐字节快照另含所有容器有效元素以避免仅凭规范化哈希判断只读
/// </summary>
class StateSnapshot
{
public:
    explicit StateSnapshot(const DataOrientedRoamState& state);
    [[nodiscard]] bool operator==(const StateSnapshot&) const = default;

private:
    template<class T>
    void Scalar(const T& value)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        const auto* bytes = reinterpret_cast<const unsigned char*>(&value);
        _bytes.insert(_bytes.end(), bytes, bytes + sizeof(T));
    }

    template<class T>
    void Sequence(const std::vector<T>& values)
    {
        // 原对象地址与容量也必须不变以发现意外重新分配
        Scalar(values.data());
        Scalar(values.size());
        Scalar(values.capacity());
        for (const auto& value : values)
            Scalar(value);
    }

    std::vector<unsigned char> _bytes;
};

inline StateSnapshot::StateSnapshot(const DataOrientedRoamState& state)
{
    Scalar(state.HeightMap);
    Scalar(state.ThreadPool);
    Scalar(state.Settings);
    Scalar(state.Stats);
    Scalar(state.VarianceHeightMap);
    Scalar(state.VarianceTreeMaxDepth);
    for (const auto& tree : state.VarianceTrees)
        Sequence(tree);
    for (const auto* paths : {&state.PreviousSplitPaths, &state.CurrentSplitPaths})
    {
        Scalar(paths->size());
        Scalar(paths->bucket_count());
        for (const auto path : *paths)
            Scalar(path);
    }
    Sequence(state.ActiveInternalNodes);
    Sequence(state.ActiveLeafNodes);
    Sequence(state.NodeMembership);
    Sequence(state.SplitQueue);
    Sequence(state.SplitQueueBlockedBuildIds);
    Sequence(state.MergeQueue);
    Scalar(state.RootA);
    Scalar(state.RootB);
    Scalar(state.ViewProjection);
    Scalar(state.FrustumPlanes);
    Scalar(state.DrawableWidth);
    Scalar(state.DrawableHeight);
    Scalar(state.RemainingSerialSplitBudget);
    Scalar(state.RemainingParallelSplitBudget.load());
    Scalar(state.TerrainSize);
    Scalar(state.HeightScale);
    Scalar(state.TopologyMaxDepth);
    Scalar(state.BuildSequence);
    const auto& mesh = state.IncrementalMesh.Metadata;
    Sequence(mesh.NodeSlots);
    Sequence(mesh.SlotOwners);
    Sequence(mesh.SlotDirtyGenerations);
    Sequence(mesh.DirtySlots);
    Sequence(mesh.UpdateRanges);
    Sequence(mesh.DebugTransitionLeaves);
    Sequence(mesh.TopologyEdits);
    Scalar(mesh.Generation);
    Scalar(mesh.RequiresFullUpload);
    Scalar(mesh.NeedsInitialization);
    Scalar(mesh.TracksTopologyEdits);
    const auto& data = state.IncrementalMesh.Data;
    Sequence(data.Vertices);
    Sequence(data.Indices);
    Scalar(data.GridWidth);
    Scalar(data.GridHeight);
    Scalar(data.TerrainSize);
    Scalar(data.HeightScale);
    Sequence(state.Nodes.Domains);
    Sequence(state.Nodes.Parents);
    Sequence(state.Nodes.LeftChildren);
    Sequence(state.Nodes.RightChildren);
    Sequence(state.Nodes.BaseNeighbors);
    Sequence(state.Nodes.LeftNeighbors);
    Sequence(state.Nodes.RightNeighbors);
    Sequence(state.Nodes.InteriorChunkIds);
    Sequence(state.Nodes.GeometricErrors);
    Sequence(state.Nodes.ScreenErrors);
    Sequence(state.Nodes.VarianceIndices);
    Sequence(state.Nodes.PathIds);
    Sequence(state.Nodes.CreatedBuildIds);
    Sequence(state.Nodes.ActivatedBuildIds);
    Sequence(state.Nodes.SplitBuildIds);
    Sequence(state.Nodes.MergeBuildIds);
    Sequence(state.Nodes.Depths);
    Sequence(state.Nodes.VarianceTreeIndices);
    Sequence(state.Nodes.ActivatedByForcedSplits);
    Sequence(state.Nodes.IsSplits);
}
}
