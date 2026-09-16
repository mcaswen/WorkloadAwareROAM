#pragma once

#include "algorithms/greedy_transactional_lod/TransactionalPipeline.h"
#include <filesystem>
#include <fstream>
#include <optional>
#include <vector>

namespace ParallelRoam::Experiment::GreedyTransactionalLod
{
/// <summary>
/// 预先冻结见证的离线追溯，兼容历史见证和显式有限见证列表
/// 全域定位与额外认证只进入独立探针，不参与正常决策或计时
/// </summary>
class TransactionalQualityProvenance
{
    using State=Algorithms::GreedyTransactionalLod::TransactionalState;
    using Samples=Algorithms::GreedyTransactionalLod::TransactionalSamples;
    using Batch=Algorithms::GreedyTransactionalLod::CertifiedBatch;
    using Config=Algorithms::GreedyTransactionalLod::Configuration;
public:
    TransactionalQualityProvenance(const std::filesystem::path& output,const Config& future,const Config& returned,
        std::optional<Algorithms::GreedyTransactionalLod::Point> additionalWitness = {});
    TransactionalQualityProvenance(const std::filesystem::path& output, const Config& future,
        const Config& returned, std::vector<Algorithms::GreedyTransactionalLod::Point> witnesses);
    /// <summary>
    /// 在首次更新之前记录真实种子，不将 frame 0 的批后状态冒充初始状态
    /// </summary>
    void Seed(const State& state, const Samples& samples);
    /// <summary>
    /// 在指定批前快照导出全部活动面的误差与密度分量，供离线排序反事实使用
    /// 只读已有闭面贡献和前缀，不改变生产账本或候选集合
    /// </summary>
    static void WritePrioritySnapshot(const std::filesystem::path& path, const State& state, const Samples& samples);
    void Before(std::size_t frame,const State& state,const Samples& samples,const Batch& batch);
    void After(std::size_t frame,const State& state,const Samples& samples);
private:
    void Observe(std::size_t frame,const char* phase,const State& state,const Samples& samples,const Batch* batch);
    // 固定未来视图只作反事实投影，不替换当前算法视图
    Config _future,_returned;
    std::ofstream _witnesses,_transactions,_recovery;
    std::ofstream _roots;
    bool _explicitWitnesses{};
    // 批前局部预测与批后内部曲面比对；不以它代替实际 float 网格评价
    std::vector<Algorithms::GreedyTransactionalLod::Point> _points;
    std::vector<double> _expected;
    std::map<Algorithms::GreedyTransactionalLod::Identity,double> _survivors;
};
}
