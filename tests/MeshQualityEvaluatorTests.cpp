#include "experiment/mesh_quality/MeshQualityEvaluator.h"
#include "experiment/formal/FormalExperimentCamera.h"
#include "experiment/formal/FormalExperimentManifest.h"
#include "algorithms/classic_roam/ClassicRoamMeshBuilder.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamPipeline.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"
#include "terrain/TerrainMeshBuilder.h"

#include <stb_image.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using namespace ParallelRoam;
using namespace Algorithms;
using namespace Experiment::MeshQuality;
using namespace Experiment::Formal;
using Mesh = Terrain::TerrainMeshData;
using Clock = std::chrono::steady_clock;

void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void Near(double actual, double expected, const char* message)
{
    Require(std::isfinite(actual) && std::abs(actual - expected) <= 1.0e-8, message);
}

double Milliseconds(Clock::time_point begin)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
}

std::ofstream Output(const std::filesystem::path& path)
{
    std::ofstream output(path);
    output.exceptions(std::ios::badbit | std::ios::failbit);
    output << std::setprecision(17);
    return output;
}

Mesh Quad(const std::array<float, 4>& heights = {}, bool otherDiagonal = false)
{
    Mesh mesh;
    const std::array<glm::vec2, 4> uvs{{{0, 0}, {1, 0}, {0, 1}, {1, 1}}};
    for (std::size_t i = 0; i < uvs.size(); ++i)
    {
        Terrain::TerrainMeshVertex vertex;
        vertex.TexCoord = uvs[i];
        vertex.Position = {uvs[i].x - 0.5F, heights[i], uvs[i].y - 0.5F};
        mesh.Vertices.push_back(vertex);
    }
    mesh.Indices = otherDiagonal ? std::vector<std::uint32_t>{0, 2, 3, 0, 3, 1} :
        std::vector<std::uint32_t>{0, 2, 1, 1, 2, 3};
    return mesh;
}

/// <summary>
/// 与资产加载使用同一灰度解码器，但保留原始整数样本，不从生产 float 高度反推参考
/// 返回值拥有样本，stb 缓冲在复制后释放；尺寸必须吻合已冻结的采样域
/// </summary>
std::vector<std::uint16_t> LoadReferenceSamples(const std::filesystem::path& path, int width, int height)
{
    int decodedWidth = 0, decodedHeight = 0, channels = 0;
    std::unique_ptr<stbi_us, decltype(&stbi_image_free)> pixels(
        stbi_load_16(path.string().c_str(), &decodedWidth, &decodedHeight, &channels, 1), &stbi_image_free);
    Require(pixels != nullptr && decodedWidth == width && decodedHeight == height,
        "raw reference decode failed or dimensions changed");
    return {pixels.get(), pixels.get() + static_cast<std::size_t>(width) * static_cast<std::size_t>(height)};
}

