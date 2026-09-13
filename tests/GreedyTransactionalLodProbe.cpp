#include "experiment/greedy_transactional_lod/TransactionalInput.h"
#include "experiment/greedy_transactional_lod/TransactionalPipeline.h"
#include "experiment/greedy_transactional_lod/TransactionalCommit.h"
#include "experiment/greedy_transactional_lod/TransactionalValidation.h"

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

/// <summary>
/// 诊断摘要仍报告独立绝对质量，局部接受不替代全域量
/// </summary>
struct Quality
{
    double Screen{}, Height{}, Rms{};
    Slot ScreenSample{}, HeightSample{};
};

Quality Summarize(const TransactionalSamples& samples)
{
    Quality result;double sum=0;std::size_t visible=0;
    for (Slot sid=0;sid<samples.Values().size();++sid)
    {
        const auto& v=samples.Values()[sid];
        if (v.ErrorSquared>result.Screen) { result.Screen=v.ErrorSquared;result.ScreenSample=sid; }
        if (v.HeightError>result.Height) { result.Height=v.HeightError;result.HeightSample=sid; }
        if (v.Visible) { sum+=v.ErrorSquared;++visible; }
    }
    result.Screen=std::sqrt(result.Screen);result.Rms=visible ? std::sqrt(sum/static_cast<double>(visible)) : 0;
    return result;
}

void WriteReport(const std::filesystem::path& path,const Configuration& config,const CertifiedBatch& batch,
    const WorkLedger& work,const Quality& before,const Quality& after,std::size_t faces,bool diagnostic)
{
    std::ofstream out(path);out<<std::setprecision(17);
    out<<"{\"numericContract\":\"gmp-v1-binary64\",\"scenario\":"<<std::quoted(config.Scenario)
        <<",\"heightPolicy\":\"LocalCenterRefit\",\"heightGuard\":"<<(config.HeightGuard ? "true" : "false")
        <<",\"sampleIndex\":"<<config.SampleIndex<<",\"diagnostic\":"<<(diagnostic ? "true" : "false")
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
        <<",\"capacityBytesRelocated\":"<<work.CapacityBytesRelocated<<'}';
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
    out<<"],\"exchanges\":[";first=true;
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
    try
    {
        if (argc!=4) throw std::runtime_error("参数：快照文件 diagnostic/timing/persistent-diagnostic/persistent-timing/guard-diagnostic/guard-timing 输出目录");
        const std::filesystem::path snapshot{argv[1]},output{argv[3]};const std::string mode{argv[2]};
        if (mode!="diagnostic" && mode!="timing" && mode!="persistent-diagnostic" && mode!="persistent-timing" &&
            mode!="guard-diagnostic" && mode!="guard-timing") throw std::runtime_error("运行模式非法");
        const bool diagnostic=mode.ends_with("diagnostic"),persistent=mode.starts_with("persistent"),guard=mode.starts_with("guard");
        if (std::filesystem::exists(output)) throw std::runtime_error("拒绝覆盖已有输出");
        std::filesystem::create_directories(output);
        WorkLedger initialization;initialization.Deadline=Clock::now()+std::chrono::seconds(120);
        auto started=Clock::now();auto input=TransactionalInput::Load(snapshot);input.Config.HeightGuard=guard;
        initialization.Seconds["input"]=Seconds(started);
        started=Clock::now();TransactionalPipeline pipeline(input);initialization.Seconds["state_initialize"]=Seconds(started);
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
        const int rounds=persistent ? 3 : 1;
        for (int round=0;round<rounds;++round)
        {
            const auto directory=persistent ? output/("round-"+std::to_string(round)) : output;
            std::filesystem::create_directories(directory);
            WorkLedger work;work.Deadline=Clock::now()+std::chrono::seconds(120);
            const auto before=diagnostic ? Summarize(pipeline.Samples()) : Quality{};
            const auto batch=pipeline.Update(work);Quality after;
            if (diagnostic)
            {
                started=Clock::now();WorkLedger check;check.Deadline=Clock::now()+std::chrono::seconds(120);
                TransactionalValidation::Validate(pipeline.State());
                TransactionalValidation::Samples(pipeline.State(),pipeline.Samples(),check);
                TransactionalValidation::Mesh(pipeline.State(),pipeline.Mesh());after=Summarize(pipeline.Samples());
                if (after.Screen>before.Screen+1e-8) throw std::runtime_error("冻结视图 sampled maximum 上升");
                if (guard && after.Height>before.Height+1e-10) throw std::runtime_error("高度保护的全 Q 误差上升");
                if (round==0)
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
            else
            {
                started=Clock::now();const auto consumed=pipeline.ConsumeMesh();static_cast<void>(consumed);
                work.Seconds["consume"]=Seconds(started);
            }
            started=Clock::now();TransactionalInput::Write(pipeline.State(),directory/"mesh.json");work.Seconds["mesh_file"]=Seconds(started);
            WriteReport(directory/"summary.json",input.Config,batch,work,before,after,pipeline.State().FaceCount(),diagnostic);
            std::cout<<input.Config.Scenario<<'/'<<round<<": 需求 "<<batch.Need<<"，局部可行 "<<batch.Feasible
                <<"，交换 "<<batch.Executed<<"，空额度 "<<batch.FreeExecuted<<"，更新 "<<work.Seconds["update"]*1000<<" ms\n";
        }
    }
    catch (const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
