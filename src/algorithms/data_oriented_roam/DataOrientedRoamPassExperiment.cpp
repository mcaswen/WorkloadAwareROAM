#include "algorithms/data_oriented_roam/DataOrientedRoamPassExperiment.h"

#include "algorithms/data_oriented_roam/DataOrientedRoamPassExecution.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamPassInput.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamPassMeasurement.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamWorkloadProbe.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <limits>
#include <set>
#include <stdexcept>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
namespace
{
using Sample = DataOrientedRoamPassExperimentSample;
using Result = DataOrientedRoamPassExperimentResult;
using Config = DataOrientedRoamPassExperimentConfig;

// 绝对块号贯穿预热和记录，六排列保证完整周期内每种网格动作占据各位置两次
constexpr std::array<std::array<std::size_t, 3>, 6> MeshOrders{{
    {0U, 1U, 2U}, {0U, 2U, 1U}, {1U, 0U, 2U},
    {1U, 2U, 0U}, {2U, 0U, 1U}, {2U, 1U, 0U}}};
constexpr std::array<const char*, 6> MeshOrderNames{"ABC", "ACB", "BAC", "BCA", "CAB", "CBA"};

std::vector<TerrainLodPassAction> Actions(TerrainLodPassId pass)
{
    switch (pass)
    {
    case TerrainLodPassId::MergeScore:
    case TerrainLodPassId::SplitScore:
        return {TerrainLodPassAction::SerialFullRefresh, TerrainLodPassAction::ParallelFullRefresh};
    case TerrainLodPassId::MergeTopology:
    case TerrainLodPassId::SplitTopology:
        return {TerrainLodPassAction::SerialImmediate, TerrainLodPassAction::ParallelAssisted};
    case TerrainLodPassId::MeshEmit:
        return {TerrainLodPassAction::SerialDirty, TerrainLodPassAction::ParallelDirty, TerrainLodPassAction::SerialFull};
    default: throw std::invalid_argument{"Expected a CPU ROAM experiment pass"};
    }
}

void ValidateConfig(const Config& config)
{
    // 绝对块号跨过预热边界后仍须可序列化，不能只分别检查两个计数
    constexpr auto maximum = std::numeric_limits<std::uint32_t>::max();
    if (config.ParallelWorkerCount == 0U || config.ParallelWorkerCount > maximum ||
        config.MeasuredRepeatCount == 0U || config.MeasuredRepeatCount > maximum ||
        config.WarmupCount > maximum - config.MeasuredRepeatCount)
        throw std::invalid_argument{"Invalid experiment counts or absolute block overflow"};
    if (config.Mode != DataOrientedRoamPassExperimentMode::Diagnostic &&
        config.Mode != DataOrientedRoamPassExperimentMode::PilotMeasurement)
        throw std::invalid_argument{"Unknown experiment mode"};
    std::set<TerrainLodPassId> unique;
    std::size_t actions = 0U;
    for (const auto pass : config.Passes)
    {
        actions += Actions(pass).size();
        if (!unique.insert(pass).second)
            throw std::invalid_argument{"Duplicate experiment pass"};
    }
    if (unique.empty() || config.WarmupCount + config.MeasuredRepeatCount >
        std::vector<Sample>{}.max_size() / actions)
        throw std::invalid_argument{"Empty pass selection or sample storage overflow"};
}

void ApplyCommonWork(Sample& sample, const DataOrientedRoamPassWorkload& work)
{
    // 分类来自计时前的同一只读探测，不能用执行后的成功提交数回填共同特征
    const bool topology = sample.PassId == TerrainLodPassId::MergeTopology ||
        sample.PassId == TerrainLodPassId::SplitTopology;
    if (topology)
    {
        sample.CandidateCount = work.PlanningInteriorCandidateCount + work.PlanningBoundaryCandidateCount;
        sample.InteriorCandidateCount = work.PlanningInteriorCandidateCount;
        sample.BoundaryCandidateCount = work.PlanningBoundaryCandidateCount;
        sample.NonEmptyChunkCount = work.PlanningNonEmptyChunkCount;
    }
    if (work.PrimaryWorkValue == 0.0F &&
        (sample.RequestedAction != TerrainLodPassAction::SerialFull || work.PreActiveTriangleCount == 0U))
    {
        sample.FallbackReason = TerrainLodPassFallbackReason::NoWork;
        sample.FallbackDetail = "no_work";
        sample.EffectiveWorkerCount = 0U;
        sample.ExecutionPath = "no_work";
    }
}

void AppendResult(Result& destination, Result&& source)
{
    destination.WarmupExecutionCount += source.WarmupExecutionCount;
    destination.WorkerPreparationMilliseconds += source.WorkerPreparationMilliseconds;
    destination.Samples.insert(destination.Samples.end(),
        std::make_move_iterator(source.Samples.begin()), std::make_move_iterator(source.Samples.end()));
    destination.WarmupSamples.insert(destination.WarmupSamples.end(),
        std::make_move_iterator(source.WarmupSamples.begin()), std::make_move_iterator(source.WarmupSamples.end()));
    if (!source.Passed) destination.FailureMessage = source.FailureMessage;
}
}

