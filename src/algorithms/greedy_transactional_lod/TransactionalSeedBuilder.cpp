#include "algorithms/greedy_transactional_lod/TransactionalSeedBuilder.h"
#include "algorithms/greedy_transactional_lod/TransactionalPredicates.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamTerrainLodAlgorithm.h"
#include "profiling/CpuProfiling.h"

#include <cmath>
#include <map>
#include <stdexcept>

namespace ParallelRoam::Algorithms::GreedyTransactionalLod
{
void TransactionalSeedBuilder::ValidateInput(const TerrainLodBuildInput& input)
{
    const auto* height=input.HeightMap;const auto& s=input.Settings;const auto& t=s.Transactional;
    if (t.ReceiverOrder != TransactionalReceiverOrder::Composite &&
        t.ReceiverOrder != TransactionalReceiverOrder::ErrorFirst)
    {
        throw std::invalid_argument("未知接收方排序政策");
    }
    if (!height || !height->IsValid() || height->Width()<2 || height->Width()!=height->Height() || height->Width()>1025 ||
        height->RawSamples().size()!=static_cast<std::size_t>(height->Width())*static_cast<std::size_t>(height->Height()))
        throw std::invalid_argument("事务化输入需要 2..1025 的方形原始高度图");
    if (!std::isfinite(s.TerrainSize) || !(s.TerrainSize>0) || !std::isfinite(s.HeightScale) || !(s.HeightScale>0) ||
        s.TriangleBudget<2 || s.TriangleBudget>200000 || s.MaxDepth<1 || s.MaxDepth>20 ||
        !std::isfinite(s.ScreenSpaceSplitThresholdPixels) || !(s.ScreenSpaceSplitThresholdPixels>0) ||
        !std::isfinite(s.ScreenSpaceMergeThresholdPixels) || s.ScreenSpaceMergeThresholdPixels<0)
        throw std::invalid_argument("事务化尺寸、预算、初始深度或阈值不在支持范围");
    if (!t.WorkerCount || t.WorkerCount>32 || !t.PrefixLimit || t.PrefixLimit>640 || !t.DonorLimit ||
        t.DonorLimit>640 || !t.SampleVisitLimit || t.SampleVisitLimit>100000000)
        throw std::invalid_argument("事务化线程、前缀或配额不在支持范围");
    if (!input.View.DrawableWidth || !input.View.DrawableHeight)
        throw std::invalid_argument("零尺寸视图应暂停更新");
    if (t.EnableFlipRecovery && (!t.PreserveSurvivingHeights || t.HeightGuard))
        throw std::invalid_argument("翻边恢复需要固定旧点且关闭 HeightGuard");
    if (t.EnableBoundaryRefinement && (!t.PreserveSurvivingHeights || t.HeightGuard))
    {
        throw std::invalid_argument("边界细分需要固定旧点且关闭 HeightGuard");
    }
    for (int col=0;col<4;++col) for (int row=0;row<4;++row)
        if (!std::isfinite(input.View.ViewProjection[col][row]))
            throw std::invalid_argument("视图矩阵包含非有限值");
}

Configuration TransactionalSeedBuilder::ConfigurationFor(const TerrainLodBuildInput& input)
{
    Configuration c;const auto& s=input.Settings;const auto& v=input.View;
    c.Scenario="platform-current-view-v1";c.Budget=s.TriangleBudget;
    c.TerrainSize=s.TerrainSize;c.HeightScale=s.HeightScale;c.SplitPixels=s.ScreenSpaceSplitThresholdPixels;
    c.PrefixLimit=s.Transactional.PrefixLimit;c.DonorLimit=s.Transactional.DonorLimit;
    c.HeightGuard=s.Transactional.HeightGuard;c.SampleVisitLimit=s.Transactional.SampleVisitLimit;
    c.PreserveSurvivingHeights=s.Transactional.PreserveSurvivingHeights;
    c.EnableFlipRecovery=s.Transactional.EnableFlipRecovery;
    c.EnableBoundaryRefinement=s.Transactional.EnableBoundaryRefinement;
    c.ReceiverOrder = s.Transactional.ReceiverOrder;
    c.Width=v.DrawableWidth;c.Height=v.DrawableHeight;c.UsesZeroToOneDepth=v.UsesZeroToOneDepth;
    for (int row=0;row<4;++row) for (int col=0;col<4;++col)
        c.Matrix[static_cast<std::size_t>(row*4+col)]=v.ViewProjection[col][row];
    return c;
}

InitialMesh TransactionalSeedBuilder::Build(const TerrainLodBuildInput& input)
{
    ROAM_CPU_ZONE("tpi.seed");
    ValidateInput(input);InitialMesh result;result.Config=ConfigurationFor(input);
    result.Source={static_cast<std::uint32_t>(input.HeightMap->Width()),
        static_cast<std::uint32_t>(input.HeightMap->Height()),input.HeightMap->RawSamples()};
    auto seedInput=input;auto& s=seedInput.Settings;s.EnableParallelSplit=false;
    s.EnableLocalConstraints=true;
    s.EnablePassEvidence=false;s.EnableTopologyPairEvidence=false;s.EnableTopologyValidation=false;
    s.PassPolicy={};s.PassPolicy.MergeScore=TerrainLodScoreRefreshAction::SerialRefresh;
    s.PassPolicy.SplitScore=TerrainLodScoreRefreshAction::SerialRefresh;
    s.PassPolicy.MergeTopology=TerrainLodTopologyAction::SerialImmediate;
    s.PassPolicy.SplitTopology=TerrainLodTopologyAction::SerialImmediate;
    s.PassPolicy.MeshEmit=TerrainLodMeshEmitAction::SerialDirty;
    s.PassPolicy.MergeScoreWorkerCount=s.PassPolicy.SplitScoreWorkerCount=1;
    s.PassPolicy.MergeTopologyWorkerCount=s.PassPolicy.SplitTopologyWorkerCount=s.PassPolicy.MeshEmitWorkerCount=1;
    DataOrientedRoam::DataOrientedRoamTerrainLodAlgorithm seed;
    TerrainLodRenderPacket packet;std::string error;
    // 恰好一次公共调用；临时状态在本函数返回前销毁，不保留为下一帧目标来源
    if (!seed.BuildRenderData(seedInput,packet,&error) || !packet.HasConsistentResourceContract())
        throw std::runtime_error("DOD 初始网格失败: "+error);
    const auto& mesh=*packet.ResolveCpuMesh();
    if (mesh.Indices.size()%3 || mesh.Indices.size()/3>s.TriangleBudget)
        throw std::runtime_error("初始索引或预算无效");
    std::map<std::pair<double,double>,Identity> ids;
    for (std::size_t first=0;first<mesh.Indices.size();first+=3)
    {
        Triangle face;face.Id=static_cast<Identity>(result.Faces.size());
        for (std::size_t corner=0;corner<3;++corner)
        {
            const auto& vertex=mesh.Vertices.at(mesh.Indices[first+corner]);
            const Point p{vertex.TexCoord.x,vertex.TexCoord.y,vertex.Position.y};
            const auto [it,added]=ids.emplace(std::pair{p.U,p.V},static_cast<Identity>(result.Vertices.size()));
            if (added) result.Vertices.emplace_back(it->second,p);
            else if (result.Vertices.at(static_cast<std::size_t>(it->second)).second!=p)
                throw std::runtime_error("公共种子共享点高度不一致");
            face.Vertices[corner]=it->second;
        }
        const auto point=[&](std::size_t i)->const Point& {
            return result.Vertices.at(static_cast<std::size_t>(face.Vertices[i])).second;
        };
        // 只统一方向，不容差焊接或重采高度；完整结构由初建核查验证
        if (TransactionalPredicates::Orientation(point(0),point(1),point(2))<0)
            std::swap(face.Vertices[1],face.Vertices[2]);
        result.Faces.push_back(face);
    }
    return result;
}
}
