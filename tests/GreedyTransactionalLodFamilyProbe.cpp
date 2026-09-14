#include "algorithms/classic_roam/ClassicRoamTerrainLodAlgorithm.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamTerrainLodAlgorithm.h"
#include "experiment/formal/FormalExperimentCamera.h"
#include "experiment/formal/FormalExperimentManifest.h"
#include "experiment/greedy_transactional_lod/TransactionalInput.h"
#include "experiment/greedy_transactional_lod/TransactionalPredicates.h"
#include "experiment/greedy_transactional_lod/TransactionalSamples.h"
#include "experiment/greedy_transactional_lod/TransactionalValidation.h"
#include "tools/profiling/ProfileSession.h"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>

namespace
{
using namespace ParallelRoam;
using namespace Experiment::GreedyTransactionalLod;
using Clock=std::chrono::steady_clock;

/// <summary>
/// 从公共输出恢复用于离线评价的几何，不读取家族实现的内部队列或目标
/// 相同参数点必须具有相同实际输出高度，不能重新采样掩盖输出差异
/// </summary>
InitialMesh Import(const Terrain::TerrainMeshData& mesh,InitialMesh reference)
{
    reference.Vertices.clear();reference.Faces.clear();
    std::map<std::pair<double,double>,Identity> ids;
    for (std::size_t first=0;first<mesh.Indices.size();first+=3)
    {
        Triangle face;face.Id=static_cast<Identity>(reference.Faces.size());
        for (std::size_t corner=0;corner<3;++corner)
        {
            const auto& vertex=mesh.Vertices.at(mesh.Indices.at(first+corner));
            const Point p{vertex.TexCoord.x,vertex.TexCoord.y,vertex.Position.y};
            const auto [entry,added]=ids.emplace(std::pair{p.U,p.V},static_cast<Identity>(reference.Vertices.size()));
            if (added) reference.Vertices.emplace_back(entry->second,p);
            else if (reference.Vertices.at(static_cast<std::size_t>(entry->second)).second!=p)
                throw std::runtime_error("公共输出共享点几何不同");
            face.Vertices[corner]=entry->second;
        }
        const auto geometry=[&](std::size_t i)->const Point& {
            return reference.Vertices.at(static_cast<std::size_t>(face.Vertices[i])).second;
        };
        if (TransactionalPredicates::Orientation(geometry(0),geometry(1),geometry(2))<0)
            std::swap(face.Vertices[1],face.Vertices[2]);
        reference.Faces.push_back(face);
    }
    return reference;
}
}

