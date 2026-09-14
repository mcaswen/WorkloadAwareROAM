#pragma once

#include "algorithms/ITerrainLodAlgorithm.h"
#include "algorithms/greedy_transactional_lod/TransactionalMesh.h"

namespace ParallelRoam::Algorithms::GreedyTransactionalLod
{
/// <summary>
/// 将核心借用与有限账本转换为公共值，不扫描 mesh 或参与算法选择
/// 桥接分配失败后的全量恢复标志由拥有核心的适配器维护
/// </summary>
struct TransactionalRenderBridge
{
    static TerrainLodRenderPacket Packet(const MeshConsumption& mesh,bool forceFull);
    static void Account(const WorkLedger& work,const CertifiedBatch& batch,TransactionalLodStats& stats);
};
}
