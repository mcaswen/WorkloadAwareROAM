#include "algorithms/greedy_transactional_lod/TransactionalCertification.h"
#include "algorithms/greedy_transactional_lod/TransactionalCommit.h"
#include "algorithms/greedy_transactional_lod/TransactionalProposals.h"
#include "algorithms/greedy_transactional_lod/TransactionalReservation.h"
#include "experiment/greedy_transactional_lod/TransactionalValidation.h"
#include "algorithms/greedy_transactional_lod/TransactionalPredicates.h"
#include "algorithms/greedy_transactional_lod/TransactionalPipeline.h"
#include "experiment/greedy_transactional_lod/TransactionalDynamicReference.h"
#include "experiment/roam_materialization/MaterializationExecutor.h"

#include <algorithm>
#include <atomic>
#include <barrier>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace ParallelRoam::Experiment::GreedyTransactionalLod;
using namespace ParallelRoam::Algorithms::GreedyTransactionalLod;
void Require(bool condition,const char* message) { if (!condition) throw std::runtime_error(message); }
template<class Action> void Throws(Action&& action)
{
    bool failed=false;try { action(); } catch (const std::exception&) { failed=true; }
    Require(failed,"预期拒绝没有发生");
}

InitialMesh Square()
{
    InitialMesh input;input.Config.Budget=8;
    input.Vertices={{0,{0,0,0}},{1,{1,0,0}},{2,{1,1,0}},{3,{0,1,0}}};
    input.Faces={{0,{0,1,2}},{1,{0,2,3}}};
    input.Source={3,3,{0,0,0,0,65535,0,0,0,0}};
    return input;
}

void SamplesAndCertification()
{
    const auto input=Square();TransactionalState state(input);TransactionalValidation::Validate(state);
    TransactionalSamples samples(input.Source);WorkLedger work;samples.Refresh(state,work);
    Require(samples.SampleCount()==33 && work.SampleContributions==38,"公共采样或闭面贡献不符");
    for (Slot sid=0;sid<33;++sid)
    {
        const auto xy=samples.Decode(sid);
        Require(samples.Value(sid).Owner==(xy[1]<=xy[0] ? 0U : 1U),"共享边唯一 owner 错误");
        Require((std::find(samples.FaceSamples(0).begin(),samples.FaceSamples(0).end(),sid)!=samples.FaceSamples(0).end())==(xy[1]<=xy[0]),"闭边样本遗漏");
    }
    Require(TransactionalCertification::ExactErrorSquared(state,samples,4)==2500,"正交误差解析值不符");
    auto perspective=input;perspective.Config.Matrix[13]=.25;TransactionalState projected(perspective);
    Require(TransactionalCertification::ExactErrorSquared(projected,samples,4)==1600,"透视误差解析值不符");
    auto candidates=TransactionalProposals::Receivers(state,samples,0);
    Require(candidates.front().Kind=='E' && candidates.front().Faces.size()==4,"边中点原语未保持两个面到四个面");
    ReceiverCursor cursor(state,samples,0);WorkLedger constructed;
    for (const auto& expected : candidates)
    {
        auto next=cursor.Next(&constructed);Require(next.has_value(),"惰性目录提前结束");
        Require(next->NewVertex==expected.NewVertex && next->Kind==expected.Kind &&
            next->Support==expected.Support && next->Faces==expected.Faces && next->Samples==expected.Samples,
            "目录命名、连接或样本顺序变化");
    }
    Require(!cursor.Next() && constructed.ReceiverConstructed==candidates.size(),"目录尾部重复构造");
    const auto reason=TransactionalCertification::Fit(state,samples,candidates.front(),work);
    Require(reason=="certified","解析接收提案无法认证");
    auto proposal=candidates.front();proposal.Reason=reason;
    CertifiedBatch batch;batch.Version=state.Version();batch.Exchanges.push_back({proposal,{},false});
    TransactionalState before(input);TransactionalCommit::Apply(state,batch,work);TransactionalValidation::Validate(state);
    Require(state.FaceCount()==4 && state.Version()==2,"单批发布或预算结果错误");
    Throws([&] { TransactionalCommit::Apply(state,batch,work); });
    Require(state.FaceCount()==4 && state.Version()==2,"过期批次修改了状态");
    auto duplicate=batch;duplicate.Exchanges.push_back(batch.Exchanges.front());
    Throws([&] { TransactionalCommit::Apply(before,duplicate,work); });
    TransactionalState original(input);Require(TransactionalValidation::Equivalent(before,original),"冲突拒绝后状态改变");
    auto small=input;small.Config.Budget=2;TransactionalState budget(small);
    Throws([&] { TransactionalCommit::Apply(budget,batch,work); });
    Require(budget.FaceCount()==2 && budget.Version()==1,"预算拒绝修改了状态");
    Require(TransactionalReservation::Conflict(TransactionalReservation::Footprint(original,proposal),
        TransactionalReservation::Footprint(original,proposal)),"相同补丁没有冲突");
}