void CheckBilinearReference()
{
    const QualityView view{glm::mat4{1.0F}, 100U, 100U};
    const std::array<std::uint16_t, 4> saddle{0U, 0U, 0U, 65535U};
    const BilinearHeightfieldReference source{saddle, 2, 2, 1.0, 1.0};
    const auto domain = Quad();
    for (const bool diagonal : {false, true})
    {
        const auto result = EvaluateMeshQuality(source, domain, Quad({0, 0, 0, 1}, diagonal), view);
        Require(result.Status == EvaluationStatus::Sampled && result.SampleCount == 11U &&
            result.ScreenSampleCount == 11U, "bilinear source domain incomplete");
        // h=u*v 的两个三角近似都在 cell 中心偏离 1/4，端点与边界则相同
        Near(result.SampledHeightMax.value(), 0.25, "bilinear cell residual");
        Near(result.SampledScreenMaxPx.value(), 12.5, "bilinear cell screen displacement");
        Near(result.ScreenMaximum.Uv.x, 0.5, "bilinear maximum u");
        Near(result.ScreenMaximum.Uv.y, 0.5, "bilinear maximum v");
        Near(result.ScreenMaximum.ReferencePosition.y, 0.25, "reference became a triangle surface");

        auto unrelatedPositions = domain;
        for (auto& vertex : unrelatedPositions.Vertices)
            vertex.Position = glm::vec3{std::numeric_limits<float>::quiet_NaN()};
        const auto independent = EvaluateMeshQuality(source, unrelatedPositions, Quad({0, 0, 0, 1}, diagonal), view);
        Require(independent.Status == result.Status && independent.SampleHash == result.SampleHash &&
            independent.ScreenSampleCount == result.ScreenSampleCount, "template positions affected source domain");
        Near(independent.SampledScreenMaxPx.value(), result.SampledScreenMaxPx.value(), "template affected source error");
    }

    // 非方形样本阵列中的斜平面跨 cell 连续，u/v=1 的样本也必须命中最后格点
    const std::array<std::uint16_t, 6> ramp{0U, 65535U, 0U, 65535U, 0U, 65535U};
    const auto planar = EvaluateMeshQuality({ramp, 2, 3, 1.0, 1.0}, domain, Quad({0, 1, 0, 1}), view);
    Require(planar.Status == EvaluationStatus::Sampled, "multi-cell planar reference rejected");
    Near(planar.SampledHeightMax.value(), 0.0, "bilinear boundary or row stride changed");

    auto invalid = source;
    invalid.Width = 1;
    Require(EvaluateMeshQuality(invalid, domain, domain, view).Status == EvaluationStatus::InvalidGeometry,
        "invalid source dimensions accepted");
    invalid = source;
    invalid.Samples = std::span<const std::uint16_t>(saddle).first(3U);
    Require(EvaluateMeshQuality(invalid, domain, domain, view).Status == EvaluationStatus::InvalidGeometry,
        "incomplete source samples accepted");
    invalid = source;
    invalid.HeightScale = std::numeric_limits<double>::infinity();
    Require(EvaluateMeshQuality(invalid, domain, domain, view).Status == EvaluationStatus::InvalidGeometry,
        "non-finite source scale accepted");
    auto outside = domain;
    outside.Vertices[0].TexCoord.x = -0.01F;
    Require(EvaluateMeshQuality(source, outside, domain, view).Status == EvaluationStatus::InvalidGeometry,
        "out-of-domain template silently clamped");

    const auto path = std::filesystem::temp_directory_path() /
        ("roam-bilinear-" + std::to_string(Clock::now().time_since_epoch().count()) + ".pgm");
    {
        std::ofstream file(path, std::ios::binary);
        file << "P5\n2 2\n255\n";
        for (const int value : {0, 85, 170, 255}) file.put(static_cast<char>(value));
    }
    const auto decoded = LoadReferenceSamples(path, 2, 2);
    std::filesystem::remove(path);
    Require(decoded == std::vector<std::uint16_t>{0U, 21845U, 43690U, 65535U},
        "8-bit gray expansion or image orientation changed");
    Near(static_cast<double>(decoded[1]) / 65535.0, 1.0 / 3.0, "raw sample normalization changed");
}

void CheckAnalyticGeometry()
{
    const QualityView view{glm::mat4{1.0F}, 100U, 100U};
    const auto reference = Quad();
    const auto same = EvaluateMeshQuality(reference, reference, view);
    Require(same.Status == EvaluationStatus::Sampled && same.SampleCount == 11U &&
        same.ScreenSampleCount == 11U, "reference shared vertices/edge were not deduplicated");
    Near(same.SampledScreenMaxPx.value(), 0.0, "identity screen error");
    Near(same.SampledHeightMax.value(), 0.0, "identity height error");

    // 斜平面上的常量高度差给出解析像素位移，期望值不通过评价器反算
    const auto slope = Quad({0.0F, 0.125F, 0.25F, 0.375F});
    auto shifted = slope;
    for (auto& vertex : shifted.Vertices) vertex.Position.y += 0.25F;
    const auto displacement = EvaluateMeshQuality(slope, shifted, view);
    Require(displacement.Status == EvaluationStatus::Sampled, "valid tilted planes rejected");
    Near(displacement.SampledScreenMaxPx.value(), 12.5, "analytic screen displacement");
    Near(displacement.SampledHeightMax.value(), 0.25, "analytic height displacement");

    const auto diagonal = EvaluateMeshQuality(Quad({0, 0, 0, 1}), Quad({0, 0, 0, 1}, true), view);
    Near(diagonal.SampledScreenMaxPx.value(), 25.0, "diagonal mismatch not observed at shared midpoint");
    Near(diagonal.SampledHeightMax.value(), 0.5, "diagonal mismatch height");

    // 只重排被测网格，公共参考和样本身份不变；索引遍历顺序不能影响残差
    std::reverse(shifted.Vertices.begin(), shifted.Vertices.end());
    for (auto& index : shifted.Indices) index = 3U - index;
    std::rotate(shifted.Indices.begin(), shifted.Indices.begin() + 3, shifted.Indices.end());
    const auto reordered = EvaluateMeshQuality(slope, shifted, view);
    Require(reordered.Status == displacement.Status && reordered.SampleHash == displacement.SampleHash,
        "measured storage order changed sample identity");
    Near(reordered.SampledScreenMaxPx.value(), displacement.SampledScreenMaxPx.value(), "reordered error");

    const auto outside = EvaluateMeshQuality(reference, Quad({3, 3, 3, 3}), view);
    Require(outside.Status == EvaluationStatus::Sampled && outside.ScreenSampleCount == 11U,
        "measured points outside viewport were dropped");
    Near(outside.SampledScreenMaxPx.value(), 150.0, "viewport coordinates were clamped");
}

