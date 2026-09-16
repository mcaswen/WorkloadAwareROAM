#include "experiment/greedy_transactional_lod/TransactionalBoundaryAudit.h"
#include "algorithms/greedy_transactional_lod/TransactionalCertification.h"
#include "algorithms/greedy_transactional_lod/TransactionalPredicates.h"
#include "algorithms/greedy_transactional_lod/TransactionalProposals.h"
#include "algorithms/greedy_transactional_lod/TransactionalReservation.h"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>

namespace ParallelRoam::Experiment::GreedyTransactionalLod
{
namespace
{
using namespace Algorithms::GreedyTransactionalLod;
using Predicates = TransactionalPredicates;
using Certification = TransactionalCertification;
using Clock = std::chrono::steady_clock;

/// <summary>
/// 单侧边也可能来自非法洞口；这里只承认已冻结单位域的四条外边界
/// </summary>
bool DomainEdge(const Point& first, const Point& second)
{
    return (first.U == second.U && (first.U == 0 || first.U == 1)) ||
        (first.V == second.V && (first.V == 0 || first.V == 1));
}

/// <summary>
/// 中点不必落在公共采样网格上，直接查询同一原始双线性高度场
/// 此查询不读取未来相机，也不从当前近似曲面反推参考
/// </summary>
double SourceHeight(const TransactionalSamples& samples, const Configuration& config, const Point& point)
{
    const auto& source = samples.Source();
    const double x = point.U * (source.Width - 1);
    const double y = point.V * (source.Height - 1);
    const auto ix = std::min(static_cast<std::uint32_t>(x), source.Width - 2);
    const auto iy = std::min(static_cast<std::uint32_t>(y), source.Height - 2);
    const auto base = static_cast<std::size_t>(iy) * source.Width + ix;
    const double tx = x - ix;
    const double ty = y - iy;
    const double lower = source.Values[base] * (1 - tx) + source.Values[base + 1] * tx;
    const double upper = source.Values[base + source.Width] * (1 - tx) + source.Values[base + source.Width + 1] * tx;
    return ((1 - ty) * lower + ty * upper) * config.HeightScale / 65535;
}

double HeightAt(const Proposal& proposal, const Point& point)
{
    for (const auto& face : proposal.Faces)
    {
        const auto& a = proposal.Points.at(face[0]);
        const auto& b = proposal.Points.at(face[1]);
        const auto& c = proposal.Points.at(face[2]);
        if (Predicates::Contains(point, a, b, c))
        {
            const auto weights = Predicates::Barycentric(point, a, b, c);
            return (weights[0] * a.Height + weights[1] * b.Height) + weights[2] * c.Height;
        }
    }
    throw std::runtime_error("边界调查见证未被局部提案覆盖");
}

/// <summary>
/// 浮点投影只报告见证变化，不能替代原区间与有理数质量认证
/// </summary>
double ScreenError(const Configuration& config, const Point& point, double reference, double height)
{
    const auto source = TransactionalSamples::Clip(config, point.U, point.V, reference);
    const auto mesh = TransactionalSamples::Clip(config, point.U, point.V, height);
    if (source[3] <= 0 || mesh[3] <= 0 || mesh[2] < (config.UsesZeroToOneDepth ? 0 : -mesh[3]))
    {
        return NAN;
    }
    return std::hypot((mesh[0] / mesh[3] - source[0] / source[3]) * config.Width * .5,
        (mesh[1] / mesh[3] - source[1] / source[3]) * config.Height * .5);
}

void Number(std::ostream& stream, double value)
{
    if (std::isfinite(value))
    {
        stream << value;
    }
    else
    {
        stream << "null";
    }
}

/// <summary>
/// 导出有限几何供独立有理检查，不序列化全生产状态
/// </summary>
void Geometry(std::ostream& stream, const Proposal& proposal, bool hasNewPoint)
{
    stream << "{\"points\":[";
    bool first = true;
    for (const auto& [id, point] : proposal.Points)
    {
        if (!first)
        {
            stream << ',';
        }
        first = false;
        stream << '[' << id << ',' << point.U << ',' << point.V << ',' << point.Height << ']';
    }
    stream << "],\"faces\":[";
    first = true;
    for (const auto& face : proposal.Faces)
    {
        if (!first)
        {
            stream << ',';
        }
        first = false;
        stream << '[' << face[0] << ',' << face[1] << ',' << face[2] << ']';
    }
    stream << "],\"newVertex\":";
    if (hasNewPoint)
    {
        stream << proposal.NewVertex;
    }
    else
    {
        stream << "null";
    }
    stream << '}';
}

/// <summary>
/// 单对调查使用原共同捐赠池与读写判定，不模拟已获批事务的占用
/// 找到一对只证明局部预算可兑现，不代表原贪心批次必然选择它
/// </summary>
void BudgetPair(std::ostream& stream, const TransactionalState& state, const TransactionalSamples& samples,
    const Proposal& receiver, WorkLedger& work)
{
    const auto remaining = state.Config().Budget - state.FaceCount();
    stream << ",\"remainingBudget\":" << remaining;
    if (remaining >= 1)
    {
        stream << ",\"budgetStatus\":\"free_slot\",\"pairChecks\":0";
        return;
    }
    const auto footprint = TransactionalReservation::Footprint(state, receiver);
    const auto pool = samples.DonorPool(state.Config().DonorLimit);
    std::map<std::string, std::size_t> reasons;
    std::size_t checked = 0;
    bool found = false;
    for (const auto center : pool)
    {
        work.CheckLimit();
        ++checked;
        const auto donor = TransactionalProposals::Donor(state, samples, center, work);
        if (donor.Reason != "certified")
        {
            ++reasons[donor.Reason];
            continue;
        }
        if (!Certification::Accepts(state, samples, donor, receiver.TargetMicropixels, work))
        {
            ++reasons["quality"];
            continue;
        }
        if (TransactionalReservation::Conflict(footprint, TransactionalReservation::Footprint(state, donor)))
        {
            ++reasons["internal_conflict"];
            continue;
        }
        if (donor.Support.size() != donor.Faces.size() + 2)
        {
            throw std::runtime_error("回收提案没有释放两个面");
        }
        found = true;
        stream << ",\"donor\":" << center << ",\"netFacesWithDonor\":-1";
        break;
    }
    stream << ",\"budgetStatus\":\"" << (found ? "pair_feasible" : "pool_exhausted")
        << "\",\"poolSize\":" << pool.size() << ",\"pairChecks\":" << checked << ",\"pairReasons\":{";
    bool first = true;
    for (const auto& [reason, count] : reasons)
    {
        if (!first)
        {
            stream << ',';
        }
        first = false;
        stream << std::quoted(reason) << ':' << count;
    }
    stream << '}';
}

/// <summary>
/// 原认证结果与逐点诊断分开保存；后者不改变接受目标
/// 最大退化位置用于识别局部最大值下降掩盖的质量交换
/// </summary>
struct VariantQuality
{
    bool Measured{};
    double OldMaximum{};
    double MaximumExcess{-INFINITY};
    Slot ExcessSample{InvalidSlot};
    double ExcessBefore{}, ExcessAfter{};
};

VariantQuality AssessVariant(const TransactionalState& state, const TransactionalSamples& samples,
    Proposal& proposal, WorkLedger& work)
{
    VariantQuality result;
    proposal.Samples = samples.VisibleSupport(proposal.Support);
    for (const auto& target : proposal.Faces)
    {
        if (!Predicates::Shape(proposal.Points.at(target[0]), proposal.Points.at(target[1]), proposal.Points.at(target[2])))
        {
            proposal.Reason = "shape_infeasible";
        }
    }
    for (const auto sid : proposal.Samples)
    {
        result.OldMaximum = std::max(result.OldMaximum, std::sqrt(samples.Projection(sid).ErrorSquared));
    }
    if (proposal.Reason.empty())
    {
        proposal.Reason = Certification::SetProgressTarget(state, samples, proposal, work);
    }
    if (proposal.Reason.empty())
    {
        result.Measured = Certification::Measure(state, samples, proposal, work);
        if (!result.Measured)
        {
            proposal.Reason = "projection_unknown";
        }
        else
        {
            proposal.Reason = Certification::Accepts(state, samples, proposal, proposal.TargetMicropixels, work) ?
                "certified" : "quality_infeasible";
        }
    }
    // 同一公共可见支持逐点比较，不因最大值换了位置而丢失退化证据
    for (const auto sid : proposal.Samples)
    {
        work.Touch();
        const auto point = samples.Parameter(sid);
        const double error = ScreenError(state.Config(), point, samples.Geometry(sid).ReferenceHeight, HeightAt(proposal, point));
        if (!std::isfinite(error))
        {
            result.MaximumExcess = NAN;
            result.ExcessSample = InvalidSlot;
            break;
        }
        const double before = std::sqrt(samples.Projection(sid).ErrorSquared);
        if (error - before > result.MaximumExcess)
        {
            result.MaximumExcess = error - before;
            result.ExcessSample = sid;
            result.ExcessBefore = before;
            result.ExcessAfter = error;
        }
    }
    return result;
}

/// <summary>
/// 一个私有高度版本完整求值后才写出一行，异常不会留下半条 JSON
/// 构造、认证、逐点诊断和预算查询费用均包含在该行费用内
/// </summary>
std::string EvaluateVariant(const TransactionalState& state, const TransactionalSamples& samples,
    const Proposal& constructed, const Proposal& old, Edge edge, const Point& witness,
    std::size_t frame, std::size_t rank, bool useSource, WorkLedger& work)
{
    const auto started = Clock::now();
    const auto touchesBefore = work.SampleTouches;
    auto proposal = constructed;
    if (useSource)
    {
        auto& point = proposal.Points.at(proposal.NewVertex);
        point.Height = SourceHeight(samples, state.Config(), point);
    }
    const auto quality = AssessVariant(state, samples, proposal, work);
    std::ostringstream record;
    record << std::setprecision(17) << "{\"frame\":" << frame << ",\"root\":" << state.Face(proposal.Root).Id
        << ",\"edge\":[" << edge[0] << ',' << edge[1] << "],\"variant\":\""
        << (useSource ? "source" : "linear") << "\",\"reason\":" << std::quoted(proposal.Reason)
        << ",\"rank\":" << rank << ",\"prefix\":" << state.Config().PrefixLimit
        << ",\"faces\":" << state.FaceCount() << ",\"budget\":" << state.Config().Budget << ",\"old\":";
    Geometry(record, old, false);
    record << ",\"target\":";
    Geometry(record, proposal, true);
    record << ",\"samples\":" << proposal.Samples.size() << ",\"oldMax\":" << quality.OldMaximum
        << ",\"targetMicropixels\":" << proposal.TargetMicropixels << ",\"newMaxUpper\":";
    Number(record, quality.Measured ? std::sqrt(proposal.ErrorUpper) : NAN);
    const double reference = SourceHeight(samples, state.Config(), witness);
    record << ",\"witness\":[" << witness.U << ',' << witness.V << "],\"reference\":" << reference
        << ",\"oldHeight\":" << HeightAt(old, witness) << ",\"newHeight\":" << HeightAt(proposal, witness)
        << ",\"oldError\":";
    Number(record, ScreenError(state.Config(), witness, reference, HeightAt(old, witness)));
    record << ",\"newError\":";
    Number(record, ScreenError(state.Config(), witness, reference, HeightAt(proposal, witness)));
    record << ",\"sampledExcessMax\":";
    Number(record, quality.MaximumExcess);
    if (quality.ExcessSample != InvalidSlot)
    {
        const auto point = samples.Parameter(quality.ExcessSample);
        record << ",\"excessWitness\":{\"sample\":" << quality.ExcessSample << ",\"uv\":[" << point.U << ',' << point.V
            << "],\"before\":" << quality.ExcessBefore << ",\"after\":" << quality.ExcessAfter
            << ",\"oldHeight\":" << HeightAt(old, point) << ",\"newHeight\":" << HeightAt(proposal, point)
            << ",\"reference\":" << samples.Geometry(quality.ExcessSample).ReferenceHeight << '}';
    }
    if (useSource && proposal.Reason == "certified")
    {
        BudgetPair(record, state, samples, proposal, work);
    }
    record << ",\"sampleTouches\":" << work.SampleTouches - touchesBefore
        << ",\"seconds\":" << std::chrono::duration<double>(Clock::now() - started).count() << '}';
    return record.str();
}
}

Proposal TransactionalBoundaryAudit::Construct(const TransactionalState& state, Slot root, Edge edge)
{
    Proposal result;
    result.Root = root;
    result.Kind = 'B';
    edge = EdgeKey(edge[0], edge[1]);
    const auto found = state.Edges().find(edge);
    if (found == state.Edges().end() || found->second.Count != 1 || found->second.Faces[0] != root)
    {
        result.Reason = "not_single_sided_root_edge";
        return result;
    }
    const auto& a = state.Vertex(edge[0]).Geometry;
    const auto& b = state.Vertex(edge[1]).Geometry;
    if (!DomainEdge(a, b))
    {
        result.Reason = "not_domain_boundary";
        return result;
    }
    const auto& face = state.Face(root).Vertices;
    result.Support = {root};
    for (const auto id : face)
    {
        result.Points.emplace(id, state.Vertex(id).Geometry);
    }
    // 私有提案互不组合，复用下一空身份不改变分配器或生产每根目录编码
    result.NewVertex = state.NextVertexId();
    const Point midpoint{(a.U + b.U) * .5, (a.V + b.V) * .5, (a.Height + b.Height) * .5};
    if ((midpoint.U == a.U && midpoint.V == a.V) || (midpoint.U == b.U && midpoint.V == b.V))
    {
        result.Reason = "midpoint_not_distinct";
        return result;
    }
    result.Points.emplace(result.NewVertex, midpoint);
    result.Free = {result.NewVertex};
    // 沿旧面方向保留另两条边，外边界在新点处分成两段
    for (std::size_t i = 0; i < 3; ++i)
    {
        const auto first = face[i];
        const auto second = face[(i + 1) % 3];
        if (EdgeKey(first, second) != edge)
        {
            result.Faces.push_back({first, second, result.NewVertex});
        }
    }
    return result;
}

void TransactionalBoundaryAudit::Run(const TransactionalState& state, const TransactionalSamples& samples,
    std::size_t frame, std::span<const Point> witnesses, const std::filesystem::path& output)
{
    if (std::filesystem::exists(output))
    {
        throw std::runtime_error("拒绝覆盖边界调查结果");
    }
    std::ofstream stream(output);
    stream.exceptions(std::ios::badbit | std::ios::failbit);
    stream << std::setprecision(17);
    const auto started = Clock::now();
    WorkLedger work;
    work.VisitLimit = 2000000;
    work.Deadline = started + std::chrono::seconds(60);
    const auto ordered = samples.Raw();
    std::set<Edge> visited;
    std::size_t candidates = 0;
    try
    {
        for (const auto& witness : witnesses)
        {
            for (const auto root : state.ActiveFaces())
            {
                work.CheckLimit();
                const auto& face = state.Face(root).Vertices;
                if (!Predicates::Contains(witness, state.Vertex(face[0]).Geometry,
                    state.Vertex(face[1]).Geometry, state.Vertex(face[2]).Geometry))
                {
                    continue;
                }
                std::array<Edge, 3> edges{EdgeKey(face[0], face[1]), EdgeKey(face[1], face[2]), EdgeKey(face[2], face[0])};
                std::sort(edges.begin(), edges.end());
                for (const auto& edge : edges)
                {
                    if (state.Edges().at(edge).Count != 1 || !visited.insert(edge).second)
                    {
                        continue;
                    }
                    if (++candidates > 6)
                    {
                        throw std::runtime_error("边界候选超过六条限额");
                    }
                    const auto constructed = Construct(state, root, edge);
                    if (!constructed.Reason.empty())
                    {
                        throw std::runtime_error("合法种子存在不支持的单侧边");
                    }
                    Proposal old;
                    old.Faces = {face};
                    for (const auto id : face)
                    {
                        old.Points.emplace(id, state.Vertex(id).Geometry);
                    }
                    const auto position = std::find(ordered.begin(), ordered.end(), root);
                    const auto rank = position == ordered.end() ? 0 : static_cast<std::size_t>(position - ordered.begin()) + 1;
                    for (const bool useSource : {false, true})
                    {
                        stream << EvaluateVariant(state, samples, constructed, old, edge, witness, frame, rank, useSource, work) << '\n';
                        stream.flush();
                    }
                }
            }
        }
    }
    catch (const std::exception& error)
    {
        // 已完成行保留，资源或数值异常记录为未知，不拼成错误的不可行结论
        stream << "{\"status\":\"unknown\",\"message\":" << std::quoted(error.what()) << "}\n";
    }
    stream << "{\"status\":\"summary\",\"candidates\":" << candidates << ",\"sampleTouches\":" << work.SampleTouches
        << ",\"seconds\":" << std::chrono::duration<double>(Clock::now() - started).count() << "}\n";
}
}
