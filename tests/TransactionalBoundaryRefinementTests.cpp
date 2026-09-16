#include "algorithms/greedy_transactional_lod/TransactionalBoundaryRefinement.h"
#include "algorithms/greedy_transactional_lod/TransactionalCommit.h"
#include "algorithms/greedy_transactional_lod/TransactionalPipeline.h"
#include "algorithms/greedy_transactional_lod/TransactionalProposals.h"
#include "algorithms/greedy_transactional_lod/TransactionalReservation.h"
#include "experiment/greedy_transactional_lod/TransactionalValidation.h"

#include <iostream>
#include <optional>
#include <stdexcept>

namespace
{
using namespace ParallelRoam::Algorithms::GreedyTransactionalLod;
using ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalValidation;
using Boundary = TransactionalBoundaryRefinement;

void Require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

template<class Action> void Throws(Action&& action)
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
    Require(rejected, "应拒绝的输入被接受");
}

/// <summary>
/// 四个扇区覆盖单位域，底边参考高度含有旧曲面遗漏的峰值
/// 单侧新点可在保持旧点不动时取得可测的屏幕误差改善
/// </summary>
InitialMesh Fan()
{
    InitialMesh input;
    input.Config.Budget = 16;
    input.Config.PreserveSurvivingHeights = true;
    input.Config.EnableBoundaryRefinement = true;
    input.Vertices = {{0, {0, 0, 0}}, {1, {1, 0, 0}}, {2, {1, 1, 0}}, {3, {0, 1, 0}}, {4, {.5, .5, 0}}};
    input.Faces = {{0, {0, 1, 4}}, {1, {1, 2, 4}}, {2, {2, 3, 4}}, {3, {3, 0, 4}}};
    input.Source = {5, 5, {}};
    for (unsigned y = 0; y < 5; ++y)
    {
        for (unsigned x = 0; x < 5; ++x)
        {
            const double height = .25 * std::max(0.0, 1 - 2 * std::abs(x * .25 - .5) - 2 * y * .25);
            input.Source.Values.push_back(static_cast<std::uint16_t>(height * 65535));
        }
    }
    return input;
}

CertifiedBatch Batch(const TransactionalPipeline& pipeline, Proposal proposal)
{
    CertifiedBatch batch;
    batch.Version = pipeline.State().Version();
    batch.Exchanges.push_back({std::move(proposal), {}, false, ExchangeKind::BoundaryRefinement});
    return batch;
}

