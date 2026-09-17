#include "algorithms/greedy_transactional_lod/TransactionalHeightRejection.h"
#include "algorithms/greedy_transactional_lod/TransactionalQualitySampleEvidence.h"
#include "algorithms/greedy_transactional_lod/TransactionalQualityFaceEvidence.h"

namespace ParallelRoam::Algorithms::GreedyTransactionalLod
{
bool TransactionalHeightRejection::ProvesDamage(const QualityEvaluation::Interval& before,
    const QualityEvaluation::Interval& after, const QualityEvaluation::Interval& cap)
{
    // 等号和区间重叠都不足以证明损伤，未知情况必须交回完整认证
    return QualityEvaluation::Finite(before) && QualityEvaluation::Finite(after) &&
        QualityEvaluation::Finite(cap) && after.Low > std::max(before.High, cap.High);
}

bool TransactionalHeightRejection::Recheck(const Configuration& config, TransactionalQualitySampleEvidence& sample,
    std::vector<TransactionalQualityFaceEvidence>& before, std::vector<TransactionalQualityFaceEvidence>& after)
{
    auto* old = sample.Cover(before);
    auto* next = sample.Cover(after);
    if (!old || !next || !sample.VisibilityAgrees())
    {
        return false;
    }
    const auto& uv = sample.CoordinateBounds();
    const auto& reference = sample.ReferenceBounds();
    const auto oldResidual = old->HeightBounds(uv) - reference[2];
    const auto newResidual = next->HeightBounds(uv) - reference[2];
    const auto target = QualityEvaluation::Interval(config.HeightScale) *
        QualityEvaluation::Interval(config.QualityHeightRatio);
    // 只用当前区间证明拒绝；不为提示增加另一套精确高度搜索
    return ProvesDamage(oldResidual * oldResidual, newResidual * newResidual, target * target);
}
}
