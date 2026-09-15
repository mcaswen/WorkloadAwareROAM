#include "algorithms/cbt_2024/d3d12/D3D12CbtDiagnostics.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <vector>

namespace ParallelRoam::Algorithms::Cbt2024::D3D12
{
namespace
{
void SetError(std::string* errorMessage, const std::string& message)
{
    if (errorMessage != nullptr)
    {
        *errorMessage = message;
    }
}

D3D12_HEAP_PROPERTIES ReadbackHeapProperties()
{
    // READBACK heap 对 CPU 可见，并且 D3D12 要求其初始状态固定为 COPY_DEST。
    D3D12_HEAP_PROPERTIES properties{};
    properties.Type = D3D12_HEAP_TYPE_READBACK;
    properties.CreationNodeMask = 1U;
    properties.VisibleNodeMask = 1U;
    return properties;
}

D3D12_RESOURCE_DESC ReadbackDescription(std::size_t byteCount)
{
    // 始终按最大验证载荷分配；普通帧仅映射计数头的有效范围。
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    description.Width = byteCount;
    description.Height = 1U;
    description.DepthOrArraySize = 1U;
    description.MipLevels = 1U;
    description.SampleDesc.Count = 1U;
    description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return description;
}

bool Near(float lhs, float rhs, float tolerance)
{
    return std::isfinite(lhs) && std::isfinite(rhs) && std::abs(lhs - rhs) <= tolerance;
}

bool Near(const glm::vec2& lhs, const glm::vec2& rhs, float tolerance)
{
    return Near(lhs.x, rhs.x, tolerance) && Near(lhs.y, rhs.y, tolerance);
}

bool Near(const glm::vec3& lhs, const glm::vec3& rhs, float tolerance)
{
    return Near(lhs.x, rhs.x, tolerance) &&
           Near(lhs.y, rhs.y, tolerance) &&
           Near(lhs.z, rhs.z, tolerance);
}

bool Near(const Terrain::TerrainMeshVertex& lhs, const Terrain::TerrainMeshVertex& rhs)
{
    return Near(lhs.Position, rhs.Position, 2.0e-4F) &&
           Near(lhs.Normal, rhs.Normal, 2.0e-4F) &&
           Near(lhs.TexCoord, rhs.TexCoord, 1.0e-5F) &&
           Near(lhs.Height, rhs.Height, 1.0e-5F) &&
           Near(lhs.DebugColor, rhs.DebugColor, 1.0e-5F) &&
           Near(lhs.DebugHighlight, rhs.DebugHighlight, 1.0e-5F);
}
} // namespace

static_assert(D3D12CbtDiagnostics::DiagnosticReadbackBytes == 140U);
static_assert(D3D12CbtDiagnostics::ValidationReadbackBytes == 1556U);

bool D3D12CbtDiagnostics::Initialize(
    Render::D3D12GraphicsBackend& backend,
    std::string* errorMessage)
{
    // 初始化保持全有或全无语义，失败不会留下部分可用的 readback ring。
    Shutdown();
    ID3D12Device* device = backend.Device();
    ID3D12CommandQueue* commandQueue = backend.CommandQueue();
    if (device == nullptr || commandQueue == nullptr)
    {
        SetError(errorMessage, "CBT diagnostics requires a D3D12 device");
        return false;
    }
    const D3D12_HEAP_PROPERTIES heap = ReadbackHeapProperties();
    const D3D12_RESOURCE_DESC description = ReadbackDescription(ValidationReadbackBytes);
    for (Microsoft::WRL::ComPtr<ID3D12Resource>& readback : _readbacks)
    {
        if (FAILED(device->CreateCommittedResource(
                &heap,
                D3D12_HEAP_FLAG_NONE,
                &description,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(readback.ReleaseAndGetAddressOf()))))
        {
            SetError(errorMessage, "Failed to create CBT diagnostic readback buffer");
            Shutdown();
            return false;
        }
    }
    // 所有 compute/geometry 区间共用一个 query heap
    // frame index 划分互斥范围，避免在途帧覆盖彼此的 timestamp
    D3D12_QUERY_HEAP_DESC queryDescription{};
    queryDescription.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    queryDescription.Count = static_cast<UINT>(FrameCount * TimestampCountPerFrame);
    if (FAILED(device->CreateQueryHeap(
            &queryDescription,
            IID_PPV_ARGS(_timestampQueryHeap.ReleaseAndGetAddressOf()))))
    {
        SetError(errorMessage, "Failed to create CBT GPU timestamp query heap");
        Shutdown();
        return false;
    }
    // timestamp 单独使用固定小 readback，普通统计不会映射基础几何尾部
    const D3D12_RESOURCE_DESC timestampDescription = ReadbackDescription(TimestampReadbackBytes);
    for (Microsoft::WRL::ComPtr<ID3D12Resource>& readback : _timestampReadbacks)
    {
        if (FAILED(device->CreateCommittedResource(
                &heap,
                D3D12_HEAP_FLAG_NONE,
                &timestampDescription,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(readback.ReleaseAndGetAddressOf()))))
        {
            SetError(errorMessage, "Failed to create CBT GPU timestamp readback buffer");
            Shutdown();
            return false;
        }
    }
    // GPU tick 只能用执行该 command list 的 queue 频率换算
    if (FAILED(commandQueue->GetTimestampFrequency(&_timestampFrequency)) || _timestampFrequency == 0U)
    {
        SetError(errorMessage, "Failed to query CBT GPU timestamp frequency");
        Shutdown();
        return false;
    }
    return true;
}

void D3D12CbtDiagnostics::Shutdown()
{
    // 清空资源和对应元数据，防止重建后消费旧 GPU 代次的期望值。
    _readbacks = {};
    _timestampReadbacks = {};
    _timestampQueryHeap.Reset();
    _timestampFrequency = 0U;
    _pending = {};
    _generations = {};
    _validationPending = {};
    _exactReferencePending = {};
    _expectations = {};
    _snapshot = {};
    _snapshot.IndexedActiveCount = CbtBaseBisectorCount;
    _faultMessage.clear();
    _faulted = false;
}

ID3D12Resource* D3D12CbtDiagnostics::Readback(std::uint32_t frameIndex) const
{
    return frameIndex < _readbacks.size() ? _readbacks[frameIndex].Get() : nullptr;
}

void D3D12CbtDiagnostics::QueueSample(
    std::uint32_t frameIndex,
    std::uint64_t generation,
    bool validation,
    bool exactReference,
    const D3D12CbtDiagnosticExpectation& expectation)
{
    // 调用点位于命令录制末尾，此处只把 CPU 参考绑定到同一个 staging 槽。
    _pending[frameIndex] = true;
    _generations[frameIndex] = generation;
    _validationPending[frameIndex] = validation;
    _exactReferencePending[frameIndex] = exactReference;
    _expectations[frameIndex] = expectation;
}

bool D3D12CbtDiagnostics::ConsumeCompleted(
    std::uint32_t frameIndex,
    std::string* errorMessage)
{
    if (!_pending[frameIndex])
    {
        return true;
    }

    // 普通诊断只读固定计数头；完整验证才会触碰 draw state 与基础节点数据。
    const std::size_t readbackBytes = _validationPending[frameIndex]
        ? ValidationReadbackBytes
        : DiagnosticReadbackBytes;
    const D3D12_RANGE readRange{0U, readbackBytes};
    void* mapped = nullptr;
    if (FAILED(_readbacks[frameIndex]->Map(0U, &readRange, &mapped)))
    {
        return LatchFault("Failed to map completed CBT diagnostic counters", errorMessage);
    }
    // 四段计数保持原始 UAV 字节布局，避免为诊断再增加 GPU packing pass。
    const auto* bytes = static_cast<const std::uint8_t*>(mapped);
    const auto* counters = reinterpret_cast<const std::uint32_t*>(bytes + ClassificationCounterOffset);
    _snapshot.SplitCandidateCount = counters[0];
    _snapshot.SimplifyCandidateCount = counters[1];
    std::memcpy(
        &_snapshot.PlannedSplitNodeCount,
        bytes + AllocationCounterOffset,
        sizeof(_snapshot.PlannedSplitNodeCount));
    const auto* memoryCounters = reinterpret_cast<const std::uint32_t*>(bytes + MemoryCounterOffset);
    _snapshot.AllocatedSplitSlotCount = memoryCounters[0];
    _snapshot.RemainingDynamicSlotCount = memoryCounters[1];
    // validation[0..1] 是首错 code/slot，后续字保存 split、merge 和帧前活动数。
    std::array<std::uint32_t, CbtValidationWordCount> validation{};
    std::memcpy(validation.data(), bytes + ValidationCounterOffset, sizeof(validation));
    _snapshot.DuplicateSplitClaimCount = validation[2];
    _snapshot.SharedCompatibilityCount = validation[3];
    _snapshot.CompatibilityStepCount = validation[4];
    _snapshot.MaximumCompatibilityLength = validation[5];
    _snapshot.CommittedDynamicSlotCount = validation[6];
    _snapshot.SplitPropagationCount = validation[7];
    std::copy_n(
        validation.begin() + 8,
        _snapshot.BisectTemplateCounts.size(),
        _snapshot.BisectTemplateCounts.begin());
    _snapshot.PreparedSimplificationCount = validation[12];
    _snapshot.ReleasedDynamicSlotCount = validation[13];
    _snapshot.SimplifyPropagationCount = validation[14];
    _snapshot.PairMergeCount = validation[15];
    _snapshot.QuadMergeCount = validation[16];
    // Merge groups partition the accepted simplify list rather than individual nodes.
    // A pair group removes one sibling, while a facing-pair group removes two siblings.
    // The frame-start root lets delayed readback verify the net split/merge occupancy delta.
    // These relations detect duplicate submission even when every surviving heapID is valid.
    // Neighbor reciprocity remains shader-side because copying the full topology would stall the frame.
    const D3D12_RANGE noWrite{0U, 0U};
    // 先发布样本代次并释放槽位；任何失败随后都会锁存并阻止继续录制。
    _snapshot.SampleGeneration = _generations[frameIndex];
    _snapshot.GpuTimingSampleGeneration = _generations[frameIndex];
    _pending[frameIndex] = false;
    if (validation[0] != 0U)
    {
        const std::string message =
            "CBT shader validation failed at generation " +
            std::to_string(_snapshot.SampleGeneration) + ": code/slot=" +
            std::to_string(validation[0]) + "/" + std::to_string(validation[1]);
        _readbacks[frameIndex]->Unmap(0U, &noWrite);
        return LatchFault(message, errorMessage);
    }
    // 活动统计属于常规诊断样本，不依赖昂贵的全拓扑验证开关。
    std::uint32_t occupancyRoot = 0U;
    CbtDrawState drawState{};
    std::memcpy(&occupancyRoot, bytes + OccupancyRootOffset, sizeof(occupancyRoot));
    std::memcpy(&drawState, bytes + DrawStateReadbackOffset, sizeof(drawState));
    _snapshot.ActiveDynamicSlotCount = occupancyRoot;
    _snapshot.IndexedActiveCount = drawState.ActiveBisectorCount;
    _snapshot.MaximumActiveDepth = validation[CbtValidationMaxActiveDepthWord];
    const std::uint32_t expectedDynamic =
        validation[17] + _snapshot.CommittedDynamicSlotCount - _snapshot.ReleasedDynamicSlotCount;
    if (_snapshot.CommittedDynamicSlotCount != _snapshot.AllocatedSplitSlotCount ||
        _snapshot.PreparedSimplificationCount !=
            _snapshot.PairMergeCount + _snapshot.QuadMergeCount ||
        _snapshot.ReleasedDynamicSlotCount !=
            _snapshot.PairMergeCount + 2U * _snapshot.QuadMergeCount ||
        occupancyRoot != expectedDynamic ||
        drawState.Active.VertexCountPerInstance / 3U != drawState.ActiveBisectorCount ||
        drawState.ActiveBisectorCount != occupancyRoot + CbtBaseBisectorCount ||
        drawState.Visible.VertexCountPerInstance > drawState.Active.VertexCountPerInstance ||
        drawState.ModifiedPositionCount / 4U > drawState.ActiveBisectorCount)
    {
        const std::string message =
            "CBT occupancy/indexation mismatch at generation " +
            std::to_string(_snapshot.SampleGeneration);
        _readbacks[frameIndex]->Unmap(0U, &noWrite);
        return LatchFault(message, errorMessage);
    }

    if (_validationPending[frameIndex])
    {
        _validationPending[frameIndex] = false;
        const bool exactReference = _exactReferencePending[frameIndex];
        _exactReferencePending[frameIndex] = false;
        if (exactReference)
        {
            // 精确参考只覆盖初始化事务，便于逐项定位 E1/E2/E3 接入错误。
            const D3D12CbtDiagnosticExpectation& expected = _expectations[frameIndex];
            if (_snapshot.SplitCandidateCount != expected.SplitCandidateCount ||
                _snapshot.SimplifyCandidateCount != expected.SimplifyCandidateCount)
            {
                const std::string message =
                    "CBT E1 CPU/GPU classification mismatch at generation " +
                    std::to_string(_snapshot.SampleGeneration) +
                    ": expected split/simplify=" + std::to_string(expected.SplitCandidateCount) + "/" +
                    std::to_string(expected.SimplifyCandidateCount) + ", GPU=" +
                    std::to_string(_snapshot.SplitCandidateCount) + "/" +
                    std::to_string(_snapshot.SimplifyCandidateCount);
                _readbacks[frameIndex]->Unmap(0U, &noWrite);
                return LatchFault(message, errorMessage);
            }
            if (_snapshot.PlannedSplitNodeCount != expected.PlannedSplitNodeCount ||
                _snapshot.AllocatedSplitSlotCount != expected.AllocatedSplitSlotCount ||
                _snapshot.RemainingDynamicSlotCount != expected.RemainingDynamicSlotCount)
            {
                const std::string message =
                    "CBT E2 planning counter mismatch at generation " +
                    std::to_string(_snapshot.SampleGeneration) +
                    ": expected nodes/slots/remaining=" + std::to_string(expected.PlannedSplitNodeCount) + "/" +
                    std::to_string(expected.AllocatedSplitSlotCount) + "/" +
                    std::to_string(expected.RemainingDynamicSlotCount) + ", GPU=" +
                    std::to_string(_snapshot.PlannedSplitNodeCount) + "/" +
                    std::to_string(_snapshot.AllocatedSplitSlotCount) + "/" +
                    std::to_string(_snapshot.RemainingDynamicSlotCount);
                _readbacks[frameIndex]->Unmap(0U, &noWrite);
                return LatchFault(message, errorMessage);
            }

            // 基础节点中的 subdivision pattern 和物理槽位用于验证四模板提交结果。
            std::array<CbtBisectorData, CbtBaseBisectorCount> baseData{};
            std::memcpy(baseData.data(), bytes + BaseBisectorDataOffset, sizeof(baseData));
            std::vector<std::uint32_t> allocatedSlots;
            for (std::size_t node = 0U; node < baseData.size(); ++node)
            {
                const std::uint32_t expectedPattern = expected.SubdivisionPatterns[node];
                if (baseData[node].SubdivisionPattern != expectedPattern)
                {
                    const std::string message =
                        "CBT E2 subdivision pattern mismatch at generation " +
                        std::to_string(_snapshot.SampleGeneration) + ", base node=" +
                        std::to_string(node) + ", expected=" + std::to_string(expectedPattern) +
                        ", GPU=" + std::to_string(baseData[node].SubdivisionPattern);
                    _readbacks[frameIndex]->Unmap(0U, &noWrite);
                    return LatchFault(message, errorMessage);
                }
                const std::uint32_t slotCount =
                    static_cast<std::uint32_t>(std::popcount(expectedPattern));
                for (std::uint32_t slot = 0U; slot < slotCount; ++slot)
                {
                    allocatedSlots.push_back(baseData[node].Indices[slot]);
                }
            }
            // 排序后可同时检查分配数、重复槽位以及旧 OCBT free-rank 的单调映射。
            std::sort(allocatedSlots.begin(), allocatedSlots.end());
            if (allocatedSlots.size() != expected.AllocatedSplitSlotCount ||
                std::adjacent_find(allocatedSlots.begin(), allocatedSlots.end()) != allocatedSlots.end())
            {
                _readbacks[frameIndex]->Unmap(0U, &noWrite);
                return LatchFault(
                    "CBT E2 allocated physical slots are incomplete or duplicated",
                    errorMessage);
            }
            for (std::uint32_t rank = 0U; rank < allocatedSlots.size(); ++rank)
            {
                if (allocatedSlots[rank] != rank)
                {
                    const std::string message =
                        "CBT E2 allocation did not use the old OCBT complement at free rank " +
                        std::to_string(rank);
                    _readbacks[frameIndex]->Unmap(0U, &noWrite);
                    return LatchFault(message, errorMessage);
                }
            }
            if (occupancyRoot != expected.AllocatedSplitSlotCount)
            {
                const std::string message =
                    "CBT committed OCBT root mismatch: expected=" +
                    std::to_string(expected.AllocatedSplitSlotCount) + ", GPU=" +
                    std::to_string(occupancyRoot);
                _readbacks[frameIndex]->Unmap(0U, &noWrite);
                return LatchFault(message, errorMessage);
            }

            // 高度图对照使用数值容差，不对 float 执行字节级比较。
            // readback 顺序与帧管线的三个 CopyBufferRegion 目标偏移完全一致。
            // 顶点比较覆盖位置、法线、UV、归一化高度、调试色和高亮值。
            // child classification 点单独比较，避免渲染输出正确却分类数据陈旧。
            // parent 点来自 heapID 奇偶选择的旧父顶点，不使用三角形质心替代。
            // 容差只吸收 CPU/HLSL 浮点运算次序差异，不掩盖纹理方向错误。
            // 非有限值会由 Near 直接拒绝，并由 fault latch 保留第一处证据。
            // 该精确载荷只在新 pipeline 首帧启用，普通帧不复制几何尾部。
            // shader 全活动验证仍逐帧覆盖后续 modified-only 更新。
            std::array<Terrain::TerrainMeshVertex, CbtBaseBisectorCount * 3U> vertices{};
            std::array<glm::vec3, CbtBaseBisectorCount * 3U> classificationPositions{};
            std::array<glm::vec3, CbtBaseBisectorCount> parentPositions{};
            std::memcpy(vertices.data(), bytes + BaseRenderVertexOffset, sizeof(vertices));
            std::memcpy(
                classificationPositions.data(),
                bytes + BaseClassificationPositionOffset,
                sizeof(classificationPositions));
            std::memcpy(
                parentPositions.data(),
                bytes + BaseParentPositionOffset,
                sizeof(parentPositions));
            for (std::size_t node = 0U; node < expected.BaseGeometry.size(); ++node)
            {
                const CbtTerrainGeometryResult& expectedGeometry = expected.BaseGeometry[node];
                if (!expectedGeometry.Valid ||
                    !Near(
                        parentPositions[node],
                        expectedGeometry.ParentClassificationPosition,
                        2.0e-4F))
                {
                    _readbacks[frameIndex]->Unmap(0U, &noWrite);
                    return LatchFault(
                        "CBT parent classification position differs from the CPU reference at base node " +
                            std::to_string(node),
                        errorMessage);
                }
                for (std::size_t vertex = 0U; vertex < expectedGeometry.Vertices.size(); ++vertex)
                {
                    const std::size_t output = node * 3U + vertex;
                    if (!Near(vertices[output], expectedGeometry.Vertices[vertex]) ||
                        !Near(
                            classificationPositions[output],
                            expectedGeometry.Vertices[vertex].Position,
                            2.0e-4F))
                    {
                        _readbacks[frameIndex]->Unmap(0U, &noWrite);
                        return LatchFault(
                            "CBT height geometry differs from the CPU reference at base node/vertex " +
                                std::to_string(node) + "/" + std::to_string(vertex),
                            errorMessage);
                    }
                }
            }
        }
    }
    // counter 与 timestamp 在同一 frame-slot fence 后消费，因此代次天然一致
    // terrain draw 的独立 query 会在 renderer 中再进行一次代次匹配
    void* timestampMapped = nullptr;
    const D3D12_RANGE timestampReadRange{0U, TimestampReadbackBytes};
    if (FAILED(_timestampReadbacks[frameIndex]->Map(0U, &timestampReadRange, &timestampMapped)))
    {
        _readbacks[frameIndex]->Unmap(0U, &noWrite);
        return LatchFault("Failed to map completed CBT GPU timestamps", errorMessage);
    }
    const auto* timestamps = static_cast<const std::uint64_t*>(timestampMapped);
    _snapshot.GpuStageMilliseconds = {};
    _snapshot.GpuStageSumMilliseconds = 0.0F;
    // 所有区间互斥，阶段和可直接表示本次 CBT GPU 工作量
    // 无 dispatch 的阶段仍保留合法的零长度区间
    for (std::size_t stage = 0U; stage < ProfiledStageCount; ++stage)
    {
        const std::uint64_t begin = timestamps[stage * 2U];
        const std::uint64_t end = timestamps[stage * 2U + 1U];
        if (end < begin)
        {
            _timestampReadbacks[frameIndex]->Unmap(0U, &noWrite);
            _readbacks[frameIndex]->Unmap(0U, &noWrite);
            return LatchFault("CBT GPU timestamp order is invalid", errorMessage);
        }
        const float milliseconds = static_cast<float>(
            static_cast<double>(end - begin) * 1000.0 /
            static_cast<double>(_timestampFrequency));
        _snapshot.GpuStageMilliseconds[stage] = milliseconds;
        _snapshot.GpuStageSumMilliseconds += milliseconds;
    }
    _timestampReadbacks[frameIndex]->Unmap(0U, &noWrite);
    _readbacks[frameIndex]->Unmap(0U, &noWrite);
    return true;
}

bool D3D12CbtDiagnostics::ConsumeAllCompleted(std::string* errorMessage)
{
    // WaitForGpuIdle 已由调用方执行，此处只决定多槽发布顺序
    // generation 为零的空槽自然排在旧样本之前
    std::array<std::uint32_t, FrameCount> frameIndices{};
    for (std::uint32_t frameIndex = 0U; frameIndex < FrameCount; ++frameIndex)
    {
        frameIndices[frameIndex] = frameIndex;
    }
    std::sort(
        frameIndices.begin(),
        frameIndices.end(),
        [&](std::uint32_t lhs, std::uint32_t rhs) {
            if (!_pending[lhs])
            {
                return false;
            }
            if (!_pending[rhs])
            {
                return true;
            }
            return _generations[lhs] < _generations[rhs];
        });
    for (std::uint32_t frameIndex : frameIndices)
    {
        if (!ConsumeCompleted(frameIndex, errorMessage))
        {
            return false;
        }
    }
    return true;
}

void D3D12CbtDiagnostics::BeginGpuStage(
    ID3D12GraphicsCommandList* commandList,
    std::uint32_t frameIndex,
    TerrainLodCbtGpuStage stage)
{
    // D3D12 timestamp 使用 EndQuery 写入瞬时值，并不存在 BeginQuery 配对状态
    const std::size_t stageIndex = static_cast<std::size_t>(stage);
    if (commandList == nullptr || _timestampQueryHeap == nullptr ||
        frameIndex >= FrameCount || stageIndex >= ProfiledStageCount)
    {
        return;
    }
    const UINT query = static_cast<UINT>(
        frameIndex * TimestampCountPerFrame + stageIndex * 2U);
    commandList->EndQuery(_timestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, query);
}

void D3D12CbtDiagnostics::EndGpuStage(
    ID3D12GraphicsCommandList* commandList,
    std::uint32_t frameIndex,
    TerrainLodCbtGpuStage stage)
{
    // 起止索引在同一 frame 范围内连续，消费端可按 stage * 2 解码
    const std::size_t stageIndex = static_cast<std::size_t>(stage);
    if (commandList == nullptr || _timestampQueryHeap == nullptr ||
        frameIndex >= FrameCount || stageIndex >= ProfiledStageCount)
    {
        return;
    }
    const UINT query = static_cast<UINT>(
        frameIndex * TimestampCountPerFrame + stageIndex * 2U + 1U);
    commandList->EndQuery(_timestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, query);
}

void D3D12CbtDiagnostics::ResolveGpuTimings(
    ID3D12GraphicsCommandList* commandList,
    std::uint32_t frameIndex)
{
    // Resolve 命令跟随所有被测 pass，确保 readback 中每对值来自同一事务
    // frame 槽再次进入 CPU 前会先等待自己的 fence
    if (commandList == nullptr || _timestampQueryHeap == nullptr ||
        frameIndex >= FrameCount || _timestampReadbacks[frameIndex] == nullptr)
    {
        return;
    }
    const UINT queryStart = static_cast<UINT>(frameIndex * TimestampCountPerFrame);
    commandList->ResolveQueryData(
        _timestampQueryHeap.Get(),
        D3D12_QUERY_TYPE_TIMESTAMP,
        queryStart,
        static_cast<UINT>(TimestampCountPerFrame),
        _timestampReadbacks[frameIndex].Get(),
        0U);
}

bool D3D12CbtDiagnostics::LatchFault(const std::string& message, std::string* errorMessage)
{
    // 持久 GPU 拓扑无法局部回滚，因此只保留首错并交给算法层整体恢复。
    if (!_faulted)
    {
        _faultMessage = message;
        _faulted = true;
    }
    SetError(errorMessage, _faultMessage);
    return false;
}

const D3D12CbtDiagnosticSnapshot& D3D12CbtDiagnostics::Snapshot() const
{
    return _snapshot;
}

bool D3D12CbtDiagnostics::IsFaulted() const
{
    return _faulted;
}

const std::string& D3D12CbtDiagnostics::FaultMessage() const
{
    return _faultMessage;
}
} // namespace ParallelRoam::Algorithms::Cbt2024::D3D12
