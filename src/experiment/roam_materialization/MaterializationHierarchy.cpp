#include "experiment/roam_materialization/MaterializationHierarchy.h"

#include "algorithms/RoamGeometry.h"

#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>

namespace ParallelRoam::Experiment::RoamMaterialization
{
namespace
{
constexpr std::int64_t Grid = 1LL << MaximumDepth;
// 三倍坐标叉积每项最多 3G²，两项相减仍须在有符号整数范围内
static_assert(Grid <= std::numeric_limits<std::int64_t>::max() / Grid / 6);

bool Contains(const Domain& domain, PointKey triplePoint)
{
    // 重心以三倍坐标表示，方向判定全部在整数域中完成
    const std::array vertices{MaterializationHierarchy::Point(domain.A),
        MaterializationHierarchy::Point(domain.B), MaterializationHierarchy::Point(domain.C)};
    for (std::size_t i = 0; i < 3; ++i)
    {
        const auto a = vertices[i], b = vertices[(i + 1) % 3];
        const auto cross = (b.X - a.X) * (triplePoint.Y - 3 * a.Y) -
            (b.Y - a.Y) * (triplePoint.X - 3 * a.X);
        if (cross > 0) return false;
    }
    return true;
}
}

int MaterializationHierarchy::Depth(NodeId id)
{
    if (id == 0) throw std::invalid_argument("空身份没有层次深度");
    const int highest = static_cast<int>(std::bit_width(id)) - 1;
    const int depth = highest >= 32 ? highest - 32 : highest;
    if (depth > MaximumDepth || (highest >= 32 && (id >> depth) != RootB))
        throw std::invalid_argument("路径不属于支持的二根层次");
    return depth;
}

NodeId MaterializationHierarchy::Parent(NodeId id)
{
    return Depth(id) == 0 ? 0 : id / 2;
}

std::array<NodeId, 2> MaterializationHierarchy::Children(NodeId id)
{
    if (Depth(id) >= MaximumDepth) throw std::invalid_argument("孩子超过最大身份深度");
    return {id * 2, id * 2 + 1};
}

Domain MaterializationHierarchy::Decode(NodeId id)
{
    const int depth = Depth(id);
    Domain domain = (id >> depth) == RootA ?
        Domain{{0, 1}, {1, 0}, {0, 0}} : Domain{{1, 0}, {0, 1}, {1, 1}};
    // 共享二分规则在所选深度内产生可精确表示的二进制坐标
    for (int bit = depth - 1; bit >= 0; --bit)
    {
        const auto children = Algorithms::Roam::SplitTriangleDomain(domain);
        domain = ((id >> bit) & 1U) == 0 ? children.Left : children.Right;
    }
    return domain;
}

PointKey MaterializationHierarchy::Point(glm::vec2 value)
{
    return {static_cast<std::int64_t>(value.x * static_cast<float>(Grid)),
        static_cast<std::int64_t>(value.y * static_cast<float>(Grid))};
}

NodeId MaterializationHierarchy::Mate(NodeId id)
{
    const auto domain = Decode(id);
    const auto a = Point(domain.A), b = Point(domain.B), c = Point(domain.C);
    const PointKey opposite{a.X + b.X - c.X, a.Y + b.Y - c.Y};
    if (opposite.X < 0 || opposite.X > Grid || opposite.Y < 0 || opposite.Y > Grid) return 0;
    const PointKey center{a.X + b.X + opposite.X, a.Y + b.Y + opposite.Y};
    NodeId found = Contains(Decode(RootA), center) ? RootA : RootB;
    auto candidate = Decode(found);
    for (int level = 0; level < Depth(id); ++level)
    {
        const auto halves = Algorithms::Roam::SplitTriangleDomain(candidate);
        const bool left = Contains(halves.Left, center);
        found = found * 2 + (left ? 0U : 1U);
        candidate = left ? halves.Left : halves.Right;
    }
    // 定位只给出候选，必须再核对同底边和反射后的顶点
    if (Edges(candidate)[0] != Edges(domain)[0] || Point(candidate.C) != opposite)
        throw std::logic_error("同层伙伴定位与精确底边不一致");
    return found;
}

NodeId MaterializationHierarchy::Group(NodeId id)
{
    const auto mate = Mate(id);
    return mate == 0 ? id : std::min(id, mate);
}

std::vector<NodeId> MaterializationHierarchy::Members(NodeId id)
{
    const auto mate = Mate(id);
    if (mate == 0) return {id};
    return {std::min(id, mate), std::max(id, mate)};
}

std::array<EdgeKey, 3> MaterializationHierarchy::Edges(const Domain& domain)
{
    const auto a = Point(domain.A), b = Point(domain.B), c = Point(domain.C);
    const auto edge = [](PointKey x, PointKey y) { return EdgeKey{std::min(x, y), std::max(x, y)}; };
    return {edge(a, b), edge(c, a), edge(b, c)};
}

bool MaterializationHierarchy::IsBoundary(const EdgeKey& e)
{
    return (e.First.X == e.Second.X && (e.First.X == 0 || e.First.X == Grid)) ||
        (e.First.Y == e.Second.Y && (e.First.Y == 0 || e.First.Y == Grid));
}
}
