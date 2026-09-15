#include "experiment/greedy_transactional_lod/TransactionalQualityProvenance.h"
#include "algorithms/greedy_transactional_lod/TransactionalProposals.h"
#include "algorithms/greedy_transactional_lod/TransactionalReservation.h"
#include "algorithms/greedy_transactional_lod/TransactionalCertification.h"
#include "algorithms/greedy_transactional_lod/TransactionalPredicates.h"
#include "algorithms/greedy_transactional_lod/TransactionalProposalEvidence.h"
#include <iomanip>
#include <optional>
#include <set>

namespace ParallelRoam::Experiment::GreedyTransactionalLod
{
namespace
{
using namespace Algorithms::GreedyTransactionalLod;
using Predicates=TransactionalPredicates;
constexpr std::array<Point,2> Witnesses{{{.96336996555328369,.7967032790184021,0},
    {.020757021382451057,.11965811997652054,0}}};

/// <summary>
/// 定位始终使用参数域中的同一个点，覆盖并列时选最小逻辑面身份
/// 不把旧物理槽或离线评价器样本编号当作跨帧身份
/// </summary>
Slot Owner(const TransactionalState& state,const Point& q)
{
    Slot result=InvalidSlot;
    for (auto slot : state.ActiveFaces())
    {
        const auto& f=state.Face(slot);
        if (Predicates::Contains(q,state.Vertex(f.Vertices[0]).Geometry,
            state.Vertex(f.Vertices[1]).Geometry,state.Vertex(f.Vertices[2]).Geometry) &&
            (result==InvalidSlot || f.Id<state.Face(result).Id)) result=slot;
    }
    if (result==InvalidSlot) throw std::runtime_error("见证缺少参数域覆盖");
    return result;
}

double Interpolate(const Point& q,const std::array<Point,3>& p)
{
    const auto w=Predicates::Barycentric(q,p[0],p[1],p[2]);
    return (w[0]*p[0].Height+w[1]*p[1].Height)+w[2]*p[2].Height;
}
double CurrentHeight(const TransactionalState& state,const Point& q,Slot slot)
{
    const auto& f=state.Face(slot).Vertices;
    return Interpolate(q,{state.Vertex(f[0]).Geometry,state.Vertex(f[1]).Geometry,state.Vertex(f[2]).Geometry});
}
std::optional<double> ReplacementHeight(const Proposal& p,const Point& q)
{
    for (const auto& f : p.Faces)
    {
        const std::array<Point,3> points{p.Points.at(f[0]),p.Points.at(f[1]),p.Points.at(f[2])};
        if (Predicates::Contains(q,points[0],points[1],points[2])) return Interpolate(q,points);
    }
    return {};
}

double ReferenceHeight(const TransactionalSamples& samples,const Point& q,double scale)
{
    // 固定离线 UV 未必属于核心采样集合，直接从原始高度样本求双线性值
    const auto& source=samples.Source();
    const double x=q.U*(source.Width-1),y=q.V*(source.Height-1);
    const auto ix=std::min(static_cast<std::uint32_t>(x),source.Width-2),iy=std::min(static_cast<std::uint32_t>(y),source.Height-2);
    const double tx=x-ix,ty=y-iy;
    const auto base=static_cast<std::size_t>(iy)*source.Width+ix;
    const double low=source.Values[base]*(1-tx)+source.Values[base+1]*tx;
    const double high=source.Values[base+source.Width]*(1-tx)+source.Values[base+source.Width+1]*tx;
    return ((1-ty)*low+ty*high)*scale/65535;
}

bool Visible(const Configuration& c,const Point& q,double reference)
{
    const auto p=TransactionalSamples::Clip(c,q.U,q.V,reference);
    return p[3]>0 && p[0]>=-p[3] && p[0]<=p[3] && p[1]>=-p[3] && p[1]<=p[3] &&
        p[2]>=(c.UsesZeroToOneDepth ? 0 : -p[3]) && p[2]<=p[3];
}
std::optional<double> Error(const Configuration& c,const Point& q,double reference,double height)
{
    // 可投影但不可见的误差仍输出，由 visible 单列其接受域；无投影意义才写 null
    const auto r=TransactionalSamples::Clip(c,q.U,q.V,reference),m=TransactionalSamples::Clip(c,q.U,q.V,height);
    if (r[3]<=0 || m[3]<=0 || m[2]<(c.UsesZeroToOneDepth ? 0 : -m[3])) return {};
    return std::hypot((m[0]/m[3]-r[0]/r[3])*c.Width*.5,(m[1]/m[3]-r[1]/r[3])*c.Height*.5);
}
void Number(std::ostream& out,const std::optional<double>& value)
{
    if (value) out<<*value;else out<<"null";
}
std::size_t Rank(const TransactionalState& state,const TransactionalSamples& samples,Slot slot)
{
    const double p=samples.PrioritySquared(slot),threshold=state.Config().SplitPixels*state.Config().SplitPixels;
    if (!std::isfinite(p) || p<=threshold) return 0;
    // 沿用生产全序，诊断全扫只求这个覆盖面的排名，不改排序索引
    const PriorityKey key{-p,state.Face(slot).Id,slot};std::size_t rank=1;
    for (auto other : state.ActiveFaces())
    {
        const double value=samples.PrioritySquared(other);
        if (std::isfinite(value) && value>threshold && PriorityKey{-value,state.Face(other).Id,other}<key) ++rank;
    }
    return rank;
}

/// <summary>
/// 只展开追加见证所覆盖的补丁，分离连接变化与新点拟合的几何作用
/// 未拟合曲面是批前状态上的局部反事实，不参与认证、选择或实际发布
/// </summary>
void WritePatch(std::ostream& out,const TransactionalState& state,const TransactionalSamples& samples,
    const Proposal& proposal,const Point& q)
{
    out<<",\"geometry\":{\"oldFaces\":[";
    for (std::size_t i=0;i<proposal.Support.size();++i)
    {
        if (i) out<<',';
        const auto& face=state.Face(proposal.Support[i]);out<<'['<<face.Id;
        for (auto id : face.Vertices)
        {
            const auto& p=state.Vertex(id).Geometry;
            out<<",["<<id<<','<<p.U<<','<<p.V<<','<<p.Height<<']';
        }
        out<<']';
    }
    out<<"],\"newFaces\":[";
    for (std::size_t i=0;i<proposal.Faces.size();++i)
    {
        if (i) out<<',';const auto& f=proposal.Faces[i];
        out<<'['<<f[0]<<','<<f[1]<<','<<f[2]<<']';
    }
    out<<"],\"pointReferences\":[";bool first=true;
    for (const auto& [id,p] : proposal.Points)
    {
        if (!first) out<<',';first=false;
        out<<'['<<id<<','<<ReferenceHeight(samples,p,state.Config().HeightScale)<<']';
    }
    out<<"],\"removedPoint\":";
    if (proposal.Kind=='D')
    {
        const auto& p=state.Vertex(proposal.Center).Geometry;
        out<<'['<<proposal.Center<<','<<p.U<<','<<p.V<<','<<p.Height<<','
            <<ReferenceHeight(samples,p,state.Config().HeightScale)<<']';
    }
    else out<<"null";
    out<<",\"newPoint\":";
    auto unfitted=proposal;
    if (proposal.Kind!='D')
    {
        const auto& p=proposal.Points.at(proposal.NewVertex);
        const double initial=CurrentHeight(state,p,proposal.Root);
        out<<"{\"id\":"<<proposal.NewVertex<<",\"initialHeight\":"<<initial
            <<",\"fittedHeight\":"<<p.Height<<",\"referenceHeight\":"
            <<ReferenceHeight(samples,p,state.Config().HeightScale)<<'}';
        unfitted.Points.at(proposal.NewVertex).Height=initial;
    }
    else out<<"null";
    out<<",\"unfittedWitnessHeight\":";Number(out,ReplacementHeight(unfitted,q));
    // 全补丁样本与当前可见认证集合分别计数，避免把离屏缺约束误写成样本缺失
    std::set<Slot> closed;
    for (auto face : proposal.Support)
        closed.insert(samples.FaceSamples(face).begin(),samples.FaceSamples(face).end());
    double oldMax=0,nearestDistance=INFINITY;Slot nearest=InvalidSlot;
    for (auto sid : proposal.Samples)
    {
        oldMax=std::max(oldMax,samples.Projection(sid).ErrorSquared);
        const auto p=samples.Parameter(sid);const double distance=std::hypot(p.U-q.U,p.V-q.V);
        if (distance<nearestDistance) { nearestDistance=distance;nearest=sid; }
    }
    out<<",\"closedSampleCount\":"<<closed.size()<<",\"oldVisibleMaxPx\":"<<std::sqrt(oldMax)
        <<",\"nearestVisibleSample\":";
    if (nearest!=InvalidSlot)
    {
        const auto p=samples.Parameter(nearest);
        out<<'['<<nearest<<','<<p.U<<','<<p.V<<','<<nearestDistance<<']';
    }
    else out<<"null";
    out<<'}';
}

/// <summary>
/// 只为已入前缀但未执行的见证根补做可行性诊断
/// 保留独立账本，不把额外回收认证写回正常批次
/// </summary>
void Recovery(std::ostream& out,std::size_t frame,std::size_t witness,const TransactionalState& state,
    const TransactionalSamples& samples,const CertifiedBatch& batch,Slot root)
{
    const auto id=state.Face(root).Id;
    const auto found=std::find(batch.IntentIds.begin(),batch.IntentIds.end(),id);
    out<<"{\"frame\":"<<frame<<",\"witness\":"<<witness<<",\"root\":"<<id;
    if (found==batch.IntentIds.end()) { out<<",\"stage\":\"outside_prefix\"}\n";return; }
    const auto index=static_cast<std::size_t>(found-batch.IntentIds.begin());
    out<<",\"stage\":\""<<batch.IntentResults[index]<<"\",\"attempts\":[";
    bool first=true;
    for (const auto& [kind,reason] : batch.Attempts[index])
    { if (!first) out<<',';first=false;out<<"[\""<<kind<<"\",\""<<reason<<"\"]"; }
    out<<']';
    const auto chosen=std::find_if(batch.Exchanges.begin(),batch.Exchanges.end(),[&](const auto& e) { return e.Receiver.Root==root; });
    if (chosen!=batch.Exchanges.end()) { out<<",\"selected\":true}\n";return; }
    if (batch.IntentResults[index]!="certified") { out<<",\"selected\":false}\n";return; }

    WorkLedger work;work.VisitLimit=100000000;
    work.Deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
    ReceiverCursor cursor(state,samples,root);std::optional<Proposal> receiver;
    while (auto next=cursor.Next(&work))
        if (TransactionalCertification::Fit(state,samples,*next,work)=="certified") { receiver=std::move(next);break; }
    if (!receiver) throw std::runtime_error("见证根独立认证与正常决策不同");
    // 仅较高优先级的已批准成员占用资源，较低成员不能反过来解释本根被拒绝
    std::vector<TransactionFootprint> prior;std::set<Identity> used;
    for (const auto& e : batch.Exchanges)
    {
        const auto position=std::find(batch.IntentIds.begin(),batch.IntentIds.end(),state.Face(e.Receiver.Root).Id);
        if (position>=found) continue;
        prior.push_back(TransactionalReservation::Footprint(state,e.Receiver));
        if (e.HasDonor) { prior.push_back(TransactionalReservation::Footprint(state,e.Donor));used.insert(e.Donor.Center); }
    }
    const auto rf=TransactionalReservation::Footprint(state,*receiver);
    const auto blocked=[&](const TransactionFootprint& f) {
        return std::any_of(prior.begin(),prior.end(),[&](const auto& p) { return TransactionalReservation::Conflict(f,p); });
    };
    std::size_t ordinal=0;
    for (std::size_t i=0;i<index;++i) if (batch.IntentResults[i]=="certified") ++ordinal;
    if (ordinal<batch.AssignedCredits)
    { out<<",\"assignedCredit\":true,\"priorConflict\":"<<blocked(rf)<<"}\n";return; }
    // 本协议不开 HeightGuard；共同回收池的额外检查只用于解释已发生的拒绝
    std::size_t certified=0,quality=0,feasible=0,unused=0,available=0;
    for (auto center : batch.PoolIds)
    {
        auto donor=TransactionalProposals::Donor(state,samples,center,work);
        if (donor.Reason!="certified") continue;
        ++certified;
        if (!TransactionalCertification::Accepts(state,samples,donor,receiver->TargetMicropixels,work)) continue;
        ++quality;const auto df=TransactionalReservation::Footprint(state,donor);
        if (TransactionalReservation::Conflict(rf,df)) continue;
        ++feasible;if (used.contains(center)) continue;
        ++unused;if (!blocked(rf) && !blocked(df)) ++available;
    }
    out<<",\"selected\":false,\"donorCertified\":"<<certified<<",\"donorQuality\":"<<quality
        <<",\"internalFeasible\":"<<feasible<<",\"unused\":"<<unused<<",\"available\":"<<available<<"}\n";
    if (available) throw std::runtime_error("见证诊断发现未解释的可用交换");
}
}

TransactionalQualityProvenance::TransactionalQualityProvenance(const std::filesystem::path& output,
    const Config& future,const Config& returned,std::optional<Point> additionalWitness) : _future(future),_returned(returned),
    _witnesses(output/"witnesses.csv"),_transactions(output/"transactions.jsonl"),_recovery(output/"recovery.jsonl")
{
    _points.assign(Witnesses.begin(),Witnesses.end());
    if (additionalWitness)
    {
        const auto& p=*additionalWitness;
        if (!std::isfinite(p.U) || !std::isfinite(p.V) || p.U<0 || p.U>1 || p.V<0 || p.V>1)
            throw std::runtime_error("追加见证必须位于有限单位参数域");
        _points.push_back(p);
    }
    _expected.resize(_points.size());
    for (auto* file : {&_witnesses,&_transactions,&_recovery})
    { file->exceptions(std::ios::badbit|std::ios::failbit);*file<<std::setprecision(17); }
    _witnesses<<"frame,phase,witness,u,v,reference,height,heightResidual,visible,error,futureError,returnError,face,priority,rank\n";
}

void TransactionalQualityProvenance::Observe(std::size_t frame,const char* phase,const State& state,
    const Samples& samples,const Batch* batch)
{
    for (std::size_t i=0;i<_points.size();++i)
    {
        const auto& q=_points[i];const auto slot=Owner(state,q);
        const double ref=ReferenceHeight(samples,q,state.Config().HeightScale),h=CurrentHeight(state,q,slot);
        _witnesses<<frame<<','<<phase<<','<<i<<','<<q.U<<','<<q.V<<','<<ref<<','<<h<<','<<h-ref<<','
            <<Visible(state.Config(),q,ref)<<',';
        Number(_witnesses,Error(state.Config(),q,ref,h));_witnesses<<',';
        Number(_witnesses,Error(_future,q,ref,h));_witnesses<<',';
        Number(_witnesses,Error(_returned,q,ref,h));
        _witnesses<<','<<state.Face(slot).Id<<','<<std::sqrt(samples.PrioritySquared(slot))<<','<<Rank(state,samples,slot)<<'\n';
        if (batch) { _expected[i]=h;Recovery(_recovery,frame,i,state,samples,*batch,slot); }
        else if (std::abs(h-_expected[i])>1e-8)
            throw std::runtime_error("见证补丁预测与批后实际曲面不同");
    }
}

void TransactionalQualityProvenance::Before(std::size_t frame,const State& state,const Samples& samples,const Batch& batch)
{
    _survivors.clear();
    if (state.Config().PreserveSurvivingHeights)
        for (const auto& v : state.Vertices()) if (v.Active) _survivors.emplace(v.Id,v.Geometry.Height);
    Observe(frame,"before",state,samples,&batch);
    for (std::size_t index=0;index<batch.Exchanges.size();++index)
    {
        const auto& exchange=batch.Exchanges[index];
        // 每个局部预测都读同一个批前状态；批准集合无冲突，因此可与最终批后值核对
        for (const auto* p : {&exchange.Receiver,exchange.HasDonor ? &exchange.Donor : nullptr})
        {
            if (!p) continue;
            _transactions<<"{\"frame\":"<<frame<<",\"exchange\":"<<index<<",\"kind\":\""<<p->Kind
                <<"\",\"receiverRoot\":"<<state.Face(exchange.Receiver.Root).Id<<",\"center\":"<<p->Center
                <<",\"sampleCount\":"<<p->Samples.size()<<",\"targetPx\":"<<exchange.Receiver.TargetMicropixels/1e6
                <<",\"errorLowerSquared\":"<<p->ErrorLower<<",\"errorUpperSquared\":"<<p->ErrorUpper
                <<",\"free\":[";
            for (std::size_t j=0;j<p->Free.size();++j) { if (j) _transactions<<',';_transactions<<p->Free[j]; }
            _transactions<<"],\"freeVisibleSupport\":[";
            if (p->Kind!='D')
            {
                // 高度变量可能完全不参与当前可见样本，单独记录其系数覆盖
                WorkLedger local;TransactionalProposalEvidence evidence(samples,*p,local);
                for (std::size_t j=0;j<p->Free.size();++j)
                {
                    std::size_t nonzero=0;
                    for (auto sid : p->Samples)
                    {
                        const auto& entry=evidence.Get(sid,local);const auto& f=p->Faces[entry.Face];
                        double coefficient=0;
                        for (std::size_t k=0;k<3;++k) if (f[k]==p->Free[j]) coefficient+=entry.Weights[k];
                        if (coefficient!=0) ++nonzero;
                    }
                    if (j) _transactions<<',';
                    _transactions<<'['<<p->Free[j]<<','<<nonzero<<']';
                }
            }
            _transactions<<"],\"points\":[";bool first=true;
            for (const auto& [id,point] : p->Points)
            {
                if (!first) _transactions<<',';first=false;
                _transactions<<"["<<id<<','<<point.U<<','<<point.V<<',';
                if (id==p->NewVertex && p->Kind!='D') _transactions<<"null";else _transactions<<state.Vertex(id).Geometry.Height;
                _transactions<<','<<point.Height<<']';
            }
            _transactions<<"],\"support\":[";
            for (std::size_t j=0;j<p->Support.size();++j) { if (j) _transactions<<',';_transactions<<state.Face(p->Support[j]).Id; }
            _transactions<<"],\"witnesses\":[";first=true;
            for (std::size_t j=0;j<_points.size();++j)
            {
                const auto& q=_points[j];const auto next=ReplacementHeight(*p,q);
                if (!next) continue;
                if (!first) _transactions<<',';first=false;
                const double old=CurrentHeight(state,q,Owner(state,q)),ref=ReferenceHeight(samples,q,state.Config().HeightScale);
                _expected[j]=*next;
                _transactions<<"{\"id\":"<<j<<",\"heightBefore\":"<<old<<",\"heightAfter\":"<<*next
                    <<",\"visible\":"<<Visible(state.Config(),q,ref)<<",\"errorBefore\":";
                Number(_transactions,Error(state.Config(),q,ref,old));_transactions<<",\"errorAfter\":";
                Number(_transactions,Error(state.Config(),q,ref,*next));_transactions<<'}';
            }
            _transactions<<']';
            if (_points.size()>Witnesses.size() && ReplacementHeight(*p,_points.back()))
                WritePatch(_transactions,state,samples,*p,_points.back());
            _transactions<<"}\n";
        }
    }
}

void TransactionalQualityProvenance::After(std::size_t frame,const State& state,const Samples& samples)
{
    // 实际 float 网格由既有独立评价器核查；此处仅核对同批预测与内部曲面
    Observe(frame,"after",state,samples,nullptr);
    for (const auto& v : state.Vertices())
    {
        const auto old=_survivors.find(v.Id);
        if (v.Active && old!=_survivors.end() && old->second!=v.Geometry.Height)
            throw std::runtime_error("冻结策略改变了存活顶点高度");
    }
}
}
