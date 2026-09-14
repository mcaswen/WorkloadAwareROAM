#pragma once

#include "algorithms/greedy_transactional_lod/TransactionalSamples.h"
#include "algorithms/greedy_transactional_lod/TransactionalMesh.h"

namespace ParallelRoam::Experiment::GreedyTransactionalLod
{
using Algorithms::GreedyTransactionalLod::TransactionalState;
using Algorithms::GreedyTransactionalLod::TransactionalSamples;
using Algorithms::GreedyTransactionalLod::WorkLedger;
using Algorithms::GreedyTransactionalLod::MeshConsumption;
/// <summary>
/// 全量独立诊断，不作为正常提交或局部派生修复的实现
/// </summary>
class TransactionalValidation
{
public:
    static void Validate(const TransactionalState& state);
    static bool Equivalent(const TransactionalState& first,const TransactionalState& second);
    static void Samples(const TransactionalState& state,const TransactionalSamples& actual,WorkLedger& work);
    static void Mesh(const TransactionalState& state,const Terrain::TerrainMeshData& mesh);
    static void Consume(const MeshConsumption& update,Terrain::TerrainMeshData& mirror);
};
}
