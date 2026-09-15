#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ParallelRoam::Algorithms
{
/// <summary>
/// CBT 动态槽池的编译期容量；基础二分器另外占用固定槽位
/// </summary>
enum class TerrainLodCbtCapacity : std::uint32_t
{
    Capacity128K = 131072U,
    Capacity256K = 262144U,
    Capacity512K = 524288U,
    Capacity1M = 1048576U,
};

/// <summary>
/// 完整拓扑验证的执行方式，阻塞模式只用于定向诊断
/// </summary>
enum class TerrainLodCbtValidationMode : std::uint8_t
{
    Off,
    Delayed,
    BlockingSmoke,
};

/// <summary>
/// 正常更新只重算修改槽，全量模式用于核对增量几何
/// </summary>
enum class TerrainLodCbtGeometryMode : std::uint8_t
{
    ModifiedOnly,
    FullDebug,
};

/// <summary>
/// GPU 时间戳的互斥阶段，顺序与迁入管线一致
/// 绘制阶段由渲染消费者单独测量
/// </summary>
enum class TerrainLodCbtGpuStage : std::uint8_t
{
    ClassificationGeometry,
    Reset,
    Classify,
    Split,
    Allocate,
    NeighborCopy,
    Bisect,
    PropagateBisect,
    PrepareSimplify,
    Simplify,
    PropagateSimplify,
    ReducePre,
    ReduceFirst,
    ReduceSecond,
    Indexation,
    RenderGeometry,
    Validation,
    TerrainRender,
    Count,
};

inline constexpr std::size_t TerrainLodCbtGpuStageCount =
    static_cast<std::size_t>(TerrainLodCbtGpuStage::Count);

/// <summary>
/// CBT 独立设置，面积和容量不参与 CPU ROAM 的误差及硬预算解释
/// </summary>
struct TerrainLodCbtSettings
{
    float TriangleAreaPixels{50.0F};
    TerrainLodCbtCapacity Capacity{TerrainLodCbtCapacity::Capacity128K};
    TerrainLodCbtValidationMode ValidationMode{TerrainLodCbtValidationMode::Off};
    TerrainLodCbtGeometryMode GeometryMode{TerrainLodCbtGeometryMode::ModifiedOnly};

    bool operator==(const TerrainLodCbtSettings&) const = default;
};

/// <summary>
/// CBT 当前提交身份及已完成的延迟样本，零代次表示尚无对应样本
/// 数量和 GPU 时间按样本代次解释，不能归到当前 CPU 帧
/// </summary>
struct TerrainLodCbtStats
{
    std::uint64_t TopologyGeneration{0U};
    std::uint64_t ClassificationSampleGeneration{0U};
    std::uint64_t GpuTimingSampleGeneration{0U};
    std::uint64_t TerrainRenderSampleGeneration{0U};
    std::uint64_t DiagnosticSampleAge{0U};
    bool DiagnosticSampleDropped{false};
    std::uint64_t ResourceGeneration{0U};
    std::uint32_t CapacitySetting{0U};
    float TriangleAreaPixelsSetting{0.0F};
    TerrainLodCbtValidationMode ValidationModeSetting{TerrainLodCbtValidationMode::Off};
    TerrainLodCbtGeometryMode GeometryModeSetting{TerrainLodCbtGeometryMode::ModifiedOnly};
    std::size_t ActiveDynamicSlotCount{0U};
    std::size_t RemainingDynamicSlotCount{0U};
    std::array<float, TerrainLodCbtGpuStageCount> GpuStageMilliseconds{};
    float GpuStageSumMilliseconds{0.0F};
    float BlockingValidationWaitMilliseconds{0.0F};
    std::size_t CommittedDynamicSlotCount{0U};
    std::size_t SplitPropagationCount{0U};
    std::array<std::size_t, 4> BisectTemplateCounts{};
    std::size_t PreparedSimplificationCount{0U};
    std::size_t ReleasedDynamicSlotCount{0U};
    std::size_t SimplifyPropagationCount{0U};
    std::size_t PairMergeCount{0U};
    std::size_t QuadMergeCount{0U};
    // 故障恢复会替换持续状态，实验必须能够识别这种运行
    std::uint64_t FaultRecoveryCount{0U};
};
} // 命名空间 ParallelRoam::Algorithms
