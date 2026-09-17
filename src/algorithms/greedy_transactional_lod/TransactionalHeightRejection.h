#pragma once

#include "algorithms/greedy_transactional_lod/TransactionalQualityEvaluation.h"

namespace ParallelRoam::Algorithms::GreedyTransactionalLod
{
class TransactionalQualitySampleEvidence;
class TransactionalQualityFaceEvidence;
/// <summary>
/// 高度检查的局部取证结果；失败样本只是后续复查提示，不是持久证书
/// </summary>
struct HeightRejectionContext
{
    bool HeightFirst{};
    Slot HintSample{InvalidSlot};
    bool Audit{};
    Slot FailureSample{InvalidSlot};
    bool IntervalFailure{};
    std::string OriginalReason;
};

/// <summary>
/// 用同一保守区间式证明单个样本违反高度约束，不负责质量接受
/// </summary>
class TransactionalHeightRejection
{
public:
    static bool ProvesDamage(const QualityEvaluation::Interval& before,
        const QualityEvaluation::Interval& after, const QualityEvaluation::Interval& cap);
    /// <summary>
    /// 对当前支持中的提示点重新定位和求值，不能判定就交回原样本遍历
    /// </summary>
    static bool Recheck(const Configuration& config, TransactionalQualitySampleEvidence& sample,
        std::vector<TransactionalQualityFaceEvidence>& before, std::vector<TransactionalQualityFaceEvidence>& after);
};
}