void ReclamationAndPredicates()
{
    auto input=Square();input.Vertices.clear();input.Faces.clear();input.Source.Values.assign(9,0);
    for (Identity y=0;y<3;++y) for (Identity x=0;x<3;++x)
        input.Vertices.push_back({y*3+x,{static_cast<double>(x)*.5,static_cast<double>(y)*.5,0}});
    for (Identity y=0;y<2;++y) for (Identity x=0;x<2;++x)
    {
        const auto a=y*3+x,b=a+1,d=a+3,c=d+1;
        input.Faces.push_back({static_cast<Identity>(input.Faces.size()),{a,b,c}});
        input.Faces.push_back({static_cast<Identity>(input.Faces.size()),{a,c,d}});
    }
    TransactionalState state(input);TransactionalValidation::Validate(state);
    TransactionalSamples samples(input.Source);WorkLedger work;samples.Refresh(state,work);
    const auto donor=TransactionalProposals::Donor(state,samples,4,work);
    Require(donor.Reason=="certified" && donor.Support.size()==6 && donor.Faces.size()==4,"六价回收没有固定释放两个面");
    Require(TransactionalCertification::Accepts(state,samples,donor,0,work),"平面回收没有精确零误差");
    const Point a{0,0,0},b{1,0,0},c{0,1,0};
    Require(TransactionalPredicates::Orientation(a,b,c)>0 && TransactionalPredicates::Shape(a,b,c),"基本方向/角度错误");
    Require(TransactionalPredicates::Orientation(a,b,{.5,0,0})==0,"共线回退错误");
    Require(!TransactionalPredicates::Shape(a,b,{.5,std::nextafter(0.0,1.0),0}),"极薄面误认证");
    Require(TransactionalPredicates::Contains({1.0/3,1.0/3,0},a,b,c),"非 dyadic 内点遗漏");
    TransactionalSamples sixths({2,2,{0,0,0,0}});
    for (Slot sid=0;sid<sixths.SampleCount();++sid)
    {
        const auto xy=sixths.Decode(sid);
        if (xy[0]==4 && xy[1]==2)
            Require(!sixths.StrictlyInside(sid,a,b,c),"舍入把精确边界样本变成了面内请求");
    }
    WorkLedger expired;expired.Deadline=std::chrono::steady_clock::time_point::min();
    Throws([&] { expired.CheckLimit(); });
}

