#include "algorithms/greedy_transactional_lod/TransactionalHeightRejection.h"

#include "algorithms/greedy_transactional_lod/TransactionalPipeline.h"
#include "algorithms/greedy_transactional_lod/TransactionalPointwiseQuality.h"
#include "algorithms/greedy_transactional_lod/TransactionalProposals.h"
#include "algorithms/greedy_transactional_lod/TransactionalSourceHeightReceiver.h"
#include "algorithms/greedy_transactional_lod/TransactionalRejectionHints.h"

#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
using namespace ParallelRoam::Algorithms;
using namespace ParallelRoam::Algorithms::GreedyTransactionalLod;
using namespace QualityEvaluation;

void Require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

void BoundSoundness()
{
    // 用精确有理值核对包含界，只要求拒绝可靠，不要求每个真损伤都被过滤
    std::size_t rejected = 0;
    for (int old = -8; old <= 8; ++old)
    {
        for (int next = -8; next <= 8; ++next)
        {
            const auto before = Interval(old / 8.0) * Interval(old / 8.0);
            const auto after = Interval(next / 8.0) * Interval(next / 8.0);
            const auto cap = Interval(.25) * Interval(.25);
            if (TransactionalHeightRejection::ProvesDamage(before, after, cap))
            {
                ++rejected;
                const R a = R(old) / 8;
                const R b = R(next) / 8;
                Require(b * b > std::max(R(a * a), R(1) / 16), "区间提前拒绝与精确损伤不符");
            }
        }
    }
    Require(rejected > 0, "没有覆盖真实提前拒绝");
    Require(!TransactionalHeightRejection::ProvesDamage({1, 1}, {1, 1}, {0, 0}), "等号被拒绝");
    Require(!TransactionalHeightRejection::ProvesDamage({0, 2}, {1, 3}, {0, 0}), "重叠区间被拒绝");
    const double inf = std::numeric_limits<double>::infinity();
    Require(!TransactionalHeightRejection::ProvesDamage({0, 1}, {2, inf}, {0, 0}), "非有限界被拒绝");
}

InitialMesh Square(bool peak)
{
    InitialMesh input;
    input.Config.Budget = 8;
    input.Config.PreserveSurvivingHeights = true;
    input.Config.ReceiverOrder = TransactionalReceiverOrder::ErrorFirst;
    input.Config.QualityPolicy = TransactionalQualityPolicy::PointwiseTarget;
    input.Config.ReceiverHeightPolicy = TransactionalReceiverHeightPolicy::SourceHeight;
    input.Source = {3, 3, std::vector<std::uint16_t>(9, 0)};
    const double height = peak ? 0 : 1;
    input.Source.Values[4] = peak ? 65535 : 0;
    input.Vertices = {{0, {0, 0, height}}, {1, {1, 0, height}},
        {2, {1, 1, height}}, {3, {0, 1, height}}};
    input.Faces = {{0, {0, 1, 2}}, {1, {0, 2, 3}}};
    return input;
}

