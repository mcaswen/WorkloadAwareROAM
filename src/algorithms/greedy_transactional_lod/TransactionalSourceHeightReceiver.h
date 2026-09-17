#pragma once

#include "algorithms/greedy_transactional_lod/TransactionalSamples.h"

namespace ParallelRoam::Algorithms::GreedyTransactionalLod
{
/// <summary>
/// 为原接收目录的唯一新点赋源高度，并核对局部结构前提。
/// 仅修改私有提案；准备成功不代表质量合格，也不授权发布。
/// </summary>
class TransactionalSourceHeightReceiver
{
public:
    /// <summary>
    /// 空字符串表示结构准备完成，调用方随后必须执行完整逐点认证。
    /// 输入必须来自同一快照的接收游标，失败不回退到旧拟合。
    /// </summary>
    static std::string Prepare(const TransactionalState& state, const TransactionalSamples& samples,
        Proposal& proposal, WorkLedger& work);
};
}
