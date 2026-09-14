#pragma once

#include "algorithms/greedy_transactional_lod/TransactionalPipeline.h"
#include <filesystem>
#include <fstream>

namespace ParallelRoam::Experiment::GreedyTransactionalLod
{
/// <summary>
/// 两个预先冻结见证的离线追溯，借用批前状态和已批准提案
/// 全域定位与额外认证只进入独立探针，不参与正常决策或计时
/// </summary>
class TransactionalQualityProvenance
{
    using State=Algorithms::GreedyTransactionalLod::TransactionalState;
    using Samples=Algorithms::GreedyTransactionalLod::TransactionalSamples;
    using Batch=Algorithms::GreedyTransactionalLod::CertifiedBatch;
    using Config=Algorithms::GreedyTransactionalLod::Configuration;
public:
    TransactionalQualityProvenance(const std::filesystem::path& output,const Config& future,const Config& returned);
    void Before(std::size_t frame,const State& state,const Samples& samples,const Batch& batch);
    void After(std::size_t frame,const State& state,const Samples& samples);
private:
    void Observe(std::size_t frame,const char* phase,const State& state,const Samples& samples,const Batch* batch);
    // 固定未来视图只作反事实投影，不替换当前算法视图
    Config _future,_returned;
    std::ofstream _witnesses,_transactions,_recovery;
    // 批前局部预测与批后内部曲面比对；不以它代替实际 float 网格评价
    std::array<double,2> _expected{};
    std::map<Algorithms::GreedyTransactionalLod::Identity,double> _survivors;
};
}