void GeometryAndCertificate()
{
    const auto input = Fan();
    TransactionalPipeline pipeline(input);
    WorkLedger work;
    pipeline.Initialize(work);
    auto proposal = Boundary::Construct(pipeline.State(), input.Source, 0, EdgeKey(0, 1), -1);
    Require(proposal.Reason.empty(), "单侧几何构造失败");
    Require(Boundary::Certify(pipeline.State(), pipeline.Samples(), proposal, work) == "certified",
        "源高新点没有通过真实进展认证");
    Require(work.BoundaryCertified == 1 && proposal.Free == std::vector<Identity>{-1}, "认证计数或可写点错误");
    Require(proposal.Points.at(-1).Height == input.Source.Values[2] / 65535.0, "新点不等于原始源高");
    Require(Boundary::Construct(pipeline.State(), input.Source, 0, EdgeKey(0, 4), -1).Reason ==
        "boundary_not_single_sided", "内部边被单侧切开");
    const HeightSource coarse{2, 2, {0, 0, 0, 0}};
    Require(Boundary::Construct(pipeline.State(), coarse, 0, EdgeKey(0, 1), -1).Reason ==
        "boundary_resolution_limit", "允许了源尺度以下分割");
    for (Slot root = 0; root < 4; ++root)
    {
        Require(Boundary::Construct(pipeline.State(), input.Source, root,
            EdgeKey(root, (root + 1) % 4), -1).Reason.empty(), "四向外边构造不一致");
    }
    Require(Boundary::Construct(pipeline.State(), input.Source, 1, EdgeKey(0, 1), -1).Reason ==
        "boundary_not_single_sided", "错误根通过单侧检查");
    auto shifted = input;
    for (auto& [id, point] : shifted.Vertices)
    {
        static_cast<void>(id);
        point.U = .25 + .5 * point.U;
        point.V = .25 + .5 * point.V;
    }
    const TransactionalState inner(shifted);
    Require(Boundary::Construct(inner, input.Source, 0, EdgeKey(0, 1), -1).Reason ==
        "boundary_not_domain_edge", "域内单侧边被当作外边界");
    auto batch = Batch(pipeline, proposal);
    for (int mutation = 0; mutation < 6; ++mutation)
    {
        auto invalid = batch;
        auto& exchange = invalid.Exchanges.front();
        if (mutation == 0) exchange.Kind = ExchangeKind::Refinement;
        if (mutation == 1) exchange.Receiver.Points.at(-1).Height += .01;
        if (mutation == 2) exchange.Receiver.Points.at(0).Height += .01;
        if (mutation == 3) exchange.Receiver.Faces.pop_back();
        if (mutation == 4) exchange.Receiver.Reason = "uncertified";
        if (mutation == 5) exchange.Receiver.Free.push_back(0);
        Throws([&] { pipeline.Apply(invalid, work); });
        Require(pipeline.State().FaceCount() == 4 && pipeline.State().Version() == 1, "失败检查污染生产状态");
    }
    // 独立提交入口不能借用缺失的源高证书，生产 Pipeline 显式提供只读源
    TransactionalState detached(input);
    Throws([&] { TransactionalCommit::Apply(detached, batch, work); });
    pipeline.Apply(batch, work);
    Require(pipeline.State().FaceCount() == 5 && pipeline.State().IsBoundary(-1), "单面预算或边界标记错误");
    Require(!pipeline.State().Edges().contains(EdgeKey(0, 1)), "旧边未移除");
    Require(pipeline.State().Edges().at(EdgeKey(0, -1)).Count == 1, "新边不是单侧关联");
    const auto pool = pipeline.Samples().DonorPool(64);
    Require(std::find(pool.begin(), pool.end(), -1) == pool.end(), "新增外边界点进入回收池");
    TransactionalValidation::Validate(pipeline.State());
    TransactionalValidation::Samples(pipeline.State(), pipeline.Samples(), work);
    TransactionalValidation::Mesh(pipeline.State(), pipeline.Mesh());
    Throws([&] { pipeline.Apply(batch, work); });
}

