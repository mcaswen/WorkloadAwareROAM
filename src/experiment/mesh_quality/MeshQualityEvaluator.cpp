#include "experiment/mesh_quality/MeshQualityEvaluator.h"

#include "terrain/TerrainMeshBuilder.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>

namespace ParallelRoam::Experiment::MeshQuality
{
namespace
{
using Mesh = Terrain::TerrainMeshData;
using Clock = std::chrono::steady_clock;
constexpr double BarycentricTolerance = 1.0e-10;
constexpr double PositionTolerance = 1.0e-8;
constexpr std::size_t NoTriangle = std::numeric_limits<std::size_t>::max();

double Milliseconds(Clock::time_point begin)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
}

template <glm::length_t N>
bool Finite(const glm::vec<N, double>& value)
{
    for (glm::length_t i = 0; i < N; ++i)
        if (!std::isfinite(value[i])) return false;
    return true;
}

double Cross(const glm::dvec2& a, const glm::dvec2& b)
{
    return a.x * b.y - a.y * b.x;
}

const Terrain::TerrainMeshVertex& Vertex(const Mesh& mesh, std::size_t triangle, std::size_t corner)
{
    return mesh.Vertices[mesh.Indices[3U * triangle + corner]];
}

std::size_t InvalidTriangles(const Mesh& mesh, bool checkPositions = true)
{
    if (mesh.Indices.size() % 3U != 0U) return 1U;
    std::size_t invalid = 0U;
    for (std::size_t t = 0; t < mesh.Indices.size() / 3U; ++t)
    {
        bool valid = true;
        for (std::size_t c = 0; c < 3U; ++c)
        {
            const auto i = mesh.Indices[3U * t + c];
            valid = valid && i < mesh.Vertices.size();
            if (i < mesh.Vertices.size())
                valid = valid && Finite(glm::dvec2(mesh.Vertices[i].TexCoord)) &&
                    (!checkPositions || Finite(glm::dvec3(mesh.Vertices[i].Position)));
        }
        if (valid)
        {
            const auto a = glm::dvec2(Vertex(mesh, t, 0U).TexCoord);
            const auto e = glm::dvec2(Vertex(mesh, t, 1U).TexCoord) - a;
            const auto f = glm::dvec2(Vertex(mesh, t, 2U).TexCoord) - a;
            // 判定采用行列式的相对舍入尺度，不以固定 UV 面积阈值删掉细小合法三角形
            const double roundoff = 32.0 * std::numeric_limits<double>::epsilon() *
                (std::abs(e.x * f.y) + std::abs(e.y * f.x));
            valid = std::abs(Cross(e, f)) > roundoff;
        }
        if (!valid) ++invalid;
    }
    return invalid;
}

/// <summary>
/// UV 包围盒覆盖一段三角形索引；节点只服务当前同步查询，不改变原 mesh 的存储顺序
/// </summary>
struct QueryNode
{
    glm::dvec2 Low{std::numeric_limits<double>::max()};
    glm::dvec2 High{std::numeric_limits<double>::lowest()};
    std::size_t First{0U};
    std::size_t Count{0U};
    std::size_t Left{0U};
    std::size_t Right{0U};
};

/// <summary>
/// 保留同域命中的歧义，不能用任意首个命中掩盖曲面重叠或多高度
/// </summary>
struct QueryHit
{
    std::size_t Triangle{NoTriangle};
    glm::dvec3 Position{0.0};
    bool Interior{false};
    bool Ambiguous{false};
};

/// <summary>
/// 在已校验的只读三角形上构建局部层次索引，避免样本与网格全对全扫描
/// </summary>
class SurfaceQuery
{
public:
    explicit SurfaceQuery(const Mesh& mesh) : _mesh(mesh), _order(mesh.Indices.size() / 3U)
    {
        std::iota(_order.begin(), _order.end(), std::size_t{0U});
        if (!_order.empty()) Build(0U, _order.size());
    }