void CheckPlatformOutputs()
{
    std::vector<double> points;
    QualityOptions options;options.PointErrors=&points;
    QualityView view{glm::mat4{1.0F},100U,100U};
    const auto source=Quad();
    const auto result=EvaluateMeshQuality(source,Quad({.25F,.25F,.25F,.25F}),view,options);
    Require(points.size()==11 && result.EvaluatedScreenCount==11,"point output incomplete");
    Near(result.TerrainSampleScreenRms.value(),12.5,"analytic RMS");
    for (double point : points) Near(point,12.5,"pointwise error");
    // NO 转 ZO 后保持同一几何视锥，不能只改变可见判断
    view.ViewProjection[2][2]=.5F;view.ViewProjection[3][2]=.5F;view.UsesZeroToOneDepth=true;
    const auto zo=EvaluateMeshQuality(source,Quad({.25F,.25F,.25F,.25F}),view,options);
    Require(zo.ScreenSampleCount==result.ScreenSampleCount,"ZO visibility differs");
    Near(zo.SampledScreenMaxPx.value(),12.5,"ZO geometric error");
    const auto missing=EvaluateMeshQuality(source,{},view,options);
    Require(missing.Status==EvaluationStatus::Incomplete &&
        std::all_of(points.begin(),points.end(),[](double e){return std::isnan(e);}),"missing points disguised as invisible");
    view.ViewProjection[3].x=10;
    const auto invisible=EvaluateMeshQuality(source,source,view,options);
    Require(invisible.Status==EvaluationStatus::Sampled && !invisible.TerrainSampleScreenRms &&
        std::all_of(points.begin(),points.end(),[](double e){return e==-1;}),"invisible point semantics changed");
}

void CheckFailures()
{
    const QualityView view{glm::mat4{1.0F}, 100U, 100U};
    const auto reference = Quad();
    const auto missing = EvaluateMeshQuality(reference, {}, view);
    Require(missing.Status == EvaluationStatus::Incomplete && missing.MissingCoverageCount == 11U &&
        missing.ScreenSampleCount == 11U &&
        !missing.SampledScreenMaxPx && !missing.SampledHeightMax, "missing surface disguised as zero error");
    auto overlap = reference;
    overlap.Indices.insert(overlap.Indices.end(), {0U, 2U, 1U});
    const auto ambiguous = EvaluateMeshQuality(reference, overlap, view);
    Require(ambiguous.Status == EvaluationStatus::Incomplete && ambiguous.AmbiguousCoverageCount > 0U,
        "overlap hidden by first-hit query");
    auto invalid = reference;
    invalid.Indices[0] = 99U;
    Require(EvaluateMeshQuality(reference, invalid, view).Status == EvaluationStatus::InvalidGeometry,
        "invalid index accepted");
    invalid = reference;
    invalid.Vertices[0].Position.y = std::numeric_limits<float>::quiet_NaN();
    Require(EvaluateMeshQuality(reference, invalid, view).Status == EvaluationStatus::InvalidGeometry,
        "non-finite mesh accepted");
    invalid = reference;
    invalid.Indices[1] = invalid.Indices[0];
    Require(EvaluateMeshQuality(reference, invalid, view).Status == EvaluationStatus::InvalidGeometry,
        "degenerate triangle accepted");

    // 相机朝高度方向观察，使高度偏移跨越近裁剪面；该异常不能丢点后报告零误差
    glm::mat4 camera{1.0F};
    camera[1] = {0, 0, -1, 0};
    camera[2] = {0, 1, 0, 0};
    const auto crossing = EvaluateMeshQuality(reference, Quad({2, 2, 2, 2}), {camera, 100U, 100U});
    Require(crossing.Status == EvaluationStatus::Incomplete && crossing.NearPlaneCrossingCount == 11U &&
        !crossing.SampledScreenMaxPx, "near-plane crossing silently omitted");
    glm::mat4 emptyView{1.0F};
    emptyView[3].x = 10.0F;
    const auto empty = EvaluateMeshQuality(reference, reference, {emptyView, 100U, 100U});
    Require(empty.Status == EvaluationStatus::Sampled && !empty.SampledScreenMaxPx &&
        empty.SampledHeightMax.has_value(), "empty reference screen must retain independent height metric");
    Require(EvaluateMeshQuality(reference, reference, {glm::mat4{0.0F}, 100U, 100U}).Status ==
        EvaluationStatus::InvalidView, "singular view accepted");
    const auto capped = EvaluateMeshQuality(reference, reference, view, {3U, 600.0});
    Require(capped.Status == EvaluationStatus::ResourceLimit && capped.SampleCount == 3U,
        "sample cap lost partial-result status");
}

