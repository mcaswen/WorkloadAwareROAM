#include "experiment/greedy_transactional_lod/TransactionalProposals.h"
#include "experiment/greedy_transactional_lod/TransactionalPredicates.h"
#include "experiment/greedy_transactional_lod/TransactionalCertification.h"

#include <algorithm>
#include <stdexcept>

namespace ParallelRoam::Experiment::GreedyTransactionalLod
{
namespace
{
using Predicates=TransactionalPredicates;

Proposal Prepare(const TransactionalState& state,const TransactionalSamples& samples,Slot root,
    char kind,std::vector<Slot> support,const Point& location,Identity oldCenter,std::size_t ordinal)
{
    Proposal result;
    result.Root=root;result.Kind=kind;result.Center=oldCenter;
    std::sort(support.begin(),support.end());result.Support=std::move(support);
    // 仅复制有界补丁几何供私有拟合，不复制当前活动网格
    for (auto slot : result.Support) for (auto id : state.Face(slot).Vertices)
        result.Points.emplace(id,state.Vertex(id).Geometry);
    result.NewVertex=state.NextVertexId()-static_cast<Identity>(root)*8-static_cast<Identity>(ordinal);
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
    result.Free=kind=='H' ? std::vector<Identity>{oldCenter,result.NewVertex} : std::vector<Identity>{result.NewVertex};
    result.Samples=samples.VisibleSupport(result.Support);
    return result;
}
}

std::vector<Proposal> TransactionalProposals::Receivers(const TransactionalState& state,const TransactionalSamples& samples,Slot root)
{
    std::vector<Proposal> result;const auto& face=state.Face(root).Vertices;
    std::array<Edge,3> edges{EdgeKey(face[0],face[1]),EdgeKey(face[1],face[2]),EdgeKey(face[2],face[0])};
    std::sort(edges.begin(),edges.end());
    // 目录顺序固定在快照几何上，不受 donor 可用性或运行线程影响
    for (const auto& edge : edges)
    {
        const auto& uses=state.Edges().at(edge);
        if (uses.Count!=2) continue;
        const auto a=state.Vertex(edge[0]).Geometry,b=state.Vertex(edge[1]).Geometry;
        const Point midpoint{(a.U+b.U)*.5,(a.V+b.V)*.5,0};
        result.push_back(Prepare(state,samples,root,'E',{uses.Faces[0],uses.Faces[1]},midpoint,0,result.size()));
    }
    const auto a=state.Vertex(face[0]).Geometry,b=state.Vertex(face[1]).Geometry,c=state.Vertex(face[2]).Geometry;
    const Point center{((a.U+b.U)+c.U)/3,((a.V+b.V)+c.V)/3,0};
    Slot witness=InvalidSlot;
    for (auto sid : samples.FaceSamples(root))
        if (samples.Values()[sid].Visible && (witness==InvalidSlot || samples.Values()[sid].ErrorSquared>samples.Values()[witness].ErrorSquared ||
            (samples.Values()[sid].ErrorSquared==samples.Values()[witness].ErrorSquared && sid<witness))) witness=sid;
    std::vector<Point> locations;
    if (witness!=InvalidSlot)
    {
        const auto point=samples.Parameter(witness);
        // 舍入后的样本位置必须严格在面内；边界不能误走面内插点原语
        if (samples.StrictlyInside(witness,a,b,c) && Predicates::Orientation(a,b,point)>0 &&
            Predicates::Orientation(b,c,point)>0 && Predicates::Orientation(c,a,point)>0)
            locations.push_back(point);
    }
    if (locations.empty() || locations[0].U!=center.U || locations[0].V!=center.V) locations.push_back(center);
    for (const auto& point : locations) result.push_back(Prepare(state,samples,root,'F',{root},point,0,result.size()));
    auto vertices=face;std::sort(vertices.begin(),vertices.end());
    for (auto id : vertices)
        if (!state.IsBoundary(id)) result.push_back(Prepare(state,samples,root,'H',state.Vertex(id).Incident,center,id,result.size()));
    if (result.size()>8) throw std::runtime_error("接收目录超过冻结上限");
    return result;
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
