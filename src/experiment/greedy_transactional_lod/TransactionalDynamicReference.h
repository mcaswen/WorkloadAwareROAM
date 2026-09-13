#pragma once

#include "experiment/greedy_transactional_lod/TransactionalPipeline.h"

namespace ParallelRoam::Experiment::GreedyTransactionalLod
{
/// <summary>
/// 动态参考的统计按决策观察累计，不解释为单一快照的需求分母
/// </summary>
struct DynamicResult
{
    CertifiedBatch Summary;
    std::vector<std::array<Identity,3>> Decisions;
    std::string Stop;
};

/// <summary>
/// 每次成功应用后重新观察全局最高请求，反馈控制独立于批次 Plan
/// 只缓存局部几何证据，资源可用性仍在每次观察时重新判断
/// </summary>
class TransactionalDynamicReference
{
public:
    explicit TransactionalDynamicReference(const InitialMesh& input) : _pipeline(input) {}
    TransactionalPipeline& Pipeline() { return _pipeline; }
    DynamicResult Update(WorkLedger& work,bool cacheEvidence=true);
private:
    struct ReceiverEntry
    {
        Proposal Value;
        std::size_t Ordinal{};
        std::vector<std::pair<char,std::string>> Attempts;
    };
    TransactionalPipeline _pipeline;
    std::map<Identity,ReceiverEntry> _receivers;
    std::map<Identity,Proposal> _donors;
    ReceiverEntry Receiver(Slot root,WorkLedger& work,bool cache);
    Proposal Donor(Identity center,WorkLedger& work,bool cache);
    void Invalidate(WorkLedger& work);
};
}
