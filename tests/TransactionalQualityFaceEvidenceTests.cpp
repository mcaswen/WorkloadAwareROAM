#include "algorithms/greedy_transactional_lod/TransactionalQualityFaceEvidence.h"
#include "algorithms/greedy_transactional_lod/TransactionalQualitySampleEvidence.h"
#include "algorithms/greedy_transactional_lod/TransactionalPipeline.h"

#include <bit>
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace ParallelRoam::Algorithms::GreedyTransactionalLod;
using namespace QualityEvaluation;

void Require(bool value, const char* message)
{
    if (!value)
    {
        throw std::runtime_error(message);
    }
}

void Same(Interval a, Interval b)
{
    const auto same = [](double x, double y) {
        return std::bit_cast<std::uint64_t>(x) == std::bit_cast<std::uint64_t>(y) ||
               (std::isnan(x) && std::isnan(y));
    };
    Require(same(a.Low, b.Low) && same(a.High, b.High), "面复用改变区间端点或符号零");
}

template<class T>
T OriginalSide(const QualityFace& f, std::size_t edge, const std::array<T, 2>& q)
{
    const auto& a = f[edge];
    const auto& b = f[(edge + 1) % 3];
    return (T(b.U) - T(a.U)) * (q[1] - T(a.V)) - (T(b.V) - T(a.V)) * (q[0] - T(a.U));
}

std::size_t NumericOracle()
{
    std::size_t checks = 0;
    for (const double scale : {1.0, 1e-6, 1e6, std::ldexp(1.0, -300), std::ldexp(1.0, 300)})
    {
        for (const double offset : {0.0, -scale * .75})
        {
            const QualityFace f{{{offset, offset, -0.0}, {offset + scale, offset, .37},
                                 {offset + scale * .25, offset + scale, -1.25}}};
            QualityFaceEvidenceWork work;
            TransactionalQualityFaceEvidence prepared(f, &work);
            Require(work.BoundsBuilds == 0 && work.ExactBuilds == 0, "构造提前支付数值准备");
            for (int i = -2; i <= 18; ++i)
            {
                // 包括顶点、共享边、域外点和非退化但尺度很小的三角形
                const double u = offset + scale * i / 16;
                for (const double v : {offset, std::nextafter(offset, INFINITY), offset + scale * .25,
                                      offset + scale})
                {
                    const std::array<Interval, 2> q{{{Down(u), Up(u)}, {Down(v), Up(v)}}};
                    const std::array<R, 2> exact{R(u), R(v)};
                    for (std::size_t edge = 0; edge < 3; ++edge)
                    {
                        Same(OriginalSide(f, edge, q), prepared.SideBounds(edge, q));
                        Require(OriginalSide(f, edge, exact) == prepared.ExactSide(edge, exact),
                                "面复用改变精确方向值");
                    }
                    Same(Height(q[0], q[1], f[0], f[1], f[2]), prepared.HeightBounds(q));
                    Require(Height(exact[0], exact[1], f[0], f[1], f[2]) == prepared.ExactHeight(exact),
                            "面复用改变精确高度");
                    ++checks;
                }
            }
            Require(work.BoundsBuilds == 1 && work.ExactBuilds == 1, "冻结面重复准备系数");
        }
    }
    // 零面积只检查原区间传播；不要求有理除零成为合法几何
    QualityFace degenerate{{{0, 0, 0}, {0, 0, 0}, {0, 0, 0}}};
    TransactionalQualityFaceEvidence prepared(degenerate);
    const std::array<Interval, 2> q{Interval(.1), Interval(.2)};
    Same(Height(q[0], q[1], degenerate[0], degenerate[1], degenerate[2]), prepared.HeightBounds(q));
    return checks;
}

