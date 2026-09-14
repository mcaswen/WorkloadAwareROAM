#include "algorithms/greedy_transactional_lod/TransactionalStateInvariant.h"
#include "algorithms/greedy_transactional_lod/TransactionalPredicates.h"

#include <boost/multiprecision/cpp_int.hpp>
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace ParallelRoam::Algorithms::GreedyTransactionalLod
{
namespace
{
void Require(bool condition,const char* message) { if (!condition) throw std::runtime_error(message); }
}

void TransactionalStateInvariant::Validate(const TransactionalState& state)
{
    using R=boost::multiprecision::cpp_rational;
    Require(state.FaceCount()<=state.Config().Budget,"活动预算越界");
    std::map<Edge,std::vector<std::array<Identity,3>>> edges;
    // 从真实面重新推导连接，不能拿待测边缓存自证正确
    std::map<Identity,std::vector<Slot>> incident;
    std::set<Identity> faceIds;
    R area=0;
    for (auto slot : state.ActiveFaces())
    {
        const auto& face=state.Face(slot);const auto& f=face.Vertices;
        Require(faceIds.insert(face.Id).second,"活动面身份重复");
        Require(state.ActiveFaces().at(state.Faces().at(slot).ActivePosition)==slot,"活动数组反向位置错误");
        const auto a=state.Vertex(f[0]).Geometry,b=state.Vertex(f[1]).Geometry,c=state.Vertex(f[2]).Geometry;
        Require(TransactionalPredicates::Shape(a,b,c),"面方向或最小角不合法");
        area+=(R(b.U)-a.U)*(R(c.V)-a.V)-(R(b.V)-a.V)*(R(c.U)-a.U);
        for (std::size_t i=0;i<3;++i)
        {
            incident[f[i]].push_back(slot);edges[EdgeKey(f[i],f[(i+1)%3])].push_back({f[i],f[(i+1)%3],slot});
        }
    }
    Require(area==2,"参数域总面积不为单位方形");
    // 面向和精确总面积联合检查，避免局部重复面或遗漏区域被容差吞掉
    Require(edges.size()==state.Edges().size(),"存活边缓存数量错误");
    std::set<Identity> boundary;
    for (const auto& [key,uses] : edges)
    {
        Require(uses.size()==1 || uses.size()==2,"边的活动面数非法");
        const auto& stored=state.Edges().at(key);
        Require(stored.Count==uses.size(),"边邻接缓存错误");
        for (const auto& use : uses)
            Require(std::find(stored.Faces.begin(),stored.Faces.end(),static_cast<Slot>(use[2]))!=stored.Faces.end(),"边缓存遗漏面");
        if (uses.size()==2) Require(uses[0][0]==uses[1][1] && uses[0][1]==uses[1][0],"共享边方向错误");
        else
        {
            const auto a=state.Vertex(key[0]).Geometry,b=state.Vertex(key[1]).Geometry;
            Require((a.U==b.U && (a.U==0 || a.U==1)) || (a.V==b.V && (a.V==0 || a.V==1)),"内部存在开口边");
            boundary.insert(key.begin(),key.end());
        }
    }
    Require(incident.size()+state.FaceCount()==edges.size()+1,"Euler 特征错误");
    // 每个顶点还须满足单连通 link，仅有 Euler 关系不能排除夹点
    std::size_t vertices=0;
    for (const auto& vertex : state.Vertices())
    {
        if (!vertex.Active) continue;
        Require(vertex.Boundary==boundary.contains(vertex.Id),"边界缓存与实际单面边不一致");
        ++vertices;const auto& p=vertex.Geometry;
        Require(std::isfinite(p.U) && std::isfinite(p.V) && std::isfinite(p.Height) && p.U>=0 && p.U<=1 && p.V>=0 && p.V<=1 &&
            p.Height>=-state.Config().HeightScale && p.Height<=2*state.Config().HeightScale,"存活顶点超出声明域");
        auto actual=incident.at(vertex.Id),stored=vertex.Incident;std::sort(actual.begin(),actual.end());std::sort(stored.begin(),stored.end());
        Require(actual==stored,"点邻接缓存不等于实际面集合");
        std::map<Identity,std::set<Identity>> link;
        for (auto slot : actual)
        {
            std::vector<Identity> other;
            for (auto id : state.Face(slot).Vertices) if (id!=vertex.Id) other.push_back(id);
            Require(other.size()==2,"面存在重复顶点");link[other[0]].insert(other[1]);link[other[1]].insert(other[0]);
        }
        std::size_t ends=0;
        for (const auto& [id,next] : link)
        {
            static_cast<void>(id);Require(next.size()==1 || next.size()==2,"顶点 link 分叉");
            ends+=next.size()==1 ? 1U : 0U;
        }
        Require(boundary.contains(vertex.Id) ? ends==2 : (ends==0 && link.size()<=19),"顶点 link 或内部度数非法");
        std::set<Identity> seen;std::vector<Identity> todo{link.begin()->first};
        while (!todo.empty())
        {
            const auto id=todo.back();todo.pop_back();
            if (!seen.insert(id).second) continue;
            for (auto next : link.at(id)) if (!seen.contains(next)) todo.push_back(next);
        }
        Require(seen.size()==link.size(),"顶点 link 不连通");
    }
    Require(vertices==incident.size(),"孤立存活顶点");
}

}