void PersistentStateAndConsumption()
{
    auto input=Square();TransactionalPipeline pipeline(input);WorkLedger initialize;pipeline.Initialize(initialize);
    ParallelRoam::Terrain::TerrainMeshData mirror;TransactionalValidation::Consume(pipeline.ConsumeMesh(),mirror);
    auto batch=TransactionalReservation::Plan(pipeline.State(),pipeline.Samples(),initialize);
    Require(!batch.Exchanges.empty(),"持续夹具没有事务");
    TransactionalExecution audit;audit.Diagnostics=true;WorkLedger auditWork;
    const auto audited=TransactionalReservation::Plan(pipeline.State(),pipeline.Samples(),auditWork,audit);
    Require(audited.PairAuditComplete && !batch.PairAuditComplete && audited.Feasible==batch.Feasible &&
        audited.Exchanges.size()==batch.Exchanges.size(),"诊断改变了可行分母或事务数量");
    TransactionalState auditState(input);TransactionalCommit::Apply(auditState,audited,auditWork);
    TransactionalState normalState(input);TransactionalCommit::Apply(normalState,batch,auditWork);
    Require(TransactionalValidation::Equivalent(auditState,normalState),"诊断改变了下一状态");
    const auto originalVersion=pipeline.State().Version();
    WorkLedger expired;expired.Deadline=std::chrono::steady_clock::time_point::min();
    Throws([&] { pipeline.Apply(batch,expired); });
    Require(pipeline.State().Version()==originalVersion,"准备失败推进了代际");
    TransactionalValidation::Samples(pipeline.State(),pipeline.Samples(),initialize);
    auto empty=pipeline.ConsumeMesh();Require(empty.Vertices.empty() && empty.Indices.empty(),"失败制造了 Pending");
    for (int round=0;round<3;++round)
    {
        WorkLedger work;const auto count=pipeline.State().FaceCount();const auto update=pipeline.Update(work);
        Require(update.AssignedCredits==std::min((input.Config.Budget-count)/2,update.Receivers),"跨批次额度没有重新命名");
        TransactionalValidation::Validate(pipeline.State());
        TransactionalValidation::Samples(pipeline.State(),pipeline.Samples(),initialize);
        TransactionalValidation::Mesh(pipeline.State(),pipeline.Mesh());
        if (round==1 || round==2) TransactionalValidation::Consume(pipeline.ConsumeMesh(),mirror);
    }
    empty=pipeline.ConsumeMesh();Require(empty.Vertices.empty() && empty.Indices.empty(),"重复消费没有清空 Pending");
    // 先完成所有局部准备再模拟配额中止，旧 live 内容仍须可供下一批继续
    {
        TransactionalState independent(input);TransactionalSamples samples(input.Source);samples.Refresh(independent,initialize);
        TransactionalMesh mesh;mesh.Initialize(independent,initialize);
        auto prepared=TransactionalCommit::Prepare(independent,batch,initialize);
        auto samplePatch=samples.Prepare(independent,prepared,initialize);auto meshPatch=mesh.Prepare(independent,prepared,initialize);
        static_cast<void>(samplePatch);static_cast<void>(meshPatch);
        Throws([&] { expired.CheckLimit(); });
        Require(independent.Version()==1 && independent.FaceCount()==2,"私有准备修改了 live 状态");
        TransactionalValidation::Samples(independent,samples,initialize);TransactionalValidation::Mesh(independent,mesh.Data());
    }
    // 构造屏幕证据域外的高度改动，保护仍须覆盖整个闭补丁
    auto hidden=input;hidden.Config.Matrix[15]=-1;
    TransactionalState source(hidden);TransactionalSamples samples(hidden.Source);samples.Refresh(source,initialize);
    for (Slot sid=0;sid<samples.SampleCount();++sid)
        Require(!samples.Projection(sid).Visible,"高度保护夹具未覆盖视域外样本");
    auto receiver=TransactionalProposals::Receivers(source,samples,0).front();
    receiver.Points.at(receiver.NewVertex).Height=100;
    Require(!TransactionalCertification::PreservesHeight(source,samples,receiver,nullptr,initialize),"全 Q 高度保护漏掉退化");
}

