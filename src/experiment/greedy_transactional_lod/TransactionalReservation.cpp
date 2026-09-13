#include "experiment/greedy_transactional_lod/TransactionalReservation.h"
#include "experiment/greedy_transactional_lod/TransactionalCertification.h"
#include "experiment/greedy_transactional_lod/TransactionalProposals.h"

#include <algorithm>
#include <optional>

namespace ParallelRoam::Experiment::GreedyTransactionalLod
{
namespace
{
using Clock=std::chrono::steady_clock;
double Seconds(Clock::time_point start) { return std::chrono::duration<double>(Clock::now()-start).count(); }
TransactionFootprint Unite(TransactionFootprint a,const TransactionFootprint& b)
{
    a.Reads.insert(b.Reads.begin(),b.Reads.end());a.Writes.insert(b.Writes.begin(),b.Writes.end());return a;
}
}

TransactionFootprint TransactionalReservation::Footprint(const TransactionalState& state,const Proposal& proposal)
{
    TransactionFootprint result;
    const auto edges=[&](const auto& face) {
        // 接口边也属于写集合，移除面不相交并不足以证明可共同发布
        for (std::size_t i=0;i<3;++i)
        {
            const auto edge=EdgeKey(face[i],face[(i+1)%3]);
            result.Reads.emplace('e',edge[0],edge[1]);result.Writes.emplace('e',edge[0],edge[1]);
        }
    };
    for (auto slot : proposal.Support)
    {
        result.Reads.emplace('f',slot,0);result.Writes.emplace('f',slot,0);
        for (auto id : state.Face(slot).Vertices) result.Reads.emplace('h',id,0);
        edges(state.Face(slot).Vertices);
    }
    for (const auto& face : proposal.Faces) edges(face);
    for (auto id : proposal.Free) result.Writes.emplace('h',id,0);
    return result;
}

bool TransactionalReservation::Conflict(const TransactionFootprint& a,const TransactionFootprint& b)
{
    for (const auto& r : a.Writes) if (b.Reads.contains(r) || b.Writes.contains(r)) return true;
    for (const auto& r : b.Writes) if (a.Reads.contains(r)) return true;
    return false;
}

CertifiedBatch TransactionalReservation::Plan(const TransactionalState& state,const TransactionalSamples& samples,WorkLedger& work)
{
    CertifiedBatch batch;batch.Version=state.Version();batch.Raw=samples.Raw().size();
    std::vector<Proposal> receivers;
    const auto prefix=std::min(batch.Raw,state.Config().PrefixLimit);
    // 原始需求完整排序之后才取有限前缀，分母始终保留全域数量
    for (std::size_t i=0;i<prefix;++i)
    {
        work.CheckLimit();const auto root=samples.Raw()[i];batch.IntentIds.push_back(state.Face(root).Id);
        auto start=Clock::now();auto proposals=TransactionalProposals::Receivers(state,samples,root);
        work.Seconds["proposal"]+=Seconds(start);
        std::string reason="no_proposal";
        std::vector<std::pair<char,std::string>> attempts;
        for (auto& proposal : proposals)
        {
            start=Clock::now();reason=TransactionalCertification::Fit(state,samples,proposal,work);
            work.Seconds["receiver_certification"]+=Seconds(start);++work.Reasons[reason];
            attempts.emplace_back(proposal.Kind,reason);
            if (reason=="certified") { proposal.Reason=reason;receivers.push_back(std::move(proposal));break; }
        }
        batch.IntentResults.push_back(reason);
        batch.Attempts.push_back(std::move(attempts));
    }
    batch.Examined=prefix;batch.Receivers=receivers.size();
    // 共同接收集合先完成，预算与 donor 不能改变前端需求的人口
    const auto credits=(state.Config().Budget-state.FaceCount())/2;
    batch.AssignedCredits=std::min(credits,receivers.size());batch.Need=receivers.size()-batch.AssignedCredits;
    auto start=Clock::now();std::vector<std::pair<double,Identity>> pool;
    for (const auto& vertex : state.Vertices())
    {
        if (!vertex.Active || state.IsBoundary(vertex.Id)) continue;
        double priority=0;
        for (auto face : vertex.Incident) priority=std::max(priority,samples.PrioritySquared(face));
        pool.emplace_back(priority,vertex.Id);
    }
    std::sort(pool.begin(),pool.end());
    // 两种回收能力的对照应使用这个共同池，不能按当前可行性重选低损伤点
    if (pool.size()>state.Config().DonorLimit) pool.resize(state.Config().DonorLimit);
    for (const auto& entry : pool) batch.PoolIds.push_back(entry.second);
    work.Seconds["donor_order"]+=Seconds(start);
    std::map<Identity,Proposal> cache;
    std::vector<TransactionFootprint> reserved;std::set<Identity> used;
    double donorSeconds=0;start=Clock::now();
    for (std::size_t i=0;i<receivers.size();++i)
    {
        const auto& receiver=receivers[i];const auto rf=Footprint(state,receiver);
        const auto blocked=[&](const TransactionFootprint& footprint) {
            return std::any_of(reserved.begin(),reserved.end(),[&](const auto& other) { return Conflict(footprint,other); });
        };
        if (i<batch.AssignedCredits)
        {
            // 本批失败额度保持闲置；下一批仅凭实际 N 重新生成命名
            if (!blocked(rf)) { batch.Exchanges.push_back({receiver,{},false});reserved.push_back(rf);++batch.FreeExecuted; }
            else ++work.Conflicts;
            continue;
        }
        bool feasible=false,accepted=false;
        // 仍检查共同池，独立记录局部可行分母；命中后的额外审计费用也计入
        for (auto center : batch.PoolIds)
        {
            ++work.PairChecks;
            if (!cache.contains(center))
            {
                // 缓存局部几何与误差，阈值随接收方变化仍须另行比较
                const auto before=Clock::now();cache.emplace(center,TransactionalProposals::Donor(state,samples,center,work));
                donorSeconds+=Seconds(before);
            }
            const auto& donor=cache.at(center);
            if (donor.Reason!="certified") { ++work.Reasons[donor.Reason];continue; }
            if (!TransactionalCertification::Accepts(state,samples,donor,receiver.TargetMicropixels,work))
            { ++work.Reasons["fast_quality_miss"];continue; }
            const auto df=Footprint(state,donor);
            // 先判断一个交换内部是否独立，再判断它与高优先级已预留事务是否冲突
            if (Conflict(rf,df)) { ++work.Reasons["internal_conflict"];continue; }
            feasible=true;
            if (accepted) continue;
            ++work.ReservationChecks;
            if (used.contains(center)) { ++work.DonorReuse;continue; }
            const auto combined=Unite(rf,df);
            if (blocked(combined)) { ++work.Conflicts;continue; }
            reserved.push_back(combined);used.insert(center);batch.Exchanges.push_back({receiver,donor,true});
            accepted=true;++batch.Executed;
        }
        if (feasible) ++batch.Feasible;
    }
    batch.UnusedCredits=batch.AssignedCredits-batch.FreeExecuted;
    // 账本随返回结果完成生命周期，存活状态只由发布后的面数量代表预算占用
    work.Seconds["donor_certification"]+=donorSeconds;
    work.Seconds["reservation"]+=Seconds(start)-donorSeconds;
    return batch;
}
}
