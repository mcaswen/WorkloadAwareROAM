#pragma once

#include "algorithms/greedy_transactional_lod/TransactionalSamples.h"

namespace ParallelRoam::Algorithms::GreedyTransactionalLod
{
/// <summary>
/// 单个提案调用期间的参数域事实；高度变化不会改变覆盖面与拟合权重。
/// double 权重只供拟合，区间和精确认证仍按各自数值域计算插值。
/// </summary>
class TransactionalProposalEvidence
{
public:
    struct Entry
    {
        std::size_t Face{};
        std::array<double,3> Weights{};
        bool Ready{};
    };
    TransactionalProposalEvidence(const TransactionalSamples& samples,const Proposal& proposal,WorkLedger& work);
    /// <summary>
    /// 首次触达按原面顺序定位；样本列表必须保持排序且在调用期间不变。
    /// </summary>
    const Entry& Get(Slot sample,WorkLedger& work);
    /// <summary>
    /// 复用参数域中的面选择，返回的点高度始终读取当前提案。
    /// </summary>
    std::array<Point,3> Face(Slot sample,WorkLedger& work);
private:
    const TransactionalSamples& _samples;
    const Proposal& _proposal;
    std::vector<Entry> _entries;
};
}