int main(int argc,char** argv)
{
    bool ownsOutput=false;
    try
    {
        if (argc!=4 && argc!=6) throw std::runtime_error("参数：sample14 快照 classic/dod 输出目录 [--profile 1]");
        const bool profiling=argc==6;
        if (profiling && (std::string(argv[4])!="--profile" || std::string(argv[5])!="1"))
            throw std::runtime_error("家族采集只支持原八轮，重放次数必须为 1");
        const std::string family{argv[2]};const std::filesystem::path output{argv[3]},root=std::filesystem::current_path();
        if (family!="classic" && family!="dod") throw std::runtime_error("家族编号非法");
        if (std::filesystem::exists(output)) throw std::runtime_error("拒绝覆盖输出");
        std::filesystem::create_directories(output);
        ownsOutput=true;
        std::unique_ptr<Tools::Profiling::ProfileSession> profile;
        if (profiling) profile=std::make_unique<Tools::Profiling::ProfileSession>(output/"profile-windows.csv");
        auto initial=TransactionalInput::Load(argv[1]);
        const auto scenarios=Experiment::Formal::LoadScenarioManifest(root/"docs/parallel-roam/cpu-pilot-scenarios-v1.csv",
            root,{"test129-a-b4096","peking547-a-b20000"});
        const auto cameras=Experiment::Formal::LoadCameraManifest(
            root/"benchmark-output/roam-materialization/mpr-01/input-freeze/inputs/camera-samples.csv",scenarios);
        const auto scenario=std::find_if(scenarios.begin(),scenarios.end(),[&](const auto& row) { return row.ScenarioId==initial.Config.Scenario; });
        if (scenario==scenarios.end()) throw std::runtime_error("来源场景未冻结");
        Terrain::HeightMap source;std::string error;
        if (!source.LoadFromFile(scenario->HeightMapPath,&error)) throw std::runtime_error(error);
        auto settings=scenario->Settings;
        // 清单为历史严格配对强制串行，这里恢复正常默认动作和自动线程选择
        settings.PassPolicy=Algorithms::TerrainLodPassPolicy{};settings.EnableParallelSplit=true;
        settings.EnablePassEvidence=false;settings.EnableTopologyPairEvidence=false;settings.EnableTopologyValidation=false;
        std::unique_ptr<Algorithms::ITerrainLodAlgorithm> algorithm;
        if (family=="classic") algorithm=std::make_unique<Algorithms::ClassicRoam::ClassicRoamTerrainLodAlgorithm>();
        else algorithm=std::make_unique<Algorithms::DataOrientedRoam::DataOrientedRoamTerrainLodAlgorithm>();
        std::ofstream rows(output/"trajectory.jsonl");rows<<std::setprecision(17);
        Algorithms::TerrainLodRenderPacket packet;double bootstrap=0;
        const std::array<unsigned,8> views{14,14,14,15,16,17,18,14};
        for (std::size_t step=0;step<22;++step)
        {
            const auto index=step<14 ? static_cast<unsigned>(step) : views[step-14];
            const auto camera=std::find_if(cameras.begin(),cameras.end(),[&](const auto& row) {
                return row.ScenarioId==initial.Config.Scenario && row.SampleIndex==index;
            });
            if (camera==cameras.end()) throw std::runtime_error("相机行缺失");
            Algorithms::TerrainLodBuildInput input{&source,Experiment::Formal::BuildCameraView(*camera),settings};
            if (profile && step>=14) profile->Begin(0,static_cast<int>(step-14));
            const auto started=Clock::now();
            if (!algorithm->BuildRenderData(input,packet,&error)) throw std::runtime_error(error);
            const auto ms=std::chrono::duration<double,std::milli>(Clock::now()-started).count();
            if (profile && step>=14) profile->End();
            if (!packet.HasConsistentResourceContract() || packet.ActiveTriangleCount>settings.TriangleBudget)
                throw std::runtime_error("家族公共输出或预算非法");
            if (step<14) { bootstrap+=ms;continue; }
            const auto& stats=algorithm->Stats();
            rows<<"{\"round\":"<<step-14<<",\"view\":"<<index<<",\"family\":"<<std::quoted(family)
                <<",\"buildMs\":"<<ms<<",\"faces\":"<<packet.ActiveTriangleCount<<",\"workers\":"<<stats.CpuWorkerCount
                <<",\"scoreMs\":"<<stats.CpuMergeCandidateMarkMilliseconds+stats.CpuSplitCandidateMarkMilliseconds
                <<",\"topologyMs\":"<<stats.CpuMergeTopologyMilliseconds+stats.CpuSplitTopologyMilliseconds
                <<",\"meshMs\":"<<stats.CpuMeshEmitMilliseconds<<"}\n";
            rows.flush();
        }
        if (profile) profile->Finish();
        // 仅返回帧作全 Q 离线评价，计时路径没有引入新原型的样本维护
        const auto started=Clock::now();auto imported=Import(*packet.ResolveCpuMesh(),std::move(initial));
        TransactionalState state(imported);TransactionalValidation::Validate(state);
        TransactionalSamples samples(imported.Source);WorkLedger work;samples.Refresh(state,work);
        double maximum=0,height=0,sum=0;std::size_t visible=0;Slot screenSample=0,heightSample=0;
        for (Slot sid=0;sid<samples.Values().size();++sid)
        {
            const auto& value=samples.Values()[sid];
            if (value.ErrorSquared>maximum) { maximum=value.ErrorSquared;screenSample=sid; }
            if (value.HeightError>height) { height=value.HeightError;heightSample=sid; }
            if (value.Visible) { ++visible;sum+=value.ErrorSquared; }
        }
        const auto qualityMs=std::chrono::duration<double,std::milli>(Clock::now()-started).count();
        TransactionalInput::Write(state,output/"mesh.json");
        std::ofstream summary(output/"quality.json");summary<<std::setprecision(17)
            <<"{\"sampledScreenMaxPx\":"<<std::sqrt(maximum)<<",\"sampledHeightMax\":"<<height
            <<",\"terrainSampleScreenRms\":"<<(visible ? std::sqrt(sum/static_cast<double>(visible)) : 0)
            <<",\"screenSample\":"<<screenSample<<",\"heightSample\":"<<heightSample
            <<",\"bootstrapMs\":"<<bootstrap<<",\"qualityMs\":"<<qualityMs<<",\"faces\":"<<state.FaceCount()<<"}";
        if (!summary || !rows) throw std::runtime_error("家族报告写入失败");
        std::cout<<family<<' '<<state.Config().Scenario<<" 完成\n";
    }
    catch (const std::exception& error)
    {
        std::cerr<<error.what()<<'\n';
        if (ownsOutput && argc>=4 && std::filesystem::is_directory(argv[3]))
        { std::ofstream failure(std::filesystem::path(argv[3])/"failure.txt");failure<<error.what()<<'\n'; }
        return 1;
    }
}
