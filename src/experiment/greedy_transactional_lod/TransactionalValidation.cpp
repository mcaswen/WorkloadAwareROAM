#include "experiment/greedy_transactional_lod/TransactionalValidation.h"
#include "algorithms/greedy_transactional_lod/TransactionalPredicates.h"

#include "algorithms/greedy_transactional_lod/TransactionalStateInvariant.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace ParallelRoam::Experiment::GreedyTransactionalLod
{
using namespace Algorithms::GreedyTransactionalLod;
namespace
{
void Require(bool condition,const char* message) { if (!condition) throw std::runtime_error(message); }
}

void TransactionalValidation::Validate(const TransactionalState& state)
{
    TransactionalStateInvariant::Validate(state);
}

bool TransactionalValidation::Equivalent(const TransactionalState& a,const TransactionalState& b)
{
    // 等价比较逻辑身份与几何；物理槽、容量和活动数组顺序不属于结果语义
    if (a.FaceCount()!=b.FaceCount() || a.Config().Budget!=b.Config().Budget || a.Version()!=b.Version()) return false;
    std::map<Identity,Point> av,bv;std::map<Identity,std::array<Identity,3>> af,bf;
    for (const auto& v : a.Vertices()) if (v.Active) av.emplace(v.Id,v.Geometry);
    for (const auto& v : b.Vertices()) if (v.Active) bv.emplace(v.Id,v.Geometry);
    for (auto slot : a.ActiveFaces()) af.emplace(a.Face(slot).Id,a.Face(slot).Vertices);
    for (auto slot : b.ActiveFaces()) bf.emplace(b.Face(slot).Id,b.Face(slot).Vertices);
    return av==bv && af==bf;
}

void TransactionalValidation::Samples(const TransactionalState& state,const TransactionalSamples& actual,WorkLedger& work)
{
    TransactionalSamples oracle(actual.Source());oracle.Refresh(state,work);
    Require(actual.Raw()==oracle.Raw(),"局部 priority 全序与全量 oracle 不同");
    Require(actual.DonorPool(state.Vertices().size())==oracle.DonorPool(state.Vertices().size()),"局部 donor 顺序与全量 oracle 不同");
    for (auto slot : state.ActiveFaces())
    {
        Require(actual.FaceSamples(slot)==oracle.FaceSamples(slot),"局部闭面贡献与全量 oracle 不同");
        Require(actual.PrioritySquared(slot)==oracle.PrioritySquared(slot),"局部 priority 值与全量 oracle 不同");
    }
    for (Slot sid=0;sid<actual.SampleCount();++sid)
    {
        const auto& a=actual.Value(sid);const auto& b=oracle.Value(sid);
        Require(a.Owner==b.Owner && a.Visible==b.Visible && a.ReferenceHeight==b.ReferenceHeight &&
            a.MeshHeight==b.MeshHeight && a.ErrorSquared==b.ErrorSquared && a.HeightError==b.HeightError,
            "局部样本评价与全量 oracle 不同");
    }
}

void TransactionalValidation::Mesh(const TransactionalState& state,const Terrain::TerrainMeshData& mesh)
{
    Require(mesh.Indices.size()==state.FaceCount()*3,"输出活动索引数量错误");
    for (std::size_t i=0;i<state.FaceCount();++i)
    {
        const auto slot=state.ActiveFaces()[i];const auto& f=state.Face(slot).Vertices;
        std::array<glm::dvec3,3> positions;
        for (Slot j=0;j<3;++j)
        {
            Require(mesh.Indices[i*3+j]==slot*3+j,"尾交换后的输出索引错误");
            const auto& p=state.Vertex(f[j]).Geometry;const auto& vertex=mesh.Vertices.at(slot*3+j);
            positions[j]={(p.U-.5)*state.Config().TerrainSize,p.Height,(p.V-.5)*state.Config().TerrainSize};
            Require(vertex.Position==glm::vec3(positions[j]) && vertex.Height==static_cast<float>(p.Height) &&
                vertex.TexCoord==glm::vec2(static_cast<float>(p.U),static_cast<float>(p.V)),"输出没有使用实际发布几何");
        }
        const auto n=glm::normalize(glm::cross(positions[2]-positions[0],positions[1]-positions[0]));
        for (Slot j=0;j<3;++j)
            Require(glm::length(mesh.Vertices[slot*3+j].Normal-glm::vec3(n))<1e-6F,"输出法线与当前面不符");
    }
}

void TransactionalValidation::Consume(const MeshConsumption& update,Terrain::TerrainMeshData& mirror)
{
    Require(update.Data!=nullptr,"输出借用为空");const auto& current=*update.Data;
    // 模拟只应用区间的消费者，保留未触及旧内容以暴露 Pending 遗漏
    mirror.Vertices.resize(current.Vertices.size());mirror.Indices.resize(current.Indices.size());
    const auto copy=[](const auto& source,auto& target,const auto& ranges) {
        for (const auto& range : ranges)
        {
            Require(range.First<=source.size() && range.Count<=source.size()-range.First,"输出脏区间越界");
            std::copy_n(source.begin()+range.First,range.Count,target.begin()+range.First);
        }
    };
    copy(current.Vertices,mirror.Vertices,update.Vertices);copy(current.Indices,mirror.Indices,update.Indices);
    Require(mirror.Indices==current.Indices,"增量索引消费遗漏");
    for (auto index : current.Indices)
    {
        const auto& a=mirror.Vertices[index];const auto& b=current.Vertices[index];
        Require(a.Position==b.Position && a.Normal==b.Normal && a.TexCoord==b.TexCoord && a.Height==b.Height &&
            a.DebugColor==b.DebugColor && a.DebugHighlight==b.DebugHighlight,"增量顶点消费遗漏");
    }
}
}
