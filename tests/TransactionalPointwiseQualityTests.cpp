#include "algorithms/greedy_transactional_lod/TransactionalPointwiseQuality.h"
#include "algorithms/greedy_transactional_lod/TransactionalPipeline.h"
#include "algorithms/greedy_transactional_lod/TransactionalReservation.h"
#include "algorithms/greedy_transactional_lod/TransactionalQualityEvaluation.h"
#include "algorithms/greedy_transactional_lod/TransactionalQualitySampleEvidence.h"

#include <iostream>
#include <bit>

namespace
{
using namespace ParallelRoam::Algorithms;
using namespace ParallelRoam::Algorithms::GreedyTransactionalLod;

void Require(bool condition, const char *message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

/// <summary>
/// 平坦参考上放置有误差的四角，中心细化可以产生明确的逐点改善
/// 高SplitPixels故意阻止旧请求，检验新资格确实由独立目标决定
/// </summary>
InitialMesh Square()
{
    InitialMesh input;
    input.Config.Budget = 8;
    input.Config.SplitPixels = 1000;
    input.Config.PreserveSurvivingHeights = true;
    input.Config.ReceiverOrder = TransactionalReceiverOrder::ErrorFirst;
    input.Config.QualityPolicy = TransactionalQualityPolicy::PointwiseTarget;
    input.Source = {3, 3, std::vector<std::uint16_t>(9, 0)};
    input.Vertices = {{0, {0, 0, 1}}, {1, {1, 0, 1}}, {2, {1, 1, 1}}, {3, {0, 1, 1}}};
    input.Faces = {{0, {0, 1, 2}}, {1, {0, 2, 3}}};
    return input;
}

Proposal Refine(const InitialMesh &input, double height)
{
    Proposal p;
    p.Kind = 'E';
    p.Root = 0;
    p.NewVertex = -1;
    p.Support = {0, 1};
    p.Faces = {{0, 1, -1}, {1, 2, -1}, {2, 3, -1}, {3, 0, -1}};
    p.Free = {-1};
    for (const auto &[id, point] : input.Vertices)
    {
        p.Points.emplace(id, point);
    }
    p.Points.emplace(-1, Point{.5, .5, height});
    return p;
}

template <class F> void MustReject(F &&action, const char *message)
{
    bool rejected = false;
    try
    {
        action();
    }
    catch (const std::exception &)
    {
        rejected = true;
    }
    Require(rejected, message);
}
} // namespace

int main()
{
    try
    {
        // 极小正数只能得到跨零区间；负数须向下取整，而不是向零截断
        using namespace QualityEvaluation;
        // 覆盖每个有限指数、两种符号和尾数极端；独立oracle保留通用有理除法
        for (std::uint64_t exponent = 0; exponent < 2047; ++exponent)
        {
            for (const std::uint64_t fraction : {std::uint64_t{0}, std::uint64_t{1}, (std::uint64_t{1} << 52) - 1})
            {
                for (const std::uint64_t sign : {std::uint64_t{0}, std::uint64_t{1} << 63})
                {
                    const double value = std::bit_cast<double>(sign | (exponent << 52) | fraction);
                    Integer expectedLow = 7, expectedHigh = -3, actualLow = 7, actualHigh = -3;
                    AddBounds(R(value), R(value), expectedLow, expectedHigh);
                    AddBinaryBounds(value, value, actualLow, actualHigh);
                    Require(expectedLow == actualLow && expectedHigh == actualHigh, "binary64定向累计失配");
                }
            }
        }
        MustReject([&] {
            Integer a = 0, b = 0;
            AddBinaryBounds(INFINITY, INFINITY, a, b);
        }, "非有限区间端点被静默接受");
        Integer low = 0, high = 0;
        const R tiny = R(1) / (Integer(1) << 140);
        AddBounds(tiny, tiny, low, high);
        Require(low == 0 && high == 1, "极小正数被错误认证为严格正下界");
        low = high = 0;
        const R negative = -R(1) / 3;
        AddBounds(negative, negative, low, high);
        const R scale = Integer(1) << 128;
        Require(R(low) / scale <= negative && R(high) / scale >= negative &&
                high - low == 1, "负有理数定向取整没有包含精确值");

        auto input = Square();
        // 此解析投影的误差恰为50px，用相邻浮点目标检查请求出生边界
        for (const double target : {std::nextafter(50.0, 0.0), 50.0, std::nextafter(50.0, 100.0)})
        {
            auto thresholdInput = input;
            thresholdInput.Config.QualityTargetPixels = target;
            TransactionalPipeline thresholdPipeline(thresholdInput);
            WorkLedger thresholdWork;
            thresholdPipeline.Initialize(thresholdWork);
            Require(thresholdPipeline.Samples().RawCount() == (target < 50 ? 2U : 0U),
                    "数值缓存目标两侧的请求资格错误");
        }
        TransactionalPipeline pipeline(input);
        WorkLedger work;
        pipeline.Initialize(work);
        const auto &state = pipeline.State();
        const auto &samples = pipeline.Samples();
        for (const bool output : {false, true})
        {
            auto proposal = Refine(input, .5);
            const auto faces = Faces(state, proposal, output, false);
            // 包括共享边与边界样本；参考按旧入口独立重算，不用缓存值自证
            for (Slot sid = 0; sid < samples.SampleCount(); ++sid)
            {
                TransactionalQualitySampleEvidence evidence(state, samples, sid, output);
                const auto expected = Reference<Interval>(state, samples, sid);
                const auto& actual = evidence.ReferenceBounds();
                for (std::size_t i = 0; i < 3; ++i)
                {
                    Require(expected[i].Low == actual[i].Low && expected[i].High == actual[i].High,
                            "参考复用改变区间端点");
                }
                Require(evidence.ExactReference() == Reference<R>(state, samples, sid), "精确参考复用失配");
                Require(evidence.ExactCoordinate() == Coordinate(Reference<R>(state, samples, sid), state.Config(), output),
                        "坐标混用表示域");
                Require(evidence.Cover(faces) == Cover(state, samples, sid, output, faces), "覆盖面选择失配");
                Require(evidence.VisibilityAgrees() == VisibilityAgrees(state, samples, sid, expected),
                        "可见性复用失配");
            }
        }
        Require(samples.RawCount() == 2, "目标超标根被旧SplitPixels挡住");
        auto legacy = input;
        legacy.Config.QualityPolicy = TransactionalQualityPolicy::Legacy;
        TransactionalPipeline old(legacy);
        old.Initialize(work);
        Require(old.Samples().RawCount() == 0, "旧资格发生变化");

        auto good = Refine(input, .5);
        // 解析平坦参考使改善方向明确，无需用生产Fit为生产认证自证
        Require(TransactionalPointwiseQuality::Certify(state, samples, good, true, work) == "certified",
                "明确逐点改善没有通过");
        good.Reason = "certified";
        CertifiedBatch batch;
        batch.Version = state.Version();
        batch.Exchanges.push_back({good, {}, false});
        TransactionalPointwiseQuality::ValidateBatch(state, samples, batch, work);

        // 认证后的任何几何变动都必须失效，不能靠同一代次重复使用旧证书
        batch.Exchanges[0].Receiver.Points.at(-1).Height = .6;
        MustReject([&] { pipeline.Apply(batch, work); }, "被篡改提案仍然发布");
        Require(pipeline.State().FaceCount() == 2, "失败后留下半批拓扑");

        auto bad = Refine(input, 1.5);
        Require(TransactionalPointwiseQuality::Certify(state, samples, bad, true, work) == "pointwise_height_damage",
                "已有超标高度继续恶化");
        auto neutral = Refine(input, 1);
        // donor安全与receiver有进展是不同义务，恒等曲面只满足前者
        Require(TransactionalPointwiseQuality::Certify(state, samples, neutral, false, work) == "certified",
                "零损伤donor被误拒");
        Require(TransactionalPointwiseQuality::Certify(state, samples, neutral, true, work) != "certified",
                "receiver没有正进展也被接受");

        // 同一补丁重复入批不能以样本去重掩盖两个改变贡献
        batch.Exchanges = {{good, {}, false}, {good, {}, false}};
        MustReject([&] { TransactionalPointwiseQuality::ValidateBatch(state, samples, batch, work); },
                   "共享改变贡献没有被发现");

        auto view = state.Config();
        // 固定目标是跨轮包络的前提，相机入口不能偷偷放宽它
        view.QualityTargetPixels = 1;
        MustReject([&] { pipeline.SetView(view, work); }, "相机刷新偷偷改变目标");

        // 真正提交后继续刷新；测试不是只生成一个正确的最终mesh
        batch.Exchanges = {{good, {}, false}};
        pipeline.Apply(batch, work);
        Require(pipeline.State().FaceCount() == 4 && pipeline.State().FaceCount() <= input.Config.Budget,
                "细化预算或面数错误");
        view = pipeline.State().Config();
        view.Matrix[3] = .01;
        pipeline.SetView(view, work);
        MustReject([&] { pipeline.Apply(batch, work); }, "旧代次批次被重放");
        Require(pipeline.Samples().RawCount() == 4, "后续候选没有按新目标恢复");
        Require(pipeline.ConsumeMesh().Data != nullptr, "输出不可继续消费");

        auto hidden = input;
        // 平移参考到裁剪域外后，世界几何损伤义务仍然存在
        hidden.Config.Matrix[3] = 10;
        TransactionalPipeline hiddenPipeline(hidden);
        hiddenPipeline.Initialize(work);
        bad = Refine(hidden, 1.5);
        Require(TransactionalPointwiseQuality::Certify(hiddenPipeline.State(), hiddenPipeline.Samples(), bad, false,
                                                       work) == "pointwise_height_damage",
                "离屏样本绕过高度损伤条件");

        // 目标以下允许有限损伤，但不能用宽高度阈值绕过屏幕约束
        auto below = input;
        below.Config.QualityHeightRatio = 1;
        for (auto &[id, point] : below.Vertices)
        {
            static_cast<void>(id);
            point.Height = 0;
        }
        TransactionalPipeline lowPipeline(below);
        lowPipeline.Initialize(work);
        auto limited = Refine(below, .005);
        Require(TransactionalPointwiseQuality::Certify(lowPipeline.State(), lowPipeline.Samples(), limited, false,
                                                       work) == "certified",
                "目标以下有限损伤被错误禁止");
        limited = Refine(below, .02);
        Require(TransactionalPointwiseQuality::Certify(lowPipeline.State(), lowPipeline.Samples(), limited, false,
                                                       work) == "pointwise_screen_damage",
                "屏幕逐点门槛未起作用");
        auto wrong = input;
        // 直接核心调用也必须拒绝错误组合，不能依赖平台配置检查兜底
        wrong.Config.PreserveSurvivingHeights = false;
        MustReject([&] { TransactionalPipeline rejected(wrong); }, "无效政策组合被接受");
        std::cout << "pointwise quality fixtures passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