void DynamicEvidenceAndView()
{
    auto input=Square();input.Vertices.clear();input.Faces.clear();input.Source={9,9,{}};
    input.Config.Budget=128;input.Config.SplitPixels=1;
    for (Identity y=0;y<9;++y) for (Identity x=0;x<9;++x)
    {
        input.Vertices.push_back({y*9+x,{static_cast<double>(x)/8,static_cast<double>(y)/8,0}});
        input.Source.Values.push_back(x%3==1 && y%3==1 ? 65535 : 0);
    }
    for (Identity y=0;y<8;++y) for (Identity x=0;x<8;++x)
    {
        const auto a=y*9+x,b=a+1,c=a+10,d=a+9;
        input.Faces.push_back({static_cast<Identity>(input.Faces.size()),{a,b,c}});
        input.Faces.push_back({static_cast<Identity>(input.Faces.size()),{a,c,d}});
    }
    TransactionalDynamicReference cached(input),fresh(input);WorkLedger a,b;
    cached.Pipeline().Initialize(a);fresh.Pipeline().Initialize(b);
    a=WorkLedger{};b=WorkLedger{};
    const auto first=cached.Update(a),second=fresh.Update(b,false);
    Require(first.Decisions==second.Decisions && first.Stop==second.Stop,"动态证据缓存改变了决策轨迹");
    Require(TransactionalValidation::Equivalent(cached.Pipeline().State(),fresh.Pipeline().State()),"动态缓存改变端点");
    Require(a.ReceiverCacheHits+a.DonorCacheHits>0,"动态夹具没有实际命中证据缓存");
    WorkLedger check;TransactionalValidation::Validate(cached.Pipeline().State());
    TransactionalValidation::Samples(cached.Pipeline().State(),cached.Pipeline().Samples(),check);
    auto oldBatch=TransactionalReservation::Plan(cached.Pipeline().State(),cached.Pipeline().Samples(),check);
    const auto version=cached.Pipeline().State().Version();auto view=input.Config;view.Matrix[0]=.9;view.SampleIndex=15;
    std::vector<Slot> owners;
    for (Slot sid=0;sid<cached.Pipeline().Samples().SampleCount();++sid)
        owners.push_back(cached.Pipeline().Samples().Geometry(sid).Owner);
    cached.Pipeline().SetView(view,check);
    Require(cached.Pipeline().State().Version()==version+1,"视图刷新没有失效旧批次");
    Throws([&] { cached.Pipeline().Apply(oldBatch,check); });
    for (Slot sid=0;sid<owners.size();++sid) Require(cached.Pipeline().Samples().Value(sid).Owner==owners[sid],"换相机重新分配了 owner");
    TransactionalValidation::Samples(cached.Pipeline().State(),cached.Pipeline().Samples(),check);
    TransactionalValidation::Mesh(cached.Pipeline().State(),cached.Pipeline().Mesh());
}

