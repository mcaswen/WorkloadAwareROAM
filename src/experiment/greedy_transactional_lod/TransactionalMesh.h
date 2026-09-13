#pragma once

#include "experiment/greedy_transactional_lod/TransactionalCommit.h"
#include "terrain/TerrainMeshBuilder.h"

namespace ParallelRoam::Experiment::GreedyTransactionalLod
{
/// <summary>
/// 独立拥有的脏区间描述，区间单位是输出元素而非字节
/// </summary>
struct MeshRange { Slot First{}, Count{}; };

/// <summary>
/// 消费返回当前借用与累积区间；借用在下一次成功发布或销毁前有效
/// </summary>
struct MeshConsumption
{
    const Terrain::TerrainMeshData* Data{};
    std::vector<MeshRange> Vertices, Indices;
    std::uint64_t Generation{};
    bool Full{};
};

/// <summary>
/// 局部输出写入与 Pending 合并在发布前完成，失败不丢失旧脏记录
/// </summary>
struct PreparedMesh
{
    std::vector<std::pair<Slot,std::array<Terrain::TerrainMeshVertex,3>>> Vertices;
    std::map<Slot,Slot> Indices;
    std::vector<Slot> PendingVertices, PendingIndices;
    std::size_t VertexCount{}, IndexCount{};
};

/// <summary>
/// 当前拟合几何的持久 CPU 输出，与拓扑选择及上传后端独立
/// 未消费记录跨轮合并，面槽复用不会改变其他输出顶点身份
/// </summary>
class TransactionalMesh
{
public:
    void Initialize(const TransactionalState& state,WorkLedger& work);
    PreparedMesh Prepare(const TransactionalState& state,const PreparedTopology& target,WorkLedger& work);
    void Publish(PreparedMesh&& prepared,std::uint64_t generation) noexcept;
    MeshConsumption Consume();
    const Terrain::TerrainMeshData& Data() const { return _data; }
private:
    static std::array<Terrain::TerrainMeshVertex,3> Build(const Configuration& config,const std::array<Point,3>& points);
    Terrain::TerrainMeshData _data;
    std::vector<Slot> _pendingVertices, _pendingIndices;
    std::uint64_t _generation{};
    bool _full{true};
};
}