void CheckRefinedSampling()
{
    const QualityView view{glm::mat4{1.0F}, 100U, 100U};
    const auto domain = Quad();
    auto triangle = domain;
    triangle.Indices.resize(3U);
    const std::array<std::size_t, 3> triangleCounts{7U, 19U, 61U};
    const std::array<std::size_t, 3> quadCounts{11U, 33U, 113U};
    for (unsigned level = 0U; level <= 2U; ++level)
    {
        QualityOptions options;
        options.SamplingLevel = level;
        const auto single = EvaluateMeshQuality(triangle, domain, view, options);
        const auto joined = EvaluateMeshQuality(domain, domain, view, options);
        Require(single.Status == EvaluationStatus::Sampled && single.SampleCount == triangleCounts[level],
            "subdivision triangle sample set incorrect");
        Require(joined.Status == EvaluationStatus::Sampled && joined.SampleCount == quadCounts[level] &&
            joined.SamplingLevel == level, "shared refined edges were not deduplicated");
        Near(joined.SampledScreenMaxPx.value(), 0.0, "refined identity error");
        const auto saddle = EvaluateMeshQuality(Quad({0, 0, 0, 1}), Quad({0, 0, 0, 1}, true), view, options);
        Near(saddle.SampledScreenMaxPx.value(), 25.0, "refinement lost original maximum");
        if (level == 0U)
        {
            // 第零层身份按旧协议的固定顶点、共享边和单元序列独立累计
            const std::array<std::uint64_t, 22> ids{0, 0, 1, 2, 0, 2, 1, 4294967298ULL, 0, 1,
                1, 1, 2, 0, 1, 8589934595ULL, 0, 3, 1, 4294967299ULL, 2, 1};
            std::uint64_t expected = 14695981039346656037ULL;
            for (auto value : ids)
                for (unsigned byte = 0U; byte < 8U; ++byte)
                {
                    expected ^= value & 255U;
                    expected *= 1099511628211ULL;
                    value >>= 8U;
                }
            Require(joined.SampleHash == expected, "original sample identity changed");
        }
    }

    // 尖峰预先放在 (1/4,1/4)，其窄支撑避开原样本；加密应找到峰顶并在下一层保留
    Mesh peak;
    for (unsigned y = 0U; y <= 8U; ++y)
        for (unsigned x = 0U; x <= 8U; ++x)
        {
            Terrain::TerrainMeshVertex vertex;
            vertex.TexCoord = {x / 8.0F, y / 8.0F};
            vertex.Position = {vertex.TexCoord.x - 0.5F, x == 2U && y == 2U ? 1.0F : 0.0F,
                vertex.TexCoord.y - 0.5F};
            peak.Vertices.push_back(vertex);
        }
    for (unsigned y = 0U; y < 8U; ++y)
        for (unsigned x = 0U; x < 8U; ++x)
        {
            const auto i = y * 9U + x;
            peak.Indices.insert(peak.Indices.end(), {i, i + 9U, i + 1U, i + 1U, i + 9U, i + 10U});
        }
    const std::array<std::uint16_t, 4> flat{};
    const BilinearHeightfieldReference source{flat, 2, 2, 1.0, 1.0};
    const auto coarse = EvaluateMeshQuality(source, domain, peak, view);
    Require(coarse.SampledHeightMax.value() < 0.5, "spike fixture already sampled its peak at level zero");
    for (unsigned level : {1U, 2U})
    {
        const QualityOptions options{40000000U, 600.0, level};
        const auto refined = EvaluateMeshQuality(source, domain, peak, view, options);
        Require(refined.Status == EvaluationStatus::Sampled, "refined peak query incomplete");
        Near(refined.SampledHeightMax.value(), 1.0, "refinement missed fixed spike");
        Near(refined.SampledScreenMaxPx.value(), 50.0, "refined spike projection");
        Near(refined.HeightMaximum.Uv.x, 0.25, "spike maximum u");
        Near(refined.HeightMaximum.Uv.y, 0.25, "spike maximum v");
        auto reordered = peak;
        std::reverse(reordered.Vertices.begin(), reordered.Vertices.end());
        for (auto& index : reordered.Indices) index = static_cast<std::uint32_t>(peak.Vertices.size() - 1U) - index;
        const auto same = EvaluateMeshQuality(source, domain, reordered, view, options);
        Require(same.Status == refined.Status && same.SampleHash == refined.SampleHash,
            "measured storage affected refined sample identity");
        Near(same.SampledScreenMaxPx.value(), refined.SampledScreenMaxPx.value(), "refined storage invariance");
    }
    const auto missing = EvaluateMeshQuality(source, domain, {}, view, {40000000U, 600.0, 2U});
    Require(missing.Status == EvaluationStatus::Incomplete && missing.MissingCoverageCount == 113U &&
        missing.ScreenSampleCount == 113U, "refined missing coverage shrank the reference domain");
    const auto capped = EvaluateMeshQuality(source, domain, peak, view, {12U, 600.0, 2U});
    Require(capped.Status == EvaluationStatus::ResourceLimit && capped.SampleCount == 12U,
        "refined sample cap ignored");
    Require(EvaluateMeshQuality(source, domain, peak, view, {40000000U, 600.0, 3U}).Status ==
        EvaluationStatus::ResourceLimit, "unsupported refinement level accepted");
}

