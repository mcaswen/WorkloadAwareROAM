#include "algorithms/greedy_transactional_lod/TransactionalRenderBridge.h"
#include "algorithms/greedy_transactional_lod/TransactionalPipeline.h"
#include "algorithms/greedy_transactional_lod/TransactionalProposals.h"
#include "algorithms/greedy_transactional_lod/TransactionalCertification.h"
#include "AllocationFailure.h"

#include <iostream>
#include <stdexcept>

namespace
{
using namespace ParallelRoam::Algorithms::GreedyTransactionalLod;
void Require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void Reject(F&& action)
{
    bool rejected=false;try { action(); } catch (const std::exception&) { rejected=true; }
    Require(rejected,"未拒绝非法输出或投影域");
}
InitialMesh Square()
{
    InitialMesh input;input.Config.Budget=8;
    input.Vertices={{0,{0,0,0}},{1,{1,0,0}},{2,{1,1,0}},{3,{0,1,0}}};
    input.Faces={{0,{0,1,2}},{1,{0,2,3}}};input.Source={3,3,{0,0,0,0,65535,0,0,0,0}};return input;
}
}
int main()
{
    try
    {
        auto input=Square();TransactionalPipeline pipeline(input);WorkLedger work;pipeline.Initialize(work);
        auto initial=TransactionalRenderBridge::Packet(pipeline.ConsumeMesh(),false);
        Require(initial.CpuMeshRequiresFullUpload && initial.CpuMesh.Vertices.empty(),"首次不是全量借用");
        auto proposal=TransactionalProposals::Receivers(pipeline.State(),pipeline.Samples(),0).front();
        proposal.Reason=TransactionalCertification::Fit(pipeline.State(),pipeline.Samples(),proposal,work);
        Require(proposal.Reason=="certified","解析提案失败");
        CertifiedBatch batch;batch.Version=pipeline.State().Version();batch.Exchanges.push_back({proposal,{},false});
        pipeline.Apply(batch,work);auto consumption=pipeline.ConsumeMesh();
        TestAllocation::Countdown=0;
        Reject([&] { static_cast<void>(TransactionalRenderBridge::Packet(consumption,false)); });
        TestAllocation::Countdown=-1;
        auto recovery=TransactionalRenderBridge::Packet(consumption,true);
        Require(recovery.CpuMeshRequiresFullUpload && recovery.ResolveCpuMesh()==&pipeline.Mesh(),"桥接失败无法重发实际完整网格");
        auto changed=TransactionalRenderBridge::Packet(consumption,false);
        Require(!changed.CpuMeshRequiresFullUpload && !changed.CpuMeshUpdateRanges.empty() && changed.HasConsistentResourceContract(),
            "非空批没有独立增量范围");
        auto view=input.Config;view.Width=101;pipeline.SetView(view,work);
        auto viewOnly=TransactionalRenderBridge::Packet(pipeline.ConsumeMesh(),false);
        Require(viewOnly.CpuMeshGeneration>changed.CpuMeshGeneration && viewOnly.CpuMeshUpdateRanges.empty() && !viewOnly.CpuMeshRequiresFullUpload,
            "纯视图代际被当成全量内容更新");

        // 同一几何用 NO/ZO 两种精确相关矩阵；w 也依赖高度，覆盖拟合近面系数
        auto no=Square();no.Config.Matrix[9]=.25;no.Config.Matrix[13]=.25;
        auto zo=no;zo.Config.UsesZeroToOneDepth=true;
        for (std::size_t i=0;i<4;++i) zo.Config.Matrix[8+i]=.5*(no.Config.Matrix[8+i]+no.Config.Matrix[12+i]);
        TransactionalState ns(no),zs(zo);TransactionalSamples nq(no.Source),zq(zo.Source);
        nq.Refresh(ns,work);zq.Refresh(zs,work);
        for (Slot sid=0;sid<nq.SampleCount();++sid)
        {
            Require(nq.Value(sid).Visible==zq.Value(sid).Visible && nq.Value(sid).ErrorSquared==zq.Value(sid).ErrorSquared,"等价深度域评价不同");
            Require(TransactionalCertification::ExactErrorSquared(ns,nq,sid)==TransactionalCertification::ExactErrorSquared(zs,zq,sid),"精确误差深度域不同");
        }
        auto np=TransactionalProposals::Receivers(ns,nq,0).front(),zp=TransactionalProposals::Receivers(zs,zq,0).front();
        const auto nr=TransactionalCertification::Fit(ns,nq,np,work),zr=TransactionalCertification::Fit(zs,zq,zp,work);
        Require(nr==zr && np.Points==zp.Points,"同视图拟合深度约定分叉");
        auto near=Square();near.Config.Matrix[9]=1;near.Config.Matrix[10]=0;near.Config.Matrix[11]=-.5;
        TransactionalState nearNo(near);TransactionalSamples nearSamples(near.Source);nearSamples.Refresh(nearNo,work);
        near.Config.UsesZeroToOneDepth=true;TransactionalState nearZo(near);
        Reject([&] { TransactionalSamples q(near.Source);q.Refresh(nearZo,work); });
        Reject([&] { static_cast<void>(TransactionalCertification::ExactErrorSquared(nearZo,nearSamples,4)); });

        auto tiny=Square();for (auto& [id,p] : tiny.Vertices) { static_cast<void>(id);p.U=.125+p.U*1e-10;p.V=.125+p.V*1e-10; }
        TransactionalState tinyState(tiny);TransactionalMesh tinyMesh;
        Reject([&] { tinyMesh.Initialize(tinyState,work); });
        std::cout<<"增量范围、NO/ZO 数值域与 float 输出核查完成\n";
    }
    catch (const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
