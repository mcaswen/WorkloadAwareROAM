#include "algorithms/greedy_transactional_lod/TransactionalQualitySampleEvidence.h"
#include "algorithms/greedy_transactional_lod/TransactionalQualityFaceEvidence.h"

namespace ParallelRoam::Algorithms::GreedyTransactionalLod
{
using namespace QualityEvaluation;

TransactionalQualitySampleEvidence::TransactionalQualitySampleEvidence(const TransactionalState& state,
    const TransactionalSamples& samples, Slot sample, bool output, QualityEvidenceWork* work)
    : _state(state), _samples(samples), _sample(sample), _output(output), _work(work)
{
    if (_work)
    {
        ++_work->Contexts;
    }
}

const std::array<Interval, 3>& TransactionalQualitySampleEvidence::ReferenceBounds()
{
    if (!_referenceBounds)
    {
        _referenceBounds = Reference<Interval>(_state, _samples, _sample);
        if (_work)
        {
            ++_work->BoundsReferences;
        }
    }
    return *_referenceBounds;
}

const std::array<R, 3>& TransactionalQualitySampleEvidence::ExactReference()
{
    if (!_exactReference)
    {
        _exactReference = Reference<R>(_state, _samples, _sample);
        if (_work)
        {
            ++_work->ExactReferences;
        }
    }
    return *_exactReference;
}

const std::array<Interval, 2>& TransactionalQualitySampleEvidence::CoordinateBounds()
{
    if (!_coordinateBounds)
    {
        if (_referenceBounds)
        {
            _coordinateBounds = Coordinate(*_referenceBounds, _state.Config(), _output);
        }
        else
        {
            // 覆盖只依赖参数坐标，不为尚未需要的参考高度执行双线性插值
            // 除法和输出域转换仍使用原区间表达式，不能先算double再包区间
            const auto xy = _samples.Decode(_sample);
            const std::array<Interval, 3> reference{
                Interval(xy[0]) / Interval(_samples.Denominator()),
                Interval(xy[1]) / Interval(_samples.Denominator()), Interval(0)};
            _coordinateBounds = Coordinate(reference, _state.Config(), _output);
        }
    }
    return *_coordinateBounds;
}

const std::array<R, 2>& TransactionalQualitySampleEvidence::ExactCoordinate()
{
    if (!_exactCoordinate)
    {
        if (_exactReference)
        {
            _exactCoordinate = Coordinate(*_exactReference, _state.Config(), _output);
        }
        else
        {
            // 边谓词歧义不等于质量谓词歧义；精确高度留到真正读取时构造
            const auto xy = _samples.Decode(_sample);
            const std::array<R, 3> reference{
                R(xy[0]) / R(_samples.Denominator()),
                R(xy[1]) / R(_samples.Denominator()), R(0)};
            _exactCoordinate = Coordinate(reference, _state.Config(), _output);
        }
    }
    return *_exactCoordinate;
}

const std::array<Interval, 4>& TransactionalQualitySampleEvidence::ClipBounds()
{
    if (!_clipBounds)
    {
        const auto& ref = ReferenceBounds();
        _clipBounds = Clip(_state.Config(), ref[0], ref[1], ref[2]);
        if (_work)
        {
            ++_work->BoundsClips;
        }
    }
    return *_clipBounds;
}

const std::array<R, 4>& TransactionalQualitySampleEvidence::ExactClip()
{
    if (!_exactClip)
    {
        const auto& ref = ExactReference();
        _exactClip = Clip(_state.Config(), ref[0], ref[1], ref[2]);
        if (_work)
        {
            ++_work->ExactClips;
        }
    }
    return *_exactClip;
}

std::optional<QualityFace> TransactionalQualitySampleEvidence::Cover(const std::vector<QualityFace>& faces)
{
    // 两次覆盖共用坐标，但面遍历顺序不变；精确坐标只在边谓词歧义时读取
    return CoverPrepared(CoordinateBounds(), faces, [this]() -> const auto& {
        if (_work)
        {
            ++_work->ExactCoverRequests;
        }
        return ExactCoordinate();
    });
}

TransactionalQualityFaceEvidence* TransactionalQualitySampleEvidence::Cover(
    std::vector<TransactionalQualityFaceEvidence>& faces)
{
    const auto& q = CoordinateBounds();
    for (auto& face : faces)
    {
        bool covered = true;
        for (std::size_t edge = 0; edge < 3; ++edge)
        {
            const auto cross = face.SideBounds(edge, q);
            if (cross.Low >= 0)
            {
                continue;
            }
            if (cross.High < 0)
            {
                covered = false;
                break;
            }
            // 与原覆盖入口一致：仅歧义边读取精确坐标，共享边仍按原面序闭包含
            if (_work)
            {
                ++_work->ExactCoverRequests;
            }
            if (face.ExactSide(edge, ExactCoordinate()) < 0)
            {
                covered = false;
                break;
            }
        }
        if (covered)
        {
            return &face;
        }
    }
    return nullptr;
}

bool TransactionalQualitySampleEvidence::VisibilityAgrees()
{
    const auto visible = VisibleBounds(_state.Config(), ClipBounds());
    const bool expected = _samples.Projection(_sample).Visible;
    if (visible)
    {
        return *visible == expected;
    }
    // 裁剪边界仍核对精确人口，复用并不把不确定点当作不可见
    if (_work)
    {
        ++_work->ExactVisibilityRequests;
    }
    return ExactVisible(_state.Config(), ExactClip()) == expected;
}
}
