#include "experiment/greedy_transactional_lod/TransactionalPipeline.h"
#include "experiment/greedy_transactional_lod/TransactionalReservation.h"
#include "experiment/greedy_transactional_lod/TransactionalCommit.h"

#include <stdexcept>

namespace ParallelRoam::Experiment::GreedyTransactionalLod
{
TransactionalPipeline::TransactionalPipeline(const InitialMesh& input) : _state(input),_samples(input.Source) {}

CertifiedBatch TransactionalPipeline::Update(WorkLedger& work)
{
    if (_updated) throw std::runtime_error("单批入口尚不提供正常局部样本续接");
    // 尚未实现的续接显式拒绝，不能悄悄全量刷新来伪装持续路径
    const auto start=std::chrono::steady_clock::now();
    _samples.Refresh(_state,work);
    work.Seconds["refresh"]=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    auto batch=TransactionalReservation::Plan(_state,_samples,work);
    TransactionalCommit::Apply(_state,batch,work);_updated=true;
    // 诊断验证和文件输出由调用者另计，发现与局部发布都留在本窗口
    work.Seconds["update"]=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    return batch;
}
}
