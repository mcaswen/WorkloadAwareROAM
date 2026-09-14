#include "algorithms/greedy_transactional_lod/TransactionalPredicates.h"

#include <boost/multiprecision/cpp_int.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ParallelRoam::Algorithms::GreedyTransactionalLod
{
namespace
{
using Rational = boost::multiprecision::cpp_rational;
double Cross(const Point& a, const Point& b, const Point& c)
{
    return (b.U - a.U) * (c.V - a.V) - (b.V - a.V) * (c.U - a.U);
}
}

int TransactionalPredicates::Orientation(const Point& a, const Point& b, const Point& c)
{
    const double first = (b.U - a.U) * (c.V - a.V), second = (b.V - a.V) * (c.U - a.U);
    const double determinant = first - second;
    // 过滤只接受远离舍入边界的符号，零和近共线情况精确回退
    const double bound = 8 * std::numeric_limits<double>::epsilon() * (std::abs(first) + std::abs(second));
    const double minimum=16*std::numeric_limits<double>::min();
    if (std::isfinite(determinant) && std::abs(determinant)>=minimum && std::abs(determinant) > bound)
        return determinant > 0 ? 1 : -1;
    const Rational exact = (Rational(b.U) - a.U) * (Rational(c.V) - a.V) -
        (Rational(b.V) - a.V) * (Rational(c.U) - a.U);
    return exact > 0 ? 1 : exact < 0 ? -1 : 0;
}

bool TransactionalPredicates::Shape(const Point& a, const Point& b, const Point& c)
{
    if (Orientation(a, b, c) <= 0) return false;
    const std::array<Point, 3> points{a,b,c};
    // 对三个角分别检查，单个正面积条件无法排除极薄面
    for (std::size_t i = 0; i < 3; ++i)
    {
        const auto& p = points[i]; const auto& q = points[(i+1)%3]; const auto& r = points[(i+2)%3];
        const double ux=q.U-p.U, uy=q.V-p.V, vx=r.U-p.U, vy=r.V-p.V;
        const double dot=ux*vx+uy*vy;
        const double lhs=10*dot*dot, rhs=9*(ux*ux+uy*uy)*(vx*vx+vy*vy);
        const double margin=128*std::numeric_limits<double>::epsilon()*std::max(lhs,rhs);
        // 只有明确落在允许域内部才跳过精确计算，边界和钝角留给统一判据
        if (dot > 0 && lhs < rhs-margin) continue;
        // 最小角使用有理平方比较；等号合法，不因舍入扩大可行域
        const Rational x=Rational(q.U)-p.U, y=Rational(q.V)-p.V;
        const Rational z=Rational(r.U)-p.U, w=Rational(r.V)-p.V;
        const Rational d=x*z+y*w;
        if (d > 0 && 10*d*d > 9*(x*x+y*y)*(z*z+w*w)) return false;
    }
    return true;
}

bool TransactionalPredicates::Contains(const Point& q, const Point& a, const Point& b, const Point& c)
{
    return Orientation(a,b,q)>=0 && Orientation(b,c,q)>=0 && Orientation(c,a,q)>=0;
}

std::array<double, 3> TransactionalPredicates::Barycentric(const Point& q, const Point& a, const Point& b, const Point& c)
{
    // 此处仅给拟合/评分近似权重，不能代替整数比样本的闭面资格判定
    const double area=Cross(a,b,c);
    if (!(area>0)) throw std::runtime_error("退化或反向面");
    const double w0=Cross(q,b,c)/area, w1=Cross(a,q,c)/area;
    return {w0,w1,1-w0-w1};
}
}
