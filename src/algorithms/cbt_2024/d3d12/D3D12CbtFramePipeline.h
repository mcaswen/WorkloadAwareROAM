#pragma once

#include "algorithms/ITerrainLodAlgorithm.h"
#include "algorithms/cbt_2024/CbtBisectorTopology.h"
#include "algorithms/cbt_2024/d3d12/D3D12CbtDiagnostics.h"
#include "algorithms/cbt_2024/d3d12/D3D12CbtGeometryPipeline.h"
#include "algorithms/cbt_2024/d3d12/D3D12CbtGpuState.h"
#include "render/D3D12GraphicsBackend.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace ParallelRoam::Algorithms::Cbt2024::D3D12
{
/// <summary>
/// CBT 正式 GPU 事务：分类、split/merge、传播、Reduce、Indexation 和高度图增量几何
///
/// 生命周期约束：
/// - Topology 由 D3D12CbtTerrainState 持有，并且必须比本对象更晚销毁；
/// - Initialize 只创建管线、描述符和几何输出，不提交独立 command queue；
/// - RecordFrame 只能在 backend BeginFrame/Present 区间调用；
/// - RenderVertices 返回借用资源，renderer 不取得 COM 所有权；
/// - Shutdown 归还连续描述符并清除全部状态镜像。
///
/// 帧内顺序：
/// - Reset 清空 transient task、validation、draw 和 dispatch 状态；
/// - Classify 消费上一帧活动列表并生成 split/simplify 候选；
/// - PrepareClassificationIndirect 生成后续拓扑 pass 的间接工作量；
/// - PrepareSimplify、Simplify 和传播在同一邻接代次回收 sibling 槽位；
/// - 三段生产 Reduce 从 split/merge 后的 occupancy bitfield 重建 OCBT 计数；
/// - Indexation 在 GPU 上派生活动、可见和修改列表；
/// - PrepareIndirect 生成 draw/dispatch 参数；
/// - 可选 base geometry pass 生成与仓库渲染器兼容的 52-byte 顶点；
/// - 最终 transition 发布 SRV 与 INDIRECT_ARGUMENT 资源。
///
/// CPU 从不读回 live draw count。TopologyFrameGeneration 只是调度诊断，
/// 不参与 ExecuteIndirect 参数计算，也不能替代 GPU draw state。
/// </summary>
class D3D12CbtFramePipeline
{
public:
    D3D12CbtFramePipeline() = default;
    ~D3D12CbtFramePipeline();

    D3D12CbtFramePipeline(const D3D12CbtFramePipeline&) = delete;
    D3D12CbtFramePipeline& operator=(const D3D12CbtFramePipeline&) = delete;

    [[nodiscard]] bool Initialize(
        Render::D3D12GraphicsBackend& backend,
        const CbtBaseTopology& topology,
        const D3D12CbtGpuResourceView& resources,
        const Terrain::HeightMap& heightMap,
        std::string* errorMessage);
    void Shutdown();

    [[nodiscard]] bool RecordFrame(
        const TerrainLodBuildInput& input,
        const CbtBaseTopology& topology,
        const D3D12CbtGpuResourceView& resources,
        bool rebuildGeometry,
        std::string* errorMessage);

    [[nodiscard]] ID3D12Resource* RenderVertices() const;
    [[nodiscard]] ID3D12Resource* ClassificationPositions() const;
    [[nodiscard]] std::size_t RenderVertexCapacityBytes() const;
    [[nodiscard]] std::size_t ClassificationPositionCapacityBytes() const;
    [[nodiscard]] std::uint64_t TopologyFrameGeneration() const;
    [[nodiscard]] std::uint32_t LastSplitCandidateCount() const;
    [[nodiscard]] std::uint32_t LastSimplifyCandidateCount() const;
    [[nodiscard]] std::uint32_t LastPlannedSplitNodeCount() const;
    [[nodiscard]] std::uint32_t LastAllocatedSplitSlotCount() const;
    [[nodiscard]] std::uint32_t LastRemainingDynamicSlotCount() const;
    [[nodiscard]] std::uint32_t LastDuplicateSplitClaimCount() const;
    [[nodiscard]] std::uint32_t LastSharedCompatibilityCount() const;
    [[nodiscard]] std::uint32_t LastCompatibilityStepCount() const;
    [[nodiscard]] std::uint32_t LastMaximumCompatibilityLength() const;
    [[nodiscard]] std::uint32_t LastCommittedDynamicSlotCount() const;
    [[nodiscard]] std::uint32_t LastSplitPropagationCount() const;
    [[nodiscard]] const std::array<std::uint32_t, 4>& LastBisectTemplateCounts() const;
    // Number of pair or facing-pair groups accepted after split commit.
    [[nodiscard]] std::uint32_t LastPreparedSimplificationCount() const;
    // Number of dynamic sibling slots cleared and returned to the free hierarchy.
    [[nodiscard]] std::uint32_t LastReleasedDynamicSlotCount() const;
    // Number of external neighbor references repaired after sibling deletion.
    [[nodiscard]] std::uint32_t LastSimplifyPropagationCount() const;
    // Accepted two-node groups; each group releases one dynamic slot.
    [[nodiscard]] std::uint32_t LastPairMergeCount() const;
    // Accepted four-node groups; each group releases two dynamic slots.
    [[nodiscard]] std::uint32_t LastQuadMergeCount() const;
    [[nodiscard]] std::uint32_t LastActiveDynamicSlotCount() const;
    [[nodiscard]] std::uint32_t LastIndexedActiveCount() const;
    [[nodiscard]] std::uint32_t LastMaximumActiveDepth() const;
    [[nodiscard]] std::uint64_t ClassificationSampleGeneration() const;
    [[nodiscard]] std::uint64_t GpuTimingSampleGeneration() const;
    [[nodiscard]] const std::array<float, TerrainLodCbtGpuStageCount>& LastGpuStageMilliseconds() const;
    [[nodiscard]] float LastGpuStageSumMilliseconds() const;
    [[nodiscard]] float LastBlockingValidationWaitMilliseconds() const;
    [[nodiscard]] bool IsFaulted() const;
    [[nodiscard]] const std::string& FaultMessage() const;
    [[nodiscard]] bool IsInitialized() const;

private:
    // 每档容量各有一组编译期定长的 OCBT shader。
    // Reset 与三段 Reduce 共享同一生产 root signature。
    struct CapacityPipelines
    {
        Microsoft::WRL::ComPtr<ID3D12PipelineState> Reset; // 清零瞬态计数但保留上一帧 active count。
        Microsoft::WRL::ComPtr<ID3D12PipelineState> Classify; // 按该档 OCBT 宏容量编译的面积分类。
        Microsoft::WRL::ComPtr<ID3D12PipelineState> PrepareClassificationIndirect; // 发布两组候选调度。
        Microsoft::WRL::ComPtr<ID3D12PipelineState> Split; // 规划兼容链并建立 allocation list。
        Microsoft::WRL::ComPtr<ID3D12PipelineState> PrepareAllocationIndirect; // 发布 allocation 调度。
        Microsoft::WRL::ComPtr<ID3D12PipelineState> Allocate; // 从旧 OCBT 补集分配唯一槽位。
        Microsoft::WRL::ComPtr<ID3D12PipelineState> Bisect; // 提交四模板、heapID 和 OCBT 位。
        Microsoft::WRL::ComPtr<ID3D12PipelineState> PreparePropagationIndirect; // 发布传播调度。
        Microsoft::WRL::ComPtr<ID3D12PipelineState> PropagateBisect; // 修复外部邻居引用。
        Microsoft::WRL::ComPtr<ID3D12PipelineState> PrepareSimplify; // 筛选两/四节点合法 merge。
        Microsoft::WRL::ComPtr<ID3D12PipelineState> PrepareSimplifyIndirect; // 发布合法 merge 调度。
        Microsoft::WRL::ComPtr<ID3D12PipelineState> Simplify; // 上移保留 heapID 并释放 sibling。
        Microsoft::WRL::ComPtr<ID3D12PipelineState> PrepareSimplifyPropagationIndirect; // 发布 merge 传播调度。
        Microsoft::WRL::ComPtr<ID3D12PipelineState> PropagateSimplify; // 修复被删除 sibling 的外部引用。
        Microsoft::WRL::ComPtr<ID3D12PipelineState> ReducePre; // 将最后一层位域归约成小树根。
        Microsoft::WRL::ComPtr<ID3D12PipelineState> ReduceFirst; // 每组归约一个固定大小子树。
        Microsoft::WRL::ComPtr<ID3D12PipelineState> ReduceSecond; // 汇总子树根并发布全局占用数。
        Microsoft::WRL::ComPtr<ID3D12PipelineState> Validate; // 检查身份关系、回收计数和双向邻接。
    };

    [[nodiscard]] bool CreateTopologyRootSignature(std::string* errorMessage);
    [[nodiscard]] bool CreateBootstrapRootSignature(std::string* errorMessage);
    [[nodiscard]] bool CreateDispatchCommandSignature(std::string* errorMessage);
    [[nodiscard]] bool CreatePipelines(std::string* errorMessage);
    [[nodiscard]] bool CreateConstantBuffers(std::string* errorMessage);
    [[nodiscard]] bool ConfigureTopologyDescriptors(
        const CbtBaseTopology& topology,
        const D3D12CbtGpuResourceView& resources,
        std::string* errorMessage);

    // 管线对象和连续描述符只在 Initialize/Shutdown 边界变化。
    Render::D3D12GraphicsBackend* _backend{nullptr}; // 借用 renderer 后端，不拥有其队列或 device。
    Microsoft::WRL::ComPtr<ID3D12RootSignature> _topologyRootSignature; // 官方 b0..b2/t0..t1/u0..u16 ABI。
    Microsoft::WRL::ComPtr<ID3D12RootSignature> _bootstrapRootSignature; // 仓库适配层的 root descriptor ABI。
    Microsoft::WRL::ComPtr<ID3D12CommandSignature> _dispatchCommandSignature; // 一组 12-byte Dispatch 参数。
    CapacityPipelines _pipelines; // 仅创建当前 OCBT 容量对应的一组特化 PSO。
    Microsoft::WRL::ComPtr<ID3D12PipelineState> _indexationPipeline; // 从 OCBT 重建紧密活动索引。
    Microsoft::WRL::ComPtr<ID3D12PipelineState> _prepareIndirectPipeline; // 重建 draw 和几何 dispatch 参数。
    D3D12CbtGeometryPipeline _geometry; // 参数几何、最终顶点及其独立状态机。
    D3D12CbtDiagnostics _diagnostics; // 延迟回读、CPU 参考对照和故障锁存。
    Render::D3D12DescriptorAllocation _topologySrvRange; // 连续 t0..t1 描述符。
    Render::D3D12DescriptorAllocation _topologyUavRange; // 连续 u0..u16 描述符。

    // 帧常量按 swap-chain frame 隔离并在整个管线生命周期中保持映射。
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, Render::D3D12GraphicsBackend::FrameCount> _constantBuffers; // 三个 256-byte CBV 槽。
    std::array<std::uint8_t*, Render::D3D12GraphicsBackend::FrameCount> _mappedConstants{}; // 持久映射的逐帧写指针。

    // 下列状态镜像覆盖所有会在 compute、vertex 和 indirect 角色间切换的资源。
    // BaseTopology 首次上传后的公开状态统一为 UAV，所以初值与它的契约一致。
    // 每个 transition helper 同步更新镜像，避免发出 before-state 错误的 barrier。
    D3D12_RESOURCE_STATES _heapIdState{D3D12_RESOURCE_STATE_UNORDERED_ACCESS}; // Classify 写路径和 Bootstrap 读路径切换。
    D3D12_RESOURCE_STATES _bisectorDataState{D3D12_RESOURCE_STATE_UNORDERED_ACCESS}; // 分类状态生产与 Indexation 消费。
    D3D12_RESOURCE_STATES _baseControlPointState{D3D12_RESOURCE_STATE_UNORDERED_ACCESS}; // 初始化上传后只读。
    // 两份邻接资源始终成对切换：已发布代次作为 COPY_SOURCE，写代次作为 COPY_DEST。
    // CopyResource 完成后二者恢复 UAV，Bisect/Propagate 只修改写代次。
    std::array<D3D12_RESOURCE_STATES, 2> _neighborStates{
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS}; // ping/pong 在原生复制与拓扑 UAV 之间切换。
    D3D12_RESOURCE_STATES _activeIndexState{D3D12_RESOURCE_STATE_UNORDERED_ACCESS}; // 上帧 SRV、本帧尾 UAV。
    D3D12_RESOURCE_STATES _visibleIndexState{D3D12_RESOURCE_STATE_UNORDERED_ACCESS}; // Indexation 的可见输出。
    D3D12_RESOURCE_STATES _modifiedIndexState{D3D12_RESOURCE_STATE_UNORDERED_ACCESS}; // 后续几何更新任务输出。
    D3D12_RESOURCE_STATES _classificationState{D3D12_RESOURCE_STATE_UNORDERED_ACCESS}; // UAV 候选与 COPY_SOURCE 回读。
    D3D12_RESOURCE_STATES _allocationState{D3D12_RESOURCE_STATE_UNORDERED_ACCESS}; // UAV 任务表与 COPY_SOURCE 诊断。
    D3D12_RESOURCE_STATES _memoryState{D3D12_RESOURCE_STATE_UNORDERED_ACCESS}; // 预留/分配计数与 COPY_SOURCE 诊断。
    D3D12_RESOURCE_STATES _validationState{D3D12_RESOURCE_STATE_UNORDERED_ACCESS}; // shader 错误头与验证回读。
    D3D12_RESOURCE_STATES _occupancyTreeState{D3D12_RESOURCE_STATE_UNORDERED_ACCESS}; // Reduce UAV 与根计数回读。
    D3D12_RESOURCE_STATES _topologyDispatchState{D3D12_RESOURCE_STATE_UNORDERED_ACCESS}; // UAV 写与 indirect 消费。
    D3D12_RESOURCE_STATES _drawState{D3D12_RESOURCE_STATE_UNORDERED_ACCESS}; // GPU 写与 renderer draw indirect。
    D3D12_RESOURCE_STATES _geometryDispatchState{D3D12_RESOURCE_STATE_UNORDERED_ACCESS}; // 上帧 Classify indirect 参数。

    // generation 只统计成功记录的完整帧事务
    // 它用于测试和诊断，不决定 GPU draw 数量或资源生命周期。
    CbtOccupancyCapacity _capacity{CbtOccupancyCapacity::Capacity128K}; // 当前选中的特化档位。
    std::uint64_t _topologyFrameGeneration{0U}; // 成功记录的最新 GPU 事务代次。
    float _lastBlockingValidationWaitMilliseconds{0.0F}; // 仅 BlockingSmoke 非零。
    std::uint32_t _neighborReadIndex{0U}; // 当前已发布的 ping/pong 邻接代次。
    bool _initialized{false}; // 所有 PSO、描述符和缓冲均可用后置位。
};
} // namespace ParallelRoam::Algorithms::Cbt2024::D3D12
