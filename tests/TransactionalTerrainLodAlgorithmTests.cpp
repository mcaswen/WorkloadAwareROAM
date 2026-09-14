#include "algorithms/greedy_transactional_lod/TransactionalTerrainLodAlgorithm.h"
#include "algorithms/greedy_transactional_lod/TransactionalSeedBuilder.h"
#include "algorithms/greedy_transactional_lod/TransactionalStateInvariant.h"
#include "algorithms/greedy_transactional_lod/TransactionalPipeline.h"
#include "algorithms/TerrainLodView.h"
#include "tools/CpuTaskExecutor.h"
#include "AllocationFailure.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <chrono>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
using namespace ParallelRoam;
using namespace Algorithms;
using namespace Algorithms::GreedyTransactionalLod;
void Require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
void Compare(const Terrain::TerrainMeshData& a,const Terrain::TerrainMeshData& b)
{
    Require(a.Indices==b.Indices && a.Vertices.size()==b.Vertices.size(),"输出连接或槽数不同");
    for (auto index : a.Indices)
    {
        const auto& x=a.Vertices[index];const auto& y=b.Vertices[index];
        Require(x.Position==y.Position && x.Normal==y.Normal && x.TexCoord==y.TexCoord && x.Height==y.Height,
            "适配输出不是同种子核心的实际拟合几何");
    }
}
TerrainLodViewInput View(float size)
{
    const glm::vec3 eye{size*.5F,size*.65F,size*.9F},target{0,size*.05F,0};
    return BuildTerrainLodViewInput(glm::lookAtRH(eye,target,glm::vec3{0,1,0}),
        glm::perspectiveRH_NO(glm::radians(60.0F),1280.0F/720.0F,.1F,500.0F),eye,target-eye,1280,720,false);
}
}

int main()
{
    try
    {
        Terrain::HeightMap height;std::string error;
        const auto asset="assets/heightmaps/Hm_Terrain_Test_129.pgm";
        Require(height.LoadFromFile(asset,&error),error.c_str());
        Require(height.RawSamples().size()==129U*129U,"原始样本缺失");
        for (int y=0;y<129;++y) for (int x=0;x<129;++x)
            Require(height.SamplePixel(x,y)==static_cast<float>(height.RawSamples()[static_cast<std::size_t>(y*129+x)])/65535.0F,
                "旧归一化像素计算改变");
        TerrainLodBuildInput input;input.HeightMap=&height;input.Settings.TriangleBudget=4096;input.View=View(30);
        auto seed=TransactionalSeedBuilder::Build(input);
        std::cout<<"test129 seed="<<seed.Faces.size()<<'\n';
        TransactionalStateInvariant::Validate(TransactionalState(seed));
        for (std::size_t workers : {1U,4U})
        {
            input.Settings.Transactional.WorkerCount=workers;seed.Config=TransactionalSeedBuilder::ConfigurationFor(input);
            Tools::CpuTaskExecutor executor(workers);
            TransactionalPipeline direct(seed,{workers,false,[&](auto n,const auto& task) { executor.Dispatch(n,task); }});
            TransactionalTerrainLodAlgorithm adapter;TerrainLodRenderPacket packet;
            Require(adapter.Capabilities().RequiresContinuousUpdate,"没有声明持续批次");
            for (int round=0;round<4;++round)
            {
                auto start=std::chrono::steady_clock::now();
                {
                    WorkLedger work;direct.SetView(seed.Config,work);auto batch=direct.Update(work);
                    static_cast<void>(direct.ConsumeMesh());
                }
                const auto directMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
                Require(adapter.BuildRenderData(input,packet,&error),error.c_str());
                Require(packet.HasConsistentResourceContract() && packet.CpuMesh.Vertices.empty(),"公共输出未保持借用契约");
                Compare(direct.Mesh(),*packet.ResolveCpuMesh());
                const auto& stats=*adapter.Stats().Transactional;
                Require(stats.Updated && stats.ColdStart==(round==0),"同视图调用被跳过或重复初建");
                std::cout<<"workers="<<workers<<", round="<<round<<", directMs="<<directMs
                    <<", adapterMs="<<stats.CpuReadyMilliseconds<<", coldMs="<<stats.SeedMilliseconds+stats.InitializeMilliseconds<<'\n';
            }
            TestAllocation::Countdown=0;
            Require(!adapter.BuildRenderData(input,packet,&error),"准备分配故障未报告");
            TestAllocation::Countdown=-1;
            Require(adapter.Stats().Transactional->Status==TransactionalLodStatus::AllocationFailed && packet.BorrowedCpuMesh,
                "准备失败损坏了已发布几何");
            adapter.Retry();adapter.RequestFullUpload();Require(adapter.BuildRenderData(input,packet,&error),error.c_str());
            Require(packet.CpuMeshRequiresFullUpload && !adapter.Stats().Transactional->ColdStart,"资源恢复重建了算法");
            const auto savedView=input.View;input.View.ViewProjection[0][0]=std::numeric_limits<float>::quiet_NaN();
            Require(!adapter.BuildRenderData(input,packet,&error) && packet.BorrowedCpuMesh,"坏视图破坏旧合法几何");
            Require(!adapter.BuildRenderData(input,packet,&error) && adapter.Stats().Transactional->Status==TransactionalLodStatus::RetrySuppressed,
                "相同非法输入持续重试");
            input.View=savedView;Require(adapter.BuildRenderData(input,packet,&error),error.c_str());
            input.View.DrawableWidth=0;Require(adapter.BuildRenderData(input,packet,&error),"零尺寸没有保留合法结果");
            Require(!adapter.Stats().Transactional->Updated,"最小化仍推进状态");input.View=savedView;
            input.Settings.TriangleBudget=512;Require(adapter.BuildRenderData(input,packet,&error),error.c_str());
            Require(adapter.Stats().Transactional->ColdStart && packet.ActiveTriangleCount<=512,"降低预算没有重新初始化");
            const auto revision=height.SourceRevision();Require(height.LoadFromFile(asset,&error),error.c_str());
            Require(height.SourceRevision()!=revision,"同路径重载未更新源身份");
            Require(adapter.BuildRenderData(input,packet,&error) && adapter.Stats().Transactional->ColdStart,"源版本变化未重建");
            input.Settings.TriangleBudget=1;Require(!adapter.BuildRenderData(input,packet,&error) && !packet.BorrowedCpuMesh,
                "非法新预算仍对外借用旧状态");input.Settings.TriangleBudget=4096;
        }
        // 非 dyadic 资产只做一次现实初建，不推进历史帧补齐种子
        Require(height.LoadFromFile("assets/heightmaps/Hm_Terrain_Peking_513.png",&error),error.c_str());
        input.Settings.TerrainSize=80;input.Settings.HeightScale=12;input.Settings.MaxDepth=20;input.Settings.TriangleBudget=20000;
        input.View=View(80);auto peking=TransactionalSeedBuilder::Build(input);
        TransactionalStateInvariant::Validate(TransactionalState(peking));std::cout<<"Peking seed="<<peking.Faces.size()<<'\n';
        std::cout<<"公共种子、逐批续接、同源对照与生命周期核查完成\n";
    }
    catch (const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
