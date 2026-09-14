#include <set>
#include "algorithms/classic_roam/ClassicRoamTerrainLodAlgorithm.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamTerrainLodAlgorithm.h"
#include "experiment/formal/FormalExperimentCamera.h"
#include "experiment/formal/FormalExperimentManifest.h"
#include "experiment/greedy_transactional_lod/TransactionalInput.h"
#include "experiment/greedy_transactional_lod/TransactionalScalingProtocol.h"
#include "experiment/greedy_transactional_lod/TransactionalQualityReport.h"
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
        if (argc<4 || (argc-4)%2) throw std::runtime_error("参数：快照 classic/dod 输出目录 [--workers 4/8] [--profile 1]");
        bool profiling=false;std::size_t workers=0;std::set<std::string> options;
        for (int i=4;i<argc;i+=2)
        {
            const std::string key=argv[i],value=argv[i+1];
            if (!options.insert(key).second) throw std::runtime_error("重复参数");
            if (key=="--profile" && value=="1") profiling=true;
            else if (key=="--workers" && (value=="4" || value=="8")) workers=value=="4" ? 4U : 8U;
            else throw std::runtime_error("家族参数非法");
        }
        const std::string family{argv[2]};const std::filesystem::path output{argv[3]},root=std::filesystem::current_path();
        if (family!="classic" && family!="dod") throw std::runtime_error("家族编号非法");
        if (std::filesystem::exists(output)) throw std::runtime_error("拒绝覆盖输出");
        std::filesystem::create_directories(output);
        ownsOutput=true;
        std::unique_ptr<Tools::Profiling::ProfileSession> profile;
        if (profiling) profile=std::make_unique<Tools::Profiling::ProfileSession>(output/"profile-windows.csv");
        auto initial=TransactionalInput::Load(argv[1]);
        const auto budget=TransactionalScalingProtocol::Budget(initial.Config.Scenario);
        const bool scaling=budget.has_value();
        if (scaling && (family!="dod" || !workers)) throw std::runtime_error("压力基线必须显式指定 DOD 线程数");
        const auto scenarios=scaling ? std::vector{TransactionalScalingProtocol::Scenario(root,*budget,workers)} :
            Experiment::Formal::LoadScenarioManifest(root/"docs/parallel-roam/cpu-pilot-scenarios-v1.csv",
                root,{"test129-a-b4096","peking547-a-b20000"});
        auto cameras=scaling ? std::vector<Experiment::Formal::CameraSample>{} : Experiment::Formal::LoadCameraManifest(
            root/"benchmark-output/roam-materialization/mpr-01/input-freeze/inputs/camera-samples.csv",scenarios);
        if (scaling)
            for (std::uint32_t index=0;index<=18;++index)
            { auto camera=TransactionalScalingProtocol::Camera(index);camera.ScenarioId=initial.Config.Scenario;cameras.push_back(camera); }
        const auto scenario=std::find_if(scenarios.begin(),scenarios.end(),[&](const auto& row) { return row.ScenarioId==initial.Config.Scenario; });
        if (scenario==scenarios.end()) throw std::runtime_error("来源场景未冻结");
        Terrain::HeightMap source;std::string error;
        if (!source.LoadFromFile(scenario->HeightMapPath,&error)) throw std::runtime_error(error);
        auto settings=scenario->Settings;
        // 清单为历史严格配对强制串行，这里恢复正常默认动作和自动线程选择
        if (!scaling) settings.PassPolicy=Algorithms::TerrainLodPassPolicy{};
        settings.EnableParallelSplit=true;
        settings.EnablePassEvidence=false;settings.EnableTopologyPairEvidence=false;settings.EnableTopologyValidation=false;
        std::unique_ptr<Algorithms::ITerrainLodAlgorithm> algorithm;
        if (family=="classic") algorithm=std::make_unique<Algorithms::ClassicRoam::ClassicRoamTerrainLodAlgorithm>();
        else algorithm=std::make_unique<Algorithms::DataOrientedRoam::DataOrientedRoamTerrainLodAlgorithm>();
        std::ofstream rows(output/"trajectory.jsonl");rows<<std::setprecision(17);
        Algorithms::TerrainLodRenderPacket packet;double bootstrap=0;
        const std::array<unsigned,8> views{14,14,14,15,16,17,18,14};
        const std::size_t prefix=scaling ? 15U : 14U;
        for (std::size_t step=0;step<prefix+8;++step)
        {
            const auto index=step<prefix ? static_cast<unsigned>(step) : views[step-prefix];
            const auto camera=std::find_if(cameras.begin(),cameras.end(),[&](const auto& row) {
                return row.ScenarioId==initial.Config.Scenario && row.SampleIndex==index;
            });
            if (camera==cameras.end()) throw std::runtime_error("相机行缺失");
            Algorithms::TerrainLodBuildInput input{&source,Experiment::Formal::BuildCameraView(*camera),settings};
            if (profile && step>=prefix) profile->Begin(0,static_cast<int>(step-prefix));
            const auto started=Clock::now();
            if (!algorithm->BuildRenderData(input,packet,&error)) throw std::runtime_error(error);
            const auto ms=std::chrono::duration<double,std::milli>(Clock::now()-started).count();
            if (profile && step>=prefix) profile->End();
            if (!packet.HasConsistentResourceContract() || packet.ActiveTriangleCount>settings.TriangleBudget)
                throw std::runtime_error("家族公共输出或预算非法");
            if (step<prefix)
            {
                bootstrap+=ms;
                if (scaling && step==14)
                {
                    const auto seed=Import(*packet.ResolveCpuMesh(),initial);
                    // 不依赖来源身份或面排列，只比较完整的参数域几何
                    const auto key=[](const InitialMesh& mesh) {
                        std::map<Identity,Point> vertices(mesh.Vertices.begin(),mesh.Vertices.end());
                        std::vector<std::array<std::array<double,3>,3>> result;
                        for (const auto& face : mesh.Faces)
                        {
                            std::array<std::array<double,3>,3> points;
                            for (std::size_t i=0;i<3;++i)
                            { const auto& p=vertices.at(face.Vertices[i]);points[i]={p.U,p.V,p.Height}; }
                            std::sort(points.begin(),points.end());result.push_back(points);
                        }
                        std::sort(result.begin(),result.end());return result;
                    };
                    if (key(seed)!=key(initial)) throw std::runtime_error("DOD 前缀几何与冻结种子不同");
                    std::ofstream(output/"seed-match.txt")<<"logical_geometry_equal\n";
                }
                continue;
            }
            const auto& stats=algorithm->Stats();
            rows<<"{\"round\":"<<step-prefix<<",\"view\":"<<index<<",\"family\":"<<std::quoted(family)
                <<",\"buildMs\":"<<ms<<",\"faces\":"<<packet.ActiveTriangleCount<<",\"workers\":"<<stats.CpuWorkerCount
                <<",\"scoreMs\":"<<stats.CpuMergeCandidateMarkMilliseconds+stats.CpuSplitCandidateMarkMilliseconds
                <<",\"topologyMs\":"<<stats.CpuMergeTopologyMilliseconds+stats.CpuSplitTopologyMilliseconds
                <<",\"meshMs\":"<<stats.CpuMeshEmitMilliseconds<<"}\n";
            rows.flush();
            if (scaling)
            {
                auto exported=Import(*packet.ResolveCpuMesh(),initial);
                exported.Config=TransactionalScalingProtocol::View(initial.Config,index);
                const auto directory=output/("round-"+std::to_string(step-prefix));
                std::filesystem::create_directories(directory);
                TransactionalState state(exported);TransactionalInput::Write(state,directory/"mesh.json");
                std::ofstream work(directory/"work.json");
                work<<"{\"split\":"<<stats.SplitCount<<",\"forcedSplit\":"<<stats.ForcedSplitCount
                    <<",\"merge\":"<<stats.MergeCount<<",\"activeNodes\":"<<stats.ActiveNodeCount
                    <<",\"requestedWorkers\":"<<workers<<",\"actualWorkers\":"<<stats.CpuWorkerCount<<"}";
            }
        }
        if (profile) profile->Finish();
        if (scaling)
        {
            const auto destroy=Clock::now();algorithm.reset();
            const auto destroyMs=std::chrono::duration<double,std::milli>(Clock::now()-destroy).count();
            // 完整轨迹结束后再评价，避免每帧全 Q 校验改变下一帧缓存条件
            const auto evaluation=Clock::now();
            for (int round=0;round<8;++round)
            {
                const auto directory=output/("round-"+std::to_string(round));
                const auto mesh=TransactionalInput::Load(directory/"mesh.json","fixed64",std::filesystem::path(argv[1]).parent_path());
                TransactionalState state(mesh);TransactionalValidation::Validate(state);
                TransactionalSamples samples(mesh.Source);WorkLedger check;samples.Refresh(state,check);
                TransactionalQualityReport::Write(samples,directory);
            }
            const auto qualityMs=std::chrono::duration<double,std::milli>(Clock::now()-evaluation).count();
            std::ofstream life(output/"lifetime.json");
            life<<std::setprecision(17)<<"{\"bootstrapMs\":"<<bootstrap<<",\"stateDestroyMs\":"<<destroyMs
                <<",\"qualityMs\":"<<qualityMs<<"}";
            return 0;
        }
        // 仅返回帧作全 Q 离线评价，计时路径没有引入新原型的样本维护
        const auto started=Clock::now();auto imported=Import(*packet.ResolveCpuMesh(),std::move(initial));
        TransactionalState state(imported);TransactionalValidation::Validate(state);
        TransactionalSamples samples(imported.Source);WorkLedger work;samples.Refresh(state,work);
        double maximum=0,height=0,sum=0;std::size_t visible=0;Slot screenSample=0,heightSample=0;
        for (Slot sid=0;sid<samples.SampleCount();++sid)
        {
            const auto& value=samples.Value(sid);
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
