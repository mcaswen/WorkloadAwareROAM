#include "algorithms/greedy_transactional_lod/TransactionalTerrainLodAlgorithm.h"
#include "algorithms/greedy_transactional_lod/TransactionalPipeline.h"
#include "algorithms/greedy_transactional_lod/TransactionalRenderBridge.h"
#include "algorithms/greedy_transactional_lod/TransactionalSeedBuilder.h"
#include "algorithms/greedy_transactional_lod/TransactionalStateInvariant.h"
#include "tools/CpuTaskExecutor.h"
#include "profiling/CpuProfiling.h"

#include <bit>
#include <chrono>
#include <optional>
#include <stdexcept>

namespace ParallelRoam::Algorithms::GreedyTransactionalLod
{
namespace
{
using Clock=std::chrono::steady_clock;
double Milliseconds(Clock::time_point start) { return std::chrono::duration<double,std::milli>(Clock::now()-start).count(); }

/// <summary>
/// 固定大小的生命周期身份不读取整张资产，也不受相机或旧 DOD 策略影响
/// 浮点值按位比较，使重复的非法 NaN 输入也能停止自动重试
/// </summary>
struct SeedKey
{
    const Terrain::HeightMap* Source{};
    std::uint64_t Revision{};
    std::array<std::uint32_t,4> Scalars{};
    int Depth{};
    std::size_t Budget{};
    TransactionalLodSettings Settings;
    bool operator==(const SeedKey&) const = default;
};
SeedKey Key(const TerrainLodBuildInput& input)
{
    const auto& s=input.Settings;
    return {input.HeightMap,input.HeightMap ? input.HeightMap->SourceRevision() : 0,
        {std::bit_cast<std::uint32_t>(s.TerrainSize),std::bit_cast<std::uint32_t>(s.HeightScale),
         std::bit_cast<std::uint32_t>(s.ScreenSpaceSplitThresholdPixels),std::bit_cast<std::uint32_t>(s.ScreenSpaceMergeThresholdPixels)},
        s.MaxDepth,s.TriangleBudget,s.Transactional};
}
std::array<std::uint32_t,19> ViewKey(const TerrainLodViewInput& view)
{
    std::array<std::uint32_t,19> key{};
    for (int col=0;col<4;++col) for (int row=0;row<4;++row)
        key[static_cast<std::size_t>(col*4+row)]=std::bit_cast<std::uint32_t>(view.ViewProjection[col][row]);
    key[16]=view.DrawableWidth;key[17]=view.DrawableHeight;key[18]=view.UsesZeroToOneDepth ? 1U : 0U;
    return key;
}
}

/// <summary>
/// 同步调用的唯一状态拥有者；先销毁核心，再回收其执行回调依赖的线程资源
/// 失败输入只记录固定身份与消息，不保存另一份网格作为回滚副本
/// </summary>
struct TransactionalTerrainLodAlgorithm::Impl
{
    std::unique_ptr<Tools::CpuTaskExecutor> Executor;
    std::unique_ptr<TransactionalPipeline> Pipeline;
    std::optional<SeedKey> CurrentKey;
    std::array<std::uint32_t,19> FailedView{};
    TerrainLodStats PublicStats;
    std::string Error;
    bool Blocked{}, ForceFull{true}, HasMesh{};
    std::size_t SeedTriangles{};
    std::uint64_t Sequence{};
    double CoreMilliseconds{};

    void Clear()
    {
        Pipeline.reset();Executor.reset();CurrentKey.reset();PublicStats={};
        Error.clear();Blocked=false;ForceFull=true;HasMesh=false;SeedTriangles=0;Sequence=0;
    }

