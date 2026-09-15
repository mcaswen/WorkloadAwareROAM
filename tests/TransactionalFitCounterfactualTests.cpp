#include "experiment/greedy_transactional_lod/TransactionalFitCounterfactual.h"
#include "algorithms/greedy_transactional_lod/TransactionalProposals.h"
#include <cmath>
#include <iostream>

namespace
{
using namespace ParallelRoam::Algorithms::GreedyTransactionalLod;
using namespace ParallelRoam::Experiment::GreedyTransactionalLod;
using Audit=TransactionalFitCounterfactual;
void Require(bool condition,const char* reason) { if (!condition) throw std::runtime_error(reason); }

void ClosedForm()
{
    WorkLedger work;
    // 单样本 |delta-1| 的最小值为零；源高度偏好超出区间时只能落在端点
    std::vector<FitSampleConstraint> one{{0,1,1,1,1,0,1,0}};
    const auto interval=Audit::Query(one,.5,-10,10,work);
    Require(interval.Status==FitQueryStatus::Feasible && interval.Lower==.5 && interval.Upper==1.5,"单样本区间不符合闭式解");
    Require(std::clamp(3.0,interval.Lower,interval.Upper)==1.5,"源高度偏好逃出可行区间");
    const auto zero=Audit::Search(one,interval,work);
    Require(zero.Status=="zero_feasible" && zero.Best.Lower==1 && zero.Best.Upper==1,"退化单点最优未保留");

    // 相向目标 max(|delta-1|,|delta+1|) 的最优值为1，不应只追逐任意一个样本
    auto two=one;two.push_back({1,1,-1,1,1,0,1,0});
    const auto both=Audit::Query(two,1.5,-10,10,work);const auto search=Audit::Search(two,both,work);
    Require(search.Status=="model_bracketed" && search.LowerTarget<=1 && search.Best.Target>=1 &&
        search.Best.Target-search.LowerTarget<=.001 && search.Queries.size()<=32,"最优阈值括区不包含闭式解");
    const auto exact=Audit::Query(two,1,-10,10,work);
    Require(exact.Status==FitQueryStatus::Feasible && exact.Lower==0 && exact.Upper==0,"相等约束不能误判为空");

    auto near=one;near[0].NearDerivative=-1;near[0].NearValue=0;
    Require(Audit::Query(near,.5,-10,10,work).Status==FitQueryStatus::Empty,"近面条件没有约束自由高度");
    auto invalid=one;invalid[0].Factor=INFINITY;
    Require(Audit::Query(invalid,.5,-10,10,work).Status==FitQueryStatus::NumericUnknown,"非有限系数被误报可行");
    auto cancellation=one;cancellation[0].WDerivative=1;
    Require(Audit::Query(cancellation,std::nextafter(1.0,2.0),-10,10,work).Status==FitQueryStatus::NumericUnknown,
        "严重消去未保留数值未知");
    WorkLedger limited;limited.VisitLimit=0;bool stopped=false;
    try { static_cast<void>(Audit::Query(one,.5,-10,10,limited)); } catch (const std::runtime_error&) { stopped=true; }
    Require(stopped,"查询没有服从样本配额");
}

void Observation()
{
    InitialMesh input;input.Config.Budget=8;input.Config.PreserveSurvivingHeights=true;
    input.Vertices={{0,{0,0,0}},{1,{1,0,0}},{2,{1,1,0}},{3,{0,1,0}}};
    input.Faces={{0,{0,1,2}},{1,{0,2,3}}};input.Source={3,3,{0,0,0,0,65535,0,0,0,0}};
    TransactionalState state(input);TransactionalSamples samples(input.Source);WorkLedger refresh;samples.Refresh(state,refresh);
    const auto directory=TransactionalProposals::Receivers(state,samples,0);
    auto plain=directory.front(),observed=plain;WorkLedger first,second;SingleHeightFitInterval interval;
    const auto a=TransactionalCertification::Fit(state,samples,plain,first);
    const auto b=TransactionalCertification::Fit(state,samples,observed,second,&interval);
    Require(a=="certified" && a==b && interval.Available,"没有取得真实一维拟合区间");
    Require(plain.Points==observed.Points && plain.TargetMicropixels==observed.TargetMicropixels &&
        plain.ErrorLower==observed.ErrorLower && plain.ErrorUpper==observed.ErrorUpper &&
        first.SampleTouches==second.SampleTouches && first.Constraints==second.Constraints && first.ExactChecks==second.ExactChecks,
        "可选观察改变了拟合或核心工作量");
    Require(interval.InitialHeight+std::min(interval.UpperDelta,std::max(interval.LowerDelta,0.0))==
        observed.Points.at(observed.NewVertex).Height,"记录区间未解释实际选值");
    auto empty=directory.front();empty.Samples.clear();
    Require(TransactionalCertification::Fit(state,samples,empty,second,&interval)=="no_screen_samples" && !interval.Available,
        "失败调用保留了上次有效区间");
    auto bad=directory.front();bad.Faces.front()[1]=bad.Faces.front()[0];interval.Available=true;
    Require(TransactionalCertification::Fit(state,samples,bad,second,&interval)=="shape_infeasible" && !interval.Available,
        "形状失败输出伪有效区间");
    auto pair=directory.front();pair.Free.push_back(0);interval.Available=true;
    static_cast<void>(TransactionalCertification::Fit(state,samples,pair,second,&interval));
    Require(!interval.Available,"二维拟合被误记为一维区间");
}
}

int main()
{
    try { ClosedForm();Observation();std::cout<<"单高度区间、有限查询和只读观察核查完成\n";return 0; }
    catch (const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
