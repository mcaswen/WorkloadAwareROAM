#pragma once

#include "experiment/greedy_transactional_lod/TransactionalSamples.h"

namespace ParallelRoam::Experiment::GreedyTransactionalLod
{
/// <summary>
/// 在同一只读网格上生成有限局部提案，不根据 donor 结果更换接收方
/// </summary>
class TransactionalProposals
{
public:
    static std::vector<Proposal> Receivers(const TransactionalState& state,
        const TransactionalSamples& samples, Slot root);
    static Proposal Donor(const TransactionalState& state, const TransactionalSamples& samples,
        Identity center, WorkLedger& work);
    static std::vector<Identity> Ring(const TransactionalState& state, Identity center);
};
}
