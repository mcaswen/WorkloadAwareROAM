#pragma once

#include "algorithms/greedy_transactional_lod/TransactionalSamples.h"

namespace ParallelRoam::Algorithms::GreedyTransactionalLod
{
class TransactionalProposalEvidence;

/// <summary>
/// 一维拟合实际形成的增量区间，供只读诊断复核选值空间
/// 有效区间不代表最终发布高度已经通过质量认证
/// </summary>
struct SingleHeightFitInterval
{
    bool Available{};
    double InitialHeight{}, LowerDelta{}, UpperDelta{};
};

/// <summary>
/// 拟合只生成高度候选，接受证据来自实际发布几何的区间或有理复核
/// 同一能力也负责回收方的误差上界，避免两套质量定义
/// </summary>
class TransactionalCertification
{
public:
    /// <summary>
    /// 由旧支持见证生成原微像素进展目标；空字符串表示目标可用
    /// 不拟合高度，净零连接修复也使用相同的见证与舍入规则
    /// </summary>
    static std::string SetProgressTarget(const TransactionalState& state,const TransactionalSamples& samples,
        Proposal& proposal,WorkLedger& work);
    static std::string Fit(const TransactionalState& state, const TransactionalSamples& samples,
        Proposal& proposal, WorkLedger& work, SingleHeightFitInterval* interval = nullptr);
    static bool Measure(const TransactionalState& state, const TransactionalSamples& samples,
        Proposal& proposal, WorkLedger& work);
    static bool Accepts(const TransactionalState& state, const TransactionalSamples& samples,
        const Proposal& proposal, std::int64_t targetMicropixels, WorkLedger& work);
    static double ExactErrorSquared(const TransactionalState& state, const TransactionalSamples& samples,
        Slot sample, const Proposal* proposal = nullptr);
    static bool PreservesHeight(const TransactionalState& state,const TransactionalSamples& samples,
        const Proposal& receiver,const Proposal* donor,WorkLedger& work);
private:
    static bool Measure(const TransactionalState& state,const TransactionalSamples& samples,
        Proposal& proposal,WorkLedger& work,TransactionalProposalEvidence* evidence);
};
}