/// <summary>
/// 从实际 UV 二分面积读取叶深度，不用最大深度统计替代完整细化证据
/// </summary>
std::array<std::size_t, 21> DepthHistogram(const Mesh& mesh)
{
    std::array<std::size_t, 21> histogram{};
    Require(!mesh.Indices.empty() && mesh.Indices.size() % 3U == 0U, "invalid mesh for depth audit");
    for (std::size_t first = 0; first < mesh.Indices.size(); first += 3U)
    {
        for (std::size_t c = 0; c < 3U; ++c)
            Require(mesh.Indices[first + c] < mesh.Vertices.size(), "depth audit index out of range");
        const auto a = glm::dvec2(mesh.Vertices[mesh.Indices[first]].TexCoord);
        const auto e = glm::dvec2(mesh.Vertices[mesh.Indices[first + 1U]].TexCoord) - a;
        const auto f = glm::dvec2(mesh.Vertices[mesh.Indices[first + 2U]].TexCoord) - a;
        const double twiceArea = std::abs(e.x * f.y - e.y * f.x);
        int exponent = 0;
        const double fraction = std::frexp(twiceArea, &exponent);
        const int depth = 1 - exponent;
        Require(fraction == 0.5 && depth >= 0 && depth <= 20, "mesh area is not a supported dyadic leaf");
        ++histogram[static_cast<std::size_t>(depth)];
    }
    return histogram;
}

template <typename Settings>
Settings FinestSettings(int depth)
{
    Settings settings;
    settings.MaxDepth = depth;
    settings.TriangleBudget = std::size_t{1} << static_cast<unsigned>(depth + 1);
    settings.SplitThreshold = -1.0F;
    settings.MergeThreshold = -1.0F;
    settings.EnableLocalConstraints = true;
    settings.EnableTopologyValidation = true;
    settings.EnablePassEvidence = true;
    settings.PassPolicy = MakeTerrainLodSerialIncrementalPolicy();
    settings.PassPolicy.MergeScoreWorkerCount = settings.PassPolicy.SplitScoreWorkerCount = 1U;
    settings.PassPolicy.MergeTopologyWorkerCount = settings.PassPolicy.SplitTopologyWorkerCount = 1U;
    settings.PassPolicy.MeshEmitWorkerCount = 1U;
    return settings;
}

template <typename Stats>
bool TopologyValid(const Stats& stats)
{
    return stats.TjunctionCount == 0U && stats.InvalidNeighborCount == 0U &&
        stats.InvalidTopologyCount == 0U && stats.QueueInvariantViolationCount == 0U;
}

void CheckRealRefinement()
{
    const auto path = std::filesystem::temp_directory_path() /
        ("roam-quality-" + std::to_string(Clock::now().time_since_epoch().count()) + ".pgm");
    {
        std::ofstream file(path, std::ios::binary);
        file << "P5\n5 5\n255\n";
        for (int i = 0; i < 25; ++i) file.put(static_cast<char>(i * 8));
    }
    Terrain::HeightMap heightMap;
    std::string error;
    const bool loaded = heightMap.LoadFromFile(path, &error);
    std::filesystem::remove(path);
    Require(loaded, "small dyadic heightmap load failed");
    TerrainLodViewInput view;
    view.DrawableWidth = view.DrawableHeight = 100U;
    DataOrientedRoam::DataOrientedRoamPipeline dod;
    ClassicRoam::ClassicRoamMeshBuilder classic;
    const auto& dm = dod.Build(heightMap, 1.0F, 0.25F, view,
        FinestSettings<DataOrientedRoam::DataOrientedRoamSettings>(4));
    const auto& cm = classic.Build(heightMap, 1.0F, 0.25F, view,
        FinestSettings<ClassicRoam::ClassicRoamSettings>(4));
    Require(TopologyValid(dod.Stats()) && TopologyValid(classic.Stats()), "finest fixture topology invalid");
    Require(DepthHistogram(dm)[4] == 32U && DepthHistogram(cm)[4] == 32U,
        "negative thresholds did not produce complete depth-4 outputs");
    // 多叶网格触发索引内部节点，恒等曲面还需保持共享边命中无歧义
    const auto result = EvaluateMeshQuality(dm, dm, {glm::mat4{1.0F}, 100U, 100U});
    Require(result.Status == EvaluationStatus::Sampled, "hierarchy boundary query incomplete");
    Near(result.SampledHeightMax.value(), 0.0, "hierarchical identity query");
}

