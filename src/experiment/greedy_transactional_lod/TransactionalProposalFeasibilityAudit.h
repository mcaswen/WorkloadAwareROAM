#pragma once

#include "algorithms/greedy_transactional_lod/TransactionalSamples.h"

#include <filesystem>
#include <iosfwd>
#include <set>

namespace ParallelRoam::Experiment::GreedyTransactionalLod
{
/// <summary>
/// 原轨迹上的只读提案审计，捕获未拟合几何并复核外部高度见证
/// 所有临时提案与账本独占，不允许向生产状态提交反事实
/// </summary>
class TransactionalProposalFeasibilityAudit
{
public:
    /// <summary>
    /// 请求范围在构造时冻结；外部见证还须与运行时完整输入逐值绑定
    /// </summary>
    TransactionalProposalFeasibilityAudit(const std::filesystem::path& specification, std::size_t frameCount);
    /// <summary>
    /// 在原批次提交前观察同一快照，输出诊断后仍由调用者发布原批次
    /// 不接管生产目录选择、资源预留或状态生命周期
    /// </summary>
    void Observe(const Algorithms::GreedyTransactionalLod::TransactionalState& state,
        const Algorithms::GreedyTransactionalLod::TransactionalSamples& samples,
        std::size_t frame, const std::filesystem::path& output);

private:
    /// <summary>
    /// 见证绑定完整的局部输入文本；哈希只用于重复统计，不作为认证前提
    /// </summary>
    struct Witness
    {
        std::size_t Frame{}, Ordinal{};
        Algorithms::GreedyTransactionalLod::Identity Root{};
        double Height{};
        std::string Binding;
    };

    void CaptureProposal(const Algorithms::GreedyTransactionalLod::TransactionalState& state,
        const Algorithms::GreedyTransactionalLod::TransactionalSamples& samples,
        const Algorithms::GreedyTransactionalLod::Proposal& initial, std::size_t frame,
        std::size_t ordinal, bool inPrefix, const std::filesystem::path& output);
    void VerifyWitnesses(std::ostream& out,
        const Algorithms::GreedyTransactionalLod::TransactionalState& state,
        const Algorithms::GreedyTransactionalLod::TransactionalSamples& samples,
        const Algorithms::GreedyTransactionalLod::Proposal& initial, std::size_t frame,
        std::size_t ordinal, const std::string& binding) const;

    std::map<std::size_t, std::vector<Algorithms::GreedyTransactionalLod::Identity>> _roots;
    std::set<Algorithms::GreedyTransactionalLod::Identity> _trackedRoots;
    std::vector<Witness> _witnesses;
    std::string _expectedSource;
    std::size_t _sampleRecords{};
    bool _costAudit{};
};
}
