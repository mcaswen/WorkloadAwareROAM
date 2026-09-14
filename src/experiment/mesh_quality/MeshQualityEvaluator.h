#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace ParallelRoam::Terrain
{
struct TerrainMeshData;
}

namespace ParallelRoam::Experiment::MeshQuality
{
/// <summary>
/// 借用原始解码的行优先灰度样本，以 uint16 / 65535 的双精度值定义双线性主参考
/// 样本在同步评价期间保持有效且不变；世界尺度独立于被测网格
/// </summary>
struct BilinearHeightfieldReference
{
    std::span<const std::uint16_t> Samples;
    int Width{0};
    int Height{0};
    double TerrainSize{1.0};
    double HeightScale{1.0};
};

/// <summary>
/// 区分有限采样结果、输入错误与提前终止；Sampled 不代表连续曲面的最大误差保证
/// </summary>
enum class EvaluationStatus
{
    Sampled,
    InvalidGeometry,
    InvalidView,
    Incomplete,
    ResourceLimit,
};

/// <summary>
/// 由调用方冻结的右手投影、深度约定和像素尺寸，默认 NO，不携带调度分数
/// </summary>
struct QualityView
{
    glm::mat4 ViewProjection{1.0F};
    std::uint32_t Width{1U};
    std::uint32_t Height{1U};
    bool UsesZeroToOneDepth{false};
};

/// <summary>
/// 限制单次离线查询的样本数和秒数，超限时保留部分结果而不声称完整覆盖
/// SamplingLevel 指定全域四分次数，当前只支持 0、1、2 层
/// </summary>
struct QualityOptions
{
    std::size_t MaximumSamples{40000000U};
    double MaximumSeconds{600.0};
    unsigned SamplingLevel{0U};
    // 可选逐点输出；NaN 保留异常，-1 仅表示参考不可见，调用者同步持有
    std::vector<double>* PointErrors{nullptr};
};

/// <summary>
/// 最大值对应的参考域点和两侧位置，供校准残差定位；序号仅在同一参考采样身份内有效
/// </summary>
struct QualityLocation
{
    std::size_t SampleOrdinal{0U};
    glm::dvec2 Uv{0.0};
    glm::dvec3 ReferencePosition{0.0};
    glm::dvec3 MeasuredPosition{0.0};
};

/// <summary>
/// 指定公共采样层的 observed 最大值及异常证据，空 optional 表示没有可求值样本
/// 不完整结果仍保留有限值，调用方必须连同 Status 与异常计数解释
/// </summary>
struct QualityResult
{
    EvaluationStatus Status{EvaluationStatus::Incomplete};
    unsigned SamplingLevel{0U};
    std::size_t SampleCount{0U};
    std::size_t ScreenSampleCount{0U};
    std::size_t MissingCoverageCount{0U};
    std::size_t AmbiguousCoverageCount{0U};
    std::size_t InvalidGeometryCount{0U};
    std::size_t InvalidProjectionCount{0U};
    std::size_t NearPlaneCrossingCount{0U};
    std::uint64_t SampleHash{14695981039346656037ULL};
    std::optional<double> SampledScreenMaxPx;
    std::optional<double> SampledHeightMax;
    // 可见参数域样本等权 RMS，不是屏幕面积权重；异常状态下只代表已求值部分
    std::optional<double> TerrainSampleScreenRms;
    double ScreenSquaredSum{0.0};
    std::size_t EvaluatedScreenCount{0U};
    QualityLocation ScreenMaximum;
    QualityLocation HeightMaximum;
    double IndexMilliseconds{0.0};
    double SampleMilliseconds{0.0};
    double TotalMilliseconds{0.0};
};

/// <summary>
/// 同步评价参考三角形的顶点、边中点与重心，样本及其归属只由参考确定
/// 两个网格在调用期间保持不变；输出指定层最大值和样本 RMS，不自动判定收敛
/// </summary>
[[nodiscard]] QualityResult EvaluateMeshQuality(const Terrain::TerrainMeshData& reference,
    const Terrain::TerrainMeshData& measured, const QualityView& view, const QualityOptions& options = {});

/// <summary>
/// 采样模板仅提供 UV 和索引，参考位置与视锥域由原始双线性高度场独立确定
/// 输入在同步调用期间保持不变；可选逐点输出用于离线配对，不自动裁决退化是否可接受
/// </summary>
[[nodiscard]] QualityResult EvaluateMeshQuality(const BilinearHeightfieldReference& reference,
    const Terrain::TerrainMeshData& samplingDomain, const Terrain::TerrainMeshData& measured,
    const QualityView& view, const QualityOptions& options = {});

[[nodiscard]] const char* ToString(EvaluationStatus status);
}
