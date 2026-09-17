#include "algorithms/greedy_transactional_lod/TransactionalSourceHeightReceiver.h"

#include "algorithms/greedy_transactional_lod/TransactionalPredicates.h"
#include "profiling/CpuProfiling.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace ParallelRoam::Algorithms::GreedyTransactionalLod
{
std::string TransactionalSourceHeightReceiver::Prepare(const TransactionalState& state,
    const TransactionalSamples& samples, Proposal& proposal, WorkLedger& work)
{
    ROAM_CPU_ZONE("gtp.receiver.source_height");
    work.CheckLimit();
    ++work.SourceHeightAttempts;
    ++work.Proposals;
    proposal.Reason.clear();
    proposal.QualityProof.reset();
    proposal.HeightProof.reset();
    proposal.HasLegacyQualityEvidence = false;

    const auto& config = state.Config();
    if (config.ReceiverHeightPolicy != TransactionalReceiverHeightPolicy::SourceHeight ||
        config.QualityPolicy != TransactionalQualityPolicy::PointwiseTarget ||
        !config.PreserveSurvivingHeights || config.HeightGuard ||
        config.ReceiverOrder != TransactionalReceiverOrder::ErrorFirst)
    {
        throw std::invalid_argument("源高度准备与当前质量配置不一致");
    }

    // 只接受游标的有界目录项，不扩成任意局部重剖分的验证入口
    if ((proposal.Kind != 'E' && proposal.Kind != 'F' && proposal.Kind != 'H') ||
        proposal.Free.size() != 1 || proposal.Free.front() != proposal.NewVertex ||
        !proposal.Points.contains(proposal.NewVertex) ||
        !std::binary_search(proposal.Support.begin(), proposal.Support.end(), proposal.Root))
    {
        return "source_structure_invalid";
    }
    const auto offset = static_cast<Identity>(proposal.Root) * 8;
    if (state.NextVertexId() <= std::numeric_limits<Identity>::min() + offset + 7)
    {
        throw std::runtime_error("源高提案身份空间用尽");
    }
    const auto first = state.NextVertexId() - offset;
    if (proposal.NewVertex > first || proposal.NewVertex < first - 7)
    {
        return "source_identity_invalid";
    }

    // 只遍历支持内的旧点，避免把一次局部准备变成活动网格扫描
    std::set<Identity> oldVertices;
    Slot previous = InvalidSlot;
    for (auto slot : proposal.Support)
    {
        if (slot >= state.Faces().size() || state.Faces()[slot].ActivePosition == InvalidSlot ||
            (previous != InvalidSlot && slot <= previous))
        {
            return "source_structure_invalid";
        }
        previous = slot;
        for (auto id : state.Face(slot).Vertices)
        {
            const auto found = proposal.Points.find(id);
            if (id == proposal.NewVertex || found == proposal.Points.end() ||
                found->second != state.Vertex(id).Geometry)
            {
                return "source_old_geometry_changed";
            }
            oldVertices.insert(id);
        }
    }
    if (proposal.Points.size() != oldVertices.size() + 1)
    {
        return "source_structure_invalid";
    }

    auto& point = proposal.Points.at(proposal.NewVertex);
    if (!std::isfinite(point.U) || !std::isfinite(point.V) ||
        point.U < 0 || point.U > 1 || point.V < 0 || point.V > 1)
    {
        return "source_coordinate_invalid";
    }
    // 源高度是一个确定试值；不得夹到可行域、搜索其他高度或回退拟合
    point.Height = TransactionalSamples::SourceHeightAt(samples.Source(), point.U, point.V, config.HeightScale);
    if (!std::isfinite(point.Height) || point.Height < -config.HeightScale || point.Height > 2 * config.HeightScale)
    {
        return "source_height_invalid";
    }
    for (const auto& face : proposal.Faces)
    {
        if (!proposal.Points.contains(face[0]) || !proposal.Points.contains(face[1]) ||
            !proposal.Points.contains(face[2]))
        {
            return "source_structure_invalid";
        }
        if (!TransactionalPredicates::Shape(proposal.Points.at(face[0]), proposal.Points.at(face[1]),
                proposal.Points.at(face[2])))
        {
            // 保留原形状失败语义；质量失败不能因此获得翻边重试资格
            return "shape_infeasible";
        }
    }
    // 舍入后的边中点可能形成额外薄面，先保留旧入口的形状拒绝与翻边资格。
    // 只有原形状判据通过后，才检查准备结果的净面数。
    if (proposal.Faces.size() != proposal.Support.size() + 2)
    {
        return "source_structure_invalid";
    }
    ++work.SourceHeightPrepared;
    return {};
}
}
