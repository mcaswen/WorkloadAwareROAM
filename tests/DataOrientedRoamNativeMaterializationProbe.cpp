#include "DataOrientedRoamNativeMaterializationTestSupport.h"
#include "DataOrientedRoamNativePlannerTestSupport.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamValidation.h"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>

using namespace NativeMaterializationTests;

namespace
{
namespace Planner = Algorithms::DataOrientedRoam::Materialization;
namespace Audit = NativePlannerTests;

/// <summary>
/// 借由结束回调比较完整参考结果，私有工作区引用不逃逸到测量结果
/// </summary>
struct FinalAudit
{
    Audit::Projection Expected;
    static void Finish(void* context, const Planner::NativePlanningView& view, const Planner::NativePlanningQueues& queues,
        const Planner::NativeTargetPlan&)
    {
        Audit::Compare(static_cast<FinalAudit*>(context)->Expected, Audit::Project(view, queues));
        Audit::Require(queues.Validate(Planner::NativePlanningQueueKind::Split) &&
            queues.Validate(Planner::NativePlanningQueueKind::Merge), "natural private heap invariant");
    }
};

Planner::NativeTargetPlan Diagnose(const Dod::DataOrientedRoamState& initial, std::size_t& eventCount)
{
    const Audit::StateSnapshot snapshot{initial};
    auto legacy = initial;
    auto iteration = Dod::BeginStrictSplitIteration(legacy);
    const auto before = Audit::Project(legacy);
    Audit::TraceLog oldTrace, newTrace;
    while (iteration.Stop == Dod::TopologySplitStop::Running)
        (void)Dod::AdvanceStrictSplitIterationWithDecisions(legacy, iteration, oldTrace.Sink());
    FinalAudit audit{Audit::Project(legacy)};
    const auto plan = Planner::BuildNativeSplitTarget(initial, {newTrace.Sink(), &audit, nullptr, FinalAudit::Finish, true});
    Audit::Compare(oldTrace, newTrace);
    Audit::VerifyPlan(before, audit.Expected, oldTrace, iteration, plan);
    Audit::Require(snapshot == Audit::StateSnapshot{initial}, "natural planner mutated source");
    eventCount = oldTrace.Events.size();
    legacy.Stats = {};
    Dod::ValidateTopology(legacy);
    Audit::Require(legacy.Stats.InvalidTopologyCount == 0 && Dod::CountPersistentQueueInvariantViolations(legacy) == 0,
        "natural reference topology or queue invariant");
    // 普通 Legacy 仍跑真实入口，完整终态对照位于计时之外
    auto ordinary = initial;
    Dod::AdvanceSplitTopologySerialForExperiment(ordinary);
    Audit::Compare(audit.Expected, Audit::Project(ordinary));
    return plan;
}

void WriteAudit(std::ostream& output, const SourceCase& item, std::size_t traceEvents,
    const Planner::NativeTargetPlan& plan)
{
    const auto& m = plan.Metrics;
    const auto& v = m.View; const auto& s = m.SplitQueue; const auto& q = m.MergeQueue; const auto& w = m.Work;
    output << "1," << item.Name << ',' << item.InputHash << ',' << plan.SourceNodeCount << ','
        << item.Initial->SplitQueue.size() << ',' << item.Initial->MergeQueue.size() << ',' << plan.SourceLeafCount << ','
        << plan.FinalLeafCount << ',' << plan.AddedEvents.size() << ',' << plan.RemovedEvents.size() << ','
        << plan.Obligations.size() << ',' << v.VirtualNodes << ',' << v.OldNodeRecords << ',' << v.VirtualNodeRecords << ','
        << v.ChangedFields << ',' << v.Queries << ',' << v.SourceQueries << ',' << v.DistinctSourceNodesRead << ','
        << s.OldSlotsWritten << ',' << s.AppendedSlotsWritten << ',' << q.OldSlotsWritten << ',' << q.AppendedSlotsWritten << ','
        << s.ReverseRecords << ',' << q.ReverseRecords << ',' << m.MergeRelationRecords << ','
        << s.Reads << ',' << q.Reads << ',' << s.Writes << ',' << q.Writes << ',' << s.Comparisons << ',' << q.Comparisons << ','
        << s.DistinctSourceSlotsRead << ',' << q.DistinctSourceSlotsRead << ','
        << s.MembershipReads + q.MembershipReads << ',' << s.MembershipWrites + q.MembershipWrites << ','
        << w.SplitRoots << ',' << w.SplitRootFailures << ',' << w.MergeRoots << ',' << w.MergeRootFailures << ',' << w.Exchanges << ','
        << w.SplitAttempts << ',' << w.ForcedAttempts << ',' << w.PrimitiveSplits << ',' << w.ForcedSplits << ',' << w.PrimitiveMerges << ','
        << w.BudgetRejections << ',' << w.ScoreEvaluations << ',' << w.NeighborhoodVisits << ',' << w.CandidateChecks << ','
        << traceEvents << ',' << plan.Iteration << ',' << static_cast<int>(plan.Stop) << ',' << plan.RemainingBudget << ','
        << m.ExtractionNodes << ',' << m.ObligationPeak << ',' << m.PayloadBytes << ',' << plan.Evaluations.size();
    for (int kind = 0; kind <= static_cast<int>(Planner::NativeObligationKind::CacheBirth); ++kind)
        output << ',' << std::count_if(plan.Obligations.begin(), plan.Obligations.end(), [&](const auto& value) {
            return static_cast<int>(value.Kind) == kind;
        });
    output << '\n';
}

void RunPlannerProbe(const char* root, const char* cameras, const char* outputPath)
{
    const std::filesystem::path auditPath{std::string(outputPath) + ".audit.csv"};
    if (std::filesystem::exists(outputPath) || std::filesystem::exists(auditPath)) throw std::invalid_argument("拒绝覆盖已有结果");
    std::ofstream output{outputPath}, diagnostics{auditPath};
    if (!output || !diagnostics) throw std::runtime_error("无法创建规划诊断输出");
    output << "schema,case,sourceHash,method,repeat,N,Qs,Qm,initialLeaves,finalLeaves,sourceMs,restoreMs,wallMs,resultCleanupMs\n";
    diagnostics << "schema,case,sourceHash,N,Qs,Qm,initialLeaves,finalLeaves,added,removed,h,g,aOld,aVirtual,changedFields,r,sourceQueries,readOldNodes,"
        "usOld,usNew,umOld,umNew,reverseS,reverseM,mergeRelations,Ls,Lm,writeS,writeM,compareS,compareM,readOldS,readOldM,memberReads,memberWrites,"
        "splitRoots,splitFailures,mergeRoots,mergeFailures,exchanges,attempts,forcedAttempts,primitiveSplits,forcedSplits,primitiveMerges,"
        "budgetRejections,scoreEvaluations,neighborhoodVisits,candidateChecks,traceEvents,iterations,stop,budgetRemaining,extractionNodes,"
        "hPeak,payloadBytes,evaluationCache,blockedObligations,failedMergeObligations,activatedObligations,splitObligations,mergeObligations,"
        "forcedObligations,cacheBirthObligations\n";
    for (std::size_t index = 0; index < 3; ++index)
    {
        auto item = PrepareSource(root, cameras, index);
        DisableDiagnostics(*item->Initial);
        const auto& initial = *item->Initial;
        std::size_t traceEvents = 0;
        const auto verified = Diagnose(initial, traceEvents);
        WriteAudit(diagnostics, *item, traceEvents, verified); diagnostics.flush();
        std::cout << item->Name << " 完整轨迹/目标/义务对照完成，events=" << traceEvents << '\n' << std::flush;
        for (int repeat = -1; repeat < 2; ++repeat)
        {
            Tools::PerformanceTimer restore;
            Dod::DataOrientedRoamState legacy{initial};
            const auto restoreMs = restore.Stop();
            Tools::PerformanceTimer timer;
            Dod::AdvanceSplitTopologySerialForExperiment(legacy);
            const auto legacyMs = timer.Stop();
            std::optional<Planner::NativeTargetPlan> plan;
            Tools::PerformanceTimer planTimer;
            plan.emplace(Planner::BuildNativeSplitTarget(initial));
            const auto planMs = planTimer.Stop();
            Audit::Require(plan->AddedEvents == verified.AddedEvents && plan->RemovedEvents == verified.RemovedEvents &&
                plan->Obligations == verified.Obligations && plan->Evaluations == verified.Evaluations && plan->Stop == verified.Stop &&
                plan->Iteration == verified.Iteration && plan->Metrics.Work == verified.Metrics.Work,
                "natural diagnostics changed planner output");
            const auto finalLeaves = plan->FinalLeafCount;
            Tools::PerformanceTimer cleanup;
            plan.reset();
            const auto cleanupMs = cleanup.Stop();
            if (repeat < 0) continue;
            const auto row = [&](const char* method, double restoreTime, double wallTime, double cleanupTime, std::size_t leaves) {
                output << std::setprecision(12) << "2," << item->Name << ',' << item->InputHash << ',' << method << ',' << repeat
                    << ',' << initial.Nodes.size() << ',' << initial.SplitQueue.size() << ',' << initial.MergeQueue.size()
                    << ',' << initial.ActiveLeafNodes.size() << ',' << leaves << ',' << item->SourceMs << ',' << restoreTime
                    << ',' << wallTime << ',' << cleanupTime << '\n';
            };
            row("legacy", restoreMs, legacyMs, 0, legacy.ActiveLeafNodes.size());
            row("planner", 0, planMs, cleanupMs, finalLeaves);
        }
        output.flush();
        std::cout << item->Name << " 短计量完成\n" << std::flush;
    }
    if (!output || !diagnostics) throw std::runtime_error("规划结果写入失败");
}
}

