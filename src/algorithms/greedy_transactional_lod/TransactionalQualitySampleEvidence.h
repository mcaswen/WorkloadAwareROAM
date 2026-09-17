#pragma once

#include "algorithms/greedy_transactional_lod/TransactionalQualityEvaluation.h"

namespace ParallelRoam::Algorithms::GreedyTransactionalLod
{
class TransactionalQualityFaceEvidence;
/// <summary>
/// 提案内累计证据构造次数与精确请求；在样本循环外汇入现有账本
/// 这些是物理工作计数，不参与访问配额或质量判断
/// </summary>
struct QualityEvidenceWork
{
    std::uint64_t Contexts{}, BoundsReferences{}, ExactReferences{}, BoundsClips{}, ExactClips{};
    std::uint64_t ExactCoverRequests{}, ExactVisibilityRequests{};
};

/// <summary>
/// 单个样本与表示域的不可变数值证据，按需复用参考、坐标和参考投影
/// 只借用本次认证的冻结输入；不拥有提案，不允许跨样本或跨线程共享
/// </summary>
class TransactionalQualitySampleEvidence
{
public:
    TransactionalQualitySampleEvidence(const TransactionalState& state,
        const TransactionalSamples& samples, Slot sample, bool output, QualityEvidenceWork* work = nullptr);

    const std::array<QualityEvaluation::Interval, 3>& ReferenceBounds();
    const std::array<QualityEvaluation::R, 3>& ExactReference();
    const std::array<QualityEvaluation::Interval, 2>& CoordinateBounds();
    const std::array<QualityEvaluation::R, 2>& ExactCoordinate();
    const std::array<QualityEvaluation::Interval, 4>& ClipBounds();
    const std::array<QualityEvaluation::R, 4>& ExactClip();
    std::optional<QualityEvaluation::QualityFace> Cover(const std::vector<QualityEvaluation::QualityFace>& faces);
    /// <summary>
    /// 返回面集合中借用的首个闭覆盖面，集合须在消费期间保持存活和位置稳定
    /// </summary>
    TransactionalQualityFaceEvidence* Cover(std::vector<TransactionalQualityFaceEvidence>& faces);
    bool VisibilityAgrees();

private:
    const TransactionalState& _state;
    const TransactionalSamples& _samples;
    Slot _sample;
    bool _output;
    QualityEvidenceWork* _work;
    // 精确域对象惰性构造，普通区间路径不提前支付有理插值或矩阵运算
    std::optional<std::array<QualityEvaluation::Interval, 3>> _referenceBounds;
    std::optional<std::array<QualityEvaluation::R, 3>> _exactReference;
    std::optional<std::array<QualityEvaluation::Interval, 2>> _coordinateBounds;
    std::optional<std::array<QualityEvaluation::R, 2>> _exactCoordinate;
    std::optional<std::array<QualityEvaluation::Interval, 4>> _clipBounds;
    std::optional<std::array<QualityEvaluation::R, 4>> _exactClip;
};
}