void WriteQuality(std::ostream& output, const CameraSample& camera, const QualityResult& result)
{
    output << camera.ScenarioId << ',' << camera.SampleIndex << ',' << camera.ViewInputHash << ','
        << ToString(result.Status) << ',' << result.SamplingLevel << ',' << result.SampleCount << ',' << result.ScreenSampleCount << ','
        << result.SampleHash << ',';
    if (result.SampledScreenMaxPx) output << *result.SampledScreenMaxPx;
    output << ',';
    if (result.SampledHeightMax) output << *result.SampledHeightMax;
    output << ',' << result.MissingCoverageCount << ',' << result.AmbiguousCoverageCount << ','
        << result.InvalidGeometryCount << ',' << result.InvalidProjectionCount << ',' << result.NearPlaneCrossingCount;
    for (const auto* location : {&result.ScreenMaximum, &result.HeightMaximum})
        output << ',' << location->SampleOrdinal << ',' << location->Uv.x << ',' << location->Uv.y
            << ',' << location->ReferencePosition.x << ',' << location->ReferencePosition.y << ','
            << location->ReferencePosition.z << ',' << location->MeasuredPosition.x << ','
            << location->MeasuredPosition.y << ',' << location->MeasuredPosition.z;
    output << ',' << result.IndexMilliseconds << ',' << result.SampleMilliseconds << ','
        << result.TotalMilliseconds << ",not_implemented\n";
    output.flush();
}

/// <summary>
/// 输出实际细化和已执行诊断，未知停止分支保留未知，不从预算余量推测内部控制
/// </summary>
template <typename Stats>
bool SaveBuild(const Mesh& mesh, const Stats& stats, int depth, double buildMilliseconds,
    const std::filesystem::path& outputPath)
{
    const auto histogram = DepthHistogram(mesh);
    auto depths = Output(outputPath / "depths.csv");
    depths << "depth,leafCount\n";
    for (std::size_t d = 0; d < histogram.size(); ++d) depths << d << ',' << histogram[d] << '\n';
    depths.close();
    const std::size_t budget = std::size_t{1} << static_cast<unsigned>(depth + 1);
    const bool complete = histogram[static_cast<std::size_t>(depth)] == budget &&
        mesh.Indices.size() / 3U == budget && stats.ActiveTriangleCount == budget && stats.MaxDepthReached == depth;
    auto build = Output(outputPath / "build.csv");
    build << "depth,budget,leaves,nodes,utilization,complete,topologyValid,split,forcedSplit,merge,rejectedSplit,"
        "budgetRejectedSplit,rejectedMerge,meshHash,normalizedMeshHash,buildMs,validationMs,evidenceMs,"
        "topologyValidation,passEvidence,pairEvidence,stopDetail\n";
    build << depth << ',' << budget << ',' << stats.ActiveTriangleCount << ',' << stats.NodeCount << ','
        << static_cast<double>(stats.ActiveTriangleCount) / static_cast<double>(budget) << ',' << complete << ','
        << TopologyValid(stats) << ',' << stats.SplitCount << ',' << stats.ForcedSplitCount << ',' << stats.MergeCount
        << ',' << stats.RejectedSplitCount << ',' << stats.BudgetRejectedSplitCount << ',' << stats.RejectedMergeCount
        << ',' << stats.MeshHash << ',' << stats.NormalizedMeshHash << ',' << buildMilliseconds << ','
        << stats.ValidateMilliseconds << ',' << stats.PassEvidenceMilliseconds << ",true,true,false,stop_detail_unavailable\n";
    build.close();
    return complete && TopologyValid(stats);
}

