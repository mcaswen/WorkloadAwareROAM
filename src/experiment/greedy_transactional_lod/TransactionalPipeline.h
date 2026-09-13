#pragma once

#include "experiment/greedy_transactional_lod/TransactionalSamples.h"

namespace ParallelRoam::Experiment::GreedyTransactionalLod
{
/// <summary>
/// 自有状态的阶段入口；当前单批更新不隐含全量诊断或文件输出
/// </summary>
class TransactionalPipeline
{
public:
    explicit TransactionalPipeline(const InitialMesh& input);
    CertifiedBatch Update(WorkLedger& work);
    const TransactionalState& State() const { return _state; }
    const TransactionalSamples& Samples() const { return _samples; }
private:
    TransactionalState _state;
    TransactionalSamples _samples;
    bool _updated{};
};
}
