#include "DataOrientedRoamNativeMaterializationTestSupport.h"
#include "DataOrientedRoamNativePlannerTestSupport.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamValidation.h"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <numeric>

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

Planner::NativeTargetPlan Diagnose(const Dod::DataOrientedRoamState& initial, std::size_t& eventCount, bool disableCache)
{
    const Audit::StateSnapshot snapshot{initial};
    auto legacy = initial;
    auto iteration = Dod::BeginStrictSplitIteration(legacy);
    const auto before = Audit::Project(legacy);
    Audit::TraceLog oldTrace, newTrace;
    while (iteration.Stop == Dod::TopologySplitStop::Running)
        (void)Dod::AdvanceStrictSplitIterationWithDecisions(legacy, iteration, oldTrace.Sink());
    FinalAudit audit{Audit::Project(legacy)};
    const auto plan = Planner::BuildNativeSplitTarget(initial,
        {newTrace.Sink(), &audit, nullptr, FinalAudit::Finish, true, nullptr, disableCache});
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
    output << ',' << w.ScoreRequests << ',' << w.ScoreCacheHits << ',' << q.DeferredRemovals << ',' << q.RestoredEntries << ','
        << q.DeferredErases << ',' << q.UnchangedUpserts << ',' << s.UnchangedUpserts << ',' << q.DeferredPeak << ','
        << q.DeferredCapacity << '\n';
}

/// <summary>
/// 有限探针模式只选择已冻结来源，不允许通过参数修改地形或目标语义
/// </summary>
struct ProbeOptions
{
    std::string_view Case;
    std::string_view Mode{"all"};
    bool Reverse{false};
    bool DisableScoreCache{false};
};

bool SamePlan(const Planner::NativeTargetPlan& a, const Planner::NativeTargetPlan& b)
{
    return a.AddedEvents == b.AddedEvents && a.RemovedEvents == b.RemovedEvents && a.Obligations == b.Obligations &&
        a.Evaluations == b.Evaluations && a.Stop == b.Stop && a.Iteration == b.Iteration &&
        a.FinalLeafCount == b.FinalLeafCount && a.RemainingBudget == b.RemainingBudget && a.Metrics.Work == b.Metrics.Work;
}

void WriteStorage(std::ostream& output, const SourceCase& item, const Planner::NativeTargetPlan& plan, std::string_view mode)
{
    constexpr std::array names{"nodes", "split_slots", "memberships", "merge_slots", "merge_reverse_shared",
        "merge_representative_shared", "merge_partner_shared", "scores"};
    for (std::size_t i = 0; i < names.size(); ++i)
    {
        const auto& m = plan.Metrics.Storage[i];
        output << "7," << item.Name << ',' << item.InputHash << ',' << mode << ',' << names[i] << ',' << m.Records << ','
            << m.RecordCapacity << ',' << m.IndexCapacity << ',' << m.RecordBytes << ',' << m.IndexBytes << ',' << m.ReservedBytes << ','
            << m.PeakReservedBytes << ',' << m.Allocations << ',' << m.Rehashes << ',' << m.MovedRecords << ','
            << m.RehashedRecords << ',' << m.InitializedSlots << ',' << m.Lookups << ',' << m.Probes << ',' << m.MaximumProbe << ','
            << m.PagedIndex << ',' << m.IndexPages << ',' << m.DirectorySize << ',' << m.DirectoryCapacity << ','
            << m.DirectoryInitialized << ',' << m.DirectoryMoved << ',' << m.DenseIndex << ',' << m.SourceCopies << ','
            << m.MovedIndexEntries << '\n';
    }
}

