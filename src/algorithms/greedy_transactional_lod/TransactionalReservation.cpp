#include "algorithms/greedy_transactional_lod/TransactionalReservation.h"
#include "profiling/CpuProfiling.h"
#include "algorithms/greedy_transactional_lod/TransactionalCertification.h"
#include "algorithms/greedy_transactional_lod/TransactionalProposals.h"

#include <algorithm>
#include <optional>

namespace ParallelRoam::Algorithms::GreedyTransactionalLod
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

CertifiedBatch TransactionalReservation::Plan(const TransactionalState& state,const TransactionalSamples& samples,WorkLedger& work,
    const TransactionalExecution& execution)
{
    ROAM_CPU_ZONE("gtp.plan");
    CertifiedBatch batch;batch.Version=state.Version();batch.Raw=samples.RawCount();batch.PairAuditComplete=execution.Diagnostics;
    std::vector<Proposal> receivers;
    const auto prefix=samples.Prefix(state.Config().PrefixLimit,&work);
    // 前缀是精确全局查询，分母始终保留全域资格数量
    batch.IntentIds.resize(prefix.size());batch.IntentResults.resize(prefix.size());batch.Attempts.resize(prefix.size());
    std::vector<std::optional<Proposal>> certified(prefix.size());
    execution.Run("receiver_stage",prefix.size(),work,[&](auto first,auto last,WorkLedger& local) {
        for (auto index=first;index<last;++index)
        {
            local.CheckLimit();const auto root=prefix[index];batch.IntentIds[index]=state.Face(root).Id;
            auto start=Clock::now();ReceiverCursor cursor(state,samples,root);
            local.Seconds["proposal"]+=Seconds(start);
            auto& reason=batch.IntentResults[index];reason="no_proposal";
            while (true)
            {
                start=Clock::now();auto next=cursor.Next(&local);local.Seconds["proposal"]+=Seconds(start);
                if (!next) break;
                auto& proposal=*next;start=Clock::now();reason=TransactionalCertification::Fit(state,samples,proposal,local);
                local.Seconds["receiver_certification"]+=Seconds(start);++local.Reasons[reason];
                batch.Attempts[index].emplace_back(proposal.Kind,reason);
                if (reason=="certified") { proposal.Reason=reason;certified[index]=std::move(proposal);break; }
            }
        }
    });
    // 完成顺序不参与优先预留，仍按同一全局前缀收集成功项
    for (auto& proposal : certified) if (proposal) receivers.push_back(std::move(*proposal));
    batch.Examined=prefix.size();batch.Receivers=receivers.size();
    // 共同接收集合先完成，预算与 donor 不能改变前端需求的人口
    const auto credits=(state.Config().Budget-state.FaceCount())/2;
    batch.AssignedCredits=std::min(credits,receivers.size());batch.Need=receivers.size()-batch.AssignedCredits;
    auto start=Clock::now();batch.PoolIds=samples.DonorPool(state.Config().DonorLimit,&work);
    work.Seconds["donor_order"]+=Seconds(start);
    std::vector<Proposal> cache(batch.Need ? batch.PoolIds.size() : 0);
    // 原有局部可行分母会检查完整共同池，提前认证不增加被检查的中心
    execution.Run("donor_stage",cache.size(),work,[&](auto first,auto last,WorkLedger& local) {
        for (auto index=first;index<last;++index)
        { ++local.DonorCertified;cache[index]=TransactionalProposals::Donor(state,samples,batch.PoolIds[index],local); }
    });
    std::vector<std::optional<TransactionFootprint>> footprints(cache.size());
    std::vector<bool> touched(cache.size());
    std::vector<TransactionFootprint> reserved;std::set<Identity> used;
    start=Clock::now();
    {
        ROAM_CPU_ZONE("gtp.reservation");
        for (std::size_t i=0;i<receivers.size();++i)
        {
            const auto& receiver=receivers[i];const auto rf=Footprint(state,receiver);++work.FootprintBuilds;
            const auto blocked=[&](const TransactionFootprint& footprint) {
                return std::any_of(reserved.begin(),reserved.end(),[&](const auto& other) { return Conflict(footprint,other); });
            };
            if (i<batch.AssignedCredits)
            {
                if (state.Config().HeightGuard && !TransactionalCertification::PreservesHeight(state,samples,receiver,nullptr,work))
                    continue;
                // 本批失败额度保持闲置；下一批仅凭实际 N 重新生成命名
                if (!blocked(rf)) { batch.Exchanges.push_back({receiver,{},false});reserved.push_back(rf);++batch.FreeExecuted; }
                else ++work.Conflicts;
                continue;
            }
            bool feasible=false,accepted=false;
            // 正常路径成功即已证明局部可行；未成功继续检查完整共同池
            for (std::size_t index=0;index<cache.size();++index)
            {
                if (!touched[index]) { touched[index]=true;++work.DonorTouched; }
                ++work.PairChecks;const auto center=batch.PoolIds[index];const auto& donor=cache[index];
                if (donor.Reason!="certified") { ++work.Reasons[donor.Reason];continue; }
                if (!TransactionalCertification::Accepts(state,samples,donor,receiver.TargetMicropixels,work))
                { ++work.Reasons["fast_quality_miss"];continue; }
                if (!footprints[index]) { footprints[index]=Footprint(state,donor);++work.FootprintBuilds; }
                const auto& df=*footprints[index];
                // 先判断一个交换内部是否独立，再判断它与高优先级已预留事务是否冲突
                if (Conflict(rf,df)) { ++work.Reasons["internal_conflict"];continue; }
                if (state.Config().HeightGuard && !TransactionalCertification::PreservesHeight(state,samples,receiver,&donor,work))
                    continue;
                feasible=true;
                if (accepted) continue;
                ++work.ReservationChecks;
                if (used.contains(center)) { ++work.DonorReuse;continue; }
                const auto combined=Unite(rf,df);
                if (blocked(combined)) { ++work.Conflicts;continue; }
                reserved.push_back(combined);used.insert(center);batch.Exchanges.push_back({receiver,donor,true});
                accepted=true;++batch.Executed;
                if (!execution.Diagnostics) break;
            }
            if (feasible) ++batch.Feasible;
        }
    }
    batch.UnusedCredits=batch.AssignedCredits-batch.FreeExecuted;
    // 账本随返回结果完成生命周期，存活状态只由发布后的面数量代表预算占用
    work.Seconds["reservation"]+=Seconds(start);
    return batch;
}
}
