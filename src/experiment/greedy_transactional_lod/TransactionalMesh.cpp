#include "experiment/greedy_transactional_lod/TransactionalMesh.h"
#include "profiling/CpuProfiling.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ParallelRoam::Experiment::GreedyTransactionalLod
{
namespace
{
std::vector<MeshRange> Ranges(const std::vector<Slot>& blocks,std::size_t elements)
{
    // 连续物理面块可以合并上传，索引截短后的尾块不再属于有效范围
    std::vector<MeshRange> result;
    for (auto block : blocks)
    {
        const auto first=block*3;
        if (first>=elements) continue;
        if (!result.empty() && result.back().First+result.back().Count==first) result.back().Count+=3;
        else result.push_back({first,3});
    }
    return result;
}
}

std::array<Terrain::TerrainMeshVertex,3> TransactionalMesh::Build(const Configuration& config,const std::array<Point,3>& p)
{
    std::array<Terrain::TerrainMeshVertex,3> result;
    std::array<glm::dvec3,3> positions{};
    for (std::size_t i=0;i<3;++i)
        positions[i]={(p[i].U-.5)*config.TerrainSize,p[i].Height,(p[i].V-.5)*config.TerrainSize};
    // 参数域正向三角形在 x/z 平面朝下，反向叉积保持朝上的几何法线
    const auto cross=glm::cross(positions[2]-positions[0],positions[1]-positions[0]);
    const auto length=glm::length(cross);
    if (!(length>0) || !std::isfinite(length)) throw std::runtime_error("输出面法线不可表示");
    for (std::size_t i=0;i<3;++i)
    {
        result[i].Position=glm::vec3(positions[i]);result[i].Normal=glm::vec3(cross/length);
        // 核心 binary64 合法仍不保证公共 float 输出有限，转换在准备期核查
        for (glm::length_t axis=0;axis<3;++axis)
            if (!std::isfinite(result[i].Position[axis])) throw std::runtime_error("输出位置超出 float 范围");
        result[i].TexCoord={static_cast<float>(p[i].U),static_cast<float>(p[i].V)};
        result[i].Height=static_cast<float>(p[i].Height);
    }
    return result;
}

void TransactionalMesh::Initialize(const TransactionalState& state,WorkLedger& work)
{
    if (state.Faces().size()>InvalidSlot/3) throw std::runtime_error("输出槽范围溢出");
    _data.Vertices.resize(state.Faces().size()*3);_data.Indices.resize(state.FaceCount()*3);
    // 顶点按槽持久保存，活动索引负责避开空槽，不强制搬动整段顶点
    _data.TerrainSize=static_cast<float>(state.Config().TerrainSize);
    _data.HeightScale=static_cast<float>(state.Config().HeightScale);
    for (std::size_t position=0;position<state.FaceCount();++position)
    {
        const auto slot=state.ActiveFaces()[position];const auto& f=state.Face(slot).Vertices;
        const auto block=Build(state.Config(),{state.Vertex(f[0]).Geometry,state.Vertex(f[1]).Geometry,state.Vertex(f[2]).Geometry});
        std::copy(block.begin(),block.end(),_data.Vertices.begin()+slot*3);
        for (Slot i=0;i<3;++i) _data.Indices[position*3+i]=slot*3+i;
    }
    work.MeshVertices+=state.FaceCount()*3;work.MeshIndices+=state.FaceCount()*3;_generation=state.Version();
}

PreparedMesh TransactionalMesh::Prepare(const TransactionalState& state,const PreparedTopology& target,WorkLedger& work,
    const TransactionalExecution& execution)
{
    ROAM_CPU_ZONE("gtp.mesh.prepare");
    PreparedMesh result;
    if (target.FinalFaceSlots>InvalidSlot/3) throw std::runtime_error("输出槽范围溢出");
    result.VertexCount=target.FinalFaceSlots*3;result.IndexCount=target.FinalActiveCount*3;
    result.PendingVertices=_pendingVertices;result.PendingIndices=_pendingIndices;
    result.Vertices.resize(target.Faces.size());
    // 先填写私有独占块，法线或数值异常不会留下半份 live 输出
    execution.Run("mesh_fill",target.Faces.size(),work,[&](auto first,auto last,WorkLedger&) {
        for (auto index=first;index<last;++index)
        {
            const auto& [slot,record]=target.Faces[index];const auto& f=record.Geometry.Vertices;
            result.Vertices[index]={slot,Build(state.Config(),{target.Geometry(state,f[0]),target.Geometry(state,f[1]),target.Geometry(state,f[2])})};
        }
    });
    for (const auto& item : target.Faces) result.PendingVertices.push_back(item.first);
    result.Indices=target.ActiveWrites;
    // 活动尾部交换来自同一拓扑准备记录，输出组件不另做一遍选择
    for (const auto& [position,slot] : result.Indices)
    {
        static_cast<void>(slot);result.PendingIndices.push_back(position);
    }
    const auto compact=[](auto& blocks) {
        std::sort(blocks.begin(),blocks.end());blocks.erase(std::unique(blocks.begin(),blocks.end()),blocks.end());
    };
    compact(result.PendingVertices);compact(result.PendingIndices);
    // 未消费集合也计入成本，不能把长期 Pending 当作免费的固定大小状态
    work.PendingBlocks+=result.PendingVertices.size()+result.PendingIndices.size();
    work.MeshVertices+=result.Vertices.size()*3;work.MeshIndices+=result.Indices.size()*3;
    work.Reserve(_data.Vertices,result.VertexCount);work.Reserve(_data.Indices,result.IndexCount);
    return result;
}

void TransactionalMesh::Publish(PreparedMesh&& prepared,std::uint64_t generation) noexcept
{
    ROAM_CPU_ZONE("gtp.mesh.publish");
    // 替换 Pending 而非清空，未消费的前几轮写入仍需交给消费者
    _data.Vertices.resize(prepared.VertexCount);_data.Indices.resize(prepared.IndexCount);
    for (const auto& [slot,block] : prepared.Vertices)
        std::copy(block.begin(),block.end(),_data.Vertices.begin()+slot*3);
    for (const auto& [position,slot] : prepared.Indices)
        for (Slot i=0;i<3;++i) _data.Indices[position*3+i]=slot*3+i;
    _pendingVertices=std::move(prepared.PendingVertices);_pendingIndices=std::move(prepared.PendingIndices);
    _generation=generation;
}

MeshConsumption TransactionalMesh::Consume()
{
    ROAM_CPU_ZONE("gtp.mesh.consume");
    // 先构造独立区间，分配失败时旧 Pending 仍然完整
    MeshConsumption result;result.Data=&_data;result.Generation=_generation;result.Full=_full;
    if (_full)
    {
        result.Vertices.push_back({0,static_cast<Slot>(_data.Vertices.size())});
        result.Indices.push_back({0,static_cast<Slot>(_data.Indices.size())});
    }
    else { result.Vertices=Ranges(_pendingVertices,_data.Vertices.size());result.Indices=Ranges(_pendingIndices,_data.Indices.size()); }
    _pendingVertices.clear();_pendingIndices.clear();_full=false;
    return result;
}
}
