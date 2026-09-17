#pragma once
#include "algorithms/greedy_transactional_lod/TransactionalProposalEvidence.h"
#include <boost/multiprecision/cpp_int.hpp>
#include <cmath>
#include <optional>
#include <type_traits>

namespace ParallelRoam::Algorithms::GreedyTransactionalLod::QualityEvaluation
{
using R = boost::multiprecision::cpp_rational;
using Integer = boost::multiprecision::cpp_int;

/// <summary>
/// 每次基本运算向外舍入；遇到零分母或非有限值时交给精确分支
/// </summary>
struct Interval
{
    double Low{}, High{};
    Interval() = default;
    Interval(double value) : Low(value), High(value)
    {
    }
    Interval(double low, double high) : Low(low), High(high)
    {
    }
};

// 外包界依赖严格浮点编译，不允许融合运算改变已冻结的求值顺序
inline double Down(double value)
{
    return std::nextafter(value, -std::numeric_limits<double>::infinity());
}
inline double Up(double value)
{
    return std::nextafter(value, std::numeric_limits<double>::infinity());
}
inline Interval operator+(Interval a, Interval b)
{
    return {Down(a.Low + b.Low), Up(a.High + b.High)};
}
inline Interval operator-(Interval a, Interval b)
{
    return {Down(a.Low - b.High), Up(a.High - b.Low)};
}
inline Interval operator*(Interval a, Interval b)
{
    // 区间允许跨零，不能只乘同侧端点来估计乘积
    const std::array<double, 4> values{a.Low * b.Low, a.Low * b.High, a.High * b.Low, a.High * b.High};
    return {Down(*std::min_element(values.begin(), values.end())), Up(*std::max_element(values.begin(), values.end()))};
}
// 分母跨零时放弃过滤，由调用方进入有理分支或报告投影未知
inline Interval operator/(Interval a, Interval b)
{
    if (b.Low <= 0 && b.High >= 0)
    {
        return {-INFINITY, INFINITY};
    }
    return a * Interval{Down(1 / b.High), Up(1 / b.Low)};
}

template <class T>
std::array<T, 3> Reference(const TransactionalState &state, const TransactionalSamples &samples, Slot sid)
{
    // 同一公式分别在区间域与有理域执行，避免两个参考曲面悄然分叉
    const auto xy = samples.Decode(sid);
    const auto &source = samples.Source();
    const auto x = std::min(xy[0] / 6, source.Width - 2), y = std::min(xy[1] / 6, source.Height - 2);
    const T tx = T(xy[0] - 6 * x) / T(6), ty = T(xy[1] - 6 * y) / T(6);
    // Q在每格的六分位置上编码；最后一格允许插值参数恰为一
    const auto index = static_cast<std::size_t>(y) * source.Width + x;
    const T bottom = T(source.Values[index]) * (T(1) - tx) + T(source.Values[index + 1]) * tx;
    const T top =
        T(source.Values[index + source.Width]) * (T(1) - tx) + T(source.Values[index + source.Width + 1]) * tx;
    return {T(xy[0]) / T(samples.Denominator()), T(xy[1]) / T(samples.Denominator()),
            ((T(1) - ty) * bottom + ty * top) * T(state.Config().HeightScale) / T(65535)};
}

// 矩阵中的二进制值就是冻结输入，认证不再重新生成一套相机矩阵
template <class T> std::array<T, 4> Clip(const Configuration &c, const T &u, const T &v, const T &h)
{
    const T x = (u - T(.5)) * T(c.TerrainSize), z = (v - T(.5)) * T(c.TerrainSize);
    std::array<T, 4> out{};
    for (std::size_t i = 0; i < 4; ++i)
    {
        out[i] = ((T(c.Matrix[4 * i]) * x + T(c.Matrix[4 * i + 1]) * h) + T(c.Matrix[4 * i + 2]) * z) +
                 T(c.Matrix[4 * i + 3]);
    }
    return out;
}

template <class T> T Height(const T &u, const T &v, const Point &a, const Point &b, const Point &c)
{
    // 插值读取实际发布高度，不能重新采原始 heightfield 替代被测几何
    const T area = (T(b.U) - T(a.U)) * (T(c.V) - T(a.V)) - (T(b.V) - T(a.V)) * (T(c.U) - T(a.U));
    const T w0 = ((T(b.U) - u) * (T(c.V) - v) - (T(b.V) - v) * (T(c.U) - u)) / area;
    const T w1 = ((u - T(a.U)) * (T(c.V) - T(a.V)) - (v - T(a.V)) * (T(c.U) - T(a.U))) / area;
    const T w2 = T(1) - w0 - w1;
    return (w0 * T(a.Height) + w1 * T(b.Height)) + w2 * T(c.Height);
}

// 旧认证的owner与提案定位入口保留，提取不能改变原政策数值语义
std::array<Point, 3> CoveringFace(const TransactionalState &, const TransactionalSamples &, Slot, const Proposal *);
std::optional<R> ExactError(const TransactionalState &, const TransactionalSamples &, Slot, const Proposal *,
                            TransactionalProposalEvidence *evidence = nullptr, WorkLedger *work = nullptr);
std::optional<Interval> ErrorBounds(const TransactionalState &, const TransactionalSamples &, Slot, const Proposal *,
                                    WorkLedger &, TransactionalProposalEvidence *evidence = nullptr);

// 下列局部几何接口由新证书使用；调用方负责旧目录的结构合法前提
using QualityFace = std::array<Point, 3>;
std::vector<QualityFace> Faces(const TransactionalState &, const Proposal &, bool output, bool next);
std::optional<QualityFace> Cover(const TransactionalState &, const TransactionalSamples &, Slot, bool output,
                                 const std::vector<QualityFace> &);
bool SameInterface(const TransactionalState &, const Proposal &, bool output, const std::vector<QualityFace> &old,
                   const std::vector<QualityFace> &next);
bool Finite(Interval);
bool VisibilityAgrees(const TransactionalState &, const TransactionalSamples &, Slot, const std::array<Interval, 3> &);
void RecordBits(const R &, WorkLedger &);
void AddBounds(const R &lower, const R &upper, Integer &low, Integer &high);

template <class T>
std::array<T, 2> Coordinate(const std::array<T, 3> &reference, const Configuration &config, bool output)
{
    // 输出域直接使用float世界位置，避免反除TerrainSize再次舍入参数坐标
    if (output)
    {
        return {(reference[0] - T(.5)) * T(config.TerrainSize), (reference[1] - T(.5)) * T(config.TerrainSize)};
    }
    return {reference[0], reference[1]};
}

template <class T>
std::optional<T> Screen(const Configuration &config, const std::array<T, 3> &reference, const T &height)
{
    const auto a = Clip(config, reference[0], reference[1], reference[2]);
    const auto b = Clip(config, reference[0], reference[1], height);
    // 只定义参考可见点到同一参数位置的屏幕位移，不混入光栅遮挡评价
    const auto near = config.UsesZeroToOneDepth ? b[2] : b[2] + b[3];
    if constexpr (std::is_same_v<T, Interval>)
    {
        if (a[3].Low <= 0 || b[3].Low <= 0 || near.Low < 0)
        {
            return {};
        }
    }
    else
    {
        if (a[3] <= 0 || b[3] <= 0 || near < 0)
        {
            return {};
        }
    }
    const T x = (b[0] / b[3] - a[0] / a[3]) * T(config.Width) / T(2);
    const T y = (b[1] / b[3] - a[1] / a[3]) * T(config.Height) / T(2);
    return T(x * x + y * y);
}

} // namespace ParallelRoam::Algorithms::GreedyTransactionalLod::QualityEvaluation
