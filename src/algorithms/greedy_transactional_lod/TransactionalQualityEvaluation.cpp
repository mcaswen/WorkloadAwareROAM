#include "algorithms/greedy_transactional_lod/TransactionalQualityEvaluation.h"
#include <bit>
#include <stdexcept>

namespace ParallelRoam::Algorithms::GreedyTransactionalLod::QualityEvaluation
{
std::array<Point, 3> CoveringFace(const TransactionalState &state, const TransactionalSamples &samples, Slot sid,
                                  const Proposal *proposal)
{
    if (!proposal)
    {
        // 旧状态的唯一 owner 足以定义共享曲面上的见证高度
        const auto &f = state.Face(samples.Geometry(sid).Owner).Vertices;
        return {state.Vertex(f[0]).Geometry, state.Vertex(f[1]).Geometry, state.Vertex(f[2]).Geometry};
    }
    for (const auto &f : proposal->Faces)
    {
        // 边界可同时命中两个面，它们使用同一存活顶点几何
        const std::array<Point, 3> p{proposal->Points.at(f[0]), proposal->Points.at(f[1]), proposal->Points.at(f[2])};
        std::array<double, 3> weights{};
        if (samples.Weights(sid, p[0], p[1], p[2], weights))
        {
            return p;
        }
    }
    throw std::runtime_error("局部提案缺失闭面样本覆盖");
}

std::optional<R> ExactError(const TransactionalState &state, const TransactionalSamples &samples, Slot sid,
                            const Proposal *proposal, TransactionalProposalEvidence *evidence, WorkLedger *work)
{
    const auto ref = Reference<R>(state, samples, sid);
    const auto p = evidence ? evidence->Face(sid, *work) : CoveringFace(state, samples, sid, proposal);
    const R height = Height(ref[0], ref[1], p[0], p[1], p[2]);
    const auto rc = Clip(state.Config(), ref[0], ref[1], ref[2]), mc = Clip(state.Config(), ref[0], ref[1], height);
    // 比较投影平方误差，避免精确认证依赖平方根舍入
    if (rc[3] <= 0 || mc[3] <= 0 || (state.Config().UsesZeroToOneDepth ? mc[2] < 0 : mc[2] < -mc[3]))
    {
        return {};
    }
    const R dx = (mc[0] / mc[3] - rc[0] / rc[3]) * state.Config().Width / 2;
    const R dy = (mc[1] / mc[3] - rc[1] / rc[3]) * state.Config().Height / 2;
    return R(dx * dx + dy * dy);
}

std::optional<Interval> ErrorBounds(const TransactionalState &state, const TransactionalSamples &samples, Slot sid,
                                    const Proposal *proposal, WorkLedger &work, TransactionalProposalEvidence *evidence)
{
    ++work.FilterChecks;
    const auto ref = Reference<Interval>(state, samples, sid);
    const auto p = evidence ? evidence->Face(sid, work) : CoveringFace(state, samples, sid, proposal);
    const auto height = Height(ref[0], ref[1], p[0], p[1], p[2]);
    const auto rc = Clip(state.Config(), ref[0], ref[1], ref[2]), mc = Clip(state.Config(), ref[0], ref[1], height);
    const auto near = state.Config().UsesZeroToOneDepth ? mc[2] : mc[2] + mc[3];
    // 只有整个区间都处于投影定义域，才允许快速接受它给出的误差界
    if (rc[3].Low > 0 && mc[3].Low > 0 && near.Low >= 0)
    {
        const auto dx = (mc[0] / mc[3] - rc[0] / rc[3]) * Interval(state.Config().Width * .5);
        const auto dy = (mc[1] / mc[3] - rc[1] / rc[3]) * Interval(state.Config().Height * .5);
        const auto value = dx * dx + dy * dy;
        if (std::isfinite(value.Low) && std::isfinite(value.High))
        {
            return Interval{std::max(0.0, value.Low), std::max(0.0, value.High)};
        }
    }
    // 过滤不能判定投影域时，不将整个补丁判坏；先复核精确二进制几何
    ++work.ExactChecks;
    const auto exact = ExactError(state, samples, sid, proposal, evidence, &work);
    if (!exact)
    {
        return {};
    }
    const double value = exact->convert_to<double>();
    return Interval{std::max(0.0, Down(value)), Up(value)};
}

Point InDomain(const Point &point, const Configuration &config, bool output)
{
    // 输出域的U/V字段临时表示世界x/z，计算时必须配合同域参考坐标
    if (!output)
    {
        return point;
    }
    const auto p = PublishedPosition(point, config.TerrainSize);
    return {p[0], p[2], p[1]};
}

std::vector<QualityFace> Faces(const TransactionalState &state, const Proposal &proposal, bool output, bool next)
{
    std::vector<QualityFace> result;
    // 顶点按稳定身份取值；核心态和提案只在读取来源上不同
    const auto append = [&](const auto &ids, const auto &geometry) {
        QualityFace face;
        for (std::size_t i = 0; i < 3; ++i)
        {
            face[i] = InDomain(geometry(ids[i]), state.Config(), output);
        }
        result.push_back(face);
    };
    if (next)
    {
        // 读取拟合已经完成的高度，不在认证过程中重算源高或修改提案
        for (const auto &ids : proposal.Faces)
        {
            append(ids, [&](Identity id) { return proposal.Points.at(id); });
        }
    }
    else
    {
        // 旧面完整保留闭补丁接口，公开曲面不能借用核心owner近似
        for (auto slot : proposal.Support)
        {
            append(state.Face(slot).Vertices, [&](Identity id) { return state.Vertex(id).Geometry; });
        }
    }
    return result;
}

std::optional<QualityFace> Cover(const TransactionalState &state, const TransactionalSamples &samples, Slot sid,
                                 bool output, const std::vector<QualityFace> &faces)
{
    const auto q = Coordinate(Reference<Interval>(state, samples, sid), state.Config(), output);
    return CoverPrepared(q, faces, [&] { return Coordinate(Reference<R>(state, samples, sid), state.Config(), output); });
}

R Area(const QualityFace &f)
{
    return (R(f[1].U) - f[0].U) * (R(f[2].V) - f[0].V) - (R(f[1].V) - f[0].V) * (R(f[2].U) - f[0].U);
}

bool SameInterface(const TransactionalState &state, const Proposal &proposal, bool output,
                   const std::vector<QualityFace> &old, const std::vector<QualityFace> &next)
{
    std::map<Edge, int> oldEdges, newEdges;
    // 身份边的使用次数区分内部边与外接口，不把公共顶点误当公共面
    const auto add = [](auto &edges, const auto &ids) {
        for (std::size_t i = 0; i < 3; ++i)
        {
            ++edges[EdgeKey(ids[i], ids[(i + 1) % 3])];
        }
    };
    for (auto slot : proposal.Support)
    {
        add(oldEdges, state.Face(slot).Vertices);
    }
    for (const auto &face : proposal.Faces)
    {
        add(newEdges, face);
    }
    // 面积相等本身不足以排除重叠，因此本函数还依赖旧目录的合法构造
    R before = 0, after = 0;
    // 正面积与总面积使用准确二进制几何；舍入后的反向面不能靠原shape掩盖
    for (const auto &f : old)
    {
        const R area = Area(f);
        if (area <= 0)
        {
            return false;
        }
        before += area;
    }
    for (const auto &f : next)
    {
        const R area = Area(f);
        if (area <= 0)
        {
            return false;
        }
        after += area;
    }
    if (before != after)
    {
        return false;
    }
    // 在既有合法局部目录前提下，固定简单接口和正面构成同一补丁区域
    // 本检查不是任意输入多边形或一般自交网格的认证器
    // 唯一允许的接口分段是同一条边上的细分，且插值高度也必须一致
    for (const auto &[edge, count] : newEdges)
    {
        if (count == 2)
        {
            continue;
        }
        if (count != 1)
        {
            return false;
        }
        // 逐条验证新外边落在旧接口上，禁止float转换扩大修改支持
        bool matched = false;
        for (const auto &[boundary, uses] : oldEdges)
        {
            if (uses != 1)
            {
                continue;
            }
            const auto a = InDomain(state.Vertex(boundary[0]).Geometry, state.Config(), output);
            const auto b = InDomain(state.Vertex(boundary[1]).Geometry, state.Config(), output);
            bool onEdge = true;
            // 检查两个端点即可认证一条线段，边界细分无需离散采样整条边
            for (auto id : edge)
            {
                const auto p = InDomain(proposal.Points.at(id), state.Config(), output);
                const R dx = R(b.U) - a.U, dy = R(b.V) - a.V;
                const R t = dx != 0 ? (R(p.U) - a.U) / dx : (R(p.V) - a.V) / dy;
                // 外边界B有意更新源高度；平面接口相同即可，域外没有邻面
                if (t < 0 || t > 1 || R(p.U) != R(a.U) + t * dx || R(p.V) != R(a.V) + t * dy ||
                    (proposal.Kind != 'B' && R(p.Height) != R(a.Height) + t * (R(b.Height) - a.Height)))
                {
                    onEdge = false;
                    break;
                }
            }
            if (onEdge)
            {
                matched = true;
                break;
            }
        }
        if (!matched)
        {
            return false;
        }
    }
    return true;
}

bool Finite(Interval a)
{
    return std::isfinite(a.Low) && std::isfinite(a.High);
}

std::optional<bool> VisibleBounds(const Configuration& config, const std::array<Interval, 4>& c)
{
    // 六个半空间与正w共同定义人口；近面同时兼容两种深度约定
    const std::array<Interval, 6> sides{
        c[0] + c[3], c[3] - c[0], c[1] + c[3], c[3] - c[1], config.UsesZeroToOneDepth ? c[2] : c[2] + c[3],
        c[3] - c[2]};
    if (c[3].Low > 0 && std::all_of(sides.begin(), sides.end(), [](auto a) { return a.Low >= 0; }))
    {
        return true;
    }
    if (c[3].High <= 0 || std::any_of(sides.begin(), sides.end(), [](auto a) { return a.High < 0; }))
    {
        return false;
    }
    return {};
}

bool ExactVisible(const Configuration& config, const std::array<R, 4>& exact)
{
    return exact[3] > 0 && exact[0] >= -exact[3] && exact[0] <= exact[3] && exact[1] >= -exact[3] &&
           exact[1] <= exact[3] && exact[2] <= exact[3] &&
           exact[2] >= (config.UsesZeroToOneDepth ? R(0) : R(-exact[3]));
}

bool VisibilityAgrees(const TransactionalState& state, const TransactionalSamples& samples, Slot sid,
    const std::array<Interval, 3>& reference)
{
    const auto& config = state.Config();
    const auto visible = VisibleBounds(config, Clip(config, reference[0], reference[1], reference[2]));
    if (visible)
    {
        return *visible == samples.Projection(sid).Visible;
    }
    // 参考恰在裁剪边界时核对人口，不能把缓存舍入差异当作不可见保护漏洞
    const auto ref = Reference<R>(state, samples, sid);
    const auto exact = Clip(config, ref[0], ref[1], ref[2]);
    return samples.Projection(sid).Visible == ExactVisible(config, exact);
}

void RecordBits(const R &value, WorkLedger &work)
{
    const Integer n = boost::multiprecision::abs(numerator(value));
    const Integer d = denominator(value);
    // 分子零没有有效位，分母始终为正；工作线程合并时对此字段取最大值
    const auto bits = std::max(n == 0 ? 0U : boost::multiprecision::msb(n) + 1U, boost::multiprecision::msb(d) + 1U);
    work.QualityMaxRationalBits = std::max(work.QualityMaxRationalBits, static_cast<std::uint64_t>(bits));
}

// 正负有理数都按数学floor处理，不能把整数除法向零截断当下界
void AddBounds(const R &lower, const R &upper, Integer &low, Integer &high)
{
    // 固定尺度整数求和避免异分母连乘，代价不会随提案样本数指数放大
    const Integer scale = Integer(1) << 128;
    const auto floor = [&](const R &r) {
        const Integer n = numerator(r) * scale, d = denominator(r);
        Integer q = n / d;
        if (n < 0 && n % d != 0)
        {
            --q;
        }
        return q;
    };
    low += floor(lower);
    // ceil(x)=-floor(-x)，同一舍入实现也覆盖负进展和恰好整除
    high -= floor(R(-upper));
}

namespace
{
Integer BinaryFloor(double value)
{
    static_assert(sizeof(double) == sizeof(std::uint64_t) && std::numeric_limits<double>::is_iec559);
    const auto bits = std::bit_cast<std::uint64_t>(value);
    const auto exponent = static_cast<int>((bits >> 52) & 0x7ff);
    const auto fraction = bits & ((std::uint64_t{1} << 52) - 1);
    const auto significand = exponent == 0 ? fraction : fraction | (std::uint64_t{1} << 52);
    const int shift = (exponent == 0 ? -1074 : exponent - 1023 - 52) + 128;
    Integer magnitude;
    bool remainder = false;
    if (shift >= 0)
    {
        magnitude = Integer(significand) << shift;
    }
    else
    {
        const auto removed = static_cast<unsigned>(-shift);
        // 移位超过机器字宽时显式给出零商，避免C++未定义行为
        const auto quotient = removed >= 64 ? 0 : significand >> removed;
        magnitude = quotient;
        remainder = removed >= 64 ? significand != 0 : (quotient << removed) != significand;
    }
    if ((bits >> 63) != 0)
    {
        // 负数带余数须再减一；±0无余数，因此都准确映射为零
        return -magnitude - static_cast<unsigned>(remainder);
    }
    return magnitude;
}
}

void AddBinaryBounds(double lower, double upper, Integer& low, Integer& high)
{
    if (!std::isfinite(lower) || !std::isfinite(upper))
    {
        // 不扩大过滤器的定义域；维持旧有理转换对于非有限输入的失败行为
        AddBounds(R(lower), R(upper), low, high);
        return;
    }
    low += BinaryFloor(lower);
    high -= BinaryFloor(-upper);
}

} // namespace ParallelRoam::Algorithms::GreedyTransactionalLod::QualityEvaluation
