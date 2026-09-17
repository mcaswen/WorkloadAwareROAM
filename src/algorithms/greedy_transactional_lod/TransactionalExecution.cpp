#include "algorithms/greedy_transactional_lod/TransactionalExecution.h"
#include "profiling/CpuProfiling.h"

#include <atomic>
#include <set>
#include <stdexcept>
#include <thread>

namespace ParallelRoam::Algorithms::GreedyTransactionalLod
{
namespace
{
void Merge(WorkLedger& target,const WorkLedger& source)
{
    // 明确列出逻辑计数，时间和配额不参与数值字段的机械相加
    constexpr auto counters=std::to_array<std::uint64_t WorkLedger::*>({
        &WorkLedger::LocationTests,&WorkLedger::SampleContributions,&WorkLedger::SampleEvaluations,
        &WorkLedger::Proposals,&WorkLedger::SampleTouches,&WorkLedger::Constraints,&WorkLedger::ExactChecks,&WorkLedger::FilterChecks,
        &WorkLedger::EarTests,&WorkLedger::RingVisits,&WorkLedger::PairChecks,&WorkLedger::ReservationChecks,
        &WorkLedger::DonorReuse,&WorkLedger::Conflicts,&WorkLedger::PreparedFaces,&WorkLedger::PreparedVertices,&WorkLedger::PreparedEdges,
        &WorkLedger::RepairSamples,&WorkLedger::RepairFaces,&WorkLedger::OrderVisits,&WorkLedger::MeshVertices,&WorkLedger::MeshIndices,&WorkLedger::PendingBlocks,
        &WorkLedger::HeightSamples,&WorkLedger::HeightExactSamples,&WorkLedger::HeightGuardChecks,&WorkLedger::HeightGuardRejected,
        &WorkLedger::CapacityGrowths,&WorkLedger::CapacityBytesReserved,&WorkLedger::CapacityBytesRelocated,
        &WorkLedger::ViewBufferAllocations,&WorkLedger::ViewBufferBytes,
        &WorkLedger::ReceiverConstructed,&WorkLedger::EvidenceLookups,&WorkLedger::EvidenceHits,&WorkLedger::EvidenceBuilds,
        &WorkLedger::EvidenceBytes,&WorkLedger::EvidenceFaceTests,&WorkLedger::FootprintBuilds,
        &WorkLedger::DonorTouched,&WorkLedger::DonorCertified,
        &WorkLedger::FlipTriggered,&WorkLedger::FlipAttempts,&WorkLedger::FlipCertified,&WorkLedger::FlipConflicts,
        &WorkLedger::BoundaryAttempts,&WorkLedger::BoundaryCertified,&WorkLedger::BoundaryResolutionRejected,&WorkLedger::BoundaryConflicts,
        &WorkLedger::IndexBlocks,&WorkLedger::IndexSlots,&WorkLedger::IndexComparisons,&WorkLedger::IndexQueryBlocks,
        &WorkLedger::CandidateUpdates,&WorkLedger::DonorIndexUpdates,&WorkLedger::ReceiverCacheHits,&WorkLedger::DonorCacheHits,
        &WorkLedger::CacheInvalidations,&WorkLedger::RootObservations});
    for (auto field : counters) target.*field+=source.*field;
    target.QualityMaxRationalBits = std::max(target.QualityMaxRationalBits, source.QualityMaxRationalBits);
    for (const auto& [key,value] : source.Reasons) target.Reasons[key]+=value;
    for (const auto& [key,value] : source.Seconds) target.Seconds["task_wall_sum_"+key]+=value;
}
}

void TransactionalExecution::Run(const std::string& phase,std::size_t count,WorkLedger& work,
    const std::function<void(std::size_t,std::size_t,WorkLedger&)>& task) const
{
    ROAM_CPU_ZONE("gtp.dispatch");
    ROAM_CPU_TEXT(phase.data(),phase.size());
    if (!Workers || (Workers>1 && !Dispatch)) throw std::runtime_error("分块执行配置缺少同步派发");
    if (!count) return;
    const auto started=std::chrono::steady_clock::now();
    const auto chunks=std::min(count,Workers);
    std::vector<WorkLedger> local(chunks);
    std::vector<std::thread::id> identities(Diagnostics ? chunks : 0);
    for (auto& entry : local) { entry.Deadline=work.Deadline;entry.VisitLimit=work.VisitLimit; }
    const auto invoke=[&](std::size_t chunk) {
        ROAM_CPU_ZONE("gtp.task");
#if defined(TRACY_ENABLE)
        // 一个单行标签兼容官方 CSV 导出，避免 Text 与 Value 合成未转义换行
        const auto taskIdentity=phase+"/"+std::to_string(chunk);
        ROAM_CPU_TEXT(taskIdentity.data(),taskIdentity.size());
#endif
        if (Diagnostics) identities[chunk]=std::this_thread::get_id();
        // 商余划分避免空任务，每项固定归属一个连续区间
        const auto first=chunk*(count/chunks)+std::min(chunk,count%chunks);
        const auto last=first+count/chunks+(chunk<count%chunks ? 1U : 0U);
        local[chunk].CheckLimit();task(first,last,local[chunk]);
    };
    if (Workers==1) invoke(0);else Dispatch(chunks,invoke);
    {
        ROAM_CPU_ZONE("gtp.ledger_merge");
        for (const auto& entry : local) Merge(work,entry);
    }
    // 每个任务的局部限制不替代全批限制；超额结果不能流入发布
    work.CheckLimit();
    if (Diagnostics)
    {
        const std::set<std::thread::id> actual(identities.begin(),identities.end());
        auto& evidence=work.Execution[phase];
        evidence[0]+=count;evidence[1]+=chunks;evidence[2]=std::max(evidence[2],actual.size());
    }
    work.Seconds[phase]+=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
}

void TransactionalExecution::RunIndependent(const std::string& phase, std::size_t count, WorkLedger& work,
    const std::function<void(std::size_t, std::size_t, WorkLedger&)>& task) const
{
    if (Workers == 1 || count <= 1)
    {
        Run(phase, count, work, task);
        return;
    }

    // 派发数量仍受线程数限制；只在现有同步任务内部领取独立索引
    // relaxed仅保证唯一归属，业务输入与输出的可见性由派发和等待边界保证
    std::atomic<std::size_t> next{0};
    Run(phase, count, work, [&](std::size_t, std::size_t, WorkLedger& local) {
        while (true)
        {
            const auto index = next.fetch_add(1, std::memory_order_relaxed);
            if (index >= count)
            {
                break;
            }

            // 账本随同步任务累积，不为每根分配容器或重置访问配额
            local.CheckLimit();
            ROAM_CPU_ZONE("gtp.item");
#if defined(TRACY_ENABLE)
            const auto itemIdentity = phase + "/" + std::to_string(index);
            ROAM_CPU_TEXT(itemIdentity.data(), itemIdentity.size());
#endif
            task(index, index + 1, local);
        }
    });
}
}
