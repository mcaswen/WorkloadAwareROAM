#pragma once

#include "experiment/greedy_transactional_lod/TransactionalState.h"
#include "experiment/greedy_transactional_lod/TransactionalExecution.h"
#include <set>

namespace ParallelRoam::Experiment::GreedyTransactionalLod
{
/// <summary>
/// 同一封闭批次的局部目标记录，供样本和输出在发布前读取
/// 只拥有变化支持，不包含完整网格、候选或目标发现状态
/// </summary>
struct PreparedTopology
{
    std::uint64_t Version{};
    std::set<Slot> Removed;
    std::set<Identity> DeletedVertices;
    std::vector<Slot> FaceSlots, VertexSlots;
    std::map<Identity,Slot> AddedIndex;
    std::map<Identity,VertexRecord> Vertices;
    std::map<Edge,EdgeRecord> Edges;
    std::vector<std::pair<Slot,FaceRecord>> Faces;
    std::map<Slot,Slot> ActiveWrites;
    std::size_t UsedFreeFaces{}, AppendFaces{}, AssignedVertices{}, UsedFreeVertices{}, AppendVertices{};
    std::size_t FinalFaceSlots{}, FinalActiveCount{};
    Identity NextFace{}, NextVertex{};
    const Point& Geometry(const TransactionalState& old,Identity id) const;
};

/// <summary>
/// 直接物化封闭批次的局部记录，不重演细分历史或重新进行选择
/// 分配、校验和邻接合成在发布前完成，已发布代际只推进一次
/// </summary>
class TransactionalCommit
{
public:
    static PreparedTopology Prepare(TransactionalState& state,const CertifiedBatch& batch,WorkLedger& work,
        const TransactionalExecution& execution={});
    static void Publish(TransactionalState& state,PreparedTopology&& prepared,WorkLedger& work);
    static void Apply(TransactionalState& state,const CertifiedBatch& batch,WorkLedger& work);
};
}