void Calibrate(const std::filesystem::path& scenariosPath, const std::filesystem::path& camerasPath,
    const std::filesystem::path& outputPath, const std::string& terrain, const std::string& implementation,
    bool bilinearSource = false, std::span<const unsigned> requestedLevels = {})
{
    Require(terrain == "test129" || terrain == "peking547", "unknown frozen terrain");
    Require(implementation == "dod" || implementation == "classic", "unknown calibration implementation");
    Require(!std::filesystem::exists(outputPath), "output directory already exists");
    std::filesystem::create_directories(outputPath);
    const auto scenarios = LoadScenarioManifest(scenariosPath, std::filesystem::current_path());
    const auto cameras = LoadCameraManifest(camerasPath, scenarios);
    const bool small = terrain == "test129";
    const std::string scenarioId = small ? "test129-a-b4096" : "peking547-a-b20000";
    const auto selected = std::find_if(scenarios.begin(), scenarios.end(), [&](const auto& s) {
        return s.ScenarioId == scenarioId;
    });
    Require(selected != scenarios.end(), "frozen scenario missing");
    const auto selectCamera = [&](const std::string& id, std::uint32_t sample) -> const CameraSample& {
        const auto found = std::find_if(cameras.begin(), cameras.end(), [&](const auto& c) {
            return c.ScenarioId == id && c.SampleIndex == sample;
        });
        Require(found != cameras.end(), "frozen camera missing");
        return *found;
    };
    const auto& firstCamera = selectCamera(scenarioId, small ? 5U : 9U);
    const auto view = BuildCameraView(firstCamera);
    Terrain::HeightMap heightMap;
    std::string error;
    Require(heightMap.LoadFromFile(selected->HeightMapPath, &error), "heightmap load failed");
    const int depth = small ? 14 : 20;
    Require(selected->Settings.MaxDepth == depth, "frozen depth changed");
    const auto& settings = selected->Settings;
    const auto referenceBegin = Clock::now();
    const auto reference = Terrain::TerrainMeshBuilder::Build(heightMap, settings.TerrainSize, settings.HeightScale);
    const double templateMilliseconds = Milliseconds(referenceBegin);
    const auto decodeBegin = Clock::now();
    const auto rawSamples = bilinearSource ?
        LoadReferenceSamples(selected->HeightMapPath, heightMap.Width(), heightMap.Height()) : std::vector<std::uint16_t>{};
    const double decodeMilliseconds = bilinearSource ? Milliseconds(decodeBegin) : 0.0;
    const BilinearHeightfieldReference source{rawSamples, heightMap.Width(), heightMap.Height(),
        settings.TerrainSize, settings.HeightScale};
    auto referenceInfo = Output(outputPath / "reference.csv");
    referenceInfo << "width,height,terrainSize,heightScale,triangles,buildMs,assetSha256,diagonal,protocol,"
        "primaryReference,decoder,normalization,decodeMs\n"
        << heightMap.Width() << ',' << heightMap.Height() << ',' << settings.TerrainSize << ',' << settings.HeightScale
        << ',' << reference.Indices.size() / 3U << ',' << templateMilliseconds << ',' << selected->HeightMapSha256
        << ",i0-i2-i1_and_i1-i2-i3,"
        << (bilinearSource ? "bilinear-source-k0-v1,continuous_bilinear,stbi_load_16_gray_no_flip,uint16_div_65535_double," :
            "reference-k0-v1,fixed_diagonal_triangles,HeightMap,SamplePixel_float,") << decodeMilliseconds << '\n';
    referenceInfo.close();

    const auto evaluate = [&](const Mesh& mesh, const auto& stats, double buildMilliseconds) {
        const bool complete = SaveBuild(mesh, stats, depth, buildMilliseconds, outputPath);
        if (!requestedLevels.empty())
        {
            Require(complete, "requested calibration output is incomplete or topology invalid");
            if (small)
                Require(stats.MeshHash == 6848791989484576542ULL &&
                    stats.NormalizedMeshHash == 6492269156412135930ULL, "rebuilt test129 differs from frozen output");
        }
        std::cout << "stage=quality complete=" << complete << " triangles=" << stats.ActiveTriangleCount << std::endl;
        auto quality = Output(outputPath / "quality.csv");
        quality << "scenarioId,sampleIndex,viewInputHash,status,level,samples,screenSamples,sampleHash,"
            "sampledScreenMaxPx,sampledHeightMax,missing,ambiguous,invalidGeometry,invalidProjection,nearCrossing,"
            "screenOrdinal,screenU,screenV,screenRefX,screenRefY,screenRefZ,screenMeshX,screenMeshY,screenMeshZ,"
            "heightOrdinal,heightU,heightV,heightRefX,heightRefY,heightRefZ,heightMeshX,heightMeshY,heightMeshZ,"
            "indexMs,samplingMs,totalMs,rmsStatus\n";
        const auto qualityBegin = Clock::now();
        const auto one = [&](const CameraSample& camera, unsigned level) {
            const auto input = BuildCameraView(camera);
            const QualityView qualityView{input.ViewProjection, input.DrawableWidth, input.DrawableHeight};
            const QualityOptions options{40000000U, 600.0 - Milliseconds(qualityBegin) / 1000.0, level};
            const auto result = bilinearSource ? EvaluateMeshQuality(source, reference, mesh, qualityView, options) :
                EvaluateMeshQuality(reference, mesh, qualityView, options);
            WriteQuality(quality, camera, result);
            std::cout << "stage=quality_done sample=" << camera.SampleIndex << " status=" << ToString(result.Status)
                << " level=" << level << " maxPx=" << result.SampledScreenMaxPx.value_or(-1.0) << std::endl;
            return result.Status == EvaluationStatus::Sampled;
        };
        const std::array<unsigned, 1> defaultLevels{0U};
        const auto levels = requestedLevels.empty() ? std::span<const unsigned>(defaultLevels) : requestedLevels;
        bool valid = true;
        for (const unsigned level : levels)
        {
            valid = one(firstCamera, level);
            if (!valid) break;
        }
        // 第二相机仅重用同一个完整输出，未完成/异常不再扩展；质量量级由检查点报告解释
        if (!small && !bilinearSource && complete && valid) one(selectCamera("peking547-a-b80000", 60U), 0U);
        quality.close();
        auto status = Output(outputPath / "status.txt");
        status << (complete ? "full_depth_output\n" : "attainable_residual_calibration_incomplete\n")
            << "sampling=";
        for (const auto level : levels) status << 'k' << level << ' ';
        status << "\nsamplingProtocol=midpoint-fourway-rational24-v1\nrms=not_implemented\n"
            "pointwiseExcess=not_paired_not_implemented\n";
        status.close();
        Require(valid, "quality incomplete; remaining requested levels were not run");
    };
    std::cout << "stage=build terrain=" << terrain << " implementation=" << implementation << std::endl;
    const auto buildBegin = Clock::now();
    if (implementation == "dod")
    {
        DataOrientedRoam::DataOrientedRoamPipeline pipeline;
        const auto& mesh = pipeline.Build(heightMap, settings.TerrainSize, settings.HeightScale, view,
            FinestSettings<DataOrientedRoam::DataOrientedRoamSettings>(depth));
        const double buildMilliseconds = Milliseconds(buildBegin);
        std::array<std::size_t, 21> actual{};
        for (const auto node : pipeline.State().ActiveLeafNodes)
            ++actual[static_cast<std::size_t>(pipeline.State().Nodes.DepthAt(node))];
        Require(actual == DepthHistogram(mesh), "DOD node depths disagree with emitted triangle areas");
        evaluate(mesh, pipeline.Stats(), buildMilliseconds);
    }
    else
    {
        ClassicRoam::ClassicRoamMeshBuilder builder;
        const auto& mesh = builder.Build(heightMap, settings.TerrainSize, settings.HeightScale, view,
            FinestSettings<ClassicRoam::ClassicRoamSettings>(depth));
        const double buildMilliseconds = Milliseconds(buildBegin);
        evaluate(mesh, builder.Stats(), buildMilliseconds);
    }
    std::cout << "stage=done" << std::endl;
}
}

