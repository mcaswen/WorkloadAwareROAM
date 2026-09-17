#pragma once

#include "algorithms/greedy_transactional_lod/TransactionalSamples.h"

namespace ParallelRoam::Algorithms::GreedyTransactionalLod
{
/// <summary>
/// 冻结目标下的逐点损伤和进展认证，不参与根选择、拟合或资源预留
/// </summary>
class TransactionalPointwiseQuality
{
  public:
    static bool Enabled(const Configuration &config);
    static void Validate(const Configuration &config);
    /// <summary>
    /// 只有认证完成才附着只读证书；拒绝原因区分损伤、无进展和数值未知
    /// </summary>
    static std::string Certify(const TransactionalState &state, const TransactionalSamples &samples, Proposal &proposal,
                               bool receiver, WorkLedger &work);
    /// <summary>
    /// 核对代次、实际提案与共享贡献；失败抛出异常，调用方不得发布部分批次
    /// </summary>
    static void ValidateBatch(const TransactionalState &state, const TransactionalSamples &samples,
                              const CertifiedBatch &batch, WorkLedger &work);
};
} // namespace ParallelRoam::Algorithms::GreedyTransactionalLod
