#include "experiment/greedy_transactional_lod/TransactionalInput.h"
#include "experiment/greedy_transactional_lod/TransactionalScalingProtocol.h"
#include "experiment/greedy_transactional_lod/TransactionalQualityReport.h"
#include "experiment/greedy_transactional_lod/TransactionalPipeline.h"
#include "experiment/greedy_transactional_lod/TransactionalCommit.h"
#include "experiment/greedy_transactional_lod/TransactionalValidation.h"
#include "experiment/greedy_transactional_lod/TransactionalDynamicReference.h"
#include "experiment/roam_materialization/MaterializationExecutor.h"
#include "tools/profiling/ProfileSession.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace ParallelRoam::Experiment::GreedyTransactionalLod;
using Clock=std::chrono::steady_clock;
double Seconds(Clock::time_point start) { return std::chrono::duration<double>(Clock::now()-start).count(); }

using Quality=TransactionalQualitySummary;
Quality Summarize(const TransactionalSamples& samples) { return TransactionalQualityReport::Summarize(samples); }

void WriteReport(const std::filesystem::path& path,const Configuration& config,const CertifiedBatch& batch,
    const WorkLedger& work,const Quality& before,const Quality& after,std::size_t faces,bool diagnostic)
{
    std::ofstream out(path);out<<std::setprecision(17);
    out<<"{\"numericContract\":\"gmp-v1-binary64\",\"scenario\":"<<std::quoted(config.Scenario)
        <<",\"heightPolicy\":\"LocalCenterRefit\",\"heightGuard\":"<<(config.HeightGuard ? "true" : "false")
        <<",\"sampleIndex\":"<<config.SampleIndex<<",\"diagnostic\":"<<(diagnostic ? "true" : "false")
        <<",\"pairAuditComplete\":"<<(batch.PairAuditComplete ? "true" : "false")
        <<",\"D_raw\":"<<batch.Raw<<",\"examined\":"<<batch.Examined<<",\"receivers\":"<<batch.Receivers
        <<",\"D_need\":"<<batch.Need<<",\"D_feasible\":"<<batch.Feasible<<",\"D_executed\":"<<batch.Executed
        <<",\"freeExecuted\":"<<batch.FreeExecuted<<",\"batchWidth\":"<<batch.Exchanges.size()
        <<",\"assignedCredits\":"<<batch.AssignedCredits<<",\"unusedCredits\":"<<batch.UnusedCredits<<",\"faces\":"<<faces;
    const auto quality=[&](const char* name,const Quality& q) {
        out<<",\""<<name<<"\":{\"sampledScreenMaxPx\":"<<q.Screen<<",\"sampledHeightMax\":"<<q.Height
            <<",\"terrainSampleScreenRms\":"<<q.Rms<<",\"screenSample\":"<<q.ScreenSample<<",\"heightSample\":"<<q.HeightSample<<'}';
    };
    if (diagnostic) { quality("before",before);quality("after",after); }
    out<<",\"seconds\":{";bool first=true;
    for (const auto& [name,value] : work.Seconds) { out<<(first ? "" : ",")<<std::quoted(name)<<':'<<value;first=false; }
    out<<"},\"reasons\":{";first=true;
    for (const auto& [name,value] : work.Reasons) { out<<(first ? "" : ",")<<std::quoted(name)<<':'<<value;first=false; }
    out<<"},\"work\":{\"locationTests\":"<<work.LocationTests<<",\"sampleContributions\":"<<work.SampleContributions
        <<",\"sampleEvaluations\":"<<work.SampleEvaluations<<",\"proposals\":"<<work.Proposals
        <<",\"sampleTouches\":"<<work.SampleTouches<<",\"constraints\":"<<work.Constraints
        <<",\"filterChecks\":"<<work.FilterChecks<<",\"exactChecks\":"<<work.ExactChecks
        <<",\"ringVisits\":"<<work.RingVisits<<",\"earTests\":"<<work.EarTests<<",\"pairChecks\":"<<work.PairChecks
        <<",\"reservationChecks\":"<<work.ReservationChecks<<",\"donorReuse\":"<<work.DonorReuse<<",\"conflicts\":"<<work.Conflicts
        <<",\"preparedFaces\":"<<work.PreparedFaces<<",\"preparedVertices\":"<<work.PreparedVertices<<",\"preparedEdges\":"<<work.PreparedEdges
        <<",\"repairSamples\":"<<work.RepairSamples<<",\"repairFaces\":"<<work.RepairFaces<<",\"orderVisits\":"<<work.OrderVisits
        <<",\"meshVertices\":"<<work.MeshVertices<<",\"meshIndices\":"<<work.MeshIndices<<",\"pendingBlocks\":"<<work.PendingBlocks
        <<",\"heightSamples\":"<<work.HeightSamples<<",\"heightExactSamples\":"<<work.HeightExactSamples
        <<",\"heightGuardChecks\":"<<work.HeightGuardChecks<<",\"heightGuardRejected\":"<<work.HeightGuardRejected
        <<",\"capacityGrowths\":"<<work.CapacityGrowths<<",\"capacityBytesReserved\":"<<work.CapacityBytesReserved
        <<",\"capacityBytesRelocated\":"<<work.CapacityBytesRelocated<<",\"candidateUpdates\":"<<work.CandidateUpdates
        <<",\"donorIndexUpdates\":"<<work.DonorIndexUpdates<<",\"receiverCacheHits\":"<<work.ReceiverCacheHits
        <<",\"donorCacheHits\":"<<work.DonorCacheHits<<",\"cacheInvalidations\":"<<work.CacheInvalidations
        <<",\"rootObservations\":"<<work.RootObservations
        <<",\"viewBufferAllocations\":"<<work.ViewBufferAllocations<<",\"viewBufferBytes\":"<<work.ViewBufferBytes
        <<",\"indexBlocks\":"<<work.IndexBlocks<<",\"indexSlots\":"<<work.IndexSlots
        <<",\"indexComparisons\":"<<work.IndexComparisons<<",\"indexQueryBlocks\":"<<work.IndexQueryBlocks
        <<",\"receiverConstructed\":"<<work.ReceiverConstructed<<",\"evidenceLookups\":"<<work.EvidenceLookups
        <<",\"evidenceHits\":"<<work.EvidenceHits<<",\"evidenceBuilds\":"<<work.EvidenceBuilds
        <<",\"evidenceBytes\":"<<work.EvidenceBytes<<",\"evidenceFaceTests\":"<<work.EvidenceFaceTests
        <<",\"footprintBuilds\":"<<work.FootprintBuilds<<",\"donorTouched\":"<<work.DonorTouched
        <<",\"donorCertified\":"<<work.DonorCertified<<'}';
    out<<",\"intents\":[";
    for (std::size_t i=0;i<batch.IntentIds.size();++i)
        out<<(i ? "," : "")<<'['<<batch.IntentIds[i]<<','<<std::quoted(batch.IntentResults[i])<<']';
    out<<"],\"pool\":[";
    for (std::size_t i=0;i<batch.PoolIds.size();++i) out<<(i ? "," : "")<<batch.PoolIds[i];
    out<<"],\"attempts\":[";
    for (std::size_t i=0;i<batch.Attempts.size();++i)
    {
        out<<(i ? "," : "")<<'[';
        for (std::size_t j=0;j<batch.Attempts[i].size();++j)
            out<<(j ? "," : "")<<'['<<std::quoted(std::string(1,batch.Attempts[i][j].first))<<','
                <<std::quoted(batch.Attempts[i][j].second)<<']';
        out<<']';
    }
    out<<"],\"execution\":{";first=true;
    for (const auto& [phase,values] : work.Execution)
    {
        out<<(first ? "" : ",")<<std::quoted(phase)<<":["<<values[0]<<','<<values[1]<<','<<values[2]<<']';first=false;
    }
    out<<"},\"exchanges\":[";first=true;
    for (const auto& exchange : batch.Exchanges)
    {
        out<<(first ? "" : ",")<<"{\"rootSlot\":"<<exchange.Receiver.Root<<",\"kind\":"<<std::quoted(std::string(1,exchange.Receiver.Kind))
            <<",\"targetMicropixels\":"<<exchange.Receiver.TargetMicropixels<<",\"hasDonor\":"<<(exchange.HasDonor ? "true" : "false")
            <<",\"donor\":"<<exchange.Donor.Center;
        const auto patch=[&](const char* name,const Proposal& proposal) {
            out<<",\""<<name<<"\":{\"support\":[";
            for (std::size_t i=0;i<proposal.Support.size();++i) out<<(i ? "," : "")<<proposal.Support[i];
            out<<"],\"faces\":[";
            for (std::size_t i=0;i<proposal.Faces.size();++i)
            {
                const auto& f=proposal.Faces[i];out<<(i ? "," : "")<<'['<<f[0]<<','<<f[1]<<','<<f[2]<<']';
            }
            out<<"]}";
        };
        patch("receiver",exchange.Receiver);if (exchange.HasDonor) patch("reclamation",exchange.Donor);
        out<<'}';first=false;
    }
    out<<"]}";out.close();if (!out) throw std::runtime_error("报告写入失败");
}
}

