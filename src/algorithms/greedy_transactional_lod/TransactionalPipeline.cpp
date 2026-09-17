#include "algorithms/greedy_transactional_lod/TransactionalPipeline.h"
#include "algorithms/greedy_transactional_lod/TransactionalReservation.h"
#include "algorithms/greedy_transactional_lod/TransactionalCommit.h"
#include "algorithms/greedy_transactional_lod/TransactionalPointwiseQuality.h"
#include "profiling/CpuProfiling.h"

#include <stdexcept>
#include <cmath>

namespace ParallelRoam::Algorithms::GreedyTransactionalLod
{
TransactionalPipeline::TransactionalPipeline(const InitialMesh& input,TransactionalExecution execution)
    : _execution(std::move(execution)),_state(input),_samples(input.Source)
{
    if (!_execution.Workers || (_execution.Workers>1 && !_execution.Dispatch))
        throw std::runtime_error("持续状态执行配置缺少同步派发");
}

TransactionalPipeline::TransactionalPipeline(InitialMesh&& input,TransactionalExecution execution)
    : _execution(std::move(execution)),_state(input),_samples(std::move(input.Source))
{
    if (!_execution.Workers || (_execution.Workers>1 && !_execution.Dispatch))
        throw std::runtime_error("持续状态执行配置缺少同步派发");
}

void TransactionalPipeline::Initialize(WorkLedger& work)
{
    ROAM_CPU_ZONE("gtp.initialize");
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
    ROAM_CPU_ZONE("gtp.apply");
    if (!_initialized) throw std::runtime_error("持续状态尚未初始化");
    if (batch.Version!=_state.Version()) throw std::runtime_error("批次快照已过期");
    if (batch.Exchanges.empty()) return;
    TransactionalPointwiseQuality::ValidateBatch(_state, _samples, batch, work);
    // 拓扑、样本和 mesh 共享同一个目标代际，发布之间不插入失败点
    auto topology=TransactionalCommit::Prepare(_state,batch,work,_execution,&_samples.Source());
    auto start=std::chrono::steady_clock::now();
    auto samples=_samples.Prepare(_state,topology,work);
    work.Seconds["sample_repair"]+=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    start=std::chrono::steady_clock::now();auto mesh=_mesh.Prepare(_state,topology,work,_execution);
    work.Seconds["mesh_prepare"]+=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    work.Seconds.try_emplace("derived_publish",0);work.CheckLimit();
    // 全部潜在分配及失败点已越过，旧 live 值到此仍未发生变化
    TransactionalCommit::Publish(_state,std::move(topology),work);
    start=std::chrono::steady_clock::now();
    _invalidatedRoots=std::move(samples.InvalidatedRoots);_invalidatedDonors=std::move(samples.InvalidatedDonors);
    _samples.Publish(std::move(samples));_mesh.Publish(std::move(mesh),_state.Version());
    work.Seconds.at("derived_publish")+=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
}

void TransactionalPipeline::SetView(const Configuration& view,WorkLedger& work)
{
    ROAM_CPU_ZONE("gtp.set_view");
    Initialize(work);const auto& old=_state.Config();
    if (view.Scenario!=old.Scenario || view.PrefixLimit!=old.PrefixLimit || view.DonorLimit!=old.DonorLimit ||
        view.Budget!=old.Budget || view.TerrainSize!=old.TerrainSize || view.HeightScale!=old.HeightScale ||
        view.SplitPixels!=old.SplitPixels || view.HeightGuard!=old.HeightGuard || view.SampleVisitLimit!=old.SampleVisitLimit ||
        view.PreserveSurvivingHeights!=old.PreserveSurvivingHeights ||
        view.EnableFlipRecovery!=old.EnableFlipRecovery ||
        view.EnableBoundaryRefinement!=old.EnableBoundaryRefinement ||
        view.ReceiverOrder != old.ReceiverOrder ||
        view.QualityPolicy != old.QualityPolicy ||
        view.QualityTargetPixels != old.QualityTargetPixels ||
        view.QualityHeightRatio != old.QualityHeightRatio ||
        !view.Width || !view.Height ||
        !std::all_of(view.Matrix.begin(),view.Matrix.end(),[](double value) { return std::isfinite(value); }))
        throw std::runtime_error("视图刷新不能改变几何、预算或质量规则");
    if (view.Matrix==old.Matrix && view.Width==old.Width && view.Height==old.Height &&
        view.UsesZeroToOneDepth==old.UsesZeroToOneDepth) return;
    // 新视图的数值域检查和投影结果完成后，才共同替换配置与派生状态
    const auto started=std::chrono::steady_clock::now();
    auto prepared=_samples.PrepareView(_state,view,work,_execution);work.CheckLimit();work.Seconds.try_emplace("view_refresh",0);
    _state._config.Matrix=view.Matrix;_state._config.Width=view.Width;_state._config.Height=view.Height;
    _state._config.UsesZeroToOneDepth=view.UsesZeroToOneDepth;
    _state._config.SampleIndex=view.SampleIndex;++_state._version;
    _samples.PublishView(std::move(prepared));_mesh.AdvanceGeneration(_state.Version());
    work.Seconds.at("view_refresh")+=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
}

CertifiedBatch TransactionalPipeline::Update(WorkLedger& work)
{
    ROAM_CPU_ZONE("gtp.update");
    const auto start=std::chrono::steady_clock::now();Initialize(work);
    work.Seconds.try_emplace("update",0);
    auto batch=TransactionalReservation::Plan(_state,_samples,work,_execution);
    Apply(batch,work);
    work.Seconds.at("update")=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    return batch;
}
}
