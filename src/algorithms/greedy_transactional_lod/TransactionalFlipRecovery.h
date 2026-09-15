#pragma once

#include "algorithms/greedy_transactional_lod/TransactionalSamples.h"
#include <optional>

namespace ParallelRoam::Algorithms::GreedyTransactionalLod
{
/// <summary>
/// 冻结快照上的两面翻边；固定旧点几何，不创建高度自由度或预算需求
/// 目录顺序由根的三条稳定边决定，认证不读取未来视图
/// </summary>
class TransactionalFlipRecovery
{
public:
    static Proposal Construct(const TransactionalState& state,Slot root,Edge edge);
    static std::optional<Proposal> FirstCertified(const TransactionalState& state,const TransactionalSamples& samples,
        Slot root,WorkLedger& work,std::vector<std::pair<char,std::string>>& attempts);
    /// <summary>
    /// 提交前核对净零类型的完整结构前提，拒绝伪造修复或改高记录
    /// 不重新求值质量，后者仍由绑定快照的认证记录负责
    /// </summary>
    static bool IsUnchangedGeometryFlip(const TransactionalState& state,const Proposal& proposal);
};
}