    QueryHit Find(const glm::dvec2& uv) const
    {
        QueryHit hit;
        if (!_nodes.empty()) FindInNode(0U, uv, hit);
        return hit;
    }

private:
    std::size_t Build(std::size_t first, std::size_t count)
    {
        const std::size_t id = _nodes.size();
        _nodes.emplace_back();
        QueryNode node;
        node.First = first;
        node.Count = count;
        for (std::size_t i = first; i < first + count; ++i)
            for (std::size_t c = 0U; c < 3U; ++c)
            {
                const auto uv = glm::dvec2(Vertex(_mesh, _order[i], c).TexCoord);
                node.Low = glm::min(node.Low, uv);
                node.High = glm::max(node.High, uv);
            }
        if (count > 8U)
        {
            const glm::length_t axis = node.High.x - node.Low.x >= node.High.y - node.Low.y ? 0 : 1;
            const auto center = [this, axis](std::size_t t) {
                return static_cast<double>(Vertex(_mesh, t, 0U).TexCoord[axis]) +
                    Vertex(_mesh, t, 1U).TexCoord[axis] + Vertex(_mesh, t, 2U).TexCoord[axis];
            };
            // 中位数同分时按原三角形编号排序，索引构建不依赖容器的未指定迭代顺序
            const std::size_t half = count / 2U;
            std::nth_element(_order.begin() + static_cast<std::ptrdiff_t>(first),
                _order.begin() + static_cast<std::ptrdiff_t>(first + half),
                _order.begin() + static_cast<std::ptrdiff_t>(first + count),
                [&center](std::size_t a, std::size_t b) {
                    const double ca = center(a), cb = center(b);
                    return ca != cb ? ca < cb : a < b;
                });
            node.Left = Build(first, half);
            node.Right = Build(first + half, count - half);
            node.Count = 0U;
        }
        _nodes[id] = node;
        return id;
    }

    void FindInNode(std::size_t id, const glm::dvec2& uv, QueryHit& hit) const
    {
        const auto& node = _nodes[id];
        const glm::dvec2 padding = (node.High - node.Low) * (2.0 * BarycentricTolerance);
        if (uv.x < node.Low.x - padding.x || uv.y < node.Low.y - padding.y ||
            uv.x > node.High.x + padding.x || uv.y > node.High.y + padding.y) return;
        if (node.Count == 0U)
        {
            FindInNode(node.Left, uv, hit);
            FindInNode(node.Right, uv, hit);
            return;
        }
        for (std::size_t i = node.First; i < node.First + node.Count; ++i)
        {
            const auto t = _order[i];
            const auto a = glm::dvec2(Vertex(_mesh, t, 0U).TexCoord);
            const auto e = glm::dvec2(Vertex(_mesh, t, 1U).TexCoord) - a;
            const auto f = glm::dvec2(Vertex(_mesh, t, 2U).TexCoord) - a;
            const double determinant = Cross(e, f);
            const double b = Cross(uv - a, f) / determinant;
            const double c = Cross(e, uv - a) / determinant;
            glm::dvec3 weights{1.0 - b - c, b, c};
            if (glm::any(glm::lessThan(weights, glm::dvec3{-BarycentricTolerance}))) continue;
            const bool interior = glm::all(glm::greaterThan(weights, glm::dvec3{BarycentricTolerance}));
            // 只修正容差内的边界舍入，再归一化；不会将明显越界点夹到曲面上
            weights = glm::max(weights, glm::dvec3{0.0});
            weights /= weights.x + weights.y + weights.z;
            const glm::dvec3 position = weights.x * glm::dvec3(Vertex(_mesh, t, 0U).Position) +
                weights.y * glm::dvec3(Vertex(_mesh, t, 1U).Position) +
                weights.z * glm::dvec3(Vertex(_mesh, t, 2U).Position);
            if (hit.Triangle != NoTriangle)
            {
                const double scale = std::max({1.0, glm::length(position), glm::length(hit.Position)});
                hit.Ambiguous = hit.Ambiguous || hit.Interior || interior ||
                    glm::length(position - hit.Position) > PositionTolerance * scale;
            }
            hit.Interior = hit.Interior || interior;
            if (t < hit.Triangle) { hit.Triangle = t; hit.Position = position; }
        }
    }