/// <summary>
/// 满预算状态用远端六价中心支付边界细分，独立核对净减一与完整续接
/// 下一批才看到释放的单面空额，不在当前批内重新命名
/// </summary>
void PairedBoundary()
{
    auto input = Fan();
    input.Vertices.clear();
    input.Faces.clear();
    input.Source = {9, 9, std::vector<std::uint16_t>(81, 0)};
    for (Identity y = 0; y < 5; ++y)
    {
        for (Identity x = 0; x < 5; ++x)
        {
            input.Vertices.push_back({y * 5 + x, {x * .25, y * .25, 0}});
        }
    }
    for (Identity y = 0; y < 4; ++y)
    {
        for (Identity x = 0; x < 4; ++x)
        {
            const auto a = y * 5 + x;
            input.Faces.push_back({static_cast<Identity>(input.Faces.size()), {a, a + 1, a + 6}});
            input.Faces.push_back({static_cast<Identity>(input.Faces.size()), {a, a + 6, a + 5}});
        }
    }
    input.Config.Budget = input.Faces.size();
    TransactionalPipeline pipeline(input);
    WorkLedger work;
    pipeline.Initialize(work);
    auto receiver = Boundary::Construct(pipeline.State(), input.Source, 0, EdgeKey(0, 1), -1);
    auto donor = TransactionalProposals::Donor(pipeline.State(), pipeline.Samples(), 18, work);
    Require(receiver.Reason.empty() && donor.Reason == "certified", "配对夹具缺少合法几何");
    receiver.Reason = "certified";
    auto batch = Batch(pipeline, receiver);
    batch.Exchanges.front().HasDonor = true;
    batch.Exchanges.front().Donor = donor;
    batch.IntentBudgets = {{1, BudgetFunding::Donor}};
    batch.ReleasedFaces = 1;
    batch.NetFaceChange = -1;
    ParallelRoam::Terrain::TerrainMeshData mirror;
    TransactionalValidation::Consume(pipeline.ConsumeMesh(), mirror);
    pipeline.Apply(batch, work);
    Require(pipeline.State().FaceCount() == 31 && pipeline.State().IsBoundary(-1), "配对净减一错误");
    TransactionalValidation::Consume(pipeline.ConsumeMesh(), mirror);
    TransactionalValidation::Validate(pipeline.State());
    TransactionalValidation::Samples(pipeline.State(), pipeline.Samples(), work);
    TransactionalValidation::Mesh(pipeline.State(), mirror);
    CertifiedBatch next;
    next.IntentBudgets = {{1, BudgetFunding::None}};
    TransactionalReservation::AssignFreeFaces(input.Config.Budget - pipeline.State().FaceCount(), next);
    Require(next.IntentBudgets.front().Funding == BudgetFunding::Free, "释放余量未成为下一批空额");
}

/// <summary>
/// 枚举短成本序列，验证完整命名、不拆分额度，以及旧双面规则的退化
/// 命名后即使执行失败也保留归属，不把一面余量误当接收数量
/// </summary>
void BudgetEnumeration()
{
    for (unsigned encoded = 0; encoded < 243; ++encoded)
    {
        for (std::size_t free = 0; free <= 8; ++free)
        {
            CertifiedBatch batch;
            auto value = encoded;
            auto remaining = free;
            std::size_t assigned = 0, count = 0, need = 0;
            for (unsigned index = 0; index < 5; ++index)
            {
                batch.IntentBudgets.push_back({value % 3, BudgetFunding::None});
                value /= 3;
            }
            TransactionalReservation::AssignFreeFaces(free, batch);
            for (const auto& intent : batch.IntentBudgets)
            {
                if (!intent.Faces) continue;
                const bool named = remaining >= intent.Faces;
                Require(intent.Funding == (named ? BudgetFunding::Free : BudgetFunding::Donor), "命名顺序错误");
                if (named)
                {
                    remaining -= intent.Faces;
                    assigned += intent.Faces;
                    ++count;
                }
                else ++need;
            }
            Require(batch.AssignedFaces == assigned && batch.AssignedCredits == count && batch.Need == need,
                "预算统计混合了面数与接收数量");
        }
    }
    CertifiedBatch mixed;
    mixed.IntentBudgets = {{2, BudgetFunding::None}, {1, BudgetFunding::None}};
    TransactionalReservation::AssignFreeFaces(1, mixed);
    Require(mixed.IntentBudgets[0].Funding == BudgetFunding::Donor &&
        mixed.IntentBudgets[1].Funding == BudgetFunding::Free, "空额被错误要求为连续前缀");
    Require(TransactionalProposals::NeedsFlipRecovery({{'E', "shape_infeasible"}, {'B', "quality_infeasible"}}),
        "B 失败干扰旧目录的翻边资格");
    Require(!TransactionalProposals::NeedsFlipRecovery({{'B', "shape_infeasible"}}), "只有 B 失败触发翻边");
}