int main(int argc, char** argv)
{
    try
    {
        if (argc == 1)
        {
            CheckAnalyticGeometry();
        CheckPlatformOutputs();
            CheckFailures();
            CheckBilinearReference();
            CheckRefinedSampling();
            CheckRealRefinement();
            std::cout << "Mesh quality analytic and refinement checks completed\n";
        }
        else if (argc == 2 && std::string(argv[1]) == "--check-bilinear")
        {
            CheckAnalyticGeometry();
        CheckPlatformOutputs();
            CheckFailures();
            CheckBilinearReference();
            std::cout << "Bilinear source and shared analytic checks completed\n";
        }
        else if (argc == 5 && std::string(argv[1]) == "--calibrate-bilinear-source")
        {
            Calibrate(argv[2], argv[3], argv[4], "test129", "dod", true);
        }
        else if (argc == 2 && std::string(argv[1]) == "--check-refined-sampling")
        {
            CheckRefinedSampling();
            CheckBilinearReference();
            CheckFailures();
            std::cout << "Refined sampling and affected analytic checks completed\n";
            std::cout << "meshVertexBytes=" << sizeof(Terrain::TerrainMeshVertex)
                << " triangleDomainBytes=" << sizeof(DataOrientedRoam::TriangleDomain)
                << " nodeMembershipBytes=" << sizeof(DataOrientedRoam::DataOrientedRoamNodeMembership) << '\n';
        }
        else if (argc == 6 && std::string(argv[1]) == "--calibrate-refinement")
        {
            const std::string terrain = argv[5];
            Require(terrain == "test129" || terrain == "peking547", "unknown refinement calibration terrain");
            const std::array<unsigned, 2> smallLevels{1U, 2U};
            const std::array<unsigned, 1> largeLevels{0U};
            Calibrate(argv[2], argv[3], argv[4], terrain, "dod", true,
                terrain == "test129" ? std::span<const unsigned>(smallLevels) : std::span<const unsigned>(largeLevels));
        }
        else
        {
            Require(argc == 7 && std::string(argv[1]) == "--calibrate-reference",
                "usage: --calibrate-reference scenarios cameras new-output test129|peking547 dod|classic");
            Calibrate(argv[2], argv[3], argv[4], argv[5], argv[6]);
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