void MeasureCosts(const SourceCase& item, std::ostream& output, std::ostream& storage, bool disableCache)
{
    constexpr std::array names{"construct", "control", "prerequisite", "merge_control", "neighborhood",
        "candidate_maintenance", "eligibility", "score", "queue", "create", "split_changes", "merge_changes",
        "extract", "sort", "metrics", "destroy"};
    static_assert(names.size() == Planner::NativePlanningCosts::Count);
    // 普通调用先形成结果对照；大区间与详细遍随后分别执行，不包含语义审计回调
    Planner::NativePlanningAudit settings;
    settings.DisableScoreCache = disableCache;
    const auto expected = Planner::BuildNativeSplitTarget(*item.Initial, settings);
    for (const bool detailed : {false, true})
    {
        Planner::NativePlanningCosts costs;
        costs.Detailed = detailed;
        Planner::NativePlanningAudit audit;
        audit.Costs = &costs;
        audit.DisableScoreCache = disableCache;
        Tools::PerformanceTimer timer;
        const auto result = Planner::BuildNativeSplitTarget(*item.Initial, audit);
        const auto wall = timer.Stop();
        Audit::Require(SamePlan(expected, result) && costs.Active == Planner::NativePlanningCost::Count,
            "cost observation changed the plan or left an active scope");
        const auto sum = std::accumulate(costs.Milliseconds.begin(), costs.Milliseconds.end(), 0.0);
        WriteStorage(storage, item, result, detailed ? "detail" : "coarse");
        for (std::size_t i = 0; i < names.size(); ++i)
            output << std::setprecision(12) << "1," << item.Name << ',' << item.InputHash << ',' << detailed << ','
                << names[i] << ',' << costs.Calls[i] << ',' << costs.Milliseconds[i] << ',' << wall << ',' << sum << '\n';
    }
}

