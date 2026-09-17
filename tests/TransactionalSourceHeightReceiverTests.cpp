#include "algorithms/greedy_transactional_lod/TransactionalSourceHeightReceiver.h"

#include "algorithms/greedy_transactional_lod/TransactionalCertification.h"
#include "algorithms/greedy_transactional_lod/TransactionalPipeline.h"
#include "algorithms/greedy_transactional_lod/TransactionalPointwiseQuality.h"
#include "algorithms/greedy_transactional_lod/TransactionalProposals.h"
#include "algorithms/greedy_transactional_lod/TransactionalReservation.h"

#include <iostream>
#include <stdexcept>

namespace
{
using namespace ParallelRoam::Algorithms;
using namespace ParallelRoam::Algorithms::GreedyTransactionalLod;

void Require(bool value, const char* message)
{
    if (!value)
    {
        throw std::runtime_error(message);
    }
}

template<class Action>
void MustReject(Action action, const char* message)
{
    bool rejected = false;
    try
    {
        action();
    }
    catch (const std::exception&)
    {
        rejected = true;
    }
    Require(rejected, message);
}

/// <summary>
/// 冻结旧四角的误差，源高中心能改善内部样本而不改变外接口。
/// </summary>
InitialMesh Square()
{
    InitialMesh input;
    input.Config.Budget = 8;
    input.Config.PreserveSurvivingHeights = true;
    input.Config.ReceiverOrder = TransactionalReceiverOrder::ErrorFirst;
    input.Config.QualityPolicy = TransactionalQualityPolicy::PointwiseTarget;
    input.Config.ReceiverHeightPolicy = TransactionalReceiverHeightPolicy::SourceHeight;
    input.Source = {3, 3, std::vector<std::uint16_t>(9, 0)};
    input.Vertices = {{0, {0, 0, 1}}, {1, {1, 0, 1}}, {2, {1, 1, 1}}, {3, {0, 1, 1}}};
    input.Faces = {{0, {0, 1, 2}}, {1, {0, 2, 3}}};
    return input;
}

Proposal EdgeProposal(TransactionalPipeline& pipeline)
{
    ReceiverCursor cursor(pipeline.State(), pipeline.Samples(), 0);
    auto proposal = cursor.Next();
    Require(proposal && proposal->Kind == 'E', "夹具必须使用真实游标的共享边提案");
    return *proposal;
}

void PreparationAndPublication()
{
    const auto input = Square();
    TransactionalPipeline pipeline(input);
    WorkLedger work;
    pipeline.Initialize(work);
    auto proposal = EdgeProposal(pipeline);
    Require(proposal.Points.at(proposal.NewVertex).Height == 1, "旧插值起点不正确");
    Require(TransactionalSourceHeightReceiver::Prepare(pipeline.State(), pipeline.Samples(), proposal, work).empty(),
        "源高结构准备失败");
    Require(proposal.Points.at(proposal.NewVertex).Height == 0 && proposal.Reason.empty() && !proposal.QualityProof,
        "准备必须取源高且不能伪造认证");
    Require(work.SourceHeightAttempts == 1 && work.SourceHeightPrepared == 1, "准备计数不完整");
    for (const auto& [id, point] : input.Vertices)
    {
        Require(proposal.Points.at(id) == point, "源高准备修改了旧点");
    }
    MustReject([&] { TransactionalCertification::Accepts(pipeline.State(), pipeline.Samples(), proposal, 0, work); },
        "无旧证据的提案被默认零门槛接纳");

    CertifiedBatch batch;
    batch.Version = pipeline.State().Version();
    proposal.Reason = "certified";
    batch.Exchanges.push_back({proposal, {}, false});
    MustReject([&] { pipeline.Apply(batch, work); }, "结构准备绕过逐点证书发布");
    const auto reason = TransactionalPointwiseQuality::Certify(pipeline.State(), pipeline.Samples(), proposal, true, work);
    Require(reason == "certified", reason.c_str());
    batch.Exchanges.front().Receiver = proposal;
    auto tampered = batch;
    tampered.Exchanges.front().Receiver.Points.at(proposal.NewVertex).Height = .25;
    MustReject([&] { pipeline.Apply(tampered, work); }, "证书接受被篡改的源高度");
    pipeline.Apply(batch, work);
    Require(pipeline.State().FaceCount() == 4 && pipeline.State().FaceCount() <= input.Config.Budget,
        "源高提交没有保持面数与预算");
    for (const auto& [id, point] : input.Vertices)
    {
        Require(pipeline.State().Vertex(id).Geometry == point, "提交修改了存活旧点");
    }
    MustReject([&] { pipeline.Apply(batch, work); }, "过期批次再次发布");
    auto changed = pipeline.State().Config();
    changed.ReceiverHeightPolicy = TransactionalReceiverHeightPolicy::LegacyFit;
    MustReject([&] { pipeline.SetView(changed, work); }, "高度政策被当成视图热切换");
    // 必须能继续发现和提交；空批缓存不应使下一轮使用旧拓扑的结果
    pipeline.Update(work);
    Require(pipeline.State().FaceCount() <= input.Config.Budget, "续接突破预算");
}

void RejectionAndConfiguration()
{
    const auto input = Square();
    for (int variant = 0; variant < 4; ++variant)
    {
        auto invalid = input;
        if (variant == 0)
        {
            invalid.Config.QualityPolicy = TransactionalQualityPolicy::Legacy;
        }
        if (variant == 1)
        {
            invalid.Config.PreserveSurvivingHeights = false;
        }
        if (variant == 2)
        {
            invalid.Config.HeightGuard = true;
        }
        if (variant == 3)
        {
            invalid.Config.ReceiverOrder = TransactionalReceiverOrder::Composite;
        }
        MustReject([&] { TransactionalPipeline pipeline(invalid); }, "非法源高组合未拒绝");
    }

    TransactionalPipeline pipeline(input);
    WorkLedger work;
    pipeline.Initialize(work);
    auto invalid = EdgeProposal(pipeline);
    invalid.Points.at(0).Height = .5;
    Require(!TransactionalSourceHeightReceiver::Prepare(pipeline.State(), pipeline.Samples(), invalid, work).empty(),
        "修改旧点的提案被接受");
    invalid = EdgeProposal(pipeline);
    invalid.Faces[0] = {0, 0, invalid.NewVertex};
    Require(TransactionalSourceHeightReceiver::Prepare(pipeline.State(), pipeline.Samples(), invalid, work) == "shape_infeasible",
        "退化面未被原形状判据拒绝");
    invalid = EdgeProposal(pipeline);
    invalid.Faces.push_back({0, 0, invalid.NewVertex});
    auto legacy = invalid;
    Require(TransactionalCertification::Fit(pipeline.State(), pipeline.Samples(), legacy, work) == "shape_infeasible",
        "额外退化面的旧拒绝语义改变");
    const auto shapeReason = TransactionalSourceHeightReceiver::Prepare(pipeline.State(), pipeline.Samples(), invalid, work);
    Require(shapeReason == "shape_infeasible" && TransactionalProposals::NeedsFlipRecovery({{'E', shapeReason}}),
        "面数检查遮蔽了形状拒绝，意外抑制翻边资格");

    auto peak = input;
    for (auto& [id, point] : peak.Vertices)
    {
        static_cast<void>(id);
        point.Height = 0;
    }
    peak.Source.Values[4] = 65535;
    TransactionalPipeline peaked(peak);
    peaked.Initialize(work);
    auto source = EdgeProposal(peaked);
    Require(TransactionalSourceHeightReceiver::Prepare(peaked.State(), peaked.Samples(), source, work).empty(),
        "源高损伤反例构造失败");
    Require(TransactionalPointwiseQuality::Certify(peaked.State(), peaked.Samples(), source, true, work) != "certified",
        "源高误被当成全补丁质量安全证明");

    // 完全匹配参考的平面不能通过细分假造正进展
    peak.Source.Values[4] = 0;
    TransactionalPipeline flat(peak);
    flat.Initialize(work);
    source = EdgeProposal(flat);
    Require(TransactionalSourceHeightReceiver::Prepare(flat.State(), flat.Samples(), source, work).empty(),
        "平面结构准备失败");
    Require(TransactionalPointwiseQuality::Certify(flat.State(), flat.Samples(), source, true, work) != "certified",
        "无真实改善的源高提案被接受");
}
}

int main()
{
    try
    {
        PreparationAndPublication();
        RejectionAndConfiguration();
        std::cout << "source height preparation, guarded certification and continuation verified\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