void ParallelExecutionAndFailure()
{
    ParallelRoam::Experiment::RoamMaterialization::MaterializationExecutor pool(4);
    const auto adapter=pool.Execution();
    TransactionalExecution execution{4,true,adapter.Dispatch};WorkLedger evidence;
    std::barrier rendezvous(4);
    execution.Run("actual_threads",4,evidence,[&](auto,auto,WorkLedger& local) {
        rendezvous.arrive_and_wait();++local.Proposals;
    });
    Require(evidence.Execution.at("actual_threads")[2]==4 && evidence.Proposals==4,"没有四个真实线程或局部计数归并错误");
    std::atomic<unsigned> drained{};
    Throws([&] {
        execution.Run("failure",4,evidence,[&](auto first,auto,WorkLedger&) {
            ++drained;rendezvous.arrive_and_wait();
            if (first==0) throw std::runtime_error("私有任务失败");
        });
    });
    Require(drained==4,"异常返回时尚有任务未排空");
    const auto input=Square();
    Throws([&] { TransactionalPipeline invalid(input,{4,false,{}}); });
    TransactionalPipeline serial(input),parallel(input,execution);WorkLedger a,b,check;
    serial.Initialize(a);parallel.Initialize(b);
    for (int round=0;round<3;++round)
    {
        a=WorkLedger{};b=WorkLedger{};
        const auto first=serial.Update(a),second=parallel.Update(b);
        Require(first.IntentIds==second.IntentIds && first.Attempts==second.Attempts &&
            first.Exchanges.size()==second.Exchanges.size(),"分块改变了候选证据或批次");
        Require(TransactionalValidation::Equivalent(serial.State(),parallel.State()),"分块改变最终状态");
        Require(a.SampleTouches==b.SampleTouches && a.ExactChecks==b.ExactChecks,"分块改变认证工作量");
        TransactionalValidation::Samples(parallel.State(),parallel.Samples(),check);
        TransactionalValidation::Mesh(parallel.State(),parallel.Mesh());
    }
    auto view=input.Config;view.Matrix[0]=.9;
    serial.SetView(view,a);parallel.SetView(view,b);
    Require(serial.Samples().Raw()==parallel.Samples().Raw(),"并行评分改变全局次序");
    TransactionalValidation::Samples(parallel.State(),parallel.Samples(),check);
    // 在真实线程填写目标面后模拟派发层失败，旧输出和代际仍可继续使用
    auto failing=execution;failing.Dispatch=[&](auto count,const auto& task) {
        adapter.Dispatch(count,task);throw std::runtime_error("排空后拒绝");
    };
    TransactionalPipeline unchanged(input,failing);unchanged.Initialize(check);
    const auto old=unchanged.State().Version();const auto batch=TransactionalReservation::Plan(unchanged.State(),unchanged.Samples(),check);
    ParallelRoam::Terrain::TerrainMeshData mirror;
    TransactionalValidation::Consume(unchanged.ConsumeMesh(),mirror);
    Throws([&] { unchanged.Apply(batch,check); });
    Require(unchanged.State().Version()==old,"并行准备失败污染 live 代际");
    TransactionalValidation::Samples(unchanged.State(),unchanged.Samples(),check);
    TransactionalValidation::Mesh(unchanged.State(),unchanged.Mesh());
    const auto empty=unchanged.ConsumeMesh();
    Require(empty.Vertices.empty() && empty.Indices.empty(),"并行失败污染 Pending");

    // 投影任务已经写满备用数组后失败，当前视图必须仍可读且能继续更新
    bool failView=true;auto recoverable=execution;
    recoverable.Dispatch=[&](auto count,const auto& task) {
        adapter.Dispatch(count,task);
        if (failView) { failView=false;throw std::runtime_error("投影准备后拒绝"); }
    };
    TransactionalPipeline views(input,recoverable);views.Initialize(check);
    const auto initialView=views.State().Version();
    const auto* initialBuffer=&views.Samples().Projection(0);
    WorkLedger viewWork;auto camera=input.Config;camera.Matrix[0]=.8;
    Throws([&] { views.SetView(camera,viewWork); });
    Require(views.State().Version()==initialView && &views.Samples().Projection(0)==initialBuffer,
        "失败发布了备用投影或视图代际");
    TransactionalValidation::Samples(views.State(),views.Samples(),check);
    views.SetView(camera,viewWork);
    Require(&views.Samples().Projection(0)!=initialBuffer,"成功视图仍在逐样本复制发布");
    views.Update(check);TransactionalValidation::Validate(views.State());
    camera.Matrix[0]=.7;views.SetView(camera,viewWork);
    Require(&views.Samples().Projection(0)==initialBuffer && viewWork.ViewBufferAllocations==1,
        "视图数组未复用或再次分配");
    TransactionalValidation::Samples(views.State(),views.Samples(),check);
    TransactionalValidation::Mesh(views.State(),views.Mesh());
}
}

int main()
{
    try { SamplesAndCertification();ReclamationAndPredicates();PersistentStateAndConsumption();DynamicEvidenceAndView();ParallelExecutionAndFailure();
        std::cout<<"数值、持续状态、动态证据缓存与视图验证完成\n"; }
    catch (const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
