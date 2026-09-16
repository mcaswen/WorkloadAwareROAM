#include "algorithms/greedy_transactional_lod/TransactionalRenderBridge.h"

#include <stdexcept>

namespace ParallelRoam::Algorithms::GreedyTransactionalLod
{
TerrainLodRenderPacket TransactionalRenderBridge::Packet(const MeshConsumption& mesh,bool forceFull)
{
    if (!mesh.Data || mesh.Data->Indices.empty()) throw std::runtime_error("无可发布的公共网格");
    TerrainLodRenderPacket packet;packet.BorrowedCpuMesh=mesh.Data;
    packet.CpuMeshLifetime=TerrainLodCpuMeshLifetime::UntilNextBuildOrReset;
    packet.CpuMeshGeneration=mesh.Generation;packet.CpuMeshRequiresFullUpload=forceFull || mesh.Full;
    packet.ActiveTriangleCount=mesh.Data->Indices.size()/3;
    packet.ActiveLeafCount=packet.ActiveTriangleCount;packet.IndexCount=mesh.Data->Indices.size();
    if (!packet.CpuMeshRequiresFullUpload)
    {
        // 顶点槽与稠密索引的脏区间独立，不能强制配对相同长度
        packet.CpuMeshUpdateRanges.reserve(mesh.Vertices.size()+mesh.Indices.size());
        for (const auto& range : mesh.Vertices)
            packet.CpuMeshUpdateRanges.push_back({range.First,range.Count,0,0});
        for (const auto& range : mesh.Indices)
            packet.CpuMeshUpdateRanges.push_back({0,0,range.First,range.Count});
    }
    if (!packet.HasConsistentResourceContract()) throw std::runtime_error("公共网格范围不合法");
    return packet;
}

void TransactionalRenderBridge::Account(const WorkLedger& work,const CertifiedBatch& batch,TransactionalLodStats& stats)
{
    stats.RawCandidates=batch.Raw;stats.Examined=batch.Examined;stats.Receivers=batch.Receivers;
    stats.Need=batch.Need;stats.Feasible=batch.Feasible;stats.Exchanges=batch.Executed;stats.FreeExecuted=batch.FreeExecuted;
    stats.PairChecks=work.PairChecks;stats.Conflicts=work.Conflicts;stats.DonorReuse=work.DonorReuse;
    stats.SampleTouches=work.SampleTouches;stats.SampleEvaluations=work.SampleEvaluations;
    stats.VertexWrites=work.MeshVertices;stats.IndexWrites=work.MeshIndices;
    stats.FlipExecuted=batch.FlipExecuted;stats.FlipTriggered=work.FlipTriggered;stats.FlipAttempts=work.FlipAttempts;
    stats.FlipCertified=work.FlipCertified;stats.FlipConflicts=work.FlipConflicts;
    stats.BoundaryAttempts = work.BoundaryAttempts;
    stats.BoundaryCertified = work.BoundaryCertified;
    stats.BoundaryResolutionRejected = work.BoundaryResolutionRejected;
    stats.BoundaryConflicts = work.BoundaryConflicts;
    stats.BoundaryFreeExecuted = batch.BoundaryFreeExecuted;
    stats.BoundaryPairedExecuted = batch.BoundaryPairedExecuted;
    stats.AssignedFaces = batch.AssignedFaces;
    stats.ConsumedFreeFaces = batch.ConsumedFreeFaces;
    stats.UnusedFaces = batch.UnusedFaces;
    stats.ReleasedFaces = batch.ReleasedFaces;
    stats.NetFaceChange = batch.NetFaceChange;
    const auto ms=[&](const char* key) { const auto it=work.Seconds.find(key);return it==work.Seconds.end() ? 0.0 : it->second*1000; };
    stats.ViewMilliseconds=ms("view_refresh");stats.ReceiverMilliseconds=ms("receiver_stage");
    stats.DonorMilliseconds=ms("donor_stage");stats.ReservationMilliseconds=ms("reservation");
    stats.TopologyPrepareMilliseconds=ms("prepare");stats.TopologyPublishMilliseconds=ms("publish");
    stats.SampleRepairMilliseconds=ms("sample_repair");stats.MeshPrepareMilliseconds=ms("mesh_prepare");
    stats.ContinuationPublishMilliseconds=ms("derived_publish");
}
}
