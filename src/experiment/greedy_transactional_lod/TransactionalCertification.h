#pragma once

#include "experiment/greedy_transactional_lod/TransactionalSamples.h"

namespace ParallelRoam::Experiment::GreedyTransactionalLod
{
class TransactionalProposalEvidence;

/// <summary>
/// 拟合只生成高度候选，接受证据来自实际发布几何的区间或有理复核
/// 同一能力也负责回收方的误差上界，避免两套质量定义
/// </summary>
class TransactionalCertification
{
public:
    static std::string Fit(const TransactionalState& state, const TransactionalSamples& samples,
        Proposal& proposal, WorkLedger& work);
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
