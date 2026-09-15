#pragma once

#include "algorithms/ITerrainLodAlgorithm.h"

#include <array>
#include <cstddef>
#include <string_view>

namespace ParallelRoam::Algorithms::Cbt2024::OfficialBaselineV1
{
// 后续研究变体使用独立算法键和 benchmark 标签，不覆盖冻结来源身份
inline constexpr std::string_view BaselineId{"cbt-2024-official-baseline-v1"};
inline constexpr std::string_view AlgorithmKey{"cbt_2024_official_baseline_v1"};
inline constexpr std::string_view DisplayName{"CBT 2024"};
inline constexpr std::string_view BenchmarkTag{"benchmark/cbt-2024-official-baseline-v1"};

// 官方远端提交与本机 NVIDIA 兼容补丁分开记录，避免把兼容提交误认为上游发布版本。
inline constexpr std::string_view OfficialUpstreamCommit{
    "7351e6fb380acc149b3aef22a6c39bf3df7950a6"};
inline constexpr std::string_view LocalCompatibilityCommit{
    "7ae736d179528a0996449c0cc2db7f3279edc8ee"};

inline constexpr int DrawableWidth = 1280;
inline constexpr int DrawableHeight = 720;

/// <summary>
/// 来源程序的冻结路径预设，仅记录来源身份，不替代当前 EIP 配置
/// </summary>
struct RuntimePathPreset
{
    // 资源索引与解码尺寸共同防止同名高度图被替换后仍沿用旧实验标签。
    int HeightMapIndex{0};
    int ExpectedHeightMapWidth{0};
    int ExpectedHeightMapHeight{0};
    // 地形尺度、深度和 CPU 阈值保证三算法正式报告仍使用同一场景输入。
    float TerrainSize{0.0F};
    float HeightScale{0.0F};
    int MaxDepth{0};
    float RoamSplitPixels{0.0F};
    float RoamMergePixels{0.0F};
    int RoamTriangleBudget{0};
    // CBT 面积与容量属于独立参数，不能由 CPU ROAM 的预算字段隐式换算。
    TerrainLodCbtCapacity CbtCapacity{TerrainLodCbtCapacity::Capacity128K};
    float CbtTriangleAreaPixels{0.0F};
    // 采样点和预热帧固定后，算法速度只影响测试墙钟时间，不改变相机姿态序列。
    std::size_t SampleCount{0U};
    std::size_t WarmupFrameCount{0U};
};

// 文件名中的 Peking_513 是资源名称；解码后的真实像素尺寸为 547x547。
inline constexpr RuntimePathPreset DefaultPath{
    0,
    129,
    129,
    30.0F,
    4.0F,
    20,
    4.0F,
    2.0F,
    20000,
    TerrainLodCbtCapacity::Capacity128K,
    58.0F,
    600U,
    16U,
};

inline constexpr RuntimePathPreset ExtremePath{
    1,
    547,
    547,
    80.0F,
    12.0F,
    20,
    0.25F,
    0.10F,
    200000,
    TerrainLodCbtCapacity::Capacity1M,
    2.05F,
    64U,
    24U,
};

// 极限路径是固定的一圈相机轨道；常量变化必须建立新的基线版本。
inline constexpr float ExtremeOrbitRadius = 58.0F;
inline constexpr float ExtremeOrbitHeight = 20.0F;
inline constexpr float ExtremeOrbitHeightAmplitude = 3.0F;
inline constexpr float ExtremeTargetRadius = 10.0F;
inline constexpr float ExtremeTargetPhase = 0.55F;
inline constexpr float ExtremeTargetHeight = 4.0F;

// 顺序与 CMake 的四容量机械特化列表一致，数量或名称变化必须升级 BaselineId。
inline constexpr std::array<std::string_view, 18> TopologyShaderEntryPoints{
    "CSResetE0",
    "CSClassify",
    "CSPrepareClassificationIndirect",
    "CSSplitE2",
    "CSPrepareAllocationIndirect",
    "CSAllocateE2",
    "CSBisectE3",
    "CSPreparePropagationIndirectE3",
    "CSPropagateBisectE3",
    "CSPrepareSimplifyF",
    "CSPrepareSimplifyIndirectF",
    "CSSimplifyF",
    "CSPrepareSimplifyPropagationIndirectF",
    "CSPropagateSimplifyF",
    "CSReducePre",
    "CSReduceFirst",
    "CSReduceSecond",
    "CSValidateF",
};

// 容量矩阵使用上游提供的四个 OCBT 静态布局，不插入非官方档位。
inline constexpr std::array<TerrainLodCbtCapacity, 4> CapacityMatrix{
    TerrainLodCbtCapacity::Capacity128K,
    TerrainLodCbtCapacity::Capacity256K,
    TerrainLodCbtCapacity::Capacity512K,
    TerrainLodCbtCapacity::Capacity1M,
};

static_assert(DefaultPath.CbtTriangleAreaPixels > ExtremePath.CbtTriangleAreaPixels);
static_assert(DefaultPath.SampleCount > ExtremePath.SampleCount);
static_assert(TopologyShaderEntryPoints.size() == 18U);
} // namespace ParallelRoam::Algorithms::Cbt2024::OfficialBaselineV1