void CoverageOracle()
{
    for (double size : {30.0, 80.0, .3})
    {
        InitialMesh input;
        input.Config.Budget = 8;
        input.Config.TerrainSize = size;
        input.Source = {3, 3, std::vector<std::uint16_t>(9, 0)};
        input.Vertices = {{0, {0, 0, .1}}, {1, {1, 0, .2}}, {2, {1, 1, .3}}, {3, {0, 1, -.1}}};
        input.Faces = {{0, {0, 1, 2}}, {1, {0, 2, 3}}};
        TransactionalPipeline pipeline(input);
        WorkLedger work;
        pipeline.Initialize(work);
        Proposal p;
        p.Support = {0, 1};
        for (bool output : {false, true})
        {
            auto faces = Faces(pipeline.State(), p, output, false);
            // 反序再次核对闭共享边的首面选择，不能只验证覆盖布尔值
            for (int order = 0; order < 2; ++order)
            {
                QualityEvidenceWork oldWork, newWork;
                QualityFaceEvidenceWork faceWork;
                std::vector<TransactionalQualityFaceEvidence> prepared;
                for (const auto& face : faces)
                {
                    prepared.emplace_back(face, &faceWork);
                }
                for (Slot sid = 0; sid < pipeline.Samples().SampleCount(); ++sid)
                {
                    TransactionalQualitySampleEvidence original(pipeline.State(), pipeline.Samples(), sid, output, &oldWork);
                    TransactionalQualitySampleEvidence current(pipeline.State(), pipeline.Samples(), sid, output, &newWork);
                    const auto expected = original.Cover(faces);
                    auto* actual = current.Cover(prepared);
                    Require(bool(expected) == bool(actual), "覆盖人口改变");
                    if (expected)
                    {
                        Require(*expected == actual->Geometry(), "闭共享边首面选择改变");
                    }
                }
                Require(oldWork.ExactCoverRequests == newWork.ExactCoverRequests, "复用改变精确覆盖回退次数");
                Require(faceWork.BoundsBuilds <= faces.size() && faceWork.ExactBuilds <= faces.size(), "面证据跨样本未复用");
                std::reverse(faces.begin(), faces.end());
            }
        }
    }
}

void KernelCost(std::size_t checks)
{
    const QualityFace face{{{0, 0, .25}, {1, 0, .375}, {.25, 1, -.125}}};
    std::vector<std::array<Interval, 2>> queries;
    for (int i = 0; i < 1024; ++i)
    {
        queries.push_back({Interval((i % 31) / 32.0), Interval((i % 29) / 32.0)});
    }
    std::array<double, 2> costs{}, sinks{};
    QualityFaceEvidenceWork work;
    for (std::size_t mode = 0; mode < 2; ++mode)
    {
        const auto started = std::chrono::steady_clock::now();
        for (int repeat = 0; repeat < 32; ++repeat)
        {
            // 每次重新准备并析构，不能用无限复用掩盖生命周期成本
            std::optional<TransactionalQualityFaceEvidence> prepared;
            if (mode)
            {
                prepared.emplace(face, &work);
            }
            for (const auto& q : queries)
            {
                for (std::size_t edge = 0; edge < 3; ++edge)
                {
                    const auto side = mode ? prepared->SideBounds(edge, q) : OriginalSide(face, edge, q);
                    sinks[mode] += side.Low;
                }
                const auto height = mode ? prepared->HeightBounds(q) : Height(q[0], q[1], face[0], face[1], face[2]);
                sinks[mode] += height.High;
            }
        }
        costs[mode] = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    }
    Require(sinks[0] == sinks[1], "微测出现不同数值");
    std::cout << "{\"numericCases\":" << checks << ",\"beforeMs\":" << costs[0]
              << ",\"afterMs\":" << costs[1] << ",\"boundsBuilds\":" << work.BoundsBuilds
              << ",\"sideQueries\":" << work.BoundsSides << ",\"heightQueries\":" << work.BoundsHeights
              << ",\"faceRecordBytes\":" << sizeof(TransactionalQualityFaceEvidence) << "}\n";
}
}

int main()
{
    try
    {
        const auto checks = NumericOracle();
        CoverageOracle();
        KernelCost(checks);
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
