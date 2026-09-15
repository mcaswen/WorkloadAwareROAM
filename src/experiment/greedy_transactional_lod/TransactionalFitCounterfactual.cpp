#include "experiment/greedy_transactional_lod/TransactionalFitCounterfactual.h"
#include "algorithms/greedy_transactional_lod/TransactionalProposalEvidence.h"
#include "algorithms/greedy_transactional_lod/TransactionalPredicates.h"
#include <cmath>
#include <fstream>
#include <iomanip>
#include <stdexcept>

namespace ParallelRoam::Experiment::GreedyTransactionalLod
{
namespace
{
using namespace Algorithms::GreedyTransactionalLod;
using Clock=std::chrono::steady_clock;
double Seconds(Clock::time_point start) { return std::chrono::duration<double>(Clock::now()-start).count(); }
const char* Name(FitQueryStatus status)
{
    switch (status)
    {
    case FitQueryStatus::Feasible:return "feasible";
    case FitQueryStatus::Empty:return "model_empty";
    default:return "numeric_unknown";
    }
}
std::ofstream File(const std::filesystem::path& path)
{
    std::ofstream out(path);out.exceptions(std::ios::badbit|std::ios::failbit);out<<std::setprecision(17);return out;
}
void Number(std::ostream& out,double value) { if (std::isfinite(value)) out<<value;else out<<"null"; }
void QueryRecord(std::ostream& out,const FitIntervalQuery& q)
{
    out<<"{\"status\":\""<<Name(q.Status)<<"\",\"targetPx\":"<<q.Target<<",\"lowerDelta\":"<<q.Lower
        <<",\"upperDelta\":"<<q.Upper<<",\"lowerSample\":"<<q.LowerSample<<",\"lowerRow\":"<<q.LowerRow
        <<",\"upperSample\":"<<q.UpperSample<<",\"upperRow\":"<<q.UpperRow<<'}';
}
double Height(const Proposal& proposal,std::size_t face,const std::array<double,3>& weights)
{
    const auto& f=proposal.Faces.at(face);
    return (weights[0]*proposal.Points.at(f[0]).Height+weights[1]*proposal.Points.at(f[1]).Height)+
        weights[2]*proposal.Points.at(f[2]).Height;
}
double At(const Proposal& proposal,const Point& point)
{
    for (std::size_t i=0;i<proposal.Faces.size();++i)
    {
        const auto& f=proposal.Faces[i];const auto a=proposal.Points.at(f[0]),b=proposal.Points.at(f[1]),c=proposal.Points.at(f[2]);
        if (TransactionalPredicates::Contains(point,a,b,c))
            return Height(proposal,i,TransactionalPredicates::Barycentric(point,a,b,c));
    }
    throw std::runtime_error("反事实见证没有提案覆盖");
}
double Reference(const TransactionalSamples& samples,const Point& point,double scale)
{
    const auto& source=samples.Source();const double x=point.U*(source.Width-1),y=point.V*(source.Height-1);
    const auto ix=std::min(static_cast<std::uint32_t>(x),source.Width-2),iy=std::min(static_cast<std::uint32_t>(y),source.Height-2);
    const double tx=x-ix,ty=y-iy;const auto base=static_cast<std::size_t>(iy)*source.Width+ix;
    const double lo=source.Values[base]*(1-tx)+source.Values[base+1]*tx;
    const double hi=source.Values[base+source.Width]*(1-tx)+source.Values[base+source.Width+1]*tx;
    return ((1-ty)*lo+ty*hi)*scale/65535;
}
double Error(const Configuration& c,const Point& q,double reference,double height)
{
    // 这里只计算投影位移，不以视口裁剪判断可见；返回视图的离屏值也需留痕
    const auto r=TransactionalSamples::Clip(c,q.U,q.V,reference),m=TransactionalSamples::Clip(c,q.U,q.V,height);
    if (r[3]<=0 || m[3]<=0 || m[2]<(c.UsesZeroToOneDepth ? 0 : -m[3]))
        return std::numeric_limits<double>::quiet_NaN();
    return std::hypot((m[0]/m[3]-r[0]/r[3])*c.Width*.5,(m[1]/m[3]-r[1]/r[3])*c.Height*.5);
}

/// <summary>
/// 闭补丁样本的不可变插值事实，仅在本次诊断内复用
/// 原选值和旧状态分别保留，逐点差分不混用两个比较对象
/// </summary>
struct LocalSample
{
    Slot Id{};Point Parameter;std::size_t Face{};std::array<double,3> Weights{};
    double Reference{}, Before{}, Original{};bool Visible{};
};

std::vector<FitSampleConstraint> BuildModel(const TransactionalState& state,const TransactionalSamples& samples,
    const Proposal& initial,WorkLedger& work)
{
    std::vector<FitSampleConstraint> result;result.reserve(initial.Samples.size());
    const auto& c=state.Config();TransactionalProposalEvidence evidence(samples,initial,work);
    for (auto sid : initial.Samples)
    {
        work.Touch();const auto& entry=evidence.Get(sid,work);const auto& face=initial.Faces[entry.Face];double weight=0;
        for (std::size_t i=0;i<3;++i) if (initial.Free[0]==face[i]) weight+=entry.Weights[i];
        const auto uv=samples.Parameter(sid);const auto& value=samples.Geometry(sid);
        const auto rc=TransactionalSamples::Clip(c,uv.U,uv.V,value.ReferenceHeight);
        const auto mc=TransactionalSamples::Clip(c,uv.U,uv.V,value.MeshHeight);
        const double cw=c.Matrix[13];
        const double kx=(c.Matrix[1]*mc[3]-cw*mc[0])*(c.Width*.5);
        const double ky=(c.Matrix[5]*mc[3]-cw*mc[1])*(c.Height*.5);
        // 原保守公式独立重建；必须先与真实 Fit 区间对齐才允许搜索
        const double k=(std::ceil(std::hypot(kx,ky)/rc[3]*1e6)+1)/1e6;
        result.push_back({sid,weight,value.ReferenceHeight-value.MeshHeight,k,mc[3],cw,
            c.UsesZeroToOneDepth ? mc[2] : mc[2]+mc[3],c.UsesZeroToOneDepth ? c.Matrix[9] : c.Matrix[9]+cw});
    }
    return result;
}

std::vector<LocalSample> LocalEvidence(const TransactionalSamples& samples,
    const Proposal& initial,const Proposal& approved,WorkLedger& work)
{
    auto all=initial;all.Samples.clear();
    for (auto face : initial.Support)
        all.Samples.insert(all.Samples.end(),samples.FaceSamples(face).begin(),samples.FaceSamples(face).end());
    std::sort(all.Samples.begin(),all.Samples.end());all.Samples.erase(std::unique(all.Samples.begin(),all.Samples.end()),all.Samples.end());
    TransactionalProposalEvidence evidence(samples,all,work);std::vector<LocalSample> result;result.reserve(all.Samples.size());
    for (auto sid : all.Samples)
    {
        work.Touch();const auto& e=evidence.Get(sid,work);const auto& g=samples.Geometry(sid);
        // 此限定 E 必须只细分旧曲面；翻边后的不同基准不能偷用这些系数
        if (std::abs(Height(initial,e.Face,e.Weights)-g.MeshHeight)>1e-10)
            throw std::runtime_error("未拟合提案没有复现旧曲面基准");
        result.push_back({sid,samples.Parameter(sid),e.Face,e.Weights,g.ReferenceHeight,g.MeshHeight,
            Height(approved,e.Face,e.Weights),samples.Projection(sid).Visible});
    }
    return result;
}
}

FitIntervalQuery TransactionalFitCounterfactual::Query(std::span<const FitSampleConstraint> samples,double target,
    double lower,double upper,Algorithms::GreedyTransactionalLod::WorkLedger& work)
{
    FitIntervalQuery q;q.Target=target;q.Lower=lower;q.Upper=upper;
    if (!std::isfinite(target) || target<0 || !std::isfinite(lower) || !std::isfinite(upper) || lower>upper || samples.empty()) return q;
    for (const auto& s : samples)
    {
        work.Touch();const double k=s.Factor,cw=s.WDerivative;
        const std::array<double,4> m{-k-target*cw,k-target*cw,-cw,-s.NearDerivative};
        const std::array<double,4> rhs{target*s.ClipW-k*s.Difference,target*s.ClipW+k*s.Difference,s.ClipW-1e-9,s.NearValue};
        for (int row=0;row<4;++row)
        {
            ++work.Constraints;const auto index=static_cast<std::size_t>(row);const double coefficient=m[index]*s.Weight;
            if (!std::isfinite(coefficient) || !std::isfinite(rhs[index])) return q;
            // 对严重消去形成的非零除数保留未知，不放大舍入误差后宣称模型无解
            const double scale=row<2 ? std::abs(k)+std::abs(target*cw) : std::abs(m[index]);
            if (coefficient!=0 && std::abs(m[index])<32*std::numeric_limits<double>::epsilon()*scale) return q;
            if (coefficient==0)
            {
                // 不受自由高度影响的样本仍可能限制整块补丁的最低误差
                if (rhs[index]<0) { q.Status=FitQueryStatus::Empty;return q; }
                continue;
            }
            const double bound=rhs[index]/coefficient;
            if (!std::isfinite(bound)) return q;
            if (coefficient>0 && bound<q.Upper) { q.Upper=bound;q.UpperSample=s.Sample;q.UpperRow=row; }
            else if (coefficient<0 && bound>q.Lower) { q.Lower=bound;q.LowerSample=s.Sample;q.LowerRow=row; }
            if (q.Lower>q.Upper)
            {
                const double margin=64*std::numeric_limits<double>::epsilon()*std::max({1.0,std::abs(q.Lower),std::abs(q.Upper)});
                q.Status=q.Lower-q.Upper>margin ? FitQueryStatus::Empty : FitQueryStatus::NumericUnknown;return q;
            }
        }
    }
    q.Status=FitQueryStatus::Feasible;return q;
}

FitIntervalSearch TransactionalFitCounterfactual::Search(std::span<const FitSampleConstraint> samples,
    const FitIntervalQuery& original,Algorithms::GreedyTransactionalLod::WorkLedger& work)
{
    FitIntervalSearch result;result.Best=original;result.Status="input_unknown";
    if (original.Status!=FitQueryStatus::Feasible) return result;
    // 全程限制在原区间，较低目标只回答同一保守模型中是否还有改善空间
    for (std::size_t i=0;i<32;++i)
    {
        const double target=i==0 ? 0 : (result.LowerTarget+result.Best.Target)*.5;
        auto q=Query(samples,target,original.Lower,original.Upper,work);result.Queries.push_back(q);
        if (q.Status==FitQueryStatus::NumericUnknown) { result.Status="numeric_unknown";return result; }
        if (q.Status==FitQueryStatus::Feasible) result.Best=q;
        else { result.LowerTarget=target;result.HasEmptyLower=true; }
        if (result.Best.Target==0) { result.Status="zero_feasible";return result; }
        if (result.HasEmptyLower && result.Best.Target-result.LowerTarget<=.001)
        { result.Status="model_bracketed";return result; }
    }
    result.Status="query_limit";return result;
}

void TransactionalFitCounterfactual::Run(const Algorithms::GreedyTransactionalLod::TransactionalState& state,
    const Algorithms::GreedyTransactionalLod::TransactionalSamples& samples,const Algorithms::GreedyTransactionalLod::Proposal& approved,
    const Algorithms::GreedyTransactionalLod::Configuration& future,const Algorithms::GreedyTransactionalLod::Configuration& returned,
    const Algorithms::GreedyTransactionalLod::Point& witness,const std::filesystem::path& output)
{
    const auto start=Clock::now();WorkLedger work;work.Deadline=start+std::chrono::seconds(60);work.VisitLimit=2000000;
    auto json=File(output/"fit-counterfactual.json"),queries=File(output/"fit-queries.csv"),variants=File(output/"fit-variants.jsonl");
    queries<<"targetPx,status,lowerDelta,upperDelta,lowerSample,lowerRow,upperSample,upperRow\n";
    json<<"{\"frame\":7,\"receiverRoot\":-99,\"newVertex\":-828436";
    std::string status="complete",detail;std::size_t verified=0;bool variantsOpen=false;
    try
    {
        if (!state.Config().PreserveSurvivingHeights || state.Config().HeightGuard || approved.Kind!='E' ||
            state.Face(approved.Root).Id!=-99 || approved.NewVertex!=-828436 || approved.Free!=std::vector<Identity>{approved.NewVertex} ||
            approved.Samples.size()!=7056 || approved.TargetMicropixels!=1724469)
            throw std::runtime_error("冻结的单高度提案身份不一致");
        auto initial=approved;initial.HeightProof.reset();initial.ErrorLower=initial.ErrorUpper=0;
        const auto& f=state.Face(initial.Root).Vertices;const auto a=state.Vertex(f[0]).Geometry,b=state.Vertex(f[1]).Geometry,c=state.Vertex(f[2]).Geometry;
        const auto weights=TransactionalPredicates::Barycentric(initial.Points.at(initial.NewVertex),a,b,c);
        const double h0=(weights[0]*a.Height+weights[1]*b.Height)+weights[2]*c.Height;
        initial.Points.at(initial.NewVertex).Height=h0;
        auto replay=initial;SingleHeightFitInterval observed;auto stage=Clock::now();
        const auto reason=TransactionalCertification::Fit(state,samples,replay,work,&observed);
        work.Seconds["original_replay"]=Seconds(stage);
        if (reason!="certified" || !observed.Available || observed.InitialHeight!=h0 || replay.Points!=approved.Points || replay.Faces!=approved.Faces ||
            replay.TargetMicropixels!=approved.TargetMicropixels || replay.ErrorLower!=approved.ErrorLower || replay.ErrorUpper!=approved.ErrorUpper)
            throw std::runtime_error("原拟合重放没有复现批准结果");
        stage=Clock::now();const auto model=BuildModel(state,samples,initial,work);
        const auto local=LocalEvidence(samples,initial,approved,work);work.Seconds["evidence"]=Seconds(stage);
        const double target=static_cast<double>(approved.TargetMicropixels)/1000000;
        const auto original=Query(model,target,-state.Config().HeightScale-h0,2*state.Config().HeightScale-h0,work);
        if (original.Status!=FitQueryStatus::Feasible || original.Lower!=observed.LowerDelta || original.Upper!=observed.UpperDelta ||
            h0+std::min(original.Upper,std::max(original.Lower,0.0))!=approved.Points.at(approved.NewVertex).Height)
            throw std::runtime_error("独立模型与真实 Fit 区间不一致");
        const double source=Reference(samples,initial.Points.at(initial.NewVertex),state.Config().HeightScale);
        json<<",\"initialHeight\":"<<h0<<",\"sourceHeight\":"<<source<<",\"sampleCount\":"<<model.size()
            <<",\"closedSampleCount\":"<<local.size()<<",\"observedInterval\":";QueryRecord(json,original);
        // 行系数和局部原始值落盘，后续审查无需只相信最终最优点
        auto facts=File(output/"fit-model.csv");facts<<"sample,beta,difference,factor,clipW,wDerivative,nearValue,nearDerivative\n";
        for (const auto& s : model) facts<<s.Sample<<','<<s.Weight<<','<<s.Difference<<','<<s.Factor<<','<<s.ClipW<<','<<s.WDerivative<<','<<s.NearValue<<','<<s.NearDerivative<<'\n';
        stage=Clock::now();const auto search=Search(model,original,work);work.Seconds["search"]=Seconds(stage);
        for (const auto& q : search.Queries)
            queries<<q.Target<<','<<Name(q.Status)<<','<<q.Lower<<','<<q.Upper<<','<<q.LowerSample<<','<<q.LowerRow<<','<<q.UpperSample<<','<<q.UpperRow<<'\n';
        json<<",\"searchStatus\":\""<<search.Status<<"\",\"queryCount\":"<<search.Queries.size()
            <<",\"lowerTarget\":"<<search.LowerTarget<<",\"hasEmptyLower\":"<<(search.HasEmptyLower ? "true" : "false")<<",\"bestModel\":";QueryRecord(json,search.Best);
        const std::array<const char*,3> names{"original","source_projection","model_minimum"};
        const std::array<double,3> deltas{std::min(original.Upper,std::max(original.Lower,0.0)),
            std::clamp(source-h0,original.Lower,original.Upper),search.Best.Lower+(search.Best.Upper-search.Best.Lower)*.5};
        const double reference=Reference(samples,witness,state.Config().HeightScale);
        auto sampleOutput=File(output/"fit-local-samples.csv");
        sampleOutput<<"variant,sample,u,v,reference,beforeHeight,originalHeight,newHeight,visible,heightExcessVsOriginal,screenExcessVsOriginal\n";
        json<<",\"variants\":[";variantsOpen=true;
        for (std::size_t v=0;v<names.size();++v)
        {
            stage=Clock::now();auto proposal=initial;proposal.Points.at(proposal.NewVertex).Height=h0+deltas[v];
            proposal.HeightProof.reset();proposal.ErrorLower=proposal.ErrorUpper=0;
            const bool measured=TransactionalCertification::Measure(state,samples,proposal,work);
            const bool accepted=measured && TransactionalCertification::Accepts(state,samples,proposal,approved.TargetMicropixels,work);
            ++verified;
            // 原认证使用真实发布高度；更低阈值向下取整，不能借微像素舍入抬高要求
            const auto smaller=static_cast<std::int64_t>(std::floor(search.Best.Target*1000000));
            const bool lowerAccepted=v==2 && measured && TransactionalCertification::Accepts(state,samples,proposal,smaller,work);
            double hmax=0,oldMax=0,heightExcess=-INFINITY,screenExcess=-INFINITY;
            for (const auto& e : local)
            {
                work.Touch();const double h=Height(proposal,e.Face,e.Weights);
                const double excess=std::abs(h-e.Reference)-std::abs(e.Original-e.Reference);
                hmax=std::max(hmax,std::abs(h-e.Reference));oldMax=std::max(oldMax,std::abs(e.Before-e.Reference));
                heightExcess=std::max(heightExcess,excess);
                double screen=std::numeric_limits<double>::quiet_NaN();
                if (e.Visible)
                {
                    screen=Error(state.Config(),e.Parameter,e.Reference,h)-Error(state.Config(),e.Parameter,e.Reference,e.Original);
                    if (!std::isfinite(screen)) throw std::runtime_error("局部可见误差投影失败");
                    screenExcess=std::max(screenExcess,screen);
                }
                sampleOutput<<names[v]<<','<<e.Id<<','<<e.Parameter.U<<','<<e.Parameter.V<<','<<e.Reference<<','<<e.Before<<','<<e.Original<<','<<h
                    <<','<<e.Visible<<','<<excess<<',';if (std::isfinite(screen)) sampleOutput<<screen;sampleOutput<<'\n';
            }
            const double height=proposal.Points.at(proposal.NewVertex).Height,qheight=At(proposal,witness);
            if (v) json<<',';
            const auto write=[&](std::ostream& out) {
                out<<"{\"name\":\""<<names[v]<<"\",\"height\":"<<height<<",\"delta\":"<<deltas[v]<<",\"sourceDistance\":"<<std::abs(height-source)
                    <<",\"measured\":"<<(measured ? "true" : "false")<<",\"acceptedOriginal\":"<<(accepted ? "true" : "false")
                    <<",\"lowerTargetMicropixels\":"<<smaller<<",\"lowerTargetChecked\":"<<(v==2 ? "true" : "false")
                    <<",\"acceptedLower\":"<<(v!=2 ? "null" : (lowerAccepted ? "true" : "false"))
                    <<",\"errorLowerPx\":";Number(out,measured ? std::sqrt(proposal.ErrorLower) : NAN);
                out<<",\"errorUpperPx\":";Number(out,measured ? std::sqrt(proposal.ErrorUpper) : NAN);
                out<<",\"witnessHeight\":"<<qheight<<",\"witnessCurrentPx\":";Number(out,Error(state.Config(),witness,reference,qheight));
                out<<",\"witnessFuturePx\":";Number(out,Error(future,witness,reference,qheight));
                out<<",\"witnessReturnProjectionPx\":";Number(out,Error(returned,witness,reference,qheight));
                out<<",\"closedHeightMax\":"<<hmax<<",\"closedBeforeHeightMax\":"<<oldMax
                    <<",\"heightExcessVsOriginal\":"<<heightExcess<<",\"screenExcessVsOriginalPx\":";Number(out,screenExcess);out<<'}';
            };
            write(json);write(variants);variants<<'\n';variants.flush();
            work.Seconds[names[v]]=Seconds(stage);
        }
        json<<']';variantsOpen=false;
        if (search.Status=="numeric_unknown" || search.Status=="query_limit") status=search.Status;
    }
    catch (const std::exception& e)
    {
        // 诊断预算耗尽不改变原批次；保留已有候选文件，不提升上限继续找正例
        status=Clock::now()>work.Deadline || work.SampleTouches>work.VisitLimit ? "budget_exhausted" : "input_or_model_mismatch";
        detail=e.what();
        // 候选数组开始后异常时，已落盘行仍完整；主文件由脚本识别诊断状态
        if (variantsOpen) json<<']';
    }
    json<<",\"status\":\""<<status<<"\",\"detail\":"<<std::quoted(detail)<<",\"seconds\":"<<Seconds(start)
        <<",\"sampleTouches\":"<<work.SampleTouches<<",\"constraints\":"<<work.Constraints<<",\"exactChecks\":"<<work.ExactChecks
        <<",\"filterChecks\":"<<work.FilterChecks<<",\"evidenceBytes\":"<<work.EvidenceBytes<<",\"verifiedCandidates\":"<<verified<<",\"stages\":{";
    bool first=true;for (const auto& [name,seconds] : work.Seconds) { if (!first) json<<',';first=false;json<<std::quoted(name)<<':'<<seconds; }
    json<<"}}\n";
}
}