    const Mesh& _mesh;
    std::vector<std::size_t> _order;
    std::vector<QueryNode> _nodes;
};

/// <summary>
/// 参考共享边只由编号最小的 incident triangle 发出中点，内存随原网格而非样本层数增长
/// </summary>
struct ReferenceEdge
{
    std::uint64_t Key;
    std::size_t Owner;
};

std::uint64_t EdgeKey(std::uint32_t a, std::uint32_t b)
{
    return (static_cast<std::uint64_t>(std::min(a, b)) << 32U) | std::max(a, b);
}

void HashValue(std::uint64_t& hash, std::uint64_t value)
{
    for (unsigned i = 0U; i < 8U; ++i)
    {
        hash ^= value & 255U;
        hash *= 1099511628211ULL;
        value >>= 8U;
    }
}

/// <summary>
/// 分母 24 可精确表示两次四分后的顶点、中点和重心，局部整数坐标用于去重与样本身份
/// 模板不接触被测网格，每个原三角形复用同一份小数组
/// </summary>
using BarycentricPoint = std::array<int, 3>;
constexpr int BarycentricDenominator = 24;

BarycentricPoint Midpoint(const BarycentricPoint& a, const BarycentricPoint& b)
{
    return {(a[0] + b[0]) / 2, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2};
}

void SubdivisionSamples(const std::array<BarycentricPoint, 3>& triangle, unsigned level,
    std::vector<BarycentricPoint>& points)
{
    const auto& a = triangle[0];
    const auto& b = triangle[1];
    const auto& c = triangle[2];
    const auto ab = Midpoint(a, b), bc = Midpoint(b, c), ca = Midpoint(c, a);
    if (level != 0U)
    {
        SubdivisionSamples({a, ab, ca}, level - 1U, points);
        SubdivisionSamples({ab, b, bc}, level - 1U, points);
        SubdivisionSamples({ca, bc, c}, level - 1U, points);
        SubdivisionSamples({ab, bc, ca}, level - 1U, points);
        return;
    }
    points.insert(points.end(), {a, b, c, ab, bc, ca,
        {(a[0] + b[0] + c[0]) / 3, (a[1] + b[1] + c[1]) / 3, (a[2] + b[2] + c[2]) / 3}});
}

std::vector<BarycentricPoint> AdditionalSamples(unsigned level)
{
    if (level == 0U) return {};
    std::vector<BarycentricPoint> points;
    SubdivisionSamples({BarycentricPoint{24, 0, 0}, {0, 24, 0}, {0, 0, 24}}, level, points);
    std::sort(points.begin(), points.end());
    points.erase(std::unique(points.begin(), points.end()), points.end());
    // 原始七点继续走旧路径，保持第零层的求值次序与哈希；此处只留下加密点
    std::erase_if(points, [](const auto& p) {
        return std::count(p.begin(), p.end(), 24) == 1 ||
            std::count(p.begin(), p.end(), 12) == 2 || p == BarycentricPoint{8, 8, 8};
    });
    return points;
}

bool InReferenceFrustum(const glm::dvec4& p)
{
    return p.w > 0.0 && p.x >= -p.w && p.x <= p.w && p.y >= -p.w && p.y <= p.w &&
        p.z >= -p.w && p.z <= p.w;
}

bool ValidSource(const BilinearHeightfieldReference& source, const Mesh& domain)
{
    if (source.Width < 2 || source.Height < 2 || !std::isfinite(source.TerrainSize) ||
        source.TerrainSize <= 0.0 || !std::isfinite(source.HeightScale) ||
        source.Samples.size() != static_cast<std::size_t>(source.Width) * static_cast<std::size_t>(source.Height))
        return false;
    for (const auto& vertex : domain.Vertices)
    {
        const auto uv = glm::dvec2(vertex.TexCoord);
        if (!Finite(uv) || glm::any(glm::lessThan(uv, glm::dvec2{0.0})) ||
            glm::any(glm::greaterThan(uv, glm::dvec2{1.0}))) return false;
    }
    return true;
}

/// <summary>
/// 从原始样本独立求值，避免复用生产 float 插值而掩盖数值残差
/// UV 已由模板验证；夹紧只处理算术边界，端点 1 落在最后格点
/// </summary>
glm::dvec3 SourcePosition(const BilinearHeightfieldReference& source, const glm::dvec2& uv)
{
    const double x = std::clamp(uv.x, 0.0, 1.0) * (source.Width - 1);
    const double y = std::clamp(uv.y, 0.0, 1.0) * (source.Height - 1);
    const int x0 = static_cast<int>(x), y0 = static_cast<int>(y);
    const int x1 = std::min(x0 + 1, source.Width - 1), y1 = std::min(y0 + 1, source.Height - 1);
    const double s = x - x0, t = y - y0;
    const auto height = [&](int column, int row) {
        return static_cast<double>(source.Samples[static_cast<std::size_t>(row) *
            static_cast<std::size_t>(source.Width) + static_cast<std::size_t>(column)]) / 65535.0;
    };
    const double low = (1.0 - s) * height(x0, y0) + s * height(x1, y0);
    const double high = (1.0 - s) * height(x0, y1) + s * height(x1, y1);
    return {(uv.x - 0.5) * source.TerrainSize, ((1.0 - t) * low + t * high) * source.HeightScale,
        (uv.y - 0.5) * source.TerrainSize};
}

void AccumulateSample(QualityResult& result, const SurfaceQuery& query, const QualityView& view,
    const glm::dvec2& uv, const glm::dvec3& referencePosition)
{
    ++result.SampleCount;
    const glm::dmat4 matrix(view.ViewProjection);
    const glm::dvec4 referenceClip = matrix * glm::dvec4(referencePosition, 1.0);
    const bool referenceVisible = Finite(referenceClip) && InReferenceFrustum(referenceClip);
    // 参考域分母先于被测查询确定，缺失覆盖不能缩小参考屏幕样本数量
    if (referenceVisible) ++result.ScreenSampleCount;
    const auto hit = query.Find(uv);
    if (hit.Triangle == NoTriangle) { ++result.MissingCoverageCount; return; }
    if (hit.Ambiguous) { ++result.AmbiguousCoverageCount; return; }
    const QualityLocation location{result.SampleCount - 1U, uv, referencePosition, hit.Position};
    const double heightError = std::abs(hit.Position.y - referencePosition.y);
    if (!result.SampledHeightMax || heightError > *result.SampledHeightMax)
    {
        result.SampledHeightMax = heightError;
        result.HeightMaximum = location;
    }
    if (!Finite(referenceClip)) { ++result.InvalidProjectionCount; return; }
    if (!referenceVisible) return;
    const glm::dvec4 measuredClip = matrix * glm::dvec4(hit.Position, 1.0);
    if (!Finite(measuredClip)) { ++result.InvalidProjectionCount; return; }
    if (measuredClip.w <= 0.0 || measuredClip.z < -measuredClip.w)
    {
        ++result.NearPlaneCrossingCount;
        return;
    }
    // 被测点移出视口仍使用未夹紧坐标，参考选定的样本不能因误差变大而消失
    const glm::dvec2 difference = (glm::dvec2(measuredClip) / measuredClip.w -
        glm::dvec2(referenceClip) / referenceClip.w) * glm::dvec2(view.Width, view.Height) * 0.5;
    const double error = glm::length(difference);
    if (!std::isfinite(error)) { ++result.InvalidProjectionCount; return; }
    if (!result.SampledScreenMaxPx || error > *result.SampledScreenMaxPx)
    {
        result.SampledScreenMaxPx = error;
        result.ScreenMaximum = location;
    }
}

/// <summary>
/// 两种参考共享采样与被测查询，双线性模式只替换参考求值，不改变 UV 样本身份
/// </summary>
QualityResult EvaluateSamples(const Mesh& reference, const Mesh& measured,
    const QualityView& view, const QualityOptions& options, const BilinearHeightfieldReference* source)
{
    const auto begin = Clock::now();
    QualityResult result;
    result.SamplingLevel = options.SamplingLevel;
    const auto finish = [&]() { result.TotalMilliseconds = Milliseconds(begin); return result; };
    bool finiteView = true;
    for (glm::length_t i = 0; i < 4; ++i) finiteView = finiteView && Finite(glm::dvec4(view.ViewProjection[i]));
    if (!finiteView || view.Width == 0U || view.Height == 0U ||
        glm::determinant(glm::dmat4(view.ViewProjection)) == 0.0)
    {
        result.Status = EvaluationStatus::InvalidView;
        return finish();
    }
    if (options.MaximumSamples == 0U || !std::isfinite(options.MaximumSeconds) || options.MaximumSeconds <= 0.0 ||
        options.SamplingLevel > 2U)
    {
        result.Status = EvaluationStatus::ResourceLimit;
        return finish();
    }
    result.InvalidGeometryCount = InvalidTriangles(reference, source == nullptr) + InvalidTriangles(measured);
    if (source && !ValidSource(*source, reference)) ++result.InvalidGeometryCount;
    if (reference.Indices.empty()) ++result.InvalidGeometryCount;
    if (result.InvalidGeometryCount != 0U)
    {
        result.Status = EvaluationStatus::InvalidGeometry;
        return finish();
    }
    SurfaceQuery query(measured);
    const auto additional = AdditionalSamples(options.SamplingLevel);
    const std::size_t triangles = reference.Indices.size() / 3U;
    std::vector<std::size_t> vertexOwner(reference.Vertices.size(), NoTriangle);
    std::vector<ReferenceEdge> edges;
    edges.reserve(reference.Indices.size());
    for (std::size_t t = 0U; t < triangles; ++t)
        for (std::size_t c = 0U; c < 3U; ++c)
        {
            const auto a = reference.Indices[3U * t + c];
            const auto b = reference.Indices[3U * t + (c + 1U) % 3U];
            vertexOwner[a] = std::min(vertexOwner[a], t);
            edges.push_back({EdgeKey(a, b), t});
        }
    std::sort(edges.begin(), edges.end(), [](const auto& a, const auto& b) {
        return a.Key != b.Key ? a.Key < b.Key : a.Owner < b.Owner;
    });
    edges.erase(std::unique(edges.begin(), edges.end(), [](const auto& a, const auto& b) {
        return a.Key == b.Key;
    }), edges.end());
    result.IndexMilliseconds = Milliseconds(begin);
    const auto samplingBegin = Clock::now();
    bool exhausted = false;
    const auto sample = [&](std::uint64_t kind, std::uint64_t id, const glm::dvec2& uv,
        const glm::dvec3& p, std::uint64_t detail = 0U) {
        if (exhausted) return;
        if (result.SampleCount >= options.MaximumSamples ||
            (result.SampleCount % 1024U == 0U && Milliseconds(begin) >= options.MaximumSeconds * 1000.0))
        {
            exhausted = true;
            return;
        }
        HashValue(result.SampleHash, kind);
        HashValue(result.SampleHash, id);
        if (kind >= 3U) HashValue(result.SampleHash, detail);
        AccumulateSample(result, query, view, uv, source ? SourcePosition(*source, uv) : p);
    };
    // 每个原参考单元都发出固定样本，共享顶点/边只发一次；被测网格不参与选点
    for (std::size_t t = 0U; t < triangles && !exhausted; ++t)
    {
        glm::dvec2 centerUv{0.0};
        glm::dvec3 centerPosition{0.0};
        for (std::size_t c = 0U; c < 3U; ++c)
        {
            const auto a = reference.Indices[3U * t + c];
            const auto b = reference.Indices[3U * t + (c + 1U) % 3U];
            const auto uv = glm::dvec2(reference.Vertices[a].TexCoord);
            const auto p = source ? glm::dvec3{0.0} : glm::dvec3(reference.Vertices[a].Position);
            if (vertexOwner[a] == t) sample(0U, a, uv, p);
            const auto key = EdgeKey(a, b);
            const auto edge = std::lower_bound(edges.begin(), edges.end(), key,
                [](const ReferenceEdge& entry, std::uint64_t value) { return entry.Key < value; });
            if (edge->Owner == t)
                sample(1U, key, (uv + glm::dvec2(reference.Vertices[b].TexCoord)) * 0.5,
                    source ? glm::dvec3{0.0} : (p + glm::dvec3(reference.Vertices[b].Position)) * 0.5);
            centerUv += uv;
            centerPosition += p;
        }
        sample(2U, t, centerUv / 3.0, centerPosition / 3.0);
        for (const auto& point : additional)
        {
            if (exhausted) break;
            const auto zero = std::find(point.begin(), point.end(), 0);
            std::uint64_t kind = 4U, id = t;
            std::uint64_t detail = static_cast<std::uint64_t>(point[0]) |
                (static_cast<std::uint64_t>(point[1]) << 8U) | (static_cast<std::uint64_t>(point[2]) << 16U);
            if (zero != point.end())
            {
                const auto opposite = static_cast<std::size_t>(zero - point.begin());
                const auto first = (opposite + 1U) % 3U, second = (opposite + 2U) % 3U;
                const auto a = reference.Indices[3U * t + first], b = reference.Indices[3U * t + second];
                id = EdgeKey(a, b);
                const auto edge = std::lower_bound(edges.begin(), edges.end(), id,
                    [](const ReferenceEdge& entry, std::uint64_t key) { return entry.Key < key; });
                if (edge->Owner != t) continue;
                kind = 3U;
                // 边的坐标沿较小顶点编号指向较大编号，避免两侧局部方向造成身份歧义
                detail = static_cast<std::uint64_t>(point[a < b ? second : first]);
            }
            glm::dvec2 uv{0.0};
            glm::dvec3 position{0.0};
            for (std::size_t c = 0U; c < 3U; ++c)
            {
                const double weight = static_cast<double>(point[c]) / BarycentricDenominator;
                uv += weight * glm::dvec2(Vertex(reference, t, c).TexCoord);
                if (!source) position += weight * glm::dvec3(Vertex(reference, t, c).Position);
            }
            sample(kind, id, uv, position, detail);
        }
    }
    result.SampleMilliseconds = Milliseconds(samplingBegin);
    result.Status = exhausted ? EvaluationStatus::ResourceLimit :
        (result.MissingCoverageCount + result.AmbiguousCoverageCount + result.InvalidProjectionCount +
            result.NearPlaneCrossingCount != 0U ? EvaluationStatus::Incomplete : EvaluationStatus::Sampled);
    return finish();
}
}

QualityResult EvaluateMeshQuality(const Mesh& reference, const Mesh& measured,
    const QualityView& view, const QualityOptions& options)
{
    return EvaluateSamples(reference, measured, view, options, nullptr);
}

QualityResult EvaluateMeshQuality(const BilinearHeightfieldReference& reference, const Mesh& samplingDomain,
    const Mesh& measured, const QualityView& view, const QualityOptions& options)
{
    return EvaluateSamples(samplingDomain, measured, view, options, &reference);
}

const char* ToString(EvaluationStatus status)
{
    switch (status)
    {
    case EvaluationStatus::Sampled: return "sampled_only";
    case EvaluationStatus::InvalidGeometry: return "invalid_geometry";
    case EvaluationStatus::InvalidView: return "invalid_view";
    case EvaluationStatus::Incomplete: return "incomplete";
    case EvaluationStatus::ResourceLimit: return "resource_limit";
    }
    return "unknown";
}
}