int main(int argc, char** argv)
{
    try
    {
        if (argc == 5 && std::string_view(argv[4]) == "--planner")
        {
            RunPlannerProbe(argv[1], argv[2], argv[3]);
            return 0;
        }
        if (argc != 4) throw std::invalid_argument("用法：探针 仓库根目录 冻结相机文件 输出文件");
        if (std::filesystem::exists(argv[3])) throw std::invalid_argument("拒绝覆盖已有结果");
        std::ofstream output(argv[3]);
        if (!output) throw std::runtime_error("无法创建输出");
        output << "schema,case,sourceHash,method,repeat,N,Qs,Qm,initialLeaves,finalLeaves,sourceMs,restoreMs,wallMs\n";
        for (std::size_t index = 0; index < 3; ++index)
        {
            auto item = PrepareSource(argv[1], argv[2], index);
            const auto& initial = *item->Initial;
            // 三次执行恢复同一输入，一次热身、两次计量；恢复和哈希均在阶段外
            for (int repeat = -1; repeat < 2; ++repeat)
            {
                Tools::PerformanceTimer restore;
                Dod::DataOrientedRoamState state{initial};
                DisableDiagnostics(state);
                const auto restoreMs = restore.Stop();
                Tools::PerformanceTimer timer;
                Dod::AdvanceSplitTopologySerialForExperiment(state);
                const auto wallMs = timer.Stop();
                if (repeat < 0) continue;
                output << std::setprecision(12) << "1," << item->Name << ',' << item->InputHash
                    << ",legacy," << repeat << ',' << initial.Nodes.size() << ',' << initial.SplitQueue.size()
                    << ',' << initial.MergeQueue.size() << ',' << initial.ActiveLeafNodes.size() << ','
                    << state.ActiveLeafNodes.size() << ',' << item->SourceMs << ',' << restoreMs << ',' << wallMs << '\n';
            }
            output.flush();
            std::cout << item->Name << " 完成\n" << std::flush;
        }
        if (!output) throw std::runtime_error("写入结果失败");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
