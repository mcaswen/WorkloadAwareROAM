#include "algorithms/greedy_transactional_lod/TransactionalCertification.h"
#include "profiling/CpuProfiling.h"
#include "algorithms/greedy_transactional_lod/TransactionalProposalEvidence.h"
#include "algorithms/greedy_transactional_lod/TransactionalPredicates.h"

#include <boost/multiprecision/cpp_int.hpp>
#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>

namespace ParallelRoam::Algorithms::GreedyTransactionalLod
{
namespace
{
using R=boost::multiprecision::cpp_rational;
using Integer=boost::multiprecision::cpp_int;

/// <summary>
/// 每次基本运算向外舍入；遇到零分母或非有限值时交给精确分支
/// </summary>
struct Interval
{
    double Low{}, High{};
    Interval()=default;
    Interval(double value) : Low(value), High(value) {}
    Interval(double low,double high) : Low(low),High(high) {}
};

double Down(double value) { return std::nextafter(value,-std::numeric_limits<double>::infinity()); }
double Up(double value) { return std::nextafter(value,std::numeric_limits<double>::infinity()); }
Interval operator+(Interval a,Interval b) { return {Down(a.Low+b.Low),Up(a.High+b.High)}; }
Interval operator-(Interval a,Interval b) { return {Down(a.Low-b.High),Up(a.High-b.Low)}; }
Interval operator*(Interval a,Interval b)
{
    // 区间允许跨零，不能只乘同侧端点来估计乘积
    const std::array<double,4> values{a.Low*b.Low,a.Low*b.High,a.High*b.Low,a.High*b.High};
    return {Down(*std::min_element(values.begin(),values.end())),Up(*std::max_element(values.begin(),values.end()))};
}
Interval operator/(Interval a,Interval b)
{
    if (b.Low<=0 && b.High>=0) return {-INFINITY,INFINITY};
    return a*Interval{Down(1/b.High),Up(1/b.Low)};
}

template<class T> std::array<T,3> Reference(const TransactionalState& state,const TransactionalSamples& samples,Slot sid)
{
    // 同一公式分别在区间域与有理域执行，避免两个参考曲面悄然分叉
    const auto xy=samples.Decode(sid);const auto& source=samples.Source();
    const auto x=std::min(xy[0]/6,source.Width-2), y=std::min(xy[1]/6,source.Height-2);
    const T tx=T(xy[0]-6*x)/T(6), ty=T(xy[1]-6*y)/T(6);
    const auto index=static_cast<std::size_t>(y)*source.Width+x;
    const T bottom=T(source.Values[index])*(T(1)-tx)+T(source.Values[index+1])*tx;
    const T top=T(source.Values[index+source.Width])*(T(1)-tx)+T(source.Values[index+source.Width+1])*tx;
    return {T(xy[0])/T(samples.Denominator()),T(xy[1])/T(samples.Denominator()),
        ((T(1)-ty)*bottom+ty*top)*T(state.Config().HeightScale)/T(65535)};
}

template<class T> std::array<T,4> Clip(const Configuration& c,const T& u,const T& v,const T& h)
{
    const T x=(u-T(.5))*T(c.TerrainSize),z=(v-T(.5))*T(c.TerrainSize);
    std::array<T,4> out{};
    for (std::size_t i=0;i<4;++i) out[i]=((T(c.Matrix[4*i])*x+T(c.Matrix[4*i+1])*h)+T(c.Matrix[4*i+2])*z)+T(c.Matrix[4*i+3]);
    return out;
}

template<class T> T Height(const T& u,const T& v,const Point& a,const Point& b,const Point& c)
{
    // 插值读取实际发布高度，不能重新采原始 heightfield 替代被测几何
    const T area=(T(b.U)-T(a.U))*(T(c.V)-T(a.V))-(T(b.V)-T(a.V))*(T(c.U)-T(a.U));
    const T w0=((T(b.U)-u)*(T(c.V)-v)-(T(b.V)-v)*(T(c.U)-u))/area;
    const T w1=((u-T(a.U))*(T(c.V)-T(a.V))-(v-T(a.V))*(T(c.U)-T(a.U)))/area;
    const T w2=T(1)-w0-w1;
    return (w0*T(a.Height)+w1*T(b.Height))+w2*T(c.Height);
}

std::array<Point,3> CoveringFace(const TransactionalState& state,const TransactionalSamples& samples,
    Slot sid,const Proposal* proposal)
{
    if (!proposal)
    {
        // 旧状态的唯一 owner 足以定义共享曲面上的见证高度
        const auto& f=state.Face(samples.Geometry(sid).Owner).Vertices;
        return {state.Vertex(f[0]).Geometry,state.Vertex(f[1]).Geometry,state.Vertex(f[2]).Geometry};
    }
    for (const auto& f : proposal->Faces)
    {
        // 边界可同时命中两个面，它们使用同一存活顶点几何
        const std::array<Point,3> p{proposal->Points.at(f[0]),proposal->Points.at(f[1]),proposal->Points.at(f[2])};
        std::array<double,3> weights{};
        if (samples.Weights(sid,p[0],p[1],p[2],weights)) return p;
    }
    throw std::runtime_error("局部提案缺失闭面样本覆盖");
}

std::optional<R> ExactError(const TransactionalState& state,const TransactionalSamples& samples,Slot sid,const Proposal* proposal,
    TransactionalProposalEvidence* evidence=nullptr,WorkLedger* work=nullptr)
{
    const auto ref=Reference<R>(state,samples,sid); const auto p=evidence ? evidence->Face(sid,*work) : CoveringFace(state,samples,sid,proposal);
    const R height=Height(ref[0],ref[1],p[0],p[1],p[2]);
    const auto rc=Clip(state.Config(),ref[0],ref[1],ref[2]),mc=Clip(state.Config(),ref[0],ref[1],height);
    // 比较投影平方误差，避免精确认证依赖平方根舍入
    if (rc[3]<=0 || mc[3]<=0 || (state.Config().UsesZeroToOneDepth ? mc[2]<0 : mc[2]<-mc[3])) return {};
    const R dx=(mc[0]/mc[3]-rc[0]/rc[3])*state.Config().Width/2;
    const R dy=(mc[1]/mc[3]-rc[1]/rc[3])*state.Config().Height/2;
    return R(dx*dx+dy*dy);
}

std::optional<Interval> ErrorBounds(const TransactionalState& state,const TransactionalSamples& samples,Slot sid,
    const Proposal* proposal,WorkLedger& work,TransactionalProposalEvidence* evidence=nullptr)
{
    ++work.FilterChecks;
    const auto ref=Reference<Interval>(state,samples,sid);
    const auto p=evidence ? evidence->Face(sid,work) : CoveringFace(state,samples,sid,proposal);
    const auto height=Height(ref[0],ref[1],p[0],p[1],p[2]);
    const auto rc=Clip(state.Config(),ref[0],ref[1],ref[2]),mc=Clip(state.Config(),ref[0],ref[1],height);
    const auto near=state.Config().UsesZeroToOneDepth ? mc[2] : mc[2]+mc[3];
    // 只有整个区间都处于投影定义域，才允许快速接受它给出的误差界
    if (rc[3].Low>0 && mc[3].Low>0 && near.Low>=0)
    {
        const auto dx=(mc[0]/mc[3]-rc[0]/rc[3])*Interval(state.Config().Width*.5);
        const auto dy=(mc[1]/mc[3]-rc[1]/rc[3])*Interval(state.Config().Height*.5);
        const auto value=dx*dx+dy*dy;
        if (std::isfinite(value.Low) && std::isfinite(value.High))
            return Interval{std::max(0.0,value.Low),std::max(0.0,value.High)};
    }
    // 过滤不能判定投影域时，不将整个补丁判坏；先复核精确二进制几何
    ++work.ExactChecks;
    const auto exact=ExactError(state,samples,sid,proposal,evidence,&work);
    if (!exact) return {};
    const double value=exact->convert_to<double>();
    return Interval{std::max(0.0,Down(value)),Up(value)};
}

using Pair=std::array<double,2>;
std::vector<Pair> ClipPolygon(const std::vector<Pair>& polygon,const Pair& coefficients,double rhs)
{
    // 二维可行多边形只产生拟合候选；浮点裁剪成功仍不构成质量证明
    std::vector<Pair> result;
    if (polygon.empty()) return result;
    Pair previous=polygon.back();double pv=coefficients[0]*previous[0]+coefficients[1]*previous[1]-rhs;
    for (const auto& current : polygon)
    {
        const double cv=coefficients[0]*current[0]+coefficients[1]*current[1]-rhs;
        if ((pv<=0)!=(cv<=0))
        {
            const double t=pv/(pv-cv);
            result.push_back({previous[0]+t*(current[0]-previous[0]),previous[1]+t*(current[1]-previous[1])});
        }
        if (cv<=0) result.push_back(current);
        previous=current;pv=cv;
    }
    std::vector<Pair> unique;
    for (const auto& point : result)
        if (std::find(unique.begin(),unique.end(),point)==unique.end()) unique.push_back(point);
    return unique;
}
}

/// <summary>
/// 完整闭补丁的高度平方误差证据；精确最大值仅在区间相交时计算
/// </summary>
struct HeightEvidence
{
    std::vector<Slot> Samples;
    Interval Old, New;
    R ExactOld{}, ExactNew{};
    bool Exact{};
};

bool TransactionalCertification::PreservesHeight(const TransactionalState& state,const TransactionalSamples& samples,
    const Proposal& receiver,const Proposal* donor,WorkLedger& work)
{
    const auto started=std::chrono::steady_clock::now();++work.HeightGuardChecks;
    const auto evidence=[&](const Proposal& proposal)->HeightEvidence& {
        // 高度保护覆盖不可见 Q，不能复用只含屏幕可见证据的 Samples 字段
        if (proposal.HeightProof) return *proposal.HeightProof;
        auto proof=std::make_shared<HeightEvidence>();
        for (auto face : proposal.Support)
            proof->Samples.insert(proof->Samples.end(),samples.FaceSamples(face).begin(),samples.FaceSamples(face).end());
        std::sort(proof->Samples.begin(),proof->Samples.end());
        proof->Samples.erase(std::unique(proof->Samples.begin(),proof->Samples.end()),proof->Samples.end());
        for (auto sid : proof->Samples)
        {
            work.Touch();++work.HeightSamples;
            const auto ref=Reference<Interval>(state,samples,sid);
            const auto bound=[&](const Proposal* p) {
                const auto f=CoveringFace(state,samples,sid,p);
                const auto h=Height(ref[0],ref[1],f[0],f[1],f[2])-ref[2];const auto sq=h*h;
                return Interval{std::max(0.0,sq.Low),sq.High};
            };
            const auto old=bound(nullptr),next=bound(&proposal);
            proof->Old.Low=std::max(proof->Old.Low,old.Low);proof->Old.High=std::max(proof->Old.High,old.High);
            proof->New.Low=std::max(proof->New.Low,next.Low);proof->New.High=std::max(proof->New.High,next.High);
        }
        proposal.HeightProof=std::move(proof);return *proposal.HeightProof;
    };
    auto& r=evidence(receiver);auto* d=donor ? &evidence(*donor) : nullptr;
    // 交换先合并两个补丁的最大值，保护并不要求每个样本逐点不退化
    const double oldLow=std::max(r.Old.Low,d ? d->Old.Low : 0),oldHigh=std::max(r.Old.High,d ? d->Old.High : 0);
    const double newLow=std::max(r.New.Low,d ? d->New.Low : 0),newHigh=std::max(r.New.High,d ? d->New.High : 0);
    bool accepted=false;
    if (!r.Samples.empty() && (!d || !d->Samples.empty()) && std::isfinite(newHigh) && newHigh<=oldLow) accepted=true;
    else if (!r.Samples.empty() && (!d || !d->Samples.empty()) && !(newLow>oldHigh))
    {
        // 边界相等使用有理平方比较，不靠像素或高度 epsilon 宣称安全
        const auto exact=[&](const Proposal& proposal,HeightEvidence& proof) {
            if (proof.Exact) return;
            for (auto sid : proof.Samples)
            {
                work.Touch();++work.HeightExactSamples;
                const auto ref=Reference<R>(state,samples,sid);
                const auto error=[&](const Proposal* p)->R {
                    const auto f=CoveringFace(state,samples,sid,p);
                    const R difference=Height(ref[0],ref[1],f[0],f[1],f[2])-ref[2];return R(difference*difference);
                };
                proof.ExactOld=std::max(proof.ExactOld,error(nullptr));
                proof.ExactNew=std::max(proof.ExactNew,error(&proposal));
            }
            proof.Exact=true;
        };
        // 同一提案在多个配对阈值下复用精确值，避免每个 pair 重扫全 Q
        exact(receiver,r);if (donor) exact(*donor,*d);
        accepted=std::max(r.ExactNew,d ? d->ExactNew : R(0))<=std::max(r.ExactOld,d ? d->ExactOld : R(0));
    }
    if (!accepted) ++work.HeightGuardRejected;
    work.Seconds["height_guard"]+=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
    return accepted;
}

double TransactionalCertification::ExactErrorSquared(const TransactionalState& state,const TransactionalSamples& samples,
    Slot sample,const Proposal* proposal)
{
    const auto value=ExactError(state,samples,sample,proposal);
    if (!value) throw std::runtime_error("精确投影不在声明域");
    return value->convert_to<double>();
}

bool TransactionalCertification::Measure(const TransactionalState& state,const TransactionalSamples& samples,
    Proposal& proposal,WorkLedger& work)
{
    // 单次回收测量没有前序拟合可复用，不为它分配整份证据缓存。
    return Measure(state,samples,proposal,work,nullptr);
}

bool TransactionalCertification::Measure(const TransactionalState& state,const TransactionalSamples& samples,
    Proposal& proposal,WorkLedger& work,TransactionalProposalEvidence* evidence)
{
    ROAM_CPU_ZONE("gtp.measure");
    proposal.ErrorLower=proposal.ErrorUpper=0;
    // 缓存整个局部曲面的最大误差区间，供多个接收阈值复用
    for (auto sid : proposal.Samples)
    {
        work.Touch();const auto error=ErrorBounds(state,samples,sid,&proposal,work,evidence);
        if (!error) return false;
        proposal.ErrorLower=std::max(proposal.ErrorLower,error->Low);
        proposal.ErrorUpper=std::max(proposal.ErrorUpper,error->High);
    }
    return true;
}

bool TransactionalCertification::Accepts(const TransactionalState& state,const TransactionalSamples& samples,
    const Proposal& proposal,std::int64_t targetMicropixels,WorkLedger& work)
{
    ROAM_CPU_ZONE("gtp.accepts");
    if (targetMicropixels<0) return false;
    const Interval target=Interval(static_cast<double>(targetMicropixels))/Interval(1000000);
    const Interval square=target*target;
    // 快速真/假必须由不相交区间支持，边界重叠才逐样本精确比较
    if (proposal.ErrorUpper<=square.Low) return true;
    if (proposal.ErrorLower>square.High) return false;
    const R exactTarget=R(targetMicropixels)/1000000;
    for (auto sid : proposal.Samples)
    {
        work.Touch();++work.ExactChecks;
        const auto error=ExactError(state,samples,sid,&proposal);
        if (!error || *error>exactTarget*exactTarget) return false;
    }
    return true;
}

std::string TransactionalCertification::Fit(const TransactionalState& state,const TransactionalSamples& samples,
    Proposal& proposal,WorkLedger& work)
{
    ROAM_CPU_ZONE("gtp.fit");
    work.CheckLimit();++work.Proposals;
    for (const auto& face : proposal.Faces)
        if (!TransactionalPredicates::Shape(proposal.Points.at(face[0]),proposal.Points.at(face[1]),proposal.Points.at(face[2])))
            return "shape_infeasible";
    if (proposal.Samples.empty()) return "no_screen_samples";
    auto witness=proposal.Samples.front();
    // 浮点评价只选择见证，实际阈值由该见证的精确旧误差向下取整
    for (auto sid : proposal.Samples)
        if (samples.Projection(sid).ErrorSquared>samples.Projection(witness).ErrorSquared) witness=sid;
    const auto oldError=ExactError(state,samples,witness,nullptr);++work.ExactChecks;
    if (!oldError) return "projection_unknown";
    const Integer scaled=numerator(*oldError)*Integer(1000000000000LL)/denominator(*oldError);
    const Integer root=boost::multiprecision::sqrt(scaled);
    if (root>std::numeric_limits<std::int64_t>::max()) return "numeric_unknown";
    proposal.TargetMicropixels=root.convert_to<std::int64_t>()-10000;
    if (proposal.TargetMicropixels<0) return "below_progress_margin";
    const double target=static_cast<double>(proposal.TargetMicropixels)/1000000;
    const auto& config=state.Config();
    const auto first=proposal.Points.at(proposal.Free[0]).Height;
    double low=-config.HeightScale-first, high=2*config.HeightScale-first;
    std::vector<Pair> polygon;
    // 高度范围以相对增量表达；旧中心与新点共享同一个局部约束系统
    if (proposal.Free.size()==2)
    {
        const double other=proposal.Points.at(proposal.Free[1]).Height;
        polygon={{low,-config.HeightScale-other},{high,-config.HeightScale-other},
            {high,2*config.HeightScale-other},{low,2*config.HeightScale-other}};
    }
    TransactionalProposalEvidence evidence(samples,proposal,work);
    for (auto sid : proposal.Samples)
    {
        work.Touch();
        const auto& entry=evidence.Get(sid,work);
        const auto& f=proposal.Faces[entry.Face];Pair beta{};
        for (std::size_t i=0;i<proposal.Free.size();++i)
            for (std::size_t j=0;j<3;++j) if (proposal.Free[i]==f[j]) beta[i]+=entry.Weights[j];
        const auto uv=samples.Parameter(sid);const auto& value=samples.Geometry(sid);
        const auto rc=TransactionalSamples::Clip(config,uv.U,uv.V,value.ReferenceHeight);
        const auto mc=TransactionalSamples::Clip(config,uv.U,uv.V,value.MeshHeight);
        const double cw=config.Matrix[13];
        const double kx=(config.Matrix[1]*mc[3]-cw*mc[0])*(config.Width*.5);
        const double ky=(config.Matrix[5]*mc[3]-cw*mc[1])*(config.Height*.5);
        const double k=(std::ceil(std::hypot(kx,ky)/rc[3]*1e6)+1)/1e6;
        // 线性上界故意向保守方向取值，遗漏可行解只记录拟合失败
        const double difference=value.ReferenceHeight-value.MeshHeight;
        // 近面关于自由高度的系数和常量必须使用同一深度约定
        const double nearCoefficient=config.UsesZeroToOneDepth ? config.Matrix[9] : config.Matrix[9]+cw;
        const double nearValue=config.UsesZeroToOneDepth ? mc[2] : mc[2]+mc[3];
        const std::array<double,4> multiplier{-k-target*cw,k-target*cw,-cw,-nearCoefficient};
        const std::array<double,4> rhs{target*mc[3]-k*difference,target*mc[3]+k*difference,mc[3]-1e-9,nearValue};
        for (std::size_t row=0;row<4;++row)
        {
            ++work.Constraints;const Pair coefficients{multiplier[row]*beta[0],multiplier[row]*beta[1]};
            if (proposal.Free.size()==2)
            {
                polygon=ClipPolygon(polygon,coefficients,rhs[row]);
                if (polygon.empty()) return "fit_bound_failed";
            }
            else
            {
                if (coefficients[0]>0) high=std::min(high,rhs[row]/coefficients[0]);
                else if (coefficients[0]<0) low=std::max(low,rhs[row]/coefficients[0]);
                else if (rhs[row]<0) return "fit_bound_failed";
                if (low>high) return "fit_bound_failed";
            }
        }
    }
    Pair delta{std::min(high,std::max(low,0.0)),0};
    // 一维选最接近零的增量，二维沿用冻结多边形顶点均值
    if (proposal.Free.size()==2)
    {
        delta={0,0};
        for (const auto& point : polygon) { delta[0]+=point[0];delta[1]+=point[1]; }
        delta[0]/=static_cast<double>(polygon.size());delta[1]/=static_cast<double>(polygon.size());
    }
    for (std::size_t i=0;i<proposal.Free.size();++i)
    {
        auto& point=proposal.Points.at(proposal.Free[i]);point.Height+=delta[i];
        if (!std::isfinite(point.Height) || point.Height < -config.HeightScale || point.Height > 2*config.HeightScale)
            return "numeric_unknown";
    }
    // 上面的加法已经舍入到真正发布值；证书不为拟合器提供容差通道
    if (!Measure(state,samples,proposal,work,&evidence) || !Accepts(state,samples,proposal,proposal.TargetMicropixels,work))
        return "numeric_unknown";
    return "certified";
}
}
