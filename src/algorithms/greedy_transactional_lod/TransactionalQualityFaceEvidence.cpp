#include "algorithms/greedy_transactional_lod/TransactionalQualityFaceEvidence.h"

namespace ParallelRoam::Algorithms::GreedyTransactionalLod
{
using namespace QualityEvaluation;

TransactionalQualityFaceEvidence::TransactionalQualityFaceEvidence(const QualityFace& face, QualityFaceEvidenceWork* work)
    : _face(face), _work(work)
{
    if (_work)
    {
        ++_work->Records;
    }
}

const QualityFace& TransactionalQualityFaceEvidence::Geometry() const
{
    return _face;
}

template<class T>
TransactionalQualityFaceEvidence::Coefficients<T> TransactionalQualityFaceEvidence::Prepare() const
{
    Coefficients<T> result;
    for (std::size_t i = 0; i < 3; ++i)
    {
        result.Points[i] = {T(_face[i].U), T(_face[i].V), T(_face[i].Height)};
    }
    for (std::size_t i = 0; i < 3; ++i)
    {
        const auto& a = result.Points[i];
        const auto& b = result.Points[(i + 1) % 3];
        result.Edges[i] = {b[0] - a[0], b[1] - a[1]};
    }
    // CA单独按原方向减法；由反向边取负可能改变零附近的区间端点
    const auto& a = result.Points[0];
    const auto& c = result.Points[2];
    result.CA = {c[0] - a[0], c[1] - a[1]};
    result.Area = result.Edges[0][0] * result.CA[1] - result.Edges[0][1] * result.CA[0];
    return result;
}

const TransactionalQualityFaceEvidence::Coefficients<Interval>& TransactionalQualityFaceEvidence::Bounds()
{
    if (!_bounds)
    {
        _bounds = Prepare<Interval>();
        if (_work)
        {
            ++_work->BoundsBuilds;
        }
    }
    return *_bounds;
}

const TransactionalQualityFaceEvidence::Coefficients<R>& TransactionalQualityFaceEvidence::Exact()
{
    if (!_exact)
    {
        _exact = Prepare<R>();
        if (_work)
        {
            ++_work->ExactBuilds;
        }
    }
    return *_exact;
}

Interval TransactionalQualityFaceEvidence::SideBounds(std::size_t edge, const std::array<Interval, 2>& q)
{
    const auto& face = Bounds();
    if (_work)
    {
        ++_work->BoundsSides;
    }
    return face.Edges[edge][0] * (q[1] - face.Points[edge][1]) -
           face.Edges[edge][1] * (q[0] - face.Points[edge][0]);
}

R TransactionalQualityFaceEvidence::ExactSide(std::size_t edge, const std::array<R, 2>& q)
{
    const auto& face = Exact();
    if (_work)
    {
        ++_work->ExactSides;
    }
    return face.Edges[edge][0] * (q[1] - face.Points[edge][1]) -
           face.Edges[edge][1] * (q[0] - face.Points[edge][0]);
}

template<class T>
T TransactionalQualityFaceEvidence::Interpolate(const Coefficients<T>& face, const std::array<T, 2>& q)
{
    const auto& a = face.Points[0];
    const auto& b = face.Points[1];
    const auto& c = face.Points[2];
    // 样本相关分子与除法不重排，分母跨零仍交给原Interval除法处理
    const T w0 = ((b[0] - q[0]) * (c[1] - q[1]) - (b[1] - q[1]) * (c[0] - q[0])) / face.Area;
    const T w1 = ((q[0] - a[0]) * face.CA[1] - (q[1] - a[1]) * face.CA[0]) / face.Area;
    const T w2 = T(1) - w0 - w1;
    return (w0 * a[2] + w1 * b[2]) + w2 * c[2];
}

Interval TransactionalQualityFaceEvidence::HeightBounds(const std::array<Interval, 2>& q)
{
    if (_work)
    {
        ++_work->BoundsHeights;
    }
    return Interpolate(Bounds(), q);
}

R TransactionalQualityFaceEvidence::ExactHeight(const std::array<R, 2>& q)
{
    if (_work)
    {
        ++_work->ExactHeights;
    }
    return Interpolate(Exact(), q);
}
}
