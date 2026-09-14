#pragma once

#include "algorithms/greedy_transactional_lod/TransactionalSamples.h"
#include "algorithms/greedy_transactional_lod/TransactionalMesh.h"

namespace ParallelRoam::Algorithms::GreedyTransactionalLod
{
/// <summary>
/// 自有状态的持续阶段入口，派生修复与拓扑共同准备后发布
/// 诊断全扫和文件输出由外部编排，正常同视图只修局部样本
/// </summary>
class TransactionalPipeline
{
public:
    explicit TransactionalPipeline(const InitialMesh& input,TransactionalExecution execution={});
    /// <summary>
    /// 初建入口可移交源样本所有权，避免临时种子到持续参考的额外复制
    /// </summary>
    explicit TransactionalPipeline(InitialMesh&& input,TransactionalExecution execution={});
    void Initialize(WorkLedger& work);
    void SetView(const Configuration& view,WorkLedger& work);
    CertifiedBatch Update(WorkLedger& work);
    void Apply(const CertifiedBatch& batch,WorkLedger& work);
    MeshConsumption ConsumeMesh() { return _mesh.Consume(); }
    const TransactionalState& State() const { return _state; }
    const TransactionalSamples& Samples() const { return _samples; }
    const Terrain::TerrainMeshData& Mesh() const { return _mesh.Data(); }
    const std::set<Identity>& InvalidatedRoots() const { return _invalidatedRoots; }
    const std::set<Identity>& InvalidatedDonors() const { return _invalidatedDonors; }
private:
    TransactionalExecution _execution;
    TransactionalState _state;
    TransactionalSamples _samples;
    TransactionalMesh _mesh;
    bool _initialized{};
    std::set<Identity> _invalidatedRoots, _invalidatedDonors;
};
}
