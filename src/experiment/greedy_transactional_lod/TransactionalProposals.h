#pragma once

#include "experiment/greedy_transactional_lod/TransactionalSamples.h"
#include <optional>

namespace ParallelRoam::Experiment::GreedyTransactionalLod
{
/// <summary>
/// 冻结快照上的 E/F/H 目录游标，仅在请求下一项时构造几何与样本。
/// ordinal 与完整目录一致；本游标存活期间不得发布新状态。
/// </summary>
class ReceiverCursor
{
public:
    ReceiverCursor(const TransactionalState& state,const TransactionalSamples& samples,Slot root);
    /// <summary>
    /// 返回下一项并推进冻结目录；耗尽后返回空值，不重新构造尾部。
    /// </summary>
    std::optional<Proposal> Next(WorkLedger* work=nullptr);
private:
    const TransactionalState& _state;
    const TransactionalSamples& _samples;
    Slot _root;
    std::array<Edge,3> _edges;
    std::array<Identity,3> _vertices;
    Point _center;
    std::vector<Point> _locations;
    std::size_t _phase{},_position{},_ordinal{};
    bool _locationsReady{};
};

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
