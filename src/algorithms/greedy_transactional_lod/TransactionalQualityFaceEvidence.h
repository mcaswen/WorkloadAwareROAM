#pragma once

#include "algorithms/greedy_transactional_lod/TransactionalQualityEvaluation.h"

namespace ParallelRoam::Algorithms::GreedyTransactionalLod
{
/// <summary>
/// 面证据的物理构造和消费数量，按提案归并，不参与质量或访问配额
/// </summary>
struct QualityFaceEvidenceWork
{
    std::uint64_t Records{};
    std::uint64_t BoundsBuilds{};
    std::uint64_t ExactBuilds{};
    std::uint64_t BoundsSides{};
    std::uint64_t ExactSides{};
    std::uint64_t BoundsHeights{};
    std::uint64_t ExactHeights{};
};

/// <summary>
/// 单面冻结几何的惰性系数，供同一提案和表示域中的多个样本复用
/// 拥有点值，不借用目录游标；不改变覆盖顺序，也不作质量接受决策
/// </summary>
class TransactionalQualityFaceEvidence
{
public:
    explicit TransactionalQualityFaceEvidence(const QualityEvaluation::QualityFace& face,
        QualityFaceEvidenceWork* work = nullptr);

    const QualityEvaluation::QualityFace& Geometry() const;
    QualityEvaluation::Interval SideBounds(std::size_t edge, const std::array<QualityEvaluation::Interval, 2>& q);
    QualityEvaluation::R ExactSide(std::size_t edge, const std::array<QualityEvaluation::R, 2>& q);
    QualityEvaluation::Interval HeightBounds(const std::array<QualityEvaluation::Interval, 2>& q);
    QualityEvaluation::R ExactHeight(const std::array<QualityEvaluation::R, 2>& q);

private:
    /// <summary>
    /// 保存原表达式树的固定子式，不能用取负或倒数变换替代区间原运算
    /// </summary>
    template<class T>
    struct Coefficients
    {
        std::array<std::array<T, 3>, 3> Points;
        std::array<std::array<T, 2>, 3> Edges;
        std::array<T, 2> CA;
        T Area;
    };

    template<class T> Coefficients<T> Prepare() const;
    template<class T> static T Interpolate(const Coefficients<T>& face, const std::array<T, 2>& q);
    const Coefficients<QualityEvaluation::Interval>& Bounds();
    const Coefficients<QualityEvaluation::R>& Exact();

    QualityEvaluation::QualityFace _face;
    QualityFaceEvidenceWork* _work;
    // 未触达的面只拥有点值；精确算术不会因区间准备而提前执行
    std::optional<Coefficients<QualityEvaluation::Interval>> _bounds;
    std::optional<Coefficients<QualityEvaluation::R>> _exact;
};
}
