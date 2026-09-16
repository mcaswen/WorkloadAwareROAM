#include "algorithms/greedy_transactional_lod/TransactionalBoundaryRefinement.h"
#include "algorithms/greedy_transactional_lod/TransactionalCertification.h"
#include "algorithms/greedy_transactional_lod/TransactionalPredicates.h"

#include <algorithm>
#include <cmath>

namespace ParallelRoam::Algorithms::GreedyTransactionalLod
{
namespace
{
using Predicates = TransactionalPredicates;

std::vector<std::array<Identity, 3>> Canonical(std::vector<std::array<Identity, 3>> faces)
{
    for (auto& face : faces)
    {
        std::rotate(face.begin(), std::min_element(face.begin(), face.end()), face.end());
    }
    std::sort(faces.begin(), faces.end());
    return faces;
}
}

Proposal TransactionalBoundaryRefinement::Construct(const TransactionalState& state, const HeightSource& source,
    Slot root, Edge edge, Identity newVertex)
{
    Proposal result;
    result.Kind = 'B';
    result.Root = root;
    result.NewVertex = newVertex;
    result.Reason = "boundary_not_single_sided";
    edge = EdgeKey(edge[0], edge[1]);
    const auto found = state.Edges().find(edge);
    if (found == state.Edges().end() || found->second.Count != 1 || found->second.Faces[0] != root)
    {
        return result;
    }
    const auto& first = state.Vertex(edge[0]).Geometry;
    const auto& second = state.Vertex(edge[1]).Geometry;
    const bool horizontal = first.V == second.V && (first.V == 0 || first.V == 1);
    const bool vertical = first.U == second.U && (first.U == 0 || first.U == 1);
    result.Reason = "boundary_not_domain_edge";
    if (!horizontal && !vertical)
    {
        return result;
    }
    Point midpoint{(first.U + second.U) * .5, (first.V + second.V) * .5, 0};
    result.Reason = "boundary_resolution_limit";
    if (source.Width < 2 || source.Height < 2)
    {
        return result;
    }
    const double interval = 1.0 / ((horizontal ? source.Width : source.Height) - 1);
    const double left = horizontal ? std::abs(midpoint.U - first.U) : std::abs(midpoint.V - first.V);
    const double right = horizontal ? std::abs(midpoint.U - second.U) : std::abs(midpoint.V - second.V);
    result.Reason = "boundary_resolution_limit";
    // 旧种子短边允许保留，但新增分段不突破源尺度，也不依赖浮点舍入继续细化
    if (left < interval || right < interval)
    {
        return result;
    }
    const auto distinct = [&](const Point& point) {
        return static_cast<float>(midpoint.U) != static_cast<float>(point.U) ||
            static_cast<float>(midpoint.V) != static_cast<float>(point.V);
    };
    result.Reason = "boundary_midpoint_collapsed";
    if (!distinct(first) || !distinct(second))
    {
        return result;
    }
    result.Reason = "boundary_invalid_identity";
    if (newVertex > state.NextVertexId() || newVertex == std::numeric_limits<Identity>::min())
    {
        return result;
    }
    midpoint.Height = TransactionalSamples::SourceHeightAt(source, midpoint.U, midpoint.V, state.Config().HeightScale);
    const auto& face = state.Face(root).Vertices;
    result.Support = {root};
    for (const auto id : face)
    {
        result.Points.emplace(id, state.Vertex(id).Geometry);
    }
    result.Points.emplace(newVertex, midpoint);
    result.Free = {newVertex};
    for (std::size_t index = 0; index < face.size(); ++index)
    {
        const auto a = face[index];
        const auto b = face[(index + 1) % face.size()];
        if (EdgeKey(a, b) != edge)
        {
            result.Faces.push_back({a, b, newVertex});
        }
    }
    result.Reason = "shape_infeasible";
    for (const auto& triangle : result.Faces)
    {
        if (!Predicates::Shape(result.Points.at(triangle[0]), result.Points.at(triangle[1]),
            result.Points.at(triangle[2])))
        {
            return result;
        }
    }
    result.Reason.clear();
    return result;
}

std::string TransactionalBoundaryRefinement::Certify(const TransactionalState& state,
    const TransactionalSamples& samples, Proposal& proposal, WorkLedger& work)
{
    ++work.BoundaryAttempts;
    if (!proposal.Reason.empty())
    {
        if (proposal.Reason == "boundary_resolution_limit")
        {
            ++work.BoundaryResolutionRejected;
        }
        return proposal.Reason;
    }
    proposal.Samples = samples.VisibleSupport(proposal.Support);
    proposal.Reason = TransactionalCertification::SetProgressTarget(state, samples, proposal, work);
    if (proposal.Reason.empty())
    {
        // 新点已经固定取源高，不进入基于旧曲面的自由高度拟合
        if (!TransactionalCertification::Measure(state, samples, proposal, work))
        {
            proposal.Reason = "projection_unknown";
        }
        else
        {
            proposal.Reason = TransactionalCertification::Accepts(state, samples, proposal,
                proposal.TargetMicropixels, work) ? "certified" : "quality_infeasible";
        }
    }
    if (proposal.Reason == "certified")
    {
        ++work.BoundaryCertified;
    }
    return proposal.Reason;
}

bool TransactionalBoundaryRefinement::IsSourceMidpointSplit(const TransactionalState& state,
    const HeightSource& source, const Proposal& proposal)
{
    if (proposal.Kind != 'B' || proposal.Support != std::vector<Slot>{proposal.Root} ||
        proposal.Faces.size() != 2 || proposal.Points.size() != 4 ||
        proposal.Free != std::vector<Identity>{proposal.NewVertex})
    {
        return false;
    }
    const auto& face = state.Face(proposal.Root).Vertices;
    // 三边有界重建，同时核对源高度与固定旧点，不能仅凭净面数放行
    for (std::size_t index = 0; index < face.size(); ++index)
    {
        const auto expected = Construct(state, source, proposal.Root,
            EdgeKey(face[index], face[(index + 1) % face.size()]), proposal.NewVertex);
        if (expected.Reason.empty() && expected.Points == proposal.Points &&
            Canonical(expected.Faces) == Canonical(proposal.Faces))
        {
            return true;
        }
    }
    return false;
}
}
