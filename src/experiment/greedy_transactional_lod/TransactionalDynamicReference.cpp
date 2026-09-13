#include "experiment/greedy_transactional_lod/TransactionalDynamicReference.h"
#include "experiment/greedy_transactional_lod/TransactionalProposals.h"
#include "experiment/greedy_transactional_lod/TransactionalCertification.h"
#include "experiment/greedy_transactional_lod/TransactionalReservation.h"

#include <optional>

namespace ParallelRoam::Experiment::GreedyTransactionalLod
{
namespace
{
using Clock=std::chrono::steady_clock;
double Seconds(Clock::time_point start) { return std::chrono::duration<double>(Clock::now()-start).count(); }
void Reidentify(Proposal& proposal,Identity id)
{
    // 缓存的是几何模板，新点身份必须来自本次可用命名区间
    const auto old=proposal.NewVertex;
    if (old==id) return;
    auto point=proposal.Points.extract(old);point.key()=id;proposal.Points.insert(std::move(point));
    for (auto& face : proposal.Faces) for (auto& vertex : face) if (vertex==old) vertex=id;
    for (auto& vertex : proposal.Free) if (vertex==old) vertex=id;
    proposal.NewVertex=id;
}
}

TransactionalDynamicReference::ReceiverEntry TransactionalDynamicReference::Receiver(Slot root,WorkLedger& work,bool cache)
{
    const auto& state=_pipeline.State();const auto id=state.Face(root).Id;
    ReceiverEntry result;
    const auto found=_receivers.find(id);
    if (cache && found!=_receivers.end()) { ++work.ReceiverCacheHits;result=found->second; }
    else
    {
        // 首个可认证提案决定本次接收模板，不借 donor 条件改变目录选择
        auto start=Clock::now();auto proposals=TransactionalProposals::Receivers(state,_pipeline.Samples(),root);
        work.Seconds["proposal"]+=Seconds(start);result.Value.Reason="no_proposal";
        for (std::size_t i=0;i<proposals.size();++i)
        {
            auto& p=proposals[i];start=Clock::now();
            p.Reason=TransactionalCertification::Fit(state,_pipeline.Samples(),p,work);
            work.Seconds["receiver_certification"]+=Seconds(start);++work.Reasons[p.Reason];
            result.Attempts.emplace_back(p.Kind,p.Reason);result.Value.Reason=p.Reason;
            if (p.Reason=="certified") { result.Value=std::move(p);result.Ordinal=i;break; }
        }
        if (cache) _receivers[id]=result;
    }
    // 缓存命中不运行拟合，身份重绑定只改变名字，不改变实际 binary64 几何
    if (result.Value.Reason=="certified")
        Reidentify(result.Value,state.NextVertexId()-static_cast<Identity>(root)*8-static_cast<Identity>(result.Ordinal));
    return result;
}

Proposal TransactionalDynamicReference::Donor(Identity center,WorkLedger& work,bool cache)
{
    const auto found=_donors.find(center);
    // 缓存 donor 几何及误差，接收阈值和资源关系由控制器另外检查
    if (cache && found!=_donors.end()) { ++work.DonorCacheHits;return found->second; }
    const auto start=Clock::now();
    auto result=TransactionalProposals::Donor(_pipeline.State(),_pipeline.Samples(),center,work);
    work.Seconds["donor_certification"]+=Seconds(start);
    if (cache) _donors[center]=result;
    return result;
}

void TransactionalDynamicReference::Invalidate(WorkLedger& work)
{
    // 样本评价共享边的外接口同样属于依赖，不能只按删除 face ID 失效
    for (auto id : _pipeline.InvalidatedRoots()) work.CacheInvalidations+=_receivers.erase(id);
    for (auto id : _pipeline.InvalidatedDonors()) work.CacheInvalidations+=_donors.erase(id);
}

DynamicResult TransactionalDynamicReference::Update(WorkLedger& work,bool cacheEvidence)
{
    const auto started=Clock::now();_pipeline.Initialize(work);_receivers.clear();_donors.clear();
    // 外部新轮清除证据，避免相机或批次资源变化依赖隐含的旧失败记录
    work.Seconds.try_emplace("update",0);
    DynamicResult result;auto& summary=result.Summary;summary.Version=_pipeline.State().Version();
    summary.Raw=_pipeline.Samples().RawCount();
    const auto initialObservations=work.RootObservations;
    for (std::size_t step=0;step<64;++step)
    {
        std::optional<Exchange> selected;
        const auto& state=_pipeline.State();const auto& samples=_pipeline.Samples();
        const auto prefix=samples.Prefix(state.Config().PrefixLimit);
        // 每次成功后的观察从新全序开头开始，不能继续使用上一次的尾部游标
        for (auto root : prefix)
        {
            work.CheckLimit();
            if (work.RootObservations-initialObservations>=4096) { result.Stop="root_observation_cap";break; }
            ++work.RootObservations;++summary.Examined;
            const auto entry=Receiver(root,work,cacheEvidence);const auto& receiver=entry.Value;
            summary.IntentIds.push_back(state.Face(root).Id);summary.IntentResults.push_back(receiver.Reason);
            summary.Attempts.push_back(entry.Attempts);
            if (receiver.Reason!="certified") continue;
            ++summary.Receivers;
            if (state.Config().Budget-state.FaceCount()>=2)
            {
                // 动态单事务参考只消费当前真实额度，不继承 B 的整批命名账本
                ++summary.AssignedCredits;
                if (state.Config().HeightGuard && !TransactionalCertification::PreservesHeight(state,samples,receiver,nullptr,work))
                    continue;
                selected=Exchange{receiver,{},false};++summary.FreeExecuted;break;
            }
            ++summary.Need;
            const auto rf=TransactionalReservation::Footprint(state,receiver);
            const auto ordered=Clock::now();const auto pool=samples.DonorPool(state.Config().DonorLimit);
            work.Seconds["donor_order"]+=Seconds(ordered);
            for (auto center : pool)
            {
                // 旧几何失败可缓存，pair 阈值及兼容性仍随当前 receiver 重新计算
                ++work.PairChecks;auto donor=Donor(center,work,cacheEvidence);
                if (donor.Reason!="certified") { ++work.Reasons[donor.Reason];continue; }
                if (!TransactionalCertification::Accepts(state,samples,donor,receiver.TargetMicropixels,work))
                { ++work.Reasons["fast_quality_miss"];continue; }
                if (TransactionalReservation::Conflict(rf,TransactionalReservation::Footprint(state,donor)))
                { ++work.Reasons["internal_conflict"];continue; }
                if (state.Config().HeightGuard && !TransactionalCertification::PreservesHeight(state,samples,receiver,&donor,work))
                    continue;
                selected=Exchange{receiver,std::move(donor),true};++summary.Feasible;++summary.Executed;break;
            }
            if (selected) break;
        }
        if (!selected)
        {
            // 有限前缀无解并不证明整个网格无解，停止标签保留这一范围
            if (result.Stop.empty()) result.Stop="no_executable_in_prefix";
            break;
        }
        // 单事务反馈不调用 B 的完整 batch planner，也不为未选择尾部预先认证
        result.Decisions.push_back({state.Face(selected->Receiver.Root).Id,
            selected->HasDonor ? selected->Donor.Center : 0,static_cast<Identity>(selected->Receiver.Kind)});
        CertifiedBatch batch;batch.Version=state.Version();batch.Exchanges.push_back(*selected);
        _pipeline.Apply(batch,work);Invalidate(work);summary.Exchanges.push_back(std::move(*selected));
    }
    if (result.Stop.empty()) result.Stop="transaction_cap";
    // 该汇总跨多个决策状态，不能与 B 的单快照 D_need 作同一统计分母
    summary.UnusedCredits=summary.AssignedCredits-summary.FreeExecuted;
    // 缓存销毁属于本轮成本，不将跨调用清理偷偷排除在 update 之外
    _receivers.clear();_donors.clear();
    work.Seconds.at("update")=Seconds(started);double accounted=0;
    // 控制和缓存费用由完整窗口扣除不重叠子阶段，未单独插入大规模探针
    for (const auto* name : {"proposal","receiver_certification","donor_certification","donor_order",
        "prepare","publish","sample_repair","mesh_prepare","derived_publish","height_guard"})
        if (work.Seconds.contains(name)) accounted+=work.Seconds.at(name);
    work.Seconds["dynamic_control"]=std::max(0.0,work.Seconds.at("update")-accounted);
    return result;
}
}