    void DescribeMesh(TerrainLodRenderPacket& packet)
    {
        if (!Pipeline || !HasMesh) return;
        // 未消费 Pending 的失败/暂停路径只重发最新完整 mesh，不清理内部待消费集合
        MeshConsumption mesh;mesh.Data=&Pipeline->Mesh();mesh.Generation=Pipeline->State().Version();mesh.Full=true;
        packet=TransactionalRenderBridge::Packet(mesh,true);
        PublicStats.ActiveTriangleCount=Pipeline->State().FaceCount();
        PublicStats.Transactional->Samples=Pipeline->Samples().SampleCount();
        PublicStats.Transactional->HasPublishedMesh=true;
    }

    bool Build(const TerrainLodBuildInput& input,TerrainLodRenderPacket& packet,std::string* message)
    {
        packet={};CoreMilliseconds=0;
        const auto key=Key(input);const auto viewKey=ViewKey(input.View);
        if (!CurrentKey || *CurrentKey!=key) { Clear();CurrentKey=key; }
        PublicStats={};PublicStats.Transactional.emplace();auto& stats=*PublicStats.Transactional;
        stats.RequestedWorkers=key.Settings.WorkerCount;stats.PrefixLimit=key.Settings.PrefixLimit;
        stats.DonorLimit=key.Settings.DonorLimit;stats.SeedTriangles=SeedTriangles;
        PublicStats.TriangleBudget=key.Budget;PublicStats.BuildSequence=++Sequence;
        WorkLedger work;work.VisitLimit=key.Settings.SampleVisitLimit;
        work.Deadline=Clock::now()+std::chrono::seconds(180);
        TransactionalLodStatus phase=TransactionalLodStatus::InputRejected;
        try
        {
            if (!input.View.DrawableWidth || !input.View.DrawableHeight)
            {
                stats.Status=TransactionalLodStatus::Paused;DescribeMesh(packet);
                stats.HasPublishedMesh=HasMesh;return HasMesh;
            }
            if (Blocked && (FailedView==viewKey || (Executor && Executor->IsStopped())))
            {
                stats.Status=TransactionalLodStatus::RetrySuppressed;DescribeMesh(packet);
                stats.HasPublishedMesh=HasMesh;if (message) *message=Error;return false;
            }
            TransactionalSeedBuilder::ValidateInput(input);
            if (!Pipeline)
            {
                stats.ColdStart=true;phase=TransactionalLodStatus::SeedRejected;
                auto start=Clock::now();auto seed=TransactionalSeedBuilder::Build(input);
                stats.SeedMilliseconds=Milliseconds(start);SeedTriangles=seed.Faces.size();stats.SeedTriangles=SeedTriangles;
                start=Clock::now();Executor=std::make_unique<Tools::CpuTaskExecutor>(key.Settings.WorkerCount);
                TransactionalExecution execution{key.Settings.WorkerCount,false,[this](auto chunks,const auto& task) {
                    Executor->Dispatch(chunks,task);
                }};
                auto next=std::make_unique<TransactionalPipeline>(std::move(seed),std::move(execution));
                TransactionalStateInvariant::Validate(next->State());next->Initialize(work);
                Pipeline=std::move(next);HasMesh=true;stats.InitializeMilliseconds=Milliseconds(start);
            }
            const auto config=TransactionalSeedBuilder::ConfigurationFor(input);
            const auto coreStart=Clock::now();
            {
                phase=TransactionalLodStatus::ViewRejected;Pipeline->SetView(config,work);stats.ViewPublished=true;
                phase=TransactionalLodStatus::UpdateRejected;auto batch=Pipeline->Update(work);
                TransactionalRenderBridge::Account(work,batch,stats);stats.Updated=true;
                // 批次记录在完整 CPU 边界内释放，不让调用者替算法承担析构时间
            }
            CoreMilliseconds=Milliseconds(coreStart);
            phase=TransactionalLodStatus::OutputRejected;
            // Consume 后桥接可能分配失败，先保留恢复标志，成功转交后才清除
            const bool full=ForceFull;ForceFull=true;auto consumed=Pipeline->ConsumeMesh();
            packet=TransactionalRenderBridge::Packet(consumed,full);ForceFull=false;
            stats.Status=TransactionalLodStatus::Ready;Blocked=false;Error.clear();if (message) message->clear();
            stats.Samples=Pipeline->Samples().SampleCount();stats.HasPublishedMesh=true;
            stats.SourceBytes=static_cast<std::uint64_t>(input.HeightMap->RawSamples().size())*sizeof(std::uint16_t);
            PublicStats.ActiveTriangleCount=Pipeline->State().FaceCount();
            PublicStats.CpuMeshDirtyRangeCount=packet.CpuMeshUpdateRanges.size();
            PublicStats.CpuMeshFullRebuildCount=stats.ColdStart ? 1U : 0U;
            PublicStats.CpuMeshUpdatedTriangleCount=stats.ColdStart ? Pipeline->State().FaceCount() : static_cast<std::size_t>(work.MeshVertices/3);
            return true;
        }
        catch (const std::bad_alloc&)
        {
            stats.Status=Executor && Executor->IsStopped() ? TransactionalLodStatus::ExecutorStopped : TransactionalLodStatus::AllocationFailed;
            Error="事务化准备或输出分配失败";
        }
        catch (const std::exception& error)
        {
            stats.Status=Executor && Executor->IsStopped() ? TransactionalLodStatus::ExecutorStopped :
                (work.SampleTouches>work.VisitLimit || Clock::now()>work.Deadline ? TransactionalLodStatus::QuotaExceeded : phase);
            Error=error.what();
        }
        Blocked=true;FailedView=viewKey;ForceFull=true;stats.HasPublishedMesh=HasMesh;
        DescribeMesh(packet);if (message) *message=Error;
        if (Pipeline && HasMesh) PublicStats.ActiveTriangleCount=Pipeline->State().FaceCount();
        return false;
    }
};

TransactionalTerrainLodAlgorithm::TransactionalTerrainLodAlgorithm() : _impl(std::make_unique<Impl>()) {}
TransactionalTerrainLodAlgorithm::~TransactionalTerrainLodAlgorithm()=default;
TerrainLodAlgorithmInfo TransactionalTerrainLodAlgorithm::Info() const
{
    return {TerrainLodAlgorithmId::TransactionalCpuLod,"transactional_cpu_lod","Transactional CPU LOD",
        "Snapshot priority-driven transactional CPU LOD"};
}
TerrainLodAlgorithmCapabilities TransactionalTerrainLodAlgorithm::Capabilities() const
{
    TerrainLodAlgorithmCapabilities c;c.SupportsCpuMeshOutput=true;c.SupportsSplit=true;c.SupportsMerge=true;
    c.SupportsCrackFix=true;c.RequiresContinuousUpdate=true;return c;
}
bool TransactionalTerrainLodAlgorithm::BuildRenderData(const TerrainLodBuildInput& input,TerrainLodRenderPacket& packet,std::string* error)
{
    ROAM_CPU_ZONE("tpi.cpu_ready");const auto start=Clock::now();const bool ok=_impl->Build(input,packet,error);
    auto& stats=*_impl->PublicStats.Transactional;stats.CpuReadyMilliseconds=Milliseconds(start);
    stats.AdapterMilliseconds=std::max(0.0,stats.CpuReadyMilliseconds-stats.SeedMilliseconds-stats.InitializeMilliseconds-_impl->CoreMilliseconds);
    _impl->PublicStats.CpuUpdateMilliseconds=static_cast<float>(stats.CpuReadyMilliseconds);return ok;
}
const TerrainLodStats& TransactionalTerrainLodAlgorithm::Stats() const { return _impl->PublicStats; }
void TransactionalTerrainLodAlgorithm::Reset() { _impl->Clear(); }
void TransactionalTerrainLodAlgorithm::RequestFullUpload() { _impl->ForceFull=true; }
void TransactionalTerrainLodAlgorithm::Retry()
{
    if (!_impl->Executor || !_impl->Executor->IsStopped()) _impl->Blocked=false;
}
}
