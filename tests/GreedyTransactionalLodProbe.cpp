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
        <<",\"sampleIndex\":"<<config.SampleIndex<<",\"diagnostic\":"<<(diagnostic ? "true" : "false")
        <<",\"D_raw\":"<<batch.Raw<<",\"examined\":"<<batch.Examined<<",\"receivers\":"<<batch.Receivers
        <<",\"D_need\":"<<batch.Need<<",\"D_feasible\":"<<batch.Feasible<<",\"D_executed\":"<<batch.Executed
        <<",\"freeExecuted\":"<<batch.FreeExecuted<<",\"batchWidth\":"<<batch.Exchanges.size()
        <<",\"assignedCredits\":"<<batch.AssignedCredits<<",\"unusedCredits\":"<<batch.UnusedCredits<<",\"faces\":"<<faces;
    const auto quality=[&](const char* name,const Quality& q) {
        out<<",\""<<name<<"\":{\"sampledScreenMaxPx\":"<<q.Screen<<",\"sampledHeightMax\":"<<q.Height
            <<",\"terrainSampleScreenRms\":"<<q.Rms<<",\"screenSample\":"<<q.ScreenSample<<",\"heightSample\":"<<q.HeightSample<<'}';
    };
    quality("before",before);if (diagnostic) quality("after",after);
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
        <<",\"preparedFaces\":"<<work.PreparedFaces<<",\"preparedVertices\":"<<work.PreparedVertices<<",\"preparedEdges\":"<<work.PreparedEdges<<'}';
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
        if (argc!=4) throw std::runtime_error("参数：快照文件 diagnostic/timing 输出目录");
        const std::filesystem::path snapshot{argv[1]},output{argv[3]};const std::string mode{argv[2]};
        if (mode!="diagnostic" && mode!="timing") throw std::runtime_error("运行模式非法");
        const bool diagnostic=mode=="diagnostic";
        if (std::filesystem::exists(output)) throw std::runtime_error("拒绝覆盖已有输出");
        std::filesystem::create_directories(output);
        WorkLedger work;work.Deadline=Clock::now()+std::chrono::seconds(120);
        auto started=Clock::now();const auto input=TransactionalInput::Load(snapshot);work.Seconds["input"]=Seconds(started);
        started=Clock::now();TransactionalPipeline pipeline(input);work.Seconds["initialize"]=Seconds(started);
        if (diagnostic) { started=Clock::now();TransactionalValidation::Validate(pipeline.State());work.Seconds["initial_validation"]=Seconds(started); }
        const auto batch=pipeline.Update(work);const auto before=Summarize(pipeline.Samples());Quality after;
        if (diagnostic)
        {
            started=Clock::now();TransactionalValidation::Validate(pipeline.State());
            TransactionalSamples following(input.Source);WorkLedger check;check.Deadline=Clock::now()+std::chrono::seconds(120);
            following.Refresh(pipeline.State(),check);after=Summarize(following);
            if (after.Screen>before.Screen+1e-8) throw std::runtime_error("冻结视图 sampled maximum 上升");
            // 独立初始化的反序应用仅作诊断，不进入正常更新时间
            TransactionalState reverse(input);auto reversed=batch;std::reverse(reversed.Exchanges.begin(),reversed.Exchanges.end());
            TransactionalCommit::Apply(reverse,reversed,check);TransactionalValidation::Validate(reverse);
            if (!TransactionalValidation::Equivalent(pipeline.State(),reverse)) throw std::runtime_error("反序逻辑结果不等价");
            work.Seconds["final_validation_next_refresh_reverse"]=Seconds(started);
        }
        started=Clock::now();TransactionalInput::Write(pipeline.State(),output/"mesh.json");work.Seconds["mesh_file"]=Seconds(started);
        WriteReport(output/"summary.json",input.Config,batch,work,before,after,pipeline.State().FaceCount(),diagnostic);
        std::cout<<input.Config.Scenario<<'/'<<input.Config.SampleIndex<<": 需求 "<<batch.Need
            <<"，局部可行 "<<batch.Feasible<<"，交换 "<<batch.Executed<<"，空额度 "<<batch.FreeExecuted
            <<"，更新 "<<work.Seconds["update"]*1000<<" ms\n";
    }
    catch (const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
