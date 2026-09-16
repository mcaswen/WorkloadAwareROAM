#include "algorithms/greedy_transactional_lod/TransactionalProposals.h"
#include "profiling/CpuProfiling.h"
#include "algorithms/greedy_transactional_lod/TransactionalPredicates.h"
#include "algorithms/greedy_transactional_lod/TransactionalCertification.h"
#include "algorithms/greedy_transactional_lod/TransactionalBoundaryRefinement.h"

#include <algorithm>
#include <stdexcept>

namespace ParallelRoam::Algorithms::GreedyTransactionalLod
{
namespace
{
using Predicates=TransactionalPredicates;

Identity ProposalIdentity(const TransactionalState& state, Slot root, std::size_t ordinal)
{
    if (ordinal >= 8)
    {
        throw std::runtime_error("接收目录超过冻结上限");
    }
    const auto offset = static_cast<Identity>(root) * 8 + static_cast<Identity>(ordinal);
    if (state.NextVertexId() <= std::numeric_limits<Identity>::min() + offset)
    {
        throw std::runtime_error("接收提案身份空间用尽");
    }
    return state.NextVertexId() - offset;
}

Proposal Prepare(const TransactionalState& state,const TransactionalSamples& samples,Slot root,
    char kind,std::vector<Slot> support,const Point& location,Identity oldCenter,std::size_t ordinal)
{
    Proposal result;
    result.Root=root;result.Kind=kind;result.Center=oldCenter;
    std::sort(support.begin(),support.end());result.Support=std::move(support);
    // 仅复制有界补丁几何供私有拟合，不复制当前活动网格
    for (auto slot : result.Support) for (auto id : state.Face(slot).Vertices)
        result.Points.emplace(id,state.Vertex(id).Geometry);
    result.NewVertex=ProposalIdentity(state,root,ordinal);
    // 所有提案的临时身份互异，失败提案不改变存活身份分配器
    const auto& original=state.Face(root).Vertices;
    const auto weights=Predicates::Barycentric(location,result.Points.at(original[0]),result.Points.at(original[1]),result.Points.at(original[2]));
    Point point=location;
    point.Height=(weights[0]*result.Points.at(original[0]).Height+weights[1]*result.Points.at(original[1]).Height)+
        weights[2]*result.Points.at(original[2]).Height;
    result.Points.emplace(result.NewVertex,point);
    for (auto slot : result.Support)
    {
        // H 包含未重新切分的相邻面，因为旧中心改高仍会改变这些面的曲面
        const auto& old=state.Face(slot).Vertices;
        if (kind=='E' || slot==root)
            for (std::size_t i=0;i<3;++i)
            {
                const auto a=old[i],b=old[(i+1)%3];
                if (kind!='E' || Predicates::Orientation(result.Points.at(a),result.Points.at(b),point)!=0)
                    result.Faces.push_back({a,b,result.NewVertex});
            }
        else result.Faces.push_back(old);
    }
    // 在构造时限制自由变量，使拟合、证书和写足迹使用同一高度策略
    // 保留完整 H 支持与目录身份，不在认证后回写旧高度
    result.Free=kind=='H' && !state.Config().PreserveSurvivingHeights ?
        std::vector<Identity>{oldCenter,result.NewVertex} : std::vector<Identity>{result.NewVertex};
    result.Samples=samples.VisibleSupport(result.Support);
    return result;
}
}

ReceiverCursor::ReceiverCursor(const TransactionalState& state,const TransactionalSamples& samples,Slot root)
    : _state(state),_samples(samples),_root(root)
{
    const auto& face=state.Face(root).Vertices;
    _edges={EdgeKey(face[0],face[1]),EdgeKey(face[1],face[2]),EdgeKey(face[2],face[0])};
    std::sort(_edges.begin(),_edges.end());_vertices=face;std::sort(_vertices.begin(),_vertices.end());
    const auto a=state.Vertex(face[0]).Geometry,b=state.Vertex(face[1]).Geometry,c=state.Vertex(face[2]).Geometry;
    _center={((a.U+b.U)+c.U)/3,((a.V+b.V)+c.V)/3,0};
}

std::optional<Proposal> ReceiverCursor::Next(WorkLedger* work)
{
    ROAM_CPU_ZONE("gtp.receivers");
    const auto prepare=[&](char kind,std::vector<Slot> support,const Point& location,Identity oldCenter) {
        if (_ordinal>=8) throw std::runtime_error("接收目录超过冻结上限");
        if (work) ++work->ReceiverConstructed;
        return Prepare(_state,_samples,_root,kind,std::move(support),location,oldCenter,_ordinal++);
    };
    while (_phase==0)
    {
        if (_position==_edges.size()) { _phase=1;_position=0;break; }
        const auto& edge=_edges[_position++];const auto& uses=_state.Edges().at(edge);
        if (uses.Count == 1 && _state.Config().EnableBoundaryRefinement)
        {
            const auto identity = ProposalIdentity(_state, _root, _ordinal++);
            if (work)
            {
                ++work->ReceiverConstructed;
            }
            return TransactionalBoundaryRefinement::Construct(_state, _samples.Source(), _root, edge, identity);
        }
        if (uses.Count!=2) continue;
        const auto a=_state.Vertex(edge[0]).Geometry,b=_state.Vertex(edge[1]).Geometry;
        return prepare('E',{uses.Faces[0],uses.Faces[1]},{(a.U+b.U)*.5,(a.V+b.V)*.5,0},0);
    }
    if (_phase==1)
    {
        if (!_locationsReady)
        {
            const auto& face=_state.Face(_root).Vertices;
            const auto a=_state.Vertex(face[0]).Geometry,b=_state.Vertex(face[1]).Geometry,c=_state.Vertex(face[2]).Geometry;
            Slot witness=InvalidSlot;
            for (auto sid : _samples.FaceSamples(_root))
                if (_samples.Projection(sid).Visible && (witness==InvalidSlot ||
                    _samples.Projection(sid).ErrorSquared>_samples.Projection(witness).ErrorSquared ||
                    (_samples.Projection(sid).ErrorSquared==_samples.Projection(witness).ErrorSquared && sid<witness))) witness=sid;
            if (witness!=InvalidSlot)
            {
                const auto point=_samples.Parameter(witness);
                if (_samples.StrictlyInside(witness,a,b,c) && Predicates::Orientation(a,b,point)>0 &&
                    Predicates::Orientation(b,c,point)>0 && Predicates::Orientation(c,a,point)>0) _locations.push_back(point);
            }
            if (_locations.empty() || _locations[0].U!=_center.U || _locations[0].V!=_center.V) _locations.push_back(_center);
            _locationsReady=true;
        }
        if (_position<_locations.size()) return prepare('F',{_root},_locations[_position++],0);
        _phase=2;_position=0;
    }
    while (_phase==2 && _position<_vertices.size())
    {
        const auto id=_vertices[_position++];
        if (!_state.IsBoundary(id)) return prepare('H',_state.Vertex(id).Incident,_center,id);
    }
    return {};
}

std::vector<Proposal> TransactionalProposals::Receivers(const TransactionalState& state,const TransactionalSamples& samples,Slot root)
{
    std::vector<Proposal> result;ReceiverCursor cursor(state,samples,root);
    while (auto proposal=cursor.Next()) result.push_back(std::move(*proposal));
    return result;
}

std::string TransactionalProposals::CertifyReceiver(const TransactionalState& state,
    const TransactionalSamples& samples, Proposal& proposal, WorkLedger& work)
{
    if (proposal.Kind == 'B')
    {
        return TransactionalBoundaryRefinement::Certify(state, samples, proposal, work);
    }
    return TransactionalCertification::Fit(state, samples, proposal, work);
}

bool TransactionalProposals::NeedsFlipRecovery(const std::vector<std::pair<char, std::string>>& attempts)
{
    bool hasOriginal = false;
    for (const auto& [kind, reason] : attempts)
    {
        if (kind == 'E' || kind == 'F' || kind == 'H')
        {
            hasOriginal = true;
            if (reason != "shape_infeasible")
            {
                return false;
            }
        }
    }
    return hasOriginal;
}

std::vector<Identity> TransactionalProposals::Ring(const TransactionalState& state,Identity center)
{
    std::map<Identity,Identity> following;
    // 围绕中心的有向对边恢复环，不能按邻点坐标排序猜测连接
    for (auto slot : state.Vertex(center).Incident)
    {
        const auto& face=state.Face(slot).Vertices;
        const auto it=std::find(face.begin(),face.end(),center);
        const auto pos=static_cast<std::size_t>(it-face.begin());
        if (pos==3 || !following.emplace(face[(pos+1)%3],face[(pos+2)%3]).second)
            throw std::runtime_error("内部点邻域不是单一有向环");
    }
    if (following.empty()) throw std::runtime_error("空邻域");
    std::vector<Identity> ring{following.begin()->first};
    while (following.at(ring.back())!=ring.front())
    {
        ring.push_back(following.at(ring.back()));
        if (ring.size()>following.size()) throw std::runtime_error("邻域环无法闭合");
    }
    if (ring.size()!=following.size()) throw std::runtime_error("邻域存在多个环");
    return ring;
}

Proposal TransactionalProposals::Donor(const TransactionalState& state,const TransactionalSamples& samples,
    Identity center,WorkLedger& work)
{
    ROAM_CPU_ZONE("gtp.donor");
    Proposal result;result.Kind='D';result.Center=center;result.Free={center};
    result.Support=state.Vertex(center).Incident;std::sort(result.Support.begin(),result.Support.end());
    auto ring=Ring(state,center);work.RingVisits+=ring.size();
    // 回收不改变环边界高度，质量代价仅来自固定边界内的重新剖分
    for (auto id : ring) result.Points.emplace(id,state.Vertex(id).Geometry);
    result.Samples=samples.VisibleSupport(result.Support);
    while (ring.size()>3)
    {
        bool found=false;auto ordered=ring;std::sort(ordered.begin(),ordered.end());
        for (auto id : ordered)
        {
            ++work.EarTests;
            const auto i=static_cast<std::size_t>(std::find(ring.begin(),ring.end(),id)-ring.begin());
            const std::array<Identity,3> f{ring[(i+ring.size()-1)%ring.size()],id,ring[(i+1)%ring.size()]};
            const auto a=result.Points.at(f[0]),b=result.Points.at(f[1]),c=result.Points.at(f[2]);
            if (!Predicates::Shape(a,b,c)) continue;
            // 闭耳内部或边上的其他点都会阻止该对角线，不能跨越保留边界
            if (std::any_of(ring.begin(),ring.end(),[&](Identity v) {
                return std::find(f.begin(),f.end(),v)==f.end() && Predicates::Contains(result.Points.at(v),a,b,c);
            })) continue;
            result.Faces.push_back(f);ring.erase(ring.begin()+static_cast<std::ptrdiff_t>(i));found=true;break;
        }
        // 固定耳切未找到解不是整个 one-ring 类别的数学不可行结论
        if (!found) { result.Reason="fast_shape_miss";return result; }
    }
    if (!Predicates::Shape(result.Points.at(ring[0]),result.Points.at(ring[1]),result.Points.at(ring[2])))
    { result.Reason="fast_shape_miss";return result; }
    result.Faces.push_back({ring[0],ring[1],ring[2]});
    result.Reason=TransactionalCertification::Measure(state,samples,result,work) ? "certified" : "projection_unknown";
    return result;
}
}
