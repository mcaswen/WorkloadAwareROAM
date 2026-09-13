#include "experiment/greedy_transactional_lod/TransactionalPipeline.h"
#include "experiment/greedy_transactional_lod/TransactionalReservation.h"
#include "experiment/greedy_transactional_lod/TransactionalCommit.h"

#include <stdexcept>

namespace ParallelRoam::Experiment::GreedyTransactionalLod
{
TransactionalPipeline::TransactionalPipeline(const InitialMesh& input) : _state(input),_samples(input.Source) {}

void TransactionalPipeline::Initialize(WorkLedger& work)
{
    if (_initialized) return;
    // 首次全 Q 与完整 mesh 初建另列，后续更新不能重新走这个入口
    const auto start=std::chrono::steady_clock::now();
    _samples.Refresh(_state,work);
    work.Seconds["sample_initialize"]=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    const auto mesh=std::chrono::steady_clock::now();_mesh.Initialize(_state,work);
    work.Seconds["mesh_initialize"]=std::chrono::duration<double>(std::chrono::steady_clock::now()-mesh).count();
    _initialized=true;
}

void TransactionalPipeline::Apply(const CertifiedBatch& batch,WorkLedger& work)
{
    if (!_initialized) throw std::runtime_error("持续状态尚未初始化");
    if (batch.Version!=_state.Version()) throw std::runtime_error("批次快照已过期");
    if (batch.Exchanges.empty()) return;
    // 拓扑、样本和 mesh 共享同一个目标代际，发布之间不插入失败点
    auto topology=TransactionalCommit::Prepare(_state,batch,work);
    auto start=std::chrono::steady_clock::now();
    auto samples=_samples.Prepare(_state,topology,work);
    work.Seconds["sample_repair"]+=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    start=std::chrono::steady_clock::now();auto mesh=_mesh.Prepare(_state,topology,work);
    work.Seconds["mesh_prepare"]+=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    work.Seconds.try_emplace("derived_publish",0);work.CheckLimit();
    // 全部潜在分配及失败点已越过，旧 live 值到此仍未发生变化
    TransactionalCommit::Publish(_state,std::move(topology),work);
    start=std::chrono::steady_clock::now();_samples.Publish(std::move(samples));_mesh.Publish(std::move(mesh),_state.Version());
    work.Seconds.at("derived_publish")+=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
}

CertifiedBatch TransactionalPipeline::Update(WorkLedger& work)
{
    const auto start=std::chrono::steady_clock::now();Initialize(work);
    work.Seconds.try_emplace("update",0);
    auto batch=TransactionalReservation::Plan(_state,_samples,work);
    Apply(batch,work);
    work.Seconds.at("update")=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    return batch;
}
}
