#pragma once

#include "experiment/greedy_transactional_lod/TransactionalSamples.h"
#include "experiment/greedy_transactional_lod/TransactionalMesh.h"

namespace ParallelRoam::Experiment::GreedyTransactionalLod
{
/// <summary>
/// 自有状态的持续阶段入口，派生修复与拓扑共同准备后发布
/// 诊断全扫和文件输出由外部编排，正常同视图只修局部样本
/// </summary>
class TransactionalPipeline
{
public:
    explicit TransactionalPipeline(const InitialMesh& input);
    void Initialize(WorkLedger& work);
    CertifiedBatch Update(WorkLedger& work);
    void Apply(const CertifiedBatch& batch,WorkLedger& work);
    MeshConsumption ConsumeMesh() { return _mesh.Consume(); }
    const TransactionalState& State() const { return _state; }
    const TransactionalSamples& Samples() const { return _samples; }
    const Terrain::TerrainMeshData& Mesh() const { return _mesh.Data(); }
private:
    TransactionalState _state;
    TransactionalSamples _samples;
    TransactionalMesh _mesh;
    bool _initialized{};
};
}
