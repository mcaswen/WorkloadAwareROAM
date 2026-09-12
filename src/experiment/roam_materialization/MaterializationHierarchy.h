#pragma once

#include "experiment/roam_materialization/MaterializationTypes.h"

namespace ParallelRoam::Experiment::RoamMaterialization
{
/// <summary>
/// 按稳定路径解码有限规则层次，不持有完整潜在节点表
/// </summary>
class MaterializationHierarchy
{
public:
    [[nodiscard]] static int Depth(NodeId id);
    [[nodiscard]] static NodeId Parent(NodeId id);
    [[nodiscard]] static std::array<NodeId, 2> Children(NodeId id);
    [[nodiscard]] static Domain Decode(NodeId id);
    [[nodiscard]] static NodeId Mate(NodeId id);
    [[nodiscard]] static NodeId Group(NodeId id);
    [[nodiscard]] static std::vector<NodeId> Members(NodeId id);
    [[nodiscard]] static std::array<EdgeKey, 3> Edges(const Domain& domain);
    [[nodiscard]] static PointKey Point(glm::vec2 value);
    [[nodiscard]] static bool IsBoundary(const EdgeKey& edge);
};
}
