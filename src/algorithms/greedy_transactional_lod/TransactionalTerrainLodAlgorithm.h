#pragma once

#include "algorithms/ITerrainLodAlgorithm.h"

#include <memory>

namespace ParallelRoam::Algorithms::GreedyTransactionalLod
{
/// <summary>
/// 公共平台适配持有独立事务状态，每次同步调用只推进一个冻结批次
/// 核心与线程池隐藏在私有实现中，渲染器只借用已发布 CPU 网格
/// </summary>
class TransactionalTerrainLodAlgorithm final : public ITerrainLodAlgorithm
{
public:
    TransactionalTerrainLodAlgorithm();
    ~TransactionalTerrainLodAlgorithm() override;
    TerrainLodAlgorithmInfo Info() const override;
    TerrainLodAlgorithmCapabilities Capabilities() const override;
    bool BuildRenderData(const TerrainLodBuildInput& input,TerrainLodRenderPacket& outPacket,std::string* errorMessage) override;
    const TerrainLodStats& Stats() const override;
    void Reset() override;

    /// <summary>
    /// 消费者失去上传资源时请求重发当前完整网格，不丢弃算法状态或已完成的事务
    /// </summary>
    void RequestFullUpload();
    void Retry();

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};
}
