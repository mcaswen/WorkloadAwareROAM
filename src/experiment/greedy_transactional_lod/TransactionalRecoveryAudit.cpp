#include "experiment/greedy_transactional_lod/TransactionalRecoveryAudit.h"
#include "algorithms/greedy_transactional_lod/TransactionalProposalEvidence.h"
#include "algorithms/greedy_transactional_lod/TransactionalPredicates.h"
#include "algorithms/greedy_transactional_lod/TransactionalProposals.h"
#include <cmath>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>

namespace ParallelRoam::Experiment::GreedyTransactionalLod
{
namespace
{
using namespace Algorithms::GreedyTransactionalLod;
using P=TransactionalPredicates;
using Face=std::array<Identity,3>;
using Clock=std::chrono::steady_clock;
double Seconds(Clock::time_point start) { return std::chrono::duration<double>(Clock::now()-start).count(); }
Face Key(Face face) { std::sort(face.begin(),face.end());return face; }
/// <summary>
/// 边使用稳定点身份去重，不依赖局部面的物理顺序。
/// 新对角线还需在调用处检查是否与域外既有边重名。
/// </summary>
std::set<Edge> Edges(const RecoveryGeometry& geometry)
{
    std::set<Edge> result;
    for (const auto& f : geometry.Faces) for (std::size_t i=0;i<3;++i) result.insert(EdgeKey(f[i],f[(i+1)%3]));
    return result;
}
std::vector<std::size_t> Uses(const RecoveryGeometry& geometry,Edge edge)
{
    std::vector<std::size_t> result;
    for (std::size_t i=0;i<geometry.Faces.size();++i)
    {
        const auto& f=geometry.Faces[i];
        if (std::find(f.begin(),f.end(),edge[0])!=f.end() && std::find(f.begin(),f.end(),edge[1])!=f.end()) result.push_back(i);
    }
    return result;
}
/// <summary>
/// 见证可落在共享边上，枚举时允许闭包含。
/// 新点插入必须使用严格内部，防止扇形产生零面积面。
/// </summary>
bool Inside(const RecoveryGeometry& g,std::size_t face,const Point& point,bool strict=false)
{
    const auto& f=g.Faces.at(face);const auto a=g.Points.at(f[0]),b=g.Points.at(f[1]),c=g.Points.at(f[2]);
    return strict ? P::Orientation(a,b,point)>0 && P::Orientation(b,c,point)>0 && P::Orientation(c,a,point)>0 : P::Contains(point,a,b,c);
}
double Interpolate(const RecoveryGeometry& g,std::size_t face,const Point& point)
{
    const auto& f=g.Faces.at(face);const auto a=g.Points.at(f[0]),b=g.Points.at(f[1]),c=g.Points.at(f[2]);
    const auto w=P::Barycentric(point,a,b,c);return (w[0]*a.Height+w[1]*b.Height)+w[2]*c.Height;
}
double At(const Proposal& proposal,const Point& point)
{
    const RecoveryGeometry geometry{proposal.Points,proposal.Faces,{}};
    for (std::size_t i=0;i<geometry.Faces.size();++i) if (Inside(geometry,i,point)) return Interpolate(geometry,i,point);
    throw std::runtime_error("见证不在有限提案支持内");
}
/// <summary>
/// 新点的源高由固定原始栅格双线性插值得到，与重连后的近似曲面无关。
/// 输入点来自已核查的局部域，不对越界坐标提供外推。
/// </summary>
double Source(const TransactionalSamples& samples,const Configuration& config,const Point& point)
{
    const auto& source=samples.Source();const double x=point.U*(source.Width-1),y=point.V*(source.Height-1);
    const auto ix=std::min(static_cast<std::uint32_t>(x),source.Width-2),iy=std::min(static_cast<std::uint32_t>(y),source.Height-2);
    const auto base=static_cast<std::size_t>(iy)*source.Width+ix;const double tx=x-ix,ty=y-iy;
    const double lo=source.Values[base]*(1-tx)+source.Values[base+1]*tx;
    const double hi=source.Values[base+source.Width]*(1-tx)+source.Values[base+source.Width+1]*tx;
    return ((1-ty)*lo+ty*hi)*config.HeightScale/65535;
}
/// <summary>
/// 固定相机上的见证投影不做视口裁除，以便追踪离屏期间的几何变化。
/// 无法投影时输出未知；它不是覆盖整个连续地形的质量证书。
/// </summary>
double Error(const Configuration& c,const Point& q,double reference,double height)
{
    const auto r=TransactionalSamples::Clip(c,q.U,q.V,reference),m=TransactionalSamples::Clip(c,q.U,q.V,height);
    if (r[3]<=0 || m[3]<=0 || m[2]<(c.UsesZeroToOneDepth ? 0 : -m[3])) return NAN;
    return std::hypot((m[0]/m[3]-r[0]/r[3])*c.Width*.5,(m[1]/m[3]-r[1]/r[3])*c.Height*.5);
}
void Number(std::ostream& out,double value) { if (std::isfinite(value)) out<<value;else out<<"null"; }
void Geometry(std::ostream& out,const RecoveryGeometry& g)
{
    out<<"{\"points\":[";bool first=true;
    for (const auto& [id,p] : g.Points)
    { if (!first) out<<',';first=false;out<<'['<<id<<','<<p.U<<','<<p.V<<','<<p.Height<<']'; }
    out<<"],\"faces\":[";first=true;
    for (const auto& f : g.Faces) { if (!first) out<<',';first=false;out<<'['<<f[0]<<','<<f[1]<<','<<f[2]<<']'; }
    out<<"],\"newVertex\":";if (g.NewVertex) out<<*g.NewVertex;else out<<"null";out<<'}';
}
RecoveryGeometry Read(const TransactionalState& state,const std::vector<Slot>& support)
{
    RecoveryGeometry g;
    for (auto slot : support)
    {
        const auto& f=state.Face(slot).Vertices;g.Faces.push_back(f);
        for (auto id : f) g.Points.emplace(id,state.Vertex(id).Geometry);
    }
    return g;
}
/// <summary>
/// 从最终连接提取净替换支持，未改变的面不进入局部质量阈值。
/// 各候选的支持可能不同，报告不能把它们的局部最大误差直接排名。
/// </summary>
Proposal Difference(const TransactionalState& state,const TransactionalSamples& samples,const std::vector<Slot>& domain,
    const RecoveryGeometry& target,Slot root)
{
    Proposal p;p.Kind='R';p.Root=root;p.Points=target.Points;
    std::set<Face> oldKeys,newKeys;for (auto slot : domain) oldKeys.insert(Key(state.Face(slot).Vertices));
    for (const auto& f : target.Faces) newKeys.insert(Key(f));
    for (auto slot : domain) if (!newKeys.contains(Key(state.Face(slot).Vertices))) p.Support.push_back(slot);
    for (const auto& f : target.Faces) if (!oldKeys.contains(Key(f))) p.Faces.push_back(f);
    if (target.NewVertex) { p.NewVertex=*target.NewVertex;p.Free={p.NewVertex}; }
    p.Samples=samples.VisibleSupport(p.Support);return p;
}

/// <summary>
/// 样本和几何在同一冻结支持上比较；当前证书与辅助见证分开输出。
/// 此处没有发布或后续恢复模拟，预算只检查快照上的单独应用。
/// </summary>
void Variant(std::ostream& out,const char* name,const TransactionalState& state,const TransactionalSamples& samples,
    Proposal proposal,std::int64_t target,const Configuration& future,const Point& witness,WorkLedger& work)
{
    proposal.HeightProof.reset();const bool measured=TransactionalCertification::Measure(state,samples,proposal,work);
    const bool accepted=measured && TransactionalCertification::Accepts(state,samples,proposal,target,work);
    auto old=proposal;old.Faces.clear();for (auto slot : proposal.Support) old.Faces.push_back(state.Face(slot).Vertices);
    const double reference=Source(samples,state.Config(),witness),before=At(old,witness),after=At(proposal,witness);
    const double oldScreen=Error(state.Config(),witness,reference,before),newScreen=Error(state.Config(),witness,reference,after);
    // 闭支持样本用于发现离屏高度代价，不扩大当前可见样本的接受契约。
    auto closed=proposal;closed.Samples.clear();
    for (auto slot : proposal.Support) closed.Samples.insert(closed.Samples.end(),samples.FaceSamples(slot).begin(),samples.FaceSamples(slot).end());
    std::sort(closed.Samples.begin(),closed.Samples.end());closed.Samples.erase(std::unique(closed.Samples.begin(),closed.Samples.end()),closed.Samples.end());
    TransactionalProposalEvidence evidence(samples,closed,work);
    double heightMax=0,oldHeightMax=0,heightExcess=-INFINITY,screenExcess=-INFINITY;Slot heightWorst=InvalidSlot,screenWorst=InvalidSlot;
    for (auto sid : closed.Samples)
    {
        work.Touch();const auto& e=evidence.Get(sid,work);const auto& f=closed.Faces[e.Face];
        const double h=(e.Weights[0]*closed.Points.at(f[0]).Height+e.Weights[1]*closed.Points.at(f[1]).Height)+e.Weights[2]*closed.Points.at(f[2]).Height;
        const auto& value=samples.Geometry(sid);const double loss=std::abs(h-value.ReferenceHeight),oldLoss=std::abs(value.MeshHeight-value.ReferenceHeight);
        heightMax=std::max(heightMax,loss);oldHeightMax=std::max(oldHeightMax,oldLoss);
        if (loss-oldLoss>heightExcess) { heightExcess=loss-oldLoss;heightWorst=sid; }
        if (samples.Projection(sid).Visible)
        {
            const double delta=Error(state.Config(),samples.Parameter(sid),value.ReferenceHeight,h)-std::sqrt(samples.Projection(sid).ErrorSquared);
            if (!std::isfinite(delta)) throw std::runtime_error("可见样本辅助投影失败");
            if (delta>screenExcess) { screenExcess=delta;screenWorst=sid; }
        }
    }
    out<<"{\"name\":"<<std::quoted(name)<<",\"height\":";if (proposal.Free.empty()) out<<"null";else out<<proposal.Points.at(proposal.NewVertex).Height;
    out<<",\"measured\":"<<(measured ? "true" : "false")<<",\"accepted\":"<<(accepted ? "true" : "false")
       <<",\"witnessProgress\":"<<(accepted && std::isfinite(newScreen) && oldScreen-newScreen>=.01 ? "true" : "false")<<",\"errorLowerPx\":";
    Number(out,measured ? std::sqrt(proposal.ErrorLower) : NAN);out<<",\"errorUpperPx\":";Number(out,measured ? std::sqrt(proposal.ErrorUpper) : NAN);
    out<<",\"witnessBeforeHeight\":"<<before<<",\"witnessAfterHeight\":"<<after<<",\"witnessBeforePx\":";Number(out,oldScreen);
    out<<",\"witnessAfterPx\":";Number(out,newScreen);out<<",\"witnessFuturePx\":";Number(out,Error(future,witness,reference,after));
    out<<",\"closedSamples\":"<<closed.Samples.size()<<",\"heightMax\":"<<heightMax<<",\"oldHeightMax\":"<<oldHeightMax
       <<",\"heightExcess\":"<<heightExcess<<",\"heightExcessSample\":"<<heightWorst<<",\"screenExcessPx\":";Number(out,screenExcess);
    out<<",\"screenExcessSample\":"<<screenWorst<<'}';
}
}

std::optional<RecoveryGeometry> TransactionalRecoveryAudit::Flip(const RecoveryGeometry& input,Algorithms::GreedyTransactionalLod::Edge edge)
{
    edge=EdgeKey(edge[0],edge[1]);
    const auto uses=Uses(input,edge);if (uses.size()!=2 || input.NewVertex) return {};
    const auto opposite=[&](std::size_t i) { for (auto id : input.Faces[i]) if (id!=edge[0] && id!=edge[1]) return id;throw std::runtime_error("退化边使用"); };
    const auto c=opposite(uses[0]),d=opposite(uses[1]);if (c==d || Edges(input).contains(EdgeKey(c,d))) return {};
    const auto a=input.Points.at(edge[0]),b=input.Points.at(edge[1]),pc=input.Points.at(c),pd=input.Points.at(d);
    // 两条对角线都严格相交才是凸四边形翻边；共线不能借舍入变成合法候选
    if (P::Orientation(a,b,pc)*P::Orientation(a,b,pd)>=0 || P::Orientation(pc,pd,a)*P::Orientation(pc,pd,b)>=0) return {};
    auto result=input;std::array<Face,2> faces{Face{c,d,edge[0]},Face{d,c,edge[1]}};
    for (auto& f : faces) if (P::Orientation(input.Points.at(f[0]),input.Points.at(f[1]),input.Points.at(f[2]))<0) std::swap(f[0],f[1]);
    result.Faces[uses[0]]=faces[0];result.Faces[uses[1]]=faces[1];return result;
}

std::optional<RecoveryGeometry> TransactionalRecoveryAudit::SplitEdge(const RecoveryGeometry& input,
    Algorithms::GreedyTransactionalLod::Edge edge,Algorithms::GreedyTransactionalLod::Identity id)
{
    edge=EdgeKey(edge[0],edge[1]);
    const auto uses=Uses(input,edge);if (uses.size()!=2 || input.NewVertex || input.Points.contains(id)) return {};
    auto result=input;const auto a=input.Points.at(edge[0]),b=input.Points.at(edge[1]);
    const Point point{(a.U+b.U)*.5,(a.V+b.V)*.5,(a.Height+b.Height)*.5};result.Points.emplace(id,point);result.NewVertex=id;result.Faces.clear();
    for (std::size_t i=0;i<input.Faces.size();++i)
    {
        const auto& f=input.Faces[i];
        if (i!=uses[0] && i!=uses[1]) { result.Faces.push_back(f);continue; }
        for (std::size_t j=0;j<3;++j)
            if (EdgeKey(f[j],f[(j+1)%3])!=edge) result.Faces.push_back({f[j],f[(j+1)%3],id});
    }
    return result;
}

std::optional<RecoveryGeometry> TransactionalRecoveryAudit::SplitFace(const RecoveryGeometry& input,std::size_t face,
    Algorithms::GreedyTransactionalLod::Point point,Algorithms::GreedyTransactionalLod::Identity id)
{
    if (input.NewVertex || input.Points.contains(id) || !Inside(input,face,point,true)) return {};
    auto result=input;point.Height=Interpolate(input,face,point);result.Points.emplace(id,point);result.NewVertex=id;
    const auto f=input.Faces.at(face);result.Faces[face]={f[0],f[1],id};
    result.Faces.push_back({f[1],f[2],id});result.Faces.push_back({f[2],f[0],id});return result;
}

std::vector<FitSampleConstraint> TransactionalRecoveryAudit::Model(const Algorithms::GreedyTransactionalLod::TransactionalState& state,
    const Algorithms::GreedyTransactionalLod::TransactionalSamples& samples,const Algorithms::GreedyTransactionalLod::Proposal& initial,
    Algorithms::GreedyTransactionalLod::WorkLedger& work)
{
    if (initial.Free.size()!=1) throw std::runtime_error("恢复模型只允许一个自由高度");
    std::vector<FitSampleConstraint> result;result.reserve(initial.Samples.size());TransactionalProposalEvidence evidence(samples,initial,work);
    const auto& c=state.Config();
    for (auto sid : initial.Samples)
    {
        work.Touch();const auto& e=evidence.Get(sid,work);const auto& f=initial.Faces[e.Face];double beta=0;
        for (std::size_t i=0;i<3;++i) if (f[i]==initial.NewVertex) beta+=e.Weights[i];
        // 翻边后固定高度曲面已经改变，基准不能再读旧 Geometry.MeshHeight
        const double base=(e.Weights[0]*initial.Points.at(f[0]).Height+e.Weights[1]*initial.Points.at(f[1]).Height)+e.Weights[2]*initial.Points.at(f[2]).Height;
        const auto uv=samples.Parameter(sid);const double reference=samples.Geometry(sid).ReferenceHeight;
        const auto rc=TransactionalSamples::Clip(c,uv.U,uv.V,reference),mc=TransactionalSamples::Clip(c,uv.U,uv.V,base);
        const double cw=c.Matrix[13],kx=(c.Matrix[1]*mc[3]-cw*mc[0])*(c.Width*.5),ky=(c.Matrix[5]*mc[3]-cw*mc[1])*(c.Height*.5);
        const double k=(std::ceil(std::hypot(kx,ky)/rc[3]*1e6)+1)/1e6;
        result.push_back({sid,beta,reference-base,k,mc[3],cw,c.UsesZeroToOneDepth ? mc[2] : mc[2]+mc[3],c.UsesZeroToOneDepth ? c.Matrix[9] : c.Matrix[9]+cw});
    }
    return result;
}

void TransactionalRecoveryAudit::Run(const Algorithms::GreedyTransactionalLod::TransactionalState& state,
    const Algorithms::GreedyTransactionalLod::TransactionalSamples& samples,const Algorithms::GreedyTransactionalLod::Configuration& future,
    const Algorithms::GreedyTransactionalLod::Point& witness,const std::filesystem::path& output)
{
    const auto start=Clock::now();WorkLedger work;work.Deadline=start+std::chrono::seconds(60);work.VisitLimit=2000000;
    std::ofstream records(output/"recovery-candidates.jsonl"),modelFile(output/"recovery-model.csv");
    records.exceptions(std::ios::badbit|std::ios::failbit);modelFile.exceptions(std::ios::badbit|std::ios::failbit);
    modelFile<<std::setprecision(17)<<"case,sample,beta,difference,factor,clipW,wDerivative,nearValue,nearDerivative\n";
    std::string status="complete",detail;std::size_t count=0,shapeCount=0;
    std::set<std::tuple<std::vector<Face>,double,double>> seen;
    try
    {
        // 入口绑定frame8；相机使用当帧矩阵，Config.SampleIndex没有随平台相机更新
        if (!state.Config().PreserveSurvivingHeights || state.Config().HeightGuard || state.Config().Budget!=50000)
            throw std::runtime_error("恢复诊断的冻结预算或高度政策不一致");
        Slot root=InvalidSlot;
        for (auto slot : state.ActiveFaces()) { ++work.LocationTests;if (state.Face(slot).Id==-111) root=slot; }
        if (root==InvalidSlot) throw std::runtime_error("冻结覆盖根缺失");
        std::vector<Slot> domain{root};const auto& face=state.Face(root).Vertices;std::vector<Edge> rootEdges;
        for (std::size_t i=0;i<3;++i)
        {
            const auto edge=EdgeKey(face[i],face[(i+1)%3]);rootEdges.push_back(edge);
            const auto& use=state.Edges().at(edge);for (std::size_t j=0;j<use.Count;++j) domain.push_back(use.Faces[j]);
        }
        std::sort(rootEdges.begin(),rootEdges.end());std::sort(domain.begin(),domain.end());domain.erase(std::unique(domain.begin(),domain.end()),domain.end());
        if (domain.size()>4) throw std::runtime_error("恢复域超过根的直接邻接");
        const auto original=Read(state,domain);const auto rootGeometry=Read(state,{root});
        if (!Inside(rootGeometry,0,witness,true)) throw std::runtime_error("冻结见证不在根严格内部");
        std::ofstream frozen(output/"recovery-domain.json");frozen.exceptions(std::ios::badbit|std::ios::failbit);
        frozen<<std::setprecision(17)<<"{\"frame\":8,\"root\":-111,\"rootVertices\":["<<face[0]<<','<<face[1]<<','<<face[2]
              <<"],\"witness\":["<<witness.U<<','<<witness.V<<"],\"faces\":"<<state.FaceCount()<<",\"budget\":"<<state.Config().Budget<<",\"faceIds\":[";
        for (std::size_t i=0;i<domain.size();++i) { if (i) frozen<<',';frozen<<state.Face(domain[i]).Id; }
        frozen<<"],\"geometry\":";Geometry(frozen,original);frozen<<",\"currentMatrix\":[";
        for (std::size_t i=0;i<16;++i) { if (i) frozen<<',';frozen<<state.Config().Matrix[i]; }
        frozen<<"],\"futureMatrix\":[";for (std::size_t i=0;i<16;++i) { if (i) frozen<<',';frozen<<future.Matrix[i]; }frozen<<"]}";

        // 每行先在内存封闭，超限时只保留完整候选行，不把半条JSON冒充失败结果
        const auto emit=[&](const std::string& name,const RecoveryGeometry& old,const RecoveryGeometry& geometry,Proposal proposal,const std::string& legacy) {
            work.CheckLimit();if (count>=64) throw std::runtime_error("candidate_cap");++count;
            const auto stage=Clock::now();std::ostringstream row;row<<std::setprecision(17)<<"{\"name\":"<<std::quoted(name)<<",\"old\":";Geometry(row,old);
            row<<",\"target\":";Geometry(row,geometry);row<<",\"legacyReason\":"<<std::quoted(legacy)<<",\"shape\":[";
            bool shape=true;for (std::size_t i=0;i<geometry.Faces.size();++i)
            { const auto& f=geometry.Faces[i];const bool valid=P::Shape(geometry.Points.at(f[0]),geometry.Points.at(f[1]),geometry.Points.at(f[2]));if (i) row<<',';row<<(valid ? "true" : "false");shape&=valid; }
            const auto delta=static_cast<std::int64_t>(proposal.Faces.size())-static_cast<std::int64_t>(proposal.Support.size());
            row<<"],\"supportIds\":[";for (std::size_t i=0;i<proposal.Support.size();++i) { if (i) row<<',';row<<state.Face(proposal.Support[i]).Id; }
            row<<"],\"samples\":"<<proposal.Samples.size()<<",\"faceDelta\":"<<delta<<",\"standaloneBudgetFits\":"
               <<(static_cast<std::int64_t>(state.FaceCount())+delta<=static_cast<std::int64_t>(state.Config().Budget) ? "true" : "false");
            if (shape && !proposal.Support.empty() && !proposal.Samples.empty() && legacy.empty())
            {
                ++shapeCount;auto oldProposal=proposal;oldProposal.Faces.clear();for (auto slot : proposal.Support) oldProposal.Faces.push_back(state.Face(slot).Vertices);
                if (!TransactionalCertification::Measure(state,samples,oldProposal,work)) throw std::runtime_error("旧支持投影未知");
                // 平方根及缩放都向外扩一格，只有同一整数格才声明旧最大值的量化结果
                const double lo=std::floor(std::nextafter(std::nextafter(std::sqrt(oldProposal.ErrorLower),-INFINITY)*1e6,-INFINITY));
                const double hi=std::floor(std::nextafter(std::nextafter(std::sqrt(oldProposal.ErrorUpper),INFINITY)*1e6,INFINITY));
                row<<",\"oldErrorLowerPx\":"<<std::sqrt(oldProposal.ErrorLower)<<",\"oldErrorUpperPx\":"<<std::sqrt(oldProposal.ErrorUpper);
                if (lo!=hi || hi>static_cast<double>(std::numeric_limits<std::int64_t>::max()) || lo<10000)
                    row<<",\"qualityStatus\":\"target_numeric_unknown\"";
                else
                {
                    const auto target=static_cast<std::int64_t>(lo)-10000;row<<",\"targetMicropixels\":"<<target;
                    if (proposal.Free.empty()) { row<<",\"variants\":[";Variant(row,"fixed",state,samples,proposal,target,future,witness,work);row<<']'; }
                    else
                    {
                        const double h0=proposal.Points.at(proposal.NewVertex).Height,source=Source(samples,state.Config(),proposal.Points.at(proposal.NewVertex));
                        const auto model=Model(state,samples,proposal,work);
                        for (const auto& s : model) modelFile<<name<<','<<s.Sample<<','<<s.Weight<<','<<s.Difference<<','<<s.Factor<<','<<s.ClipW<<','<<s.WDerivative<<','<<s.NearValue<<','<<s.NearDerivative<<'\n';
                        const auto interval=TransactionalFitCounterfactual::Query(model,static_cast<double>(target)/1e6,-state.Config().HeightScale-h0,2*state.Config().HeightScale-h0,work);
                        row<<",\"initialHeight\":"<<h0<<",\"sourceHeight\":"<<source<<",\"modelStatus\":"
                           <<std::quoted(interval.Status==FitQueryStatus::Feasible ? "feasible" : interval.Status==FitQueryStatus::Empty ? "model_empty" : "numeric_unknown")
                           <<",\"lowerDelta\":"<<interval.Lower<<",\"upperDelta\":"<<interval.Upper<<",\"variants\":[";
                        auto sourceProposal=proposal;sourceProposal.Points.at(proposal.NewVertex).Height=source;Variant(row,"source",state,samples,sourceProposal,target,future,witness,work);
                        if (interval.Status==FitQueryStatus::Feasible)
                        {
                            proposal.Points.at(proposal.NewVertex).Height=h0+std::clamp(source-h0,interval.Lower,interval.Upper);
                            if (proposal.Points.at(proposal.NewVertex).Height!=source)
                            { row<<',';Variant(row,"source_projected",state,samples,proposal,target,future,witness,work); }
                        }
                        row<<"],\"projectedReusesSource\":"<<(interval.Status==FitQueryStatus::Feasible && proposal.Points.at(proposal.NewVertex).Height==source ? "true" : "false");
                    }
                }
            }
            row<<",\"seconds\":"<<Seconds(stage)<<'}';records<<row.str()<<'\n';records.flush();
        };

        // 原目录单独保留，包括超出新搜索域的既有H提案；只复核已知失败。
        auto originals=TransactionalProposals::Receivers(state,samples,root);
        if (originals.size()!=7) throw std::runtime_error("冻结原目录数量改变");
        for (std::size_t i=0;i<originals.size();++i)
        {
            auto p=originals[i];auto fitted=p;const auto reason=TransactionalCertification::Fit(state,samples,fitted,work);
            if (reason!="shape_infeasible") throw std::runtime_error("原目录形状阻塞未复现");
            emit("original-"+std::to_string(i)+"-"+p.Kind,Read(state,p.Support),{p.Points,p.Faces,p.NewVertex},p,reason);
        }
        std::vector<std::pair<std::string,RecoveryGeometry>> bases{{"unflipped",original}};
        for (const auto& edge : rootEdges)
        {
            const auto name="flip-"+std::to_string(edge[0])+"-"+std::to_string(edge[1]);auto next=Flip(original,edge);
            std::string reject;
            if (!next) reject="nonconvex_or_existing_diagonal";
            else for (const auto& newEdge : Edges(*next))
                if (!Edges(original).contains(newEdge) && state.Edges().contains(newEdge)) reject="external_edge_exists";
            if (!reject.empty()) records<<"{\"name\":"<<std::quoted(name)<<",\"generationReject\":"<<std::quoted(reject)<<"}\n";
            else bases.emplace_back(name,std::move(*next));
        }
        // F-witness在生成新连接前冻结，不能根据新候选的收益重新挑位置。
        Slot worst=InvalidSlot;
        for (auto sid : samples.FaceSamples(root)) if (samples.Projection(sid).Visible &&
            (worst==InvalidSlot || samples.Projection(sid).ErrorSquared>samples.Projection(worst).ErrorSquared)) worst=sid;
        Identity nextId=state.NextVertexId()-1;
        const auto submit=[&](const std::string& name,RecoveryGeometry geometry) {
            // 去重按连接和新点坐标，不让每次分配的临时身份制造重复几何
            std::vector<Face> keys;
            for (auto f : geometry.Faces)
            { if (geometry.NewVertex) for (auto& id : f) if (id==*geometry.NewVertex) id=std::numeric_limits<Identity>::min();keys.push_back(Key(f)); }
            std::sort(keys.begin(),keys.end());
            const auto point=geometry.NewVertex ? geometry.Points.at(*geometry.NewVertex) : Point{};
            if (!seen.emplace(keys,point.U,point.V).second) return;
            auto p=Difference(state,samples,domain,geometry,root);if (p.Support.empty()) return;
            emit(name,original,geometry,p,{});
        };
        for (const auto& [name,g] : bases)
        {
            if (name!="unflipped") submit(name,g);
            for (std::size_t f=0;f<g.Faces.size();++f) if (Inside(g,f,witness))
            {
                const auto faceKey=Key(g.Faces[f]);const auto tag=name+"-face-"+std::to_string(faceKey[0])+"-"+std::to_string(faceKey[1])+"-"+std::to_string(faceKey[2]);
                for (std::size_t e=0;e<3;++e)
                {
                    const auto edge=EdgeKey(g.Faces[f][e],g.Faces[f][(e+1)%3]);
                    if (auto next=SplitEdge(g,edge,nextId--)) submit(tag+"-E-"+std::to_string(e),std::move(*next));
                }
                const auto a=g.Points.at(g.Faces[f][0]),b=g.Points.at(g.Faces[f][1]),c=g.Points.at(g.Faces[f][2]);
                const Point center{((a.U+b.U)+c.U)/3,((a.V+b.V)+c.V)/3,0};
                if (auto next=SplitFace(g,f,center,nextId--)) submit(tag+"-F-center",std::move(*next));
                if (worst!=InvalidSlot)
                    if (auto next=SplitFace(g,f,samples.Parameter(worst),nextId--)) submit(tag+"-F-witness",std::move(*next));
            }
        }
    }
    catch (const std::exception& e)
    {
        detail=e.what();status=detail=="candidate_cap" ? "candidate_cap" : Clock::now()>work.Deadline || work.SampleTouches>work.VisitLimit ? "budget_exhausted" : "input_or_numeric_unknown";
    }
    std::ofstream summary(output/"recovery-summary.json");summary.exceptions(std::ios::badbit|std::ios::failbit);
    summary<<std::setprecision(17)<<"{\"status\":"<<std::quoted(status)<<",\"detail\":"<<std::quoted(detail)<<",\"seconds\":"<<Seconds(start)
       <<",\"geometries\":"<<count<<",\"shapeEligible\":"<<shapeCount<<",\"sampleTouches\":"<<work.SampleTouches
       <<",\"locationTests\":"<<work.LocationTests<<",\"constraints\":"<<work.Constraints<<",\"filterChecks\":"<<work.FilterChecks
       <<",\"exactChecks\":"<<work.ExactChecks<<",\"evidenceBytes\":"<<work.EvidenceBytes<<"}\n";
}
}