void RunPlannerProbe(const char* root, const char* cameras, const char* outputPath, const ProbeOptions& options)
{
    const std::filesystem::path auditPath{std::string(outputPath) + ".audit.csv"};
    const std::filesystem::path costsPath{std::string(outputPath) + ".costs.csv"};
    const std::filesystem::path storagePath{std::string(outputPath) + ".storage.csv"};
    if (std::filesystem::exists(outputPath) || std::filesystem::exists(auditPath) || std::filesystem::exists(costsPath) ||
        std::filesystem::exists(storagePath))
        throw std::invalid_argument("拒绝覆盖已有结果");
    std::ofstream output{outputPath}, diagnostics{auditPath}, costOutput{costsPath}, storage{storagePath};
    if (!output || !diagnostics || !costOutput || !storage) throw std::runtime_error("无法创建规划诊断输出");
    costOutput << "schema,case,sourceHash,detailed,phase,calls,selfMs,wallMs,sumSelfMs\n";
    storage << "schema,case,sourceHash,mode,table,records,recordCapacity,indexCapacity,recordBytes,indexBytes,reservedBytes,peakReservedBytes,"
        "allocations,rehashes,movedRecords,rehashedRecords,initializedSlots,lookups,probes,maximumProbe,"
        "pagedIndex,indexPages,directorySize,directoryCapacity,directoryInitialized,directoryMoved,denseIndex,sourceCopies,movedIndexEntries\n";
    output << "schema,case,sourceHash,method,repeat,N,Qs,Qm,initialLeaves,finalLeaves,sourceMs,restoreMs,wallMs,resultCleanupMs\n";
    diagnostics << "schema,case,sourceHash,N,Qs,Qm,initialLeaves,finalLeaves,added,removed,h,g,aOld,aVirtual,changedFields,r,sourceQueries,readOldNodes,"
        "usOld,usNew,umOld,umNew,reverseS,reverseM,mergeRelations,Ls,Lm,writeS,writeM,compareS,compareM,readOldS,readOldM,memberReads,memberWrites,"
        "splitRoots,splitFailures,mergeRoots,mergeFailures,exchanges,attempts,forcedAttempts,primitiveSplits,forcedSplits,primitiveMerges,"
        "budgetRejections,scoreEvaluations,neighborhoodVisits,candidateChecks,traceEvents,iterations,stop,budgetRemaining,extractionNodes,"
        "hPeak,payloadBytes,evaluationCache,blockedObligations,failedMergeObligations,activatedObligations,splitObligations,mergeObligations,"
        "forcedObligations,cacheBirthObligations,scoreRequests,scoreCacheHits,deferredMergeRemovals,restoredMergeEntries,"
        "deferredMergeErases,unchangedMergeUpserts,unchangedSplitUpserts,deferredMergePeak,deferredMergeCapacity\n";
    constexpr std::array cases{"test129-a-b4096-sample14", "peking547-a-b20000-sample14",
        "peking547-budget-orbit64-b200000-sample14"};
    if (!options.Case.empty() && std::find(cases.begin(), cases.end(), options.Case) == cases.end())
        throw std::invalid_argument("未知冻结案例");
    if (options.Mode != "all" && options.Mode != "timing" && options.Mode != "diagnostic" && options.Mode != "cost")
        throw std::invalid_argument("未知探针模式");
    for (std::size_t index = 0; index < cases.size(); ++index)
    {
        if (!options.Case.empty() && options.Case != cases[index]) continue;
        auto item = PrepareSource(root, cameras, index);
        DisableDiagnostics(*item->Initial);
        const auto& initial = *item->Initial;
        std::optional<Planner::NativeTargetPlan> verified;
        if (options.Mode == "all" || options.Mode == "diagnostic")
        {
            std::size_t traceEvents = 0;
            verified = Diagnose(initial, traceEvents, options.DisableScoreCache);
            WriteAudit(diagnostics, *item, traceEvents, *verified); diagnostics.flush();
            WriteStorage(storage, *item, *verified, "semantic");
            std::cout << item->Name << " 完整轨迹/目标/义务对照完成，events=" << traceEvents << '\n' << std::flush;
        }
        if (options.Mode == "diagnostic") continue;
        if (options.Mode == "cost")
        {
            MeasureCosts(*item, costOutput, storage, options.DisableScoreCache); costOutput.flush();
            std::cout << item->Name << " 互斥成本诊断完成\n" << std::flush;
            continue;
        }
        for (int repeat = -1; repeat < 2; ++repeat)
        {
            float restoreMs = 0, legacyMs = 0, planMs = 0;
            std::size_t legacyLeaves = 0;
            const auto runLegacy = [&] {
                Tools::PerformanceTimer restore;
                Dod::DataOrientedRoamState legacy{initial};
                restoreMs = restore.Stop();
                Tools::PerformanceTimer timer;
                Dod::AdvanceSplitTopologySerialForExperiment(legacy);
                legacyMs = timer.Stop(); legacyLeaves = legacy.ActiveLeafNodes.size();
            };
            std::optional<Planner::NativeTargetPlan> plan;
            const auto runPlan = [&] {
                Tools::PerformanceTimer planTimer;
                Planner::NativePlanningAudit settings;
                settings.DisableScoreCache = options.DisableScoreCache;
                plan.emplace(Planner::BuildNativeSplitTarget(initial, settings));
                planMs = planTimer.Stop();
            };
            if (options.Reverse) { runPlan(); runLegacy(); }
            else { runLegacy(); runPlan(); }
            if (verified) Audit::Require(SamePlan(*plan, *verified), "natural diagnostics changed planner output");
            Audit::Require(plan->FinalLeafCount == legacyLeaves, "timing methods disagree on leaf count");
            const auto finalLeaves = plan->FinalLeafCount;
            if (!verified)
            {
                // 无诊断计量只在预热后保存一次结果，完整终态审计由独立模式承担
                WriteAudit(diagnostics, *item, 0, *plan);
                WriteStorage(storage, *item, *plan, "normal");
                verified = *plan;
            }
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
            row("legacy", restoreMs, legacyMs, 0, legacyLeaves);
            row("planner", 0, planMs, cleanupMs, finalLeaves);
        }
        output.flush();
        std::cout << item->Name << " 短计量完成\n" << std::flush;
    }
    if (!output || !diagnostics || !costOutput || !storage) throw std::runtime_error("规划结果写入失败");
}
}

int main(int argc, char** argv)
{
    try
    {
        if (argc >= 5 && std::string_view(argv[4]) == "--planner")
        {
            ProbeOptions options;
            for (int i = 5; i < argc; ++i)
            {
                const std::string_view argument{argv[i]};
                if (argument == "--reverse") options.Reverse = true;
                else if (argument == "--no-score-cache") options.DisableScoreCache = true;
                else if ((argument == "--case" || argument == "--mode") && i + 1 < argc)
                {
                    const std::string_view value{argv[++i]};
                    if (argument == "--case") options.Case = value; else options.Mode = value;
                }
                else throw std::invalid_argument("未知或不完整的规划探针参数");
            }
            RunPlannerProbe(argv[1], argv[2], argv[3], options);
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
