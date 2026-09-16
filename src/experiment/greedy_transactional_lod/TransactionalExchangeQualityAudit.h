#pragma once

#include "algorithms/greedy_transactional_lod/TransactionalSamples.h"
#include <filesystem>

namespace ParallelRoam::Experiment::GreedyTransactionalLod
{
/// <summary>
/// 导出原批准批次的闭支持与实际几何，不拟合新提案，也不发布筛选结果
/// 独立分析器依据原始源值和可往返 double 重建有理质量证据
/// </summary>
class TransactionalExchangeQualityAudit
{
public:
    /// <summary>
    /// 仅在 Plan 与 Apply 之间调用，所有输入只借读至函数返回
    /// 超过样本上限时整批标为删失，不输出可误认为完整的部分样本
    /// </summary>
    static void Capture(const Algorithms::GreedyTransactionalLod::TransactionalState& state,
        const Algorithms::GreedyTransactionalLod::TransactionalSamples& samples,
        const Algorithms::GreedyTransactionalLod::CertifiedBatch& batch,
        std::size_t frame, const std::filesystem::path& output, std::size_t sampleLimit = 2000000);
};
}
