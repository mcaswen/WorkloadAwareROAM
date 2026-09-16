#pragma once

#include "algorithms/greedy_transactional_lod/TransactionalSamples.h"

namespace ParallelRoam::Algorithms::GreedyTransactionalLod
{
/// <summary>
/// 单侧外边界中点细分，固定旧点并用连续源高度生成唯一新点
/// 只生成和认证局部提案，不决定全局顺序、预算或发布
/// </summary>
class TransactionalBoundaryRefinement
{
public:
    static Proposal Construct(const TransactionalState& state, const HeightSource& source,
        Slot root, Edge edge, Identity newVertex);
    static std::string Certify(const TransactionalState& state, const TransactionalSamples& samples,
        Proposal& proposal, WorkLedger& work);
    /// <summary>
    /// 准备发布前重建有界结构和源高证书，不重新进行质量求值
    /// </summary>
    static bool IsSourceMidpointSplit(const TransactionalState& state, const HeightSource& source,
        const Proposal& proposal);
};
}
