#include "algorithms/cbt_2024/d3d12/D3D12CbtTerrainLodAlgorithm.h"

#include "algorithms/cbt_2024/Cbt2024Baseline.h"
#include "algorithms/cbt_2024/Cbt2024Support.h"
#include "algorithms/cbt_2024/d3d12/D3D12CbtFramePipeline.h"
#include "algorithms/cbt_2024/d3d12/D3D12CbtGpuState.h"
#include "render/D3D12GraphicsBackend.h"
#include "terrain/TerrainMeshBuilder.h"
#include "tools/PerformanceTimer.h"

#include <d3d12.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace ParallelRoam::Algorithms::Cbt2024::D3D12
{
struct D3D12CbtTerrainState
{
    // Topology 必须晚于 Pipeline 析构；成员逆序销毁保证描述符先归还再释放资源
    D3D12CbtGpuState Topology;
    D3D12CbtFramePipeline Pipeline;
    float TerrainSize{0.0F};
    float HeightScale{0.0F};
    bool GeometryInitialized{false};
};

namespace
{
void SetError(std::string* errorMessage, const std::string& message)
{
    if (errorMessage != nullptr)
    {
        *errorMessage = message;
    }
}

CbtOccupancyCapacity ToCbtCapacity(TerrainLodCbtCapacity capacity)
{
    switch (capacity)
    {
    case TerrainLodCbtCapacity::Capacity128K: return CbtOccupancyCapacity::Capacity128K;
    case TerrainLodCbtCapacity::Capacity256K: return CbtOccupancyCapacity::Capacity256K;
    case TerrainLodCbtCapacity::Capacity512K: return CbtOccupancyCapacity::Capacity512K;
    case TerrainLodCbtCapacity::Capacity1M: return CbtOccupancyCapacity::Capacity1M;
    }
    return CbtOccupancyCapacity::Capacity128K;
}

void FillRenderPacket(
    const D3D12CbtTerrainState& state,
    TerrainLodRenderPacket& outPacket)
{
    const CbtBaseTopology& topology = state.Topology.Topology();
    const D3D12CbtGpuResourceView resources = state.Topology.Resources();
    const auto& templates = state.Pipeline.LastBisectTemplateCounts();
    outPacket.Mode = TerrainLodRenderMode::GpuProceduralIndirect;
    outPacket.StatusMessage =
        "CBT 2024 baseline v1 " + std::string{CbtOccupancyCapacityName(topology.Layout.Occupancy.Capacity)} +
        " commit: split=" + std::to_string(state.Pipeline.LastSplitCandidateCount()) +
        " simplify=" + std::to_string(state.Pipeline.LastSimplifyCandidateCount()) +
        " nodes=" + std::to_string(state.Pipeline.LastPlannedSplitNodeCount()) +
        " slots=" + std::to_string(state.Pipeline.LastAllocatedSplitSlotCount()) +
        " committed=" + std::to_string(state.Pipeline.LastCommittedDynamicSlotCount()) +
        " propagate=" + std::to_string(state.Pipeline.LastSplitPropagationCount()) +
        " merge=" + std::to_string(state.Pipeline.LastPreparedSimplificationCount()) +
        " released=" + std::to_string(state.Pipeline.LastReleasedDynamicSlotCount()) +
        " merge-propagate=" + std::to_string(state.Pipeline.LastSimplifyPropagationCount()) +
        " pair/quad=" + std::to_string(state.Pipeline.LastPairMergeCount()) + "/" +
        std::to_string(state.Pipeline.LastQuadMergeCount()) +
        " templates=" + std::to_string(templates[0]) + "/" +
        std::to_string(templates[1]) + "/" + std::to_string(templates[2]) + "/" +
        std::to_string(templates[3]) +
        " active=" + std::to_string(state.Pipeline.LastIndexedActiveCount()) +
        " remaining=" + std::to_string(state.Pipeline.LastRemainingDynamicSlotCount()) +
        " duplicate/shared=" + std::to_string(state.Pipeline.LastDuplicateSplitClaimCount()) + "/" +
        std::to_string(state.Pipeline.LastSharedCompatibilityCount()) +
        " chain=" + std::to_string(state.Pipeline.LastCompatibilityStepCount()) + "/" +
        std::to_string(state.Pipeline.LastMaximumCompatibilityLength()) +
        " sample=" + std::to_string(state.Pipeline.ClassificationSampleGeneration());
    outPacket.NativeResourceApi = TerrainLodNativeResourceApi::Direct3D12;
    outPacket.NativeVertexBuffer = reinterpret_cast<std::uintptr_t>(state.Pipeline.RenderVertices());
    outPacket.NativeActiveLeafBuffer = reinterpret_cast<std::uintptr_t>(resources.ActiveIndices);
    outPacket.NativeLodStateBuffer = reinterpret_cast<std::uintptr_t>(resources.BisectorData);
    outPacket.NativeIndirectDrawBuffer = reinterpret_cast<std::uintptr_t>(resources.IndirectDrawState);
    outPacket.GpuVertexBufferCapacityBytes = state.Pipeline.RenderVertexCapacityBytes();
    outPacket.GpuVertexStrideBytes = sizeof(Terrain::TerrainMeshVertex);
    outPacket.GpuActiveLeafBufferCapacityBytes =
        static_cast<std::size_t>(topology.Layout.IndexElementCount) * sizeof(std::uint32_t);
    outPacket.GpuActiveLeafStrideBytes = sizeof(std::uint32_t);
    outPacket.GpuLodStateBufferCapacityBytes =
        static_cast<std::size_t>(topology.Layout.TotalElementCount) * sizeof(CbtBisectorData);
    outPacket.GpuLodStateStrideBytes = sizeof(CbtBisectorData);
    outPacket.GpuIndirectDrawBufferCapacityBytes = sizeof(CbtDrawState);
    outPacket.GpuIndirectDrawArgumentOffsetBytes = offsetof(CbtDrawState, Active);
    outPacket.GpuResourceLifetime = TerrainLodGpuResourceLifetime::UntilNextBuildOrReset;
    outPacket.GpuResourceGeneration = state.Topology.Generation();
    // 数量来自延迟诊断镜像；绘制正确性仍只依赖 GPU draw state。
    outPacket.ActiveLeafCount = state.Pipeline.LastIndexedActiveCount();
    outPacket.ActiveTriangleCount = state.Pipeline.LastIndexedActiveCount();
}
} // namespace

D3D12CbtTerrainLodAlgorithm::D3D12CbtTerrainLodAlgorithm(Render::D3D12GraphicsBackend& backend)
    : _backend(&backend),
      _state(std::make_unique<D3D12CbtTerrainState>())
{
}

D3D12CbtTerrainLodAlgorithm::~D3D12CbtTerrainLodAlgorithm() = default;

TerrainLodAlgorithmInfo D3D12CbtTerrainLodAlgorithm::Info() const
{
    return TerrainLodAlgorithmInfo{
        TerrainLodAlgorithmId::Cbt2024,
        OfficialBaselineV1::AlgorithmKey,
        OfficialBaselineV1::DisplayName,
        "冻结上游 pass 语义、四档容量、高度图增量几何、延迟诊断和逐阶段 GPU 计时",
    };
}

TerrainLodAlgorithmCapabilities D3D12CbtTerrainLodAlgorithm::Capabilities() const
{
    return TerrainLodAlgorithmCapabilities{
        .SupportsCpuMeshOutput = false,
        .SupportsGpuDrivenRendering = true,
        .SupportsProceduralIndirectRendering = true,
        .SupportsSplit = true,
        .SupportsMerge = true,
        .SupportsCrackFix = true,
        .SupportsTopologyValidation = true,
        .RequiresShaderModel66 = true,
        .RequiresInt64ShaderOps = true,
        .RequiresInt64Atomics = true,
        .UpdatePolicy = TerrainLodUpdatePolicy::EveryFrame,
    };
}

bool D3D12CbtTerrainLodAlgorithm::BuildRenderData(
    const TerrainLodBuildInput& input,
    TerrainLodRenderPacket& outPacket,
    std::string* errorMessage)
{
    Tools::PerformanceTimer buildTimer;
    outPacket = {};
    _stats = {};

    if (_backend == nullptr || _backend->Device() == nullptr)
    {
        SetError(errorMessage, "CBT 2024 requires an initialized D3D12 device");
        return false;
    }
    if (!_backend->FrameOpen() || _backend->CommandList() == nullptr)
    {
        SetError(errorMessage, "CBT 2024 H must record between BeginFrame and Present");
        return false;
    }
    const Cbt2024Availability availability = QueryCbt2024Availability(*_backend);
    if (!availability.Available)
    {
        SetError(errorMessage, availability.UnavailableReason);
        return false;
    }
    if (input.HeightMap == nullptr || !input.HeightMap->IsValid())
    {
        SetError(errorMessage, "CBT 2024 requires a valid terrain input");
        return false;
    }

    const CbtOccupancyCapacity requestedCapacity = ToCbtCapacity(input.Settings.CbtCapacity);
    // 容量会改变全部持久 buffer 与编译期特化 PSO；切换时等待旧资源代不再被 GPU 使用。
    if (_state->Topology.IsInitialized() &&
        _state->Topology.Topology().Layout.Occupancy.Capacity != requestedCapacity)
    {
        _backend->WaitForGpuIdle();
        _state = std::make_unique<D3D12CbtTerrainState>();
    }

    const auto initializeState = [&](D3D12CbtTerrainState& state) {
        if (!state.Topology.IsInitialized() &&
            !state.Topology.Rebuild(
                *_backend,
                requestedCapacity,
                errorMessage))
        {
            return false;
        }
        return state.Pipeline.IsInitialized() ||
            state.Pipeline.Initialize(
                *_backend,
                state.Topology.Topology(),
                state.Topology.Resources(),
                *input.HeightMap,
                errorMessage);
    };
    if (!initializeState(*_state))
    {
        return false;
    }

    auto recordState = [&](D3D12CbtTerrainState& state) {
        const bool rebuild = !state.GeometryInitialized ||
            state.TerrainSize != input.Settings.TerrainSize ||
            state.HeightScale != input.Settings.HeightScale;
        return state.Pipeline.RecordFrame(
            input,
            state.Topology.Topology(),
            state.Topology.Resources(),
            rebuild,
            errorMessage);
    };
    bool rebuildGeometry =
        !_state->GeometryInitialized || _state->TerrainSize != input.Settings.TerrainSize ||
        _state->HeightScale != input.Settings.HeightScale;
    if (!recordState(*_state))
    {
        if (!_state->Pipeline.IsFaulted())
        {
            return false;
        }

        // 延迟错误意味着持久邻接/OCBT 已不可继续消费。异常路径允许一次 GPU idle，
        // 随后用全新资源代在当前空闲命令列表中重新 Bootstrap，而不是尝试局部回滚。
        _lastRecoveryMessage = _state->Pipeline.FaultMessage();
        _backend->WaitForGpuIdle();
        _state = std::make_unique<D3D12CbtTerrainState>();
        if (!initializeState(*_state) || !recordState(*_state))
        {
            const std::string recoveryError = errorMessage != nullptr ? *errorMessage : std::string{};
            SetError(
                errorMessage,
                "CBT fault recovery failed after: " + _lastRecoveryMessage +
                    (recoveryError.empty() ? std::string{} : "; retry: " + recoveryError));
            return false;
        }
        ++_recoveryCount;
        rebuildGeometry = true;
    }
    if (rebuildGeometry)
    {
        _state->TerrainSize = input.Settings.TerrainSize;
        _state->HeightScale = input.Settings.HeightScale;
        _state->GeometryInitialized = true;
    }

    const std::size_t activeCount = _state->Pipeline.LastIndexedActiveCount();
    const std::size_t committedNodeCount = _state->Pipeline.LastPlannedSplitNodeCount();
    _stats.ActiveTriangleCount = activeCount;
    _stats.GpuTopologyFrameGeneration = _state->Pipeline.TopologyFrameGeneration();
    _stats.GpuClassificationSampleGeneration = _state->Pipeline.ClassificationSampleGeneration();
    _stats.CbtGpuTimingSampleGeneration = _state->Pipeline.GpuTimingSampleGeneration();
    _stats.CbtDiagnosticSampleAge =
        _stats.GpuTopologyFrameGeneration >= _stats.GpuClassificationSampleGeneration
        ? _stats.GpuTopologyFrameGeneration - _stats.GpuClassificationSampleGeneration
        : 0U;
    _stats.CbtDiagnosticSampleDropped =
        _stats.GpuClassificationSampleGeneration == 0U ||
        _stats.GpuClassificationSampleGeneration == _lastPublishedDiagnosticGeneration ||
        (_lastPublishedDiagnosticGeneration != 0U &&
         _stats.GpuClassificationSampleGeneration > _lastPublishedDiagnosticGeneration + 1U);
    if (_stats.GpuClassificationSampleGeneration != 0U &&
        _stats.GpuClassificationSampleGeneration != _lastPublishedDiagnosticGeneration)
    {
        _lastPublishedDiagnosticGeneration = _stats.GpuClassificationSampleGeneration;
    }
    _stats.CbtResourceGeneration = _state->Topology.Generation();
    _stats.CbtCapacitySetting = static_cast<std::uint32_t>(input.Settings.CbtCapacity);
    _stats.CbtTriangleAreaPixelsSetting = input.Settings.CbtTriangleAreaPixels;
    _stats.CbtValidationModeSetting = input.Settings.CbtValidationMode;
    _stats.CbtGeometryModeSetting = input.Settings.CbtGeometryMode;
    // 延迟计数和 compute 时间由同一诊断槽发布
    // renderer 会用 topology generation 对齐独立的 terrain draw query
    _stats.CbtActiveDynamicSlotCount = _state->Pipeline.LastActiveDynamicSlotCount();
    _stats.CbtRemainingDynamicSlotCount = _state->Pipeline.LastRemainingDynamicSlotCount();
    _stats.CbtGpuStageMilliseconds = _state->Pipeline.LastGpuStageMilliseconds();
    _stats.CbtGpuStageSumMilliseconds = _state->Pipeline.LastGpuStageSumMilliseconds();
    _stats.CbtBlockingValidationWaitMilliseconds =
        _state->Pipeline.LastBlockingValidationWaitMilliseconds();
    _stats.ActiveNodeCount = activeCount;
    _stats.OriginalTriangleCount = CbtBaseBisectorCount;
    _stats.SubdividedTriangleCount = activeCount > CbtBaseBisectorCount
        ? activeCount - CbtBaseBisectorCount
        : 0U;
    _stats.RebuiltTriangleCount = committedNodeCount +
        _state->Pipeline.LastCommittedDynamicSlotCount() +
        _state->Pipeline.LastPreparedSimplificationCount();
    _stats.ActiveSplitCount = committedNodeCount;
    _stats.SplitCount = committedNodeCount;
    _stats.ForcedSplitCount = committedNodeCount > _state->Pipeline.LastSplitCandidateCount()
        ? committedNodeCount - _state->Pipeline.LastSplitCandidateCount()
        : 0U;
    _stats.MergeCount = _state->Pipeline.LastPreparedSimplificationCount();
    _stats.ConstraintPassCount = _state->Pipeline.LastSplitPropagationCount() +
        _state->Pipeline.LastSimplifyPropagationCount();
    _stats.CandidatePeakCount = _state->Pipeline.LastSplitCandidateCount() +
        _state->Pipeline.LastSimplifyCandidateCount();
    _stats.SplitTopologyCandidateCount = _state->Pipeline.LastSplitCandidateCount();
    _stats.MergeTopologyCandidateCount = _state->Pipeline.LastSimplifyCandidateCount();
    _stats.CbtCommittedDynamicSlotCount = _state->Pipeline.LastCommittedDynamicSlotCount();
    _stats.CbtSplitPropagationCount = _state->Pipeline.LastSplitPropagationCount();
    _stats.CbtPreparedSimplificationCount = _state->Pipeline.LastPreparedSimplificationCount();
    _stats.CbtReleasedDynamicSlotCount = _state->Pipeline.LastReleasedDynamicSlotCount();
    _stats.CbtSimplifyPropagationCount = _state->Pipeline.LastSimplifyPropagationCount();
    _stats.CbtPairMergeCount = _state->Pipeline.LastPairMergeCount();
    _stats.CbtQuadMergeCount = _state->Pipeline.LastQuadMergeCount();
    const auto& templateCounts = _state->Pipeline.LastBisectTemplateCounts();
    for (std::size_t index = 0U; index < templateCounts.size(); ++index)
    {
        _stats.CbtBisectTemplateCounts[index] = templateCounts[index];
    }
    _stats.MaxActiveDepth = static_cast<int>(_state->Pipeline.LastMaximumActiveDepth());
    _stats.CpuGpuReadbackBytes = (input.Settings.CbtValidationMode != TerrainLodCbtValidationMode::Off
        ? D3D12CbtDiagnostics::ValidationReadbackBytes
        : D3D12CbtDiagnostics::DiagnosticReadbackBytes) +
        D3D12CbtDiagnostics::GpuTimestampReadbackBytes;
    const auto stage = [&](TerrainLodCbtGpuStage value) {
        return _stats.CbtGpuStageMilliseconds[static_cast<std::size_t>(value)];
    };
    _stats.SplitMilliseconds =
        stage(TerrainLodCbtGpuStage::Split) +
        stage(TerrainLodCbtGpuStage::Allocate) +
        stage(TerrainLodCbtGpuStage::NeighborCopy) +
        stage(TerrainLodCbtGpuStage::Bisect) +
        stage(TerrainLodCbtGpuStage::PropagateBisect);
    _stats.MergeMilliseconds =
        stage(TerrainLodCbtGpuStage::PrepareSimplify) +
        stage(TerrainLodCbtGpuStage::Simplify) +
        stage(TerrainLodCbtGpuStage::PropagateSimplify);
    _stats.EmitMilliseconds =
        stage(TerrainLodCbtGpuStage::ClassificationGeometry) +
        stage(TerrainLodCbtGpuStage::RenderGeometry);
    _stats.ValidateMilliseconds = stage(TerrainLodCbtGpuStage::Validation);
    _stats.CpuUpdateMilliseconds = buildTimer.Stop();

    FillRenderPacket(*_state, outPacket);
    if (_recoveryCount != 0U)
    {
        outPacket.StatusMessage +=
            " recovery=" + std::to_string(_recoveryCount) + " last-fault={" +
            _lastRecoveryMessage + "}";
    }
    if (!outPacket.HasConsistentResourceContract())
    {
        SetError(errorMessage, "CBT 2024 produced an inconsistent GPU render packet");
        return false;
    }
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }
    return true;
}

const TerrainLodStats& D3D12CbtTerrainLodAlgorithm::Stats() const
{
    return _stats;
}

void D3D12CbtTerrainLodAlgorithm::Reset()
{
    // benchmark 的每种路径必须从基础拓扑开始；等待只发生在显式 reset 边界
    if (_backend != nullptr && _state != nullptr && _state->Topology.IsInitialized())
    {
        _backend->WaitForGpuIdle();
    }
    _state = std::make_unique<D3D12CbtTerrainState>();
    _stats = {};
    _lastPublishedDiagnosticGeneration = 0U;
}
} // namespace ParallelRoam::Algorithms::Cbt2024::D3D12