void ContinuationAndComposition()
{
    auto input = Fan();
    TransactionalPipeline first(input), second(input);
    WorkLedger work;
    first.Initialize(work);
    second.Initialize(work);
    auto bottom = Boundary::Construct(first.State(), input.Source, 0, EdgeKey(0, 1), -1);
    auto top = Boundary::Construct(first.State(), input.Source, 2, EdgeKey(2, 3), -2);
    // 本夹具隔离组合与续接；质量接受由独立的真实认证夹具覆盖
    bottom.Reason = top.Reason = "certified";
    Require(!TransactionalReservation::Conflict(TransactionalReservation::Footprint(first.State(), bottom),
        TransactionalReservation::Footprint(first.State(), top)), "只读共享中心产生了虚假冲突");
    auto batch = Batch(first, bottom);
    batch.Exchanges.push_back({top, {}, false, ExchangeKind::BoundaryRefinement});
    auto reversed = batch;
    std::reverse(reversed.Exchanges.begin(), reversed.Exchanges.end());
    ParallelRoam::Terrain::TerrainMeshData mirror;
    TransactionalValidation::Consume(first.ConsumeMesh(), mirror);
    first.Apply(batch, work);
    second.Apply(reversed, work);
    Require(TransactionalValidation::Equivalent(first.State(), second.State()), "反序应用改变逻辑状态");
    auto right = Boundary::Construct(first.State(), input.Source, 1, EdgeKey(1, 2), -3);
    Require(right.Reason.empty(), "第二批无法继续构造");
    right.Reason = "certified";
    first.Apply(Batch(first, right), work);
    const auto pending = first.ConsumeMesh();
    Require(!pending.Full && !pending.Vertices.empty() && !pending.Indices.empty(), "连续批次丢失增量记录");
    TransactionalValidation::Consume(pending, mirror);
    TransactionalValidation::Mesh(first.State(), mirror);
    TransactionalValidation::Validate(first.State());
    TransactionalValidation::Samples(first.State(), first.Samples(), work);
    for (const auto& [id, point] : input.Vertices)
    {
        Require(first.State().Vertex(id).Geometry == point, "存活旧点被修改");
    }
    auto changed = input.Config;
    changed.EnableBoundaryRefinement = false;
    Throws([&] { first.SetView(changed, work); });
    input.Config.PreserveSurvivingHeights = false;
    Throws([&] { TransactionalState invalid(input); });
}

/// <summary>
/// 连续几何提交穷尽四条边的可分割段，源尺度负责终止而非时间配额
/// 此处隔离生命周期，不能据此宣称重复细分始终通过动态质量认证
/// </summary>
void FiniteBoundaryLifecycle()
{
    const auto input = Fan();
    TransactionalPipeline pipeline(input);
    WorkLedger work;
    pipeline.Initialize(work);
    std::size_t changes = 0;
    for (;;)
    {
        std::optional<Proposal> next;
        for (const auto& [edge, association] : pipeline.State().Edges())
        {
            if (association.Count != 1) continue;
            auto proposal = Boundary::Construct(pipeline.State(), input.Source, association.Faces[0],
                edge, pipeline.State().NextVertexId());
            if (proposal.Reason.empty())
            {
                next = std::move(proposal);
                break;
            }
            Require(proposal.Reason == "boundary_resolution_limit", "终止夹具出现非分辨率拒绝");
        }
        if (!next) break;
        Require(++changes <= 12, "边界细分超过源尺度数量界");
        next->Reason = "certified";
        pipeline.Apply(Batch(pipeline, *next), work);
    }
    Require(changes == 12 && pipeline.State().FaceCount() == 16, "源尺度前过早终止或账本错误");
    TransactionalValidation::Validate(pipeline.State());
    TransactionalValidation::Samples(pipeline.State(), pipeline.Samples(), work);
    TransactionalValidation::Mesh(pipeline.State(), pipeline.Mesh());
}
}

int main()
{
    try
    {
        GeometryAndCertificate();
        BudgetEnumeration();
        PairedBoundary();
        ContinuationAndComposition();
        FiniteBoundaryLifecycle();
        std::cout << "单侧源高细化、混合预算和局部续接完成\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