int main(int argc,char** argv)
{
    bool ownsOutput=false;
    try
    {
        if (argc<4 || (argc-4)%2) throw std::runtime_error("参数：快照文件 运行模式 输出目录 [--profile 重放次数]");
        const std::filesystem::path snapshot{argv[1]},rootOutput{argv[3]};const std::string mode{argv[2]};
        if (mode!="diagnostic" && mode!="timing" && mode!="persistent-diagnostic" && mode!="persistent-timing" &&
            mode!="guard-diagnostic" && mode!="guard-timing" && mode!="trajectory-a-diagnostic" && mode!="trajectory-a-timing" &&
            mode!="trajectory-b-diagnostic" && mode!="trajectory-b-timing" &&
            mode!="trajectory-c-diagnostic" && mode!="trajectory-c-timing") throw std::runtime_error("运行模式非法");
        const bool diagnostic=mode.ends_with("diagnostic"),persistent=mode.starts_with("persistent"),guard=mode.starts_with("guard");
        const bool trajectory=mode.starts_with("trajectory"),dynamicMode=mode.starts_with("trajectory-a");
        const bool parallel=mode.starts_with("trajectory-c");
        bool profiling=false;int replays=1;std::size_t workers=parallel ? 4U : 1U;
        std::string limitPolicy="fixed64";std::set<std::string> options;
        for (int i=4;i<argc;i+=2)
        {
            const std::string key=argv[i],value=argv[i+1];
            if (!options.insert(key).second) throw std::runtime_error("重复参数");
            if (key=="--profile")
            {
                std::size_t used{};replays=std::stoi(value,&used);
                if (used!=value.size()) throw std::runtime_error("重放次数非法");
                profiling=true;
            }
            else if (key=="--workers" && (value=="4" || value=="8") && parallel)
                workers=value=="4" ? 4U : 8U;
            else if (key=="--limit-policy" && (value=="fixed64" || value=="scaled") && trajectory && !dynamicMode)
                limitPolicy=value;
            else throw std::runtime_error("运行参数或组合非法");
        }
        if (profiling && (!trajectory || diagnostic || replays<1 || replays>32))
            throw std::runtime_error("profile 仅允许轨迹计时模式和 1～32 次同种子重放");
        if (std::filesystem::exists(rootOutput)) throw std::runtime_error("拒绝覆盖已有输出");
        std::filesystem::create_directories(rootOutput);
        ownsOutput=true;
        std::unique_ptr<ParallelRoam::Tools::Profiling::ProfileSession> profile;
        if (profiling) profile=std::make_unique<ParallelRoam::Tools::Profiling::ProfileSession>(rootOutput/"profile-windows.csv");
        WorkLedger initialization;initialization.Deadline=Clock::now()+std::chrono::seconds(120);
        auto started=Clock::now();auto input=TransactionalInput::Load(snapshot,limitPolicy);input.Config.HeightGuard=guard;
        const bool scaling=TransactionalScalingProtocol::Budget(input.Config.Scenario).has_value();
        initialization.Seconds["input"]=Seconds(started);
        std::vector<Configuration> views;
        if (trajectory)
        {
            started=Clock::now();views=TransactionalInput::Views(std::filesystem::current_path(),input.Config);
            initialization.Seconds["view_file"]=Seconds(started);
        }
        // 线程池在整个轨迹期间复用，启动成本另列；正常 update 包含每次派发与等待
        started=Clock::now();
        std::unique_ptr<ParallelRoam::Experiment::RoamMaterialization::MaterializationExecutor> executor;
        TransactionalExecution execution;execution.Diagnostics=diagnostic;
        if (parallel)
        {
            executor=std::make_unique<ParallelRoam::Experiment::RoamMaterialization::MaterializationExecutor>(workers);
            const auto adapter=executor->Execution();execution.Workers=adapter.Workers;execution.Dispatch=adapter.Dispatch;
        }
        initialization.Seconds["executor_initialize"]=Seconds(started);
        // 补采重建同一不可变输入的状态，不能重复已收敛帧来改变工作定义
        for (int replay=0;replay<replays;++replay)
        {
            const auto output=profiling ? rootOutput/("replay-"+std::to_string(replay)) : rootOutput;
            if (profiling) std::filesystem::create_directories(output);
            started=Clock::now();
            std::unique_ptr<TransactionalDynamicReference> reference;
            std::unique_ptr<TransactionalPipeline> batchPipeline;
            if (dynamicMode) reference=std::make_unique<TransactionalDynamicReference>(input);
            else batchPipeline=std::make_unique<TransactionalPipeline>(input,execution);
            auto& pipeline=dynamicMode ? reference->Pipeline() : *batchPipeline;
            initialization.Seconds["state_initialize"]=Seconds(started);
            pipeline.Initialize(initialization);
            {
                std::ofstream out(output/"initialize.json");out<<std::setprecision(17)<<'{';bool first=true;
                for (const auto& [name,value] : initialization.Seconds)
                { out<<(first ? "" : ",")<<std::quoted(name)<<':'<<value;first=false; } out<<'}';
            }
            ParallelRoam::Terrain::TerrainMeshData mirror;
            if (diagnostic)
            {
                TransactionalValidation::Validate(pipeline.State());
                TransactionalValidation::Consume(pipeline.ConsumeMesh(),mirror);
            }
            const int rounds=trajectory ? 8 : (persistent ? 3 : 1);
            std::ofstream trajectoryLog;
            if (trajectory) trajectoryLog.open(output/"trajectory.jsonl");
            std::vector<double> leavingErrors;std::vector<bool> lastVisible;
            const std::array<int,8> viewOrder{0,0,0,1,2,3,4,0};
            for (int round=0;round<rounds;++round)
            {
                const auto directory=(persistent || trajectory) ? output/("round-"+std::to_string(round)) : output;
                std::filesystem::create_directories(directory);
                WorkLedger work;work.Deadline=Clock::now()+std::chrono::seconds(120);
                if (diagnostic && trajectory)
                {
                    lastVisible.clear();
                    for (Slot sid=0;sid<pipeline.Samples().SampleCount();++sid)
                        lastVisible.push_back(pipeline.Samples().Projection(sid).Visible);
                }
                if (profile) profile->Begin(replay,round);
                const auto frameStart=Clock::now();
                if (trajectory) pipeline.SetView(views[static_cast<std::size_t>(viewOrder[static_cast<std::size_t>(round)])],work);
                double returnBefore=0,newVisibleBefore=0;std::vector<Slot> newlyVisible;
                const auto excess=[&]() {
                    double maximum=-std::numeric_limits<double>::infinity();
                    for (Slot sid=0;sid<leavingErrors.size();++sid)
                        if (pipeline.Samples().Value(sid).Visible)
                            maximum=std::max(maximum,std::sqrt(pipeline.Samples().Value(sid).ErrorSquared)-leavingErrors[sid]);
                    return std::isfinite(maximum) ? maximum : 0;
                };
                // 逐点质量诊断单列，计时遍不遍历 Q 做额外归因
                auto qualityStart=Clock::now();
                if (diagnostic && trajectory)
                {
                    if (round==7) returnBefore=excess();
                    for (Slot sid=0;sid<lastVisible.size();++sid)
                        if (pipeline.Samples().Value(sid).Visible && !lastVisible[sid])
                        {
                            newlyVisible.push_back(sid);
                            newVisibleBefore=std::max(newVisibleBefore,std::sqrt(pipeline.Samples().Value(sid).ErrorSquared));
                        }
                }
                const auto before=diagnostic ? Summarize(pipeline.Samples()) : Quality{};
                const double preDiagnostic=Seconds(qualityStart);
                CertifiedBatch batch;std::string stop="batch_complete";std::vector<std::array<Identity,3>> decisions;
                if (dynamicMode)
                {
                    auto result=reference->Update(work);batch=std::move(result.Summary);stop=result.Stop;decisions=std::move(result.Decisions);
                }
                else batch=pipeline.Update(work);
                work.Seconds["frame_update"]=Seconds(frameStart)-preDiagnostic;Quality after;
                // 就绪边界包含 Pending 消费与其临时记录释放，文件和质量输出随后执行
                if (!diagnostic)
                {
                    started=Clock::now();
                    { const auto consumed=pipeline.ConsumeMesh();static_cast<void>(consumed); }
                    work.Seconds["consume"]=Seconds(started);
                    work.Seconds["frame_ready"]=work.Seconds["frame_update"]+work.Seconds["consume"];
                }
                if (profile) profile->End();
                if (diagnostic)
                {
                    started=Clock::now();WorkLedger check;check.Deadline=Clock::now()+std::chrono::seconds(120);
                    TransactionalValidation::Validate(pipeline.State());
                    TransactionalValidation::Samples(pipeline.State(),pipeline.Samples(),check);
                    TransactionalValidation::Mesh(pipeline.State(),pipeline.Mesh());after=Summarize(pipeline.Samples());
                    if (after.Screen>before.Screen+1e-8) throw std::runtime_error("冻结视图 sampled maximum 上升");
                    if (guard && after.Height>before.Height+1e-10) throw std::runtime_error("高度保护的全 Q 误差上升");
                    if (round==0 && !dynamicMode)
                    {
                        // 反序只作首批诊断，不让整份状态副本进入正常路径
                        TransactionalState reverse(input);auto reversed=batch;std::reverse(reversed.Exchanges.begin(),reversed.Exchanges.end());
                        TransactionalCommit::Apply(reverse,reversed,check);TransactionalValidation::Validate(reverse);
                        if (!TransactionalValidation::Equivalent(pipeline.State(),reverse)) throw std::runtime_error("反序逻辑结果不等价");
                    }
                    if (round%2==1 || round==rounds-1)
                    {
                        TransactionalValidation::Consume(pipeline.ConsumeMesh(),mirror);
                        const auto empty=pipeline.ConsumeMesh();
                        if (!empty.Vertices.empty() || !empty.Indices.empty()) throw std::runtime_error("重复消费仍有 Pending");
                    }
                    work.Seconds["diagnostic"]=Seconds(started);
                }
                if (trajectory)
                {
                    double returnAfter=0,newVisibleAfter=0;
                    if (diagnostic)
                    {
                        for (auto sid : newlyVisible)
                            newVisibleAfter=std::max(newVisibleAfter,std::sqrt(pipeline.Samples().Value(sid).ErrorSquared));
                        if (round==7) returnAfter=excess();
                        if (round==2)
                        {
                            leavingErrors.clear();
                            for (Slot sid=0;sid<pipeline.Samples().SampleCount();++sid)
                                leavingErrors.push_back(std::sqrt(pipeline.Samples().Projection(sid).ErrorSquared));
                        }
                    }
                    trajectoryLog<<std::setprecision(17)<<"{\"round\":"<<round<<",\"view\":"<<pipeline.State().Config().SampleIndex
                        <<",\"strategy\":"<<std::quoted(dynamicMode ? "dynamic-serial" : (parallel ? "batch-parallel" : "batch-serial"))
                        <<",\"stop\":"<<std::quoted(stop)<<",\"transactions\":"<<batch.Exchanges.size()
                        <<",\"faces\":"<<pipeline.State().FaceCount()<<",\"frameUpdateMs\":"<<work.Seconds["frame_update"]*1000;
                    if (diagnostic)
                        trajectoryLog<<",\"returnBeforeExcessPx\":"<<returnBefore<<",\"returnAfterExcessPx\":"<<returnAfter
                            <<",\"returnRecoveryCensored\":"<<(round==7 && returnAfter>1e-9 ? "true" : "false")
                            <<",\"newlyVisibleSamples\":"<<newlyVisible.size()<<",\"newlyVisibleBeforeMaxPx\":"<<newVisibleBefore
                            <<",\"newlyVisibleAfterMaxPx\":"<<newVisibleAfter;
                    trajectoryLog<<",\"decisions\":[";
                    for (std::size_t i=0;i<decisions.size();++i)
                        trajectoryLog<<(i ? "," : "")<<'['<<decisions[i][0]<<','<<decisions[i][1]<<','<<decisions[i][2]<<']';
                    trajectoryLog<<"]}\n";trajectoryLog.flush();
                }
                if (scaling)
                {
                    // 这些观察只导出状态，不能计入正式墙钟或假装为算法固有维护
                    std::size_t associations=0;
                    for (auto slot : pipeline.State().ActiveFaces()) associations+=pipeline.Samples().FaceSamples(slot).size();
                    std::ofstream metrics(directory/"configuration.json");
                    metrics<<"{\"limitPolicy\":"<<std::quoted(limitPolicy)<<",\"requestedWorkers\":"<<workers
                        <<",\"prefixLimit\":"<<input.Config.PrefixLimit<<",\"donorLimit\":"<<input.Config.DonorLimit
                        <<",\"budget\":"<<input.Config.Budget<<",\"q\":"<<pipeline.Samples().SampleCount()
                        <<",\"associations\":"<<associations<<",\"faceSlots\":"<<pipeline.State().Faces().size()
                        <<",\"vertexSlots\":"<<pipeline.State().Vertices().size()<<",\"rawAfter\":"<<pipeline.Samples().RawCount()<<"}";
                    if (diagnostic) TransactionalQualityReport::Write(pipeline.Samples(),directory);
                }
                started=Clock::now();TransactionalInput::Write(pipeline.State(),directory/"mesh.json");work.Seconds["mesh_file"]=Seconds(started);
                WriteReport(directory/"summary.json",pipeline.State().Config(),batch,work,before,after,pipeline.State().FaceCount(),diagnostic);
                std::cout<<input.Config.Scenario<<'/'<<round<<": 需求 "<<batch.Need<<"，局部可行 "<<batch.Feasible
                    <<"，交换 "<<batch.Executed<<"，空额度 "<<batch.FreeExecuted<<"，更新 "<<work.Seconds["frame_update"]*1000<<" ms"<<std::endl;
            }
            started=Clock::now();reference.reset();batchPipeline.reset();
            if (scaling)
            { std::ofstream life(output/"lifetime.json");life<<"{\"stateDestroyMs\":"<<Seconds(started)*1000<<"}"; }
        }
        started=Clock::now();executor.reset();
        if (scaling)
        { std::ofstream life(rootOutput/"executor-lifetime.json");life<<"{\"destroyMs\":"<<Seconds(started)*1000<<"}"; }
        if (profile) profile->Finish();
    }
    catch (const std::exception& error)
    {
        std::cerr<<error.what()<<'\n';
        if (ownsOutput && argc>=4 && std::filesystem::is_directory(argv[3]))
        {
            const auto path=std::filesystem::path(argv[3])/"failure.txt";
            if (!std::filesystem::exists(path)) { std::ofstream out(path);out<<error.what()<<'\n'; }
        }
        return 1;
    }
}