DataOrientedRoamPassExperimentResult RunFrozenDataOrientedRoamPassExperiment(
    const DataOrientedRoamState& stageInput, TerrainLodPassId passId,
    const DataOrientedRoamPassExperimentConfig& config)
{
    Result result;
    try
    {
        ValidateConfig(config);
        const auto actions = Actions(passId);
        if (std::find(config.Passes.begin(), config.Passes.end(), passId) == config.Passes.end())
            throw std::invalid_argument{"Frozen pass is outside the requested selection"};
        const auto inputHash = HashDataOrientedRoamPassInput(stageInput, passId);
        const auto work = ProbeDataOrientedRoamPassWorkload(stageInput, passId);
        // 先统一准备并行资源，使第一个执行的动作不承担其他动作尚未创建的线程
        result.WorkerPreparationMilliseconds =
            PrepareDataOrientedRoamPassWorkers(stageInput, passId, config.ParallelWorkerCount);
        std::uint64_t referenceResult = 0U;
        const auto blocks = config.WarmupCount + config.MeasuredRepeatCount;
        for (std::size_t block = 0U; block < blocks; ++block)
        {
            const bool warmup = block < config.WarmupCount;
            auto& samples = warmup ? result.WarmupSamples : result.Samples;
            const auto begin = samples.size();
            const std::string orderName = actions.size() == 3U ? MeshOrderNames[block % 6U] :
                (block % 2U == 0U ? "AB" : "BA");
            for (std::size_t position = 0U; position < actions.size(); ++position)
            {
                // 每个位置重新复制同一来源，前一动作的结果只用于比较，不能作为下一动作输入
                const auto index = actions.size() == 3U ? MeshOrders[block % 6U][position] :
                    (position + block % 2U) % 2U;
                auto sample = MeasureDataOrientedRoamPass(stageInput, passId, actions[index], config);
                sample.AbsoluteBlockIndex = block;
                sample.IsWarmup = warmup;
                sample.RepeatIndex = warmup ? block : block - config.WarmupCount;
                sample.ExecutionOrder = position;
                sample.BlockOrder = orderName;
                ApplyCommonWork(sample, work);
                samples.push_back(std::move(sample));
                if (warmup) ++result.WarmupExecutionCount;
            }
            // 预热同样需要正确，后续块沿用第一个结果，防止跨重复的状态漂移被遗漏
            if (referenceResult == 0U) referenceResult = samples[begin].ResultHash;
            bool equivalent = referenceResult != 0U;
            for (std::size_t index = begin; index < samples.size(); ++index)
            {
                const auto& sample = samples[index];
                equivalent = equivalent && sample.Correct && sample.ValidationPerformed &&
                    sample.StageInputHash == inputHash && sample.ResultHash == referenceResult &&
                    sample.FrozenStateHash == samples[begin].FrozenStateHash;
            }
            for (std::size_t index = begin; index < samples.size(); ++index)
                samples[index].Equivalent = equivalent;
            if (!equivalent)
                throw std::runtime_error{"Frozen block input, correctness or result equivalence failed"};
        }
        // 生产状态及容器身份另由快照测试检查，运行时核对完整有效输入
        if (HashDataOrientedRoamPassInput(stageInput, passId) != inputHash)
            throw std::runtime_error{"Pairing changed the source state"};
        result.Passed = true;
    }
    catch (const std::exception& error)
    {
        result.FailureMessage = error.what();
        // 已执行行继续保留，但一个坏块会使整个目标失去有效配对资格
        for (auto& sample : result.Samples) sample.Equivalent = false;
        for (auto& sample : result.WarmupSamples) sample.Equivalent = false;
    }
    return result;
}

DataOrientedRoamPassExperimentResult RunDataOrientedRoamPassExperiment(
    const DataOrientedRoamState& previousFrameState, const TerrainLodViewInput& nextView,
    const DataOrientedRoamSettings& settings, const DataOrientedRoamPassExperimentConfig& config)
{
    Result result;
    try
    {
        ValidateConfig(config);
        if (previousFrameState.HeightMap == nullptr || !previousFrameState.HeightMap->IsValid())
            throw std::invalid_argument{"A valid previous frame height map is required"};
        if (previousFrameState.Settings.MaxDepth != settings.MaxDepth ||
            previousFrameState.Settings.TriangleBudget != settings.TriangleBudget)
            throw std::invalid_argument{"Replay cannot change depth or triangle budget"};
        DataOrientedRoamState state{previousFrameState};
        if (!PrepareDataOrientedRoamFrame(state, *state.HeightMap, state.TerrainSize,
                state.HeightScale, nextView, settings))
            throw std::runtime_error{"Cannot prepare replay frame"};
        // 只改来源动作和线程，保留阈值、预算及其他输入设置
        ConfigureDataOrientedRoamPassAction(state, TerrainLodPassId::MergeScore, TerrainLodPassAction::SerialFullRefresh, 1U);
        ConfigureDataOrientedRoamPassAction(state, TerrainLodPassId::MergeTopology, TerrainLodPassAction::SerialImmediate, 1U);
        ConfigureDataOrientedRoamPassAction(state, TerrainLodPassId::SplitScore, TerrainLodPassAction::SerialFullRefresh, 1U);
        ConfigureDataOrientedRoamPassAction(state, TerrainLodPassId::SplitTopology, TerrainLodPassAction::SerialImmediate, 1U);
        ConfigureDataOrientedRoamPassAction(state, TerrainLodPassId::MeshEmit, TerrainLodPassAction::SerialDirty, 1U);
        ExecuteDataOrientedRoamCpuPasses(state, [&](const auto& input, auto pass) {
            // 未选阶段仍由生产编排推进，因此筛选不会改变后续阶段的输入身份
            if (std::find(config.Passes.begin(), config.Passes.end(), pass) == config.Passes.end()) return;
            auto measured = RunFrozenDataOrientedRoamPassExperiment(input, pass, config);
            const bool passed = measured.Passed;
            AppendResult(result, std::move(measured));
            if (!passed) throw std::runtime_error{result.FailureMessage};
        });
        result.Passed = true;
    }
    catch (const std::exception& error)
    {
        result.FailureMessage = error.what();
    }
    return result;
}
}
