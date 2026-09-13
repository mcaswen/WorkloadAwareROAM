#include "experiment/greedy_transactional_lod/TransactionalCertification.h"
#include "experiment/greedy_transactional_lod/TransactionalCommit.h"
#include "experiment/greedy_transactional_lod/TransactionalProposals.h"
#include "experiment/greedy_transactional_lod/TransactionalReservation.h"
#include "experiment/greedy_transactional_lod/TransactionalValidation.h"
#include "experiment/greedy_transactional_lod/TransactionalPredicates.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace ParallelRoam::Experiment::GreedyTransactionalLod;
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
    Require(samples.Values().size()==33 && work.SampleContributions==38,"公共采样或闭面贡献不符");
    for (Slot sid=0;sid<33;++sid)
    {
        const auto xy=samples.Decode(sid);
        Require(samples.Values()[sid].Owner==(xy[1]<=xy[0] ? 0U : 1U),"共享边唯一 owner 错误");
        Require((std::find(samples.FaceSamples(0).begin(),samples.FaceSamples(0).end(),sid)!=samples.FaceSamples(0).end())==(xy[1]<=xy[0]),"闭边样本遗漏");
    }
    Require(TransactionalCertification::ExactErrorSquared(state,samples,4)==2500,"正交误差解析值不符");
    auto perspective=input;perspective.Config.Matrix[13]=.25;TransactionalState projected(perspective);
    Require(TransactionalCertification::ExactErrorSquared(projected,samples,4)==1600,"透视误差解析值不符");
    auto candidates=TransactionalProposals::Receivers(state,samples,0);
    Require(candidates.front().Kind=='E' && candidates.front().Faces.size()==4,"边中点原语未保持两个面到四个面");
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
    for (Slot sid=0;sid<sixths.Values().size();++sid)
    {
        const auto xy=sixths.Decode(sid);
        if (xy[0]==4 && xy[1]==2)
            Require(!sixths.StrictlyInside(sid,a,b,c),"舍入把精确边界样本变成了面内请求");
    }
    WorkLedger expired;expired.Deadline=std::chrono::steady_clock::time_point::min();
    Throws([&] { expired.CheckLimit(); });
}
}

int main()
{
    try { SamplesAndCertification();ReclamationAndPredicates();std::cout<<"数值、样本、局部几何、冲突与发布验证完成\n"; }
    catch (const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
