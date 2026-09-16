#pragma once

#include "algorithms/greedy_transactional_lod/TransactionalTypes.h"

namespace ParallelRoam::Algorithms::GreedyTransactionalLod
{
class TransactionalCommit;

/// <summary>
/// 顶点的局部续接记录，邻面列表随局部发布替换而不全域重建
/// </summary>
struct VertexRecord
{
    Identity Id{};
    Point Geometry;
    std::vector<Slot> Incident;
    bool Active{true};
    // 由边关联初建；单侧细分的新点在准备发布时显式恢复
    bool Boundary{};
};

/// <summary>
/// 活动面槽保存稠密活动数组中的反向位置，移除只修补尾部
/// </summary>
struct FaceRecord
{
    Triangle Geometry;
    Slot ActivePosition{InvalidSlot};
};

/// <summary>
/// 无向边最多两个面；方向一致性由独立验证器检查
/// </summary>
struct EdgeRecord
{
    std::array<Slot, 2> Faces{InvalidSlot, InvalidSlot};
    std::size_t Count{};
};

/// <summary>
/// 独立一般网格的唯一可写状态；策略和认证只借用只读访问
/// 局部写权限限于已封闭批次的提交组件
/// </summary>
class TransactionalState
{
public:
    explicit TransactionalState(const InitialMesh& input);
    const Configuration& Config() const { return _config; }
    const std::vector<VertexRecord>& Vertices() const { return _vertices; }
    const std::vector<FaceRecord>& Faces() const { return _faces; }
    const std::vector<Slot>& ActiveFaces() const { return _activeFaces; }
    const std::map<Edge, EdgeRecord>& Edges() const { return _edges; }
    const VertexRecord& Vertex(Identity id) const;
    Slot VertexSlot(Identity id) const { return _vertexIndex.at(id); }
    const Triangle& Face(Slot slot) const;
    bool IsBoundary(Identity id) const;
    std::uint64_t Version() const { return _version; }
    Identity NextVertexId() const { return _nextVertexId; }
    std::size_t FaceCount() const { return _activeFaces.size(); }

private:
    friend class TransactionalCommit;
    friend class TransactionalPipeline;
    Configuration _config;
    std::vector<VertexRecord> _vertices;
    std::vector<FaceRecord> _faces;
    std::vector<Slot> _activeFaces, _freeFaces, _freeVertices;
    std::map<Identity, Slot> _vertexIndex;
    std::map<Edge, EdgeRecord> _edges;
    std::uint64_t _version{1};
    Identity _nextVertexId{-1}, _nextFaceId{-1};
};
}
