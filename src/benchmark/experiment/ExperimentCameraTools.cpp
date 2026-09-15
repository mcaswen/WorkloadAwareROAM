#include "benchmark/experiment/ExperimentCameraTools.h"
#include "benchmark/TransactionalPlatformProtocol.h"
#include "experiment/formal/FormalExperimentCamera.h"
#include <bit>
#include <iomanip>
#include "experiment/infrastructure/CameraRecipe.h"
#include "experiment/infrastructure/TerrainAssetCatalog.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <fstream>

namespace ParallelRoam::Benchmark::Experiment
{
int RunExperimentCameraTools(int argc, char** argv)
{
    try
    {
        if (argc >= 3 && std::string(argv[2]) == "preview") return RunExperimentCameraPreview(argc,argv);
        namespace Data = ParallelRoam::Experiment::Infrastructure;
        if (argc == 4 && std::string(argv[2]) == "audit")
        {
            if (std::filesystem::exists(argv[3])) throw std::runtime_error("拒绝覆盖相机审计");
            std::ofstream out(argv[3]);
            out << std::setprecision(17) << "peking,index,oldNoMs,newExportMs,oldCameraPoseHash,newPoseHash,matrixBitEqual\n";
            for (bool peking : {false,true})
            {
                const auto first=std::chrono::steady_clock::now();
                std::vector<Render::RenderContext> old;
                for (std::uint32_t index=0;index<64;++index) old.push_back(PlatformReplayCamera(peking,index,false));
                const auto second=std::chrono::steady_clock::now();
                const auto modern=ExportLegacyCamera(peking ? "historical-orbit" : "historical-a64",peking);
                const auto last=std::chrono::steady_clock::now();
                for (std::uint32_t index=0;index<64;++index)
                {
                    bool equal=true;
                    for (int c=0;c<4;++c) for (int r=0;r<4;++r)
                        equal=equal && std::bit_cast<std::uint32_t>(old[index].View[c][r])==
                            std::bit_cast<std::uint32_t>(modern[index].View[c][r]) &&
                            std::bit_cast<std::uint32_t>(old[index].Projection[c][r])==
                            std::bit_cast<std::uint32_t>(modern[index].ProjectionNo[c][r]);
                    if (!equal) throw std::runtime_error("历史矩阵位不一致");
                    out << peking << ',' << index << ','
                        << std::chrono::duration<double,std::milli>(second-first).count() << ','
                        << std::chrono::duration<double,std::milli>(last-second).count() << ',';
                    if (!peking)
                    {
                        ParallelRoam::Experiment::Formal::FormalScenario source;
                        source.TrajectoryId="A";source.Settings.TerrainSize=30;
                        out << ParallelRoam::Experiment::Formal::GenerateCameraSample(source,index).CameraPoseHash;
                    }
                    out << ',' << modern[index].PoseHash << ",1\n";
                }
            }
            if (!out) throw std::runtime_error("相机审计写入失败");
            return 0;
        }
        if (argc == 4 && std::string(argv[2]) == "check")
        {
            const auto frames = Data::LoadCameraSequence(argv[3]);
            std::cout << "frames=" << frames.size() << '\n';
            return 0;
        }
        if (argc < 4) throw std::runtime_error("用法: --experiment-camera build ASSET RECIPE OUTPUT | legacy ID test129|peking OUTPUT | check CSV");
        Data::CameraSequence frames;
        const auto start = std::chrono::steady_clock::now();
        if (std::string(argv[2]) == "legacy" && argc == 6)
        {
            if (std::string(argv[4]) != "test129" && std::string(argv[4]) != "peking")
                throw std::runtime_error("未知历史资产");
            frames = ExportLegacyCamera(argv[3], std::string(argv[4]) == "peking");
        }
        else if (std::string(argv[2]) == "build" && argc == 6)
        {
            const auto assets = Data::LoadTerrainAssetCatalog("assets/experiments/terrain_catalog.json");
            const auto asset = std::find_if(assets.begin(),assets.end(),[&](const auto& a){return a.Id==argv[3];});
            if (asset == assets.end()) throw std::runtime_error("未知路线资产");
            Terrain::HeightMap reference;
            std::string error;
            if (!reference.LoadFromFile(asset->Path,&error)) throw std::runtime_error(error);
            frames = Data::GenerateCameraRecipe(argv[4],reference,asset->TerrainSize,asset->HeightScale);
        }
        else throw std::runtime_error("相机参数不完整");
        const auto generated = std::chrono::steady_clock::now();
        Data::SaveCameraSequence(argv[5],frames);
        const auto restored = Data::LoadCameraSequence(argv[5]);
        if (restored.size()!=frames.size()) throw std::runtime_error("路线往返缺行");
        for (std::size_t i=0;i<frames.size();++i)
            if (frames[i].PoseHash!=restored[i].PoseHash || frames[i].NoHash!=restored[i].NoHash ||
                frames[i].ZoHash!=restored[i].ZoHash) throw std::runtime_error("路线往返输入变化");
        std::cout << "frames=" << frames.size() << ",generationMs="
            << std::chrono::duration<double,std::milli>(generated-start).count()
            << ",writeReadMs=" << std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-generated).count() << '\n';
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
}
