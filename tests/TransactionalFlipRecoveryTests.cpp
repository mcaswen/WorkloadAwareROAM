#include "algorithms/greedy_transactional_lod/TransactionalFlipRecovery.h"
#include "algorithms/greedy_transactional_lod/TransactionalCertification.h"
#include "algorithms/greedy_transactional_lod/TransactionalPipeline.h"
#include "algorithms/greedy_transactional_lod/TransactionalProposals.h"
#include "algorithms/greedy_transactional_lod/TransactionalReservation.h"
#include "algorithms/greedy_transactional_lod/TransactionalSeedBuilder.h"
#include "algorithms/greedy_transactional_lod/TransactionalTerrainLodAlgorithm.h"
#include "algorithms/TerrainLodView.h"
#include "experiment/greedy_transactional_lod/TransactionalValidation.h"
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <iostream>

namespace
{
using namespace ParallelRoam::Algorithms::GreedyTransactionalLod;
using ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalValidation;
void Require(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
template<class Action> void Throws(Action&& action)
{
    bool failed=false;try { action(); } catch (const std::exception&) { failed=true; }
    Require(failed,"应拒绝的输入被接受");
}

/// <summary>
/// 旧对角线高估中心，参考样本来自另一条对角线的折面
/// 翻边改善来自连接本身，四个角的几何在整个测试中保持不变
/// </summary>
InitialMesh Square()
{
    InitialMesh input;input.Config.Budget=2;input.Config.PreserveSurvivingHeights=true;input.Config.EnableFlipRecovery=true;
    input.Vertices={{0,{0,0,0}},{1,{1,0,0}},{2,{1,1,1}},{3,{0,1,0}}};
    input.Faces={{0,{0,1,2}},{1,{0,2,3}}};input.Source={3,3,{0,0,0,0,0,32768,0,32768,65535}};
    return input;
}
Proposal CertifiedFlip(const TransactionalPipeline& pipeline,Slot root,WorkLedger& work)
{
    std::vector<std::pair<char,std::string>> attempts;
    auto p=TransactionalFlipRecovery::FirstCertified(pipeline.State(),pipeline.Samples(),root,work,attempts);
    Require(p.has_value(),"解析翻边未通过真实进展认证");return *p;
}
CertifiedBatch Batch(const TransactionalPipeline& pipeline,Proposal p)
{
    CertifiedBatch b;b.Version=pipeline.State().Version();b.FlipExecuted=1;
    b.Exchanges.push_back({std::move(p),{},false,ExchangeKind::ConnectivityRepair});return b;
}

void GeometryAndBudget()
{
    const auto input=Square();TransactionalPipeline pipeline(input);WorkLedger work;pipeline.Initialize(work);
    const auto p=CertifiedFlip(pipeline,0,work);const auto batch=Batch(pipeline,p);
    Require(p.Free.empty() && p.Points.size()==4 && p.Faces.size()==2 && p.Support.size()==2,"净零几何改变点人口");
    Require(TransactionalFlipRecovery::Construct(pipeline.State(),0,EdgeKey(0,1)).Reason=="flip_not_interior","边界被翻转");
    Require(TransactionalFlipRecovery::IsUnchangedGeometryFlip(pipeline.State(),p),"合法结构证书被拒绝");
    for (int mutation=0;mutation<6;++mutation)
    {
        auto invalid=batch;auto& e=invalid.Exchanges[0];
        if (mutation==0) e.HasDonor=true;
        if (mutation==1) e.Kind=ExchangeKind::Refinement;
        if (mutation==2) e.Receiver.Points.begin()->second.Height+=.1;
        if (mutation==3) e.Receiver.Free.push_back(0);
        if (mutation==4) e.Receiver.Faces[0]=input.Faces[0].Vertices;
        if (mutation==5) e.Receiver.Reason="uncertified";
        Throws([&] { pipeline.Apply(invalid,work); });
        Require(pipeline.State().Version()==1 && pipeline.State().FaceCount()==2,"错误证书污染状态");
    }
    auto duplicate=batch;duplicate.Exchanges.push_back(batch.Exchanges.front());
    Throws([&] { pipeline.Apply(duplicate,work); });
    // 类型不代替硬预算：即使净零合法，普通净+2提案仍不能借同一额度发布
    auto receiver=TransactionalProposals::Receivers(pipeline.State(),pipeline.Samples(),0).front();receiver.Reason="certified";
    auto over=batch;over.Exchanges={{receiver,{},false}};
    Throws([&] { pipeline.Apply(over,work); });
    const auto nextVertex=pipeline.State().NextVertexId();pipeline.Apply(batch,work);
    Require(pipeline.State().FaceCount()==2 && pipeline.State().NextVertexId()==nextVertex,"满预算翻边改变了预算或点身份");
    TransactionalValidation::Validate(pipeline.State());TransactionalValidation::Samples(pipeline.State(),pipeline.Samples(),work);
    TransactionalValidation::Mesh(pipeline.State(),pipeline.Mesh());
    for (const auto& [id,point] : input.Vertices) Require(pipeline.State().Vertex(id).Geometry==point,"翻边修改旧点");
    Throws([&] { pipeline.Apply(batch,work); });
    auto reverse=TransactionalFlipRecovery::Construct(pipeline.State(),pipeline.State().ActiveFaces().front(),EdgeKey(1,3));
    Require(reverse.Reason.empty(),"反向几何不存在，未覆盖质量防翻回");
    reverse.Samples=pipeline.Samples().VisibleSupport(reverse.Support);
    const auto target=TransactionalCertification::SetProgressTarget(pipeline.State(),pipeline.Samples(),reverse,work);
    const bool measure=TransactionalCertification::Measure(pipeline.State(),pipeline.Samples(),reverse,work);
    Require(!target.empty() || !measure || !TransactionalCertification::Accepts(pipeline.State(),pipeline.Samples(),reverse,reverse.TargetMicropixels,work),
        "固定视图直接翻回仍被进展规则接受");
}

/// <summary>
/// 两个对角单元共享只读中心点，边写集合不交；独立准备后必须合成邻接
/// </summary>
InitialMesh Grid()
{
    auto input=Square();input.Config.Budget=10;input.Vertices.clear();input.Faces.clear();input.Source={5,5,{}};
    const auto height=[](double u,double v) { return std::max(u+v-1.0,0.0); };
    for (Identity y=0;y<3;++y) for (Identity x=0;x<3;++x)
        input.Vertices.push_back({y*3+x,{x*.5,y*.5,((x==1 && y==1) || (x==2 && y==2)) ? 1.0 : 0.0}});
    for (Identity y=0;y<2;++y) for (Identity x=0;x<2;++x)
    {
        const auto a=y*3+x,b=a+1,c=a+4,d=a+3;
        input.Faces.push_back({static_cast<Identity>(input.Faces.size()),{a,b,c}});
        input.Faces.push_back({static_cast<Identity>(input.Faces.size()),{a,c,d}});
    }
    for (unsigned y=0;y<5;++y) for (unsigned x=0;x<5;++x)
        input.Source.Values.push_back(static_cast<std::uint16_t>(height(x*.25,y*.25)*65535));
    return input;
}

void ContinuationAndComposition()
{
    auto input=Grid();TransactionalPipeline a(input),b(input);WorkLedger work;a.Initialize(work);b.Initialize(work);
    auto p=TransactionalFlipRecovery::Construct(a.State(),0,EdgeKey(0,4));
    auto q=TransactionalFlipRecovery::Construct(a.State(),6,EdgeKey(4,8));
    Require(p.Reason.empty() && q.Reason.empty(),"共享只读点夹具形状失败");
    const auto pf=TransactionalReservation::Footprint(a.State(),p),qf=TransactionalReservation::Footprint(a.State(),q);
    Require(!TransactionalReservation::Conflict(pf,qf),"只读共享点被错误当作高度写冲突");
    auto heightWrite=qf;heightWrite.Writes.emplace('h',4,0);
    Require(TransactionalReservation::Conflict(pf,heightWrite),"共享点高度依赖漏边");
    // 此处只测试提交和续接，质量单独由上面的真实认证夹具承担
    p.Reason=q.Reason="certified";auto together=Batch(a,p);together.Exchanges.push_back({q,{},false,ExchangeKind::ConnectivityRepair});
    auto reversed=together;std::reverse(reversed.Exchanges.begin(),reversed.Exchanges.end());
    ParallelRoam::Terrain::TerrainMeshData mirror;TransactionalValidation::Consume(a.ConsumeMesh(),mirror);
    static_cast<void>(b.ConsumeMesh());
    WorkLedger expired;expired.Deadline=std::chrono::steady_clock::time_point::min();
    Throws([&] { a.Apply(together,expired); });
    Require(a.ConsumeMesh().Vertices.empty() && a.State().Version()==1,"准备失败写出 Pending");
    a.Apply(together,work);b.Apply(reversed,work);
    Require(TransactionalValidation::Equivalent(a.State(),b.State()),"反序发布改变逻辑连接");
    TransactionalValidation::Samples(a.State(),a.Samples(),work);TransactionalValidation::Mesh(a.State(),a.Mesh());
    // 第二个批次发生在尚未消费的第一批之后，旧脏范围必须留存
    const auto slot=a.State().Edges().at(EdgeKey(1,3)).Faces[0];
    auto again=TransactionalFlipRecovery::Construct(a.State(),slot,EdgeKey(1,3));again.Reason="certified";
    a.Apply(Batch(a,again),work);
    const auto pending=a.ConsumeMesh();Require(!pending.Full && !pending.Vertices.empty() && !pending.Indices.empty(),"净零发布丢失增量输出");
    TransactionalValidation::Consume(pending,mirror);TransactionalValidation::Mesh(a.State(),mirror);
    TransactionalValidation::Samples(a.State(),a.Samples(),work);
    Require(a.ConsumeMesh().Vertices.empty(),"重复消费仍保留脏区间");
    auto changed=input.Config;changed.EnableFlipRecovery=false;Throws([&] { a.SetView(changed,work); });
    input.Config.PreserveSurvivingHeights=false;Throws([&] { TransactionalPipeline bad(input); });
    input.Config.PreserveSurvivingHeights=true;input.Config.HeightGuard=true;Throws([&] { TransactionalPipeline bad(input); });
    ParallelRoam::Algorithms::TerrainLodBuildInput publicInput;publicInput.Settings.Transactional.EnableFlipRecovery=true;
    Require(TransactionalSeedBuilder::ConfigurationFor(publicInput).EnableFlipRecovery,"公共开关没有传到核心");
    Require(!Configuration{}.EnableFlipRecovery && !ParallelRoam::Algorithms::TransactionalLodSettings{}.EnableFlipRecovery,"开关默认打开");
}

void MixedBatch()
{
    const auto input=Grid();TransactionalPipeline a(input),b(input);WorkLedger work;a.Initialize(work);b.Initialize(work);
    auto receiver=TransactionalProposals::Receivers(a.State(),a.Samples(),0).front();receiver.Reason="certified";
    auto repair=TransactionalFlipRecovery::Construct(a.State(),6,EdgeKey(4,8));repair.Reason="certified";
    Require(!TransactionalReservation::Conflict(TransactionalReservation::Footprint(a.State(),receiver),
        TransactionalReservation::Footprint(a.State(),repair)),"混合夹具不是独立事务");
    auto batch=Batch(a,repair);batch.Exchanges.push_back({receiver,{},false});batch.FreeExecuted=1;
    auto reverse=batch;std::reverse(reverse.Exchanges.begin(),reverse.Exchanges.end());
    a.Apply(batch,work);b.Apply(reverse,work);
    Require(a.State().FaceCount()==10 && TransactionalValidation::Equivalent(a.State(),b.State()),"混合事务的净预算或顺序不同");
    TransactionalValidation::Validate(a.State());TransactionalValidation::Samples(a.State(),a.Samples(),work);
    TransactionalValidation::Mesh(a.State(),a.Mesh());
}

void PublicLifecycle()
{
    using namespace ParallelRoam::Algorithms;
    ParallelRoam::Terrain::HeightMap source;std::string error;
    Require(source.LoadFromFile("assets/heightmaps/Hm_Terrain_Test_129.pgm",&error),error.c_str());
    TerrainLodBuildInput input;input.HeightMap=&source;
    input.Settings.TriangleBudget=64;input.Settings.MaxDepth=6;
    auto& settings=input.Settings.Transactional;settings.WorkerCount=1;settings.PrefixLimit=settings.DonorLimit=2;
    settings.PreserveSurvivingHeights=true;
    const glm::vec3 eye{15,20,27},target{0,0,0};
    input.View=BuildTerrainLodViewInput(glm::lookAtRH(eye,target,glm::vec3{0,1,0}),
        glm::perspectiveRH_NO(glm::radians(60.0F),1.0F,.1F,500.0F),eye,target-eye,128,128,false);
    TransactionalTerrainLodAlgorithm adapter;TerrainLodRenderPacket packet;
    Require(adapter.BuildRenderData(input,packet,&error),error.c_str());
    Require(adapter.Stats().Transactional->ColdStart,"初建没有报告种子");
    settings.EnableFlipRecovery=true;
    Require(adapter.BuildRenderData(input,packet,&error),error.c_str());
    Require(adapter.Stats().Transactional->ColdStart,"切换政策沿用了旧生命周期");
    Require(adapter.BuildRenderData(input,packet,&error),error.c_str());
    Require(!adapter.Stats().Transactional->ColdStart,"固定政策每轮重复初建");
    settings.EnableBoundaryRefinement = true;
    Require(adapter.BuildRenderData(input, packet, &error), error.c_str());
    Require(adapter.Stats().Transactional->ColdStart, "切换边界政策沿用了旧生命周期");
    input.View.DrawableWidth=0;Require(adapter.BuildRenderData(input,packet,&error),error.c_str());
    Require(!adapter.Stats().Transactional->Updated,"暂停状态推进了事务");
    input.View.DrawableWidth=128;adapter.Reset();Require(adapter.BuildRenderData(input,packet,&error),error.c_str());
    Require(adapter.Stats().Transactional->ColdStart,"Reset没有重建受控政策");
}
}

int main()
{
    try { GeometryAndBudget();ContinuationAndComposition();MixedBatch();PublicLifecycle();std::cout<<"翻边进展、净零预算、失败原子性和局部续接完成\n";return 0; }
    catch (const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