void CertificationEquivalence()
{
    for (bool peak : {false, true})
    {
        TransactionalPipeline pipeline(Square(peak));
        WorkLedger initialization;
        pipeline.Initialize(initialization);
        ReceiverCursor cursor(pipeline.State(), pipeline.Samples(), 0);
        auto proposal = cursor.Next();
        Require(proposal.has_value(), "缺少接收提案");
        Require(TransactionalSourceHeightReceiver::Prepare(pipeline.State(), pipeline.Samples(), *proposal,
            initialization).empty(), "源高准备失败");
        auto reference = *proposal;
        WorkLedger referenceWork;
        const auto expected = TransactionalPointwiseQuality::Certify(pipeline.State(), pipeline.Samples(),
            reference, true, referenceWork);
        WorkLedger candidateWork;
        HeightRejectionContext context;
        context.HeightFirst = true;
        const auto actual = TransactionalPointwiseQuality::Certify(pipeline.State(), pipeline.Samples(),
            *proposal, true, candidateWork, &context);
        Require(actual == expected, "提前高度检查改变认证理由");
        Require(static_cast<bool>(proposal->QualityProof) == static_cast<bool>(reference.QualityProof),
            "失败项残留证书或成功项没有完整证书");
        Require(candidateWork.SampleTouches == referenceWork.SampleTouches, "单样本前移改变了样本人口");
        if (peak)
        {
            Require(context.FailureSample != InvalidSlot && context.IntervalFailure, "缺少当前高度失败见证");
            Require(candidateWork.Reasons.at("quality_early_height_rejects") == 1, "没有发生前移");
            auto hinted = reference;
            HeightRejectionContext hint;
            hint.HeightFirst = true;
            hint.HintSample = context.FailureSample;
            hint.Audit = true;
            WorkLedger hintedWork;
            Require(TransactionalPointwiseQuality::Certify(pipeline.State(), pipeline.Samples(), hinted, true,
                hintedWork, &hint) == expected, "提示拒绝改变可接受集合");
            Require(hint.OriginalReason == expected && hintedWork.Reasons.at("quality_hint_rejected") == 1,
                "没有独立重放原拒绝");
            Require(!hinted.QualityProof, "提示拒绝产生了成功证书");
            WorkLedger limited;
            limited.VisitLimit = 0;
            bool stopped = false;
            try
            {
                TransactionalPointwiseQuality::Certify(pipeline.State(), pipeline.Samples(), hinted, true, limited, &hint);
            }
            catch (const std::runtime_error&)
            {
                stopped = true;
            }
            Require(stopped && !hinted.QualityProof, "提示额外访问绕过配额或残留证书");
        }
        // 任意域外提示只能回退；成功项依然必须完成双域证书
        HeightRejectionContext outside;
        outside.HeightFirst = true;
        outside.HintSample = InvalidSlot - 1;
        WorkLedger outsideWork;
        auto unchanged = reference;
        Require(TransactionalPointwiseQuality::Certify(pipeline.State(), pipeline.Samples(), unchanged, true,
            outsideWork, &outside) == expected, "域外提示改变了结果");
        Require(outsideWork.Reasons.at("quality_hint_outside") == 1, "没有检查提示当前支持成员资格");

        if (!peak)
        {
            // 同样的样本身份面对已改善曲面不能沿用另一状态的失败结论
            for (Slot sample = 0; sample < pipeline.Samples().SampleCount(); ++sample)
            {
                auto improved = reference;
                HeightRejectionContext stale;
                stale.HeightFirst = true;
                stale.HintSample = sample;
                WorkLedger staleWork;
                Require(TransactionalPointwiseQuality::Certify(pipeline.State(), pipeline.Samples(), improved, true,
                    staleWork, &stale) == expected, "失效提示挡住合法改善");
                Require(improved.QualityProof != nullptr, "失效提示跳过完整成功证书");
            }
        }
    }
}

void CacheAndContinuation()
{
    TransactionalRejectionHints cache;
    WorkLedger work;
    std::vector<RejectionHint> initial;
    for (std::size_t i = 0; i < TransactionalRejectionHints::Capacity; ++i)
    {
        initial.push_back({static_cast<Identity>(i), 'E', 0, static_cast<Slot>(i)});
    }
    cache.Remember(initial, work);
    cache.Remember({{0, 'E', 0, 123}}, work);
    Require(cache.Find(0, 'E', 0) == 123, "同键样本没有更新");
    cache.Remember({{9999, 'F', 1, 7}}, work);
    Require(!cache.Find(0, 'E', 0) && cache.Find(1, 'E', 0) == 1, "更新值意外改变FIFO年龄");
    Require(cache.Size() == TransactionalRejectionHints::Capacity, "提示容量无界增长");
    Require(!cache.Find(9999, 'F', 0) && !cache.Find(9999, 'E', 1), "目录键没有区分身份");
    TransactionalRejectionHints reset;
    Require(reset.Size() == 0 && !reset.Find(9999, 'F', 1), "新实例复用了旧源的提示");

    std::vector<std::tuple<Identity, char, std::size_t, Slot>> serialHints, parallelHints;
    const auto execution = [&](std::size_t workers, auto& observed) {
        TransactionalExecution result;
        result.Workers = workers;
        result.Dispatch = [](std::size_t count, const auto& task) {
            std::vector<std::jthread> threads;
            for (std::size_t i = 0; i < count; ++i)
            {
                threads.emplace_back([&, i] { task(i); });
            }
        };
        result.ObserveHeightFailure = [&](Identity root, char kind, std::size_t ordinal, Slot sample, bool,
                                         const std::string&) { observed.emplace_back(root, kind, ordinal, sample); };
        return result;
    };
    TransactionalPipeline serial(Square(true), execution(1, serialHints));
    TransactionalPipeline parallel(Square(true), execution(8, parallelHints));
    for (int frame = 0; frame < 3; ++frame)
    {
        WorkLedger a, b;
        auto view = Square(true).Config;
        view.Matrix[3] = frame * .001;
        serial.SetView(view, a);
        parallel.SetView(view, b);
        const auto first = serial.Update(a);
        const auto second = parallel.Update(b);
        Require(first.Attempts == second.Attempts && first.IntentIds == second.IntentIds &&
            first.Exchanges.size() == second.Exchanges.size(), "任务完成顺序改变提示或选择");
        Require(serial.State().FaceCount() == parallel.State().FaceCount(), "提示导致续接状态不同");
    }
    Require(serialHints == parallelHints, "提示归并不确定");
}
}

int main()
{
    try
    {
        BoundSoundness();
        CertificationEquivalence();
        CacheAndContinuation();
        std::cout << "height rejection bounds and certification equivalence verified\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
