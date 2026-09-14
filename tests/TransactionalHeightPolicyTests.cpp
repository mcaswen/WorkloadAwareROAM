#include "algorithms/greedy_transactional_lod/TransactionalProposals.h"
#include "algorithms/greedy_transactional_lod/TransactionalCertification.h"
#include "algorithms/greedy_transactional_lod/TransactionalSeedBuilder.h"
#include "algorithms/greedy_transactional_lod/TransactionalPipeline.h"
#include "algorithms/greedy_transactional_lod/TransactionalStateInvariant.h"
#include <iostream>

namespace
{
using namespace ParallelRoam::Algorithms;
using namespace GreedyTransactionalLod;
void Require(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }

/// <summary>
/// 中心峰参考与平面旧网格提供真实可接受细分，不靠伪造证书提交
/// 四角的存活高度必须跨新点插入保持原值
/// </summary>
InitialMesh Square()
{
    InitialMesh input;input.Config.Budget=8;input.Config.PreserveSurvivingHeights=true;
    input.Vertices={{0,{0,0,0}},{1,{1,0,0}},{2,{1,1,0}},{3,{0,1,0}}};
    input.Faces={{0,{0,1,2}},{1,{0,2,3}}};input.Source={3,3,{0,0,0,0,65535,0,0,0,0}};
    return input;
}

void Directory()
{
    auto input=Square();input.Config.Budget=16;input.Vertices.clear();input.Faces.clear();
    for (Identity y=0;y<3;++y) for (Identity x=0;x<3;++x)
        input.Vertices.push_back({y*3+x,{static_cast<double>(x)*.5,static_cast<double>(y)*.5,0}});
    for (Identity y=0;y<2;++y) for (Identity x=0;x<2;++x)
    {
        const auto a=y*3+x,b=a+1,c=a+4,d=a+3;
        input.Faces.push_back({static_cast<Identity>(input.Faces.size()),{a,b,c}});
        input.Faces.push_back({static_cast<Identity>(input.Faces.size()),{a,c,d}});
    }
    TransactionalState fixed(input);input.Config.PreserveSurvivingHeights=false;TransactionalState old(input);
    TransactionalSamples samples(input.Source);WorkLedger work;samples.Refresh(old,work);
    const auto normal=TransactionalProposals::Receivers(old,samples,0);
    auto frozen=TransactionalProposals::Receivers(fixed,samples,0);bool sawH=false;
    Require(normal.size()==frozen.size(),"高度策略改变了目录数量");
    for (std::size_t i=0;i<normal.size();++i)
    {
        const auto& a=normal[i];auto& b=frozen[i];
        Require(a.Kind==b.Kind && a.NewVertex==b.NewVertex && a.Support==b.Support &&
            a.Faces==b.Faces && a.Points==b.Points && a.Samples==b.Samples,"冻结策略改变了目录身份或输入几何");
        if (a.Kind=='H')
        {
            sawH=true;Require(a.Free.size()==2 && b.Free==std::vector<Identity>{b.NewVertex},"H 自由变量没有收紧");
        }
        static_cast<void>(TransactionalCertification::Fit(fixed,samples,b,work));
        // 失败与成功拟合都不能修改任何已有点；新点不是强制参考采样
        for (const auto& [id,p] : b.Points)
            if (id!=b.NewVertex) Require(p==fixed.Vertex(id).Geometry,"拟合修改了旧点");
    }
    Require(sawH,"夹具没有覆盖 H 目录");
}

void Persistent()
{
    auto input=Square();TransactionalPipeline pipeline(input);WorkLedger work;pipeline.Initialize(work);
    auto batch=pipeline.Update(work);Require(!batch.Exchanges.empty(),"冻结策略未执行真实细分");
    TransactionalStateInvariant::Validate(pipeline.State());
    for (const auto& [id,p] : input.Vertices) Require(pipeline.State().Vertex(id).Geometry==p,"发布修改存活点");
    static_cast<void>(pipeline.ConsumeMesh());
    // 策略是初建身份，不能在一个持续状态上静默改变
    auto changed=input.Config;changed.PreserveSurvivingHeights=false;bool rejected=false;
    try { pipeline.SetView(changed,work); } catch (const std::runtime_error&) { rejected=true; }
    Require(rejected,"中途改变高度策略未拒绝");
    TerrainLodBuildInput publicInput;publicInput.Settings.Transactional.PreserveSurvivingHeights=true;
    Require(TransactionalSeedBuilder::ConfigurationFor(publicInput).PreserveSurvivingHeights,"公共配置未传入核心");
    Require(!TransactionalLodSettings{}.PreserveSurvivingHeights && !Configuration{}.PreserveSurvivingHeights,"默认算法被替换");
}
}

int main()
{
    try { Directory();Persistent();std::cout<<"高度自由度、目录身份和持续状态检查完成\n";return 0; }
    catch (const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
