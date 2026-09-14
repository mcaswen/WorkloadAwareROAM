#include "app/ApplicationCommandLine.h"
#include "algorithms/TerrainLodAlgorithmRegistry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <limits>
#include <string>
#include <string_view>

namespace ParallelRoam::App
{
namespace
{
/// <summary>
/// 把一个可读名称和程序内部值放在同一张映射表中
/// </summary>
template <typename Value>
struct NamedValue
{
    std::string_view Name;
    Value ParsedValue;
};

/// <summary>
/// 从映射表解析枚举或预设，所有别名都走同一套查找逻辑
/// </summary>
template <typename Value, std::size_t ValueCount>
[[nodiscard]] bool ParseNamedValue(
    std::string_view text,
    const std::array<NamedValue<Value>, ValueCount>& values,
    Value& output)
{
    const auto match = std::find_if(
        values.begin(),
        values.end(),
        [text](const NamedValue<Value>& value) {
            return value.Name == text;
        });
    if (match == values.end())
    {
        return false;
    }

    output = match->ParsedValue;
    return true;
}

/// <summary>
/// 按参数出现顺序完成分流、取值、转换和冲突检查
/// </summary>
class CommandLineParser
{
public:
    CommandLineParser(int argumentCount, char** argumentValues)
        : _argumentCount(argumentCount),
          _argumentValues(argumentValues)
    {
    }

    [[nodiscard]] ApplicationCommandLineParseResult Parse()
    {
        // 保持旧入口从左到右的处理顺序，遇到无窗口入口就立即完成分流
        for (int index = 1; index < _argumentCount; ++index)
        {
            const std::string_view argument{_argumentValues[index]};
            const ApplicationLaunchMode launchMode = FindLaunchMode(argument);
            if (launchMode != ApplicationLaunchMode::Application)
            {
                _result.Options.LaunchMode = launchMode;
                return _result;
            }

            const OptionDescriptor* descriptor = FindDescriptor(argument);
            if (descriptor == nullptr)
            {
                // 未识别参数沿用旧入口行为，交互启动时直接忽略
                continue;
            }

            std::string_view value;
            if (descriptor->RequiresValue)
            {
                // 参数表统一决定是否消费下一个值，处理函数不再修改遍历位置
                if (index + 1 >= _argumentCount)
                {
                    _result.Error = "Missing value for " + std::string{argument};
                    break;
                }
                value = _argumentValues[++index];
            }

            if (!(this->*descriptor->Handler)(argument, value))
            {
                break;
            }
        }

        // 自动实验和固定帧冒烟测试都有自己的退出条件，不能同时启用
        if (_result.Error.empty() &&
            _result.Options.AutomaticRuntimeBenchmark &&
            _result.Options.FixedFrameSmokeTest)
        {
            _result.Error = "--runtime-benchmark cannot be combined with a smoke-test option.";
        }
        return _result;
    }

private:
    // 处理函数只负责解释单个参数，不参与参数名匹配和遍历
    using OptionHandler = bool (CommandLineParser::*)(std::string_view, std::string_view);

    /// <summary>
    /// 描述参数是否带值以及匹配后应调用的处理函数
    /// </summary>
    struct OptionDescriptor
    {
        std::string_view Name;
        bool RequiresValue{false};
        OptionHandler Handler{nullptr};
    };

    /// <summary>
    /// 描述需要在创建应用前直接执行的入口
    /// </summary>
    struct LaunchModeDescriptor
    {
        std::string_view Name;
        ApplicationLaunchMode Mode{ApplicationLaunchMode::Application};
    };

    [[nodiscard]] static ApplicationLaunchMode FindLaunchMode(std::string_view argument)
    {
        // 独立入口直接分流，是否创建窗口由各自有限入口决定
        static constexpr std::array<LaunchModeDescriptor, 4> modes{{
            {"--transactional-platform-replay", ApplicationLaunchMode::TransactionalPlatformReplay},
            {"--transactional-platform-check", ApplicationLaunchMode::TransactionalPlatformCheck},
            {"--roam-probe", ApplicationLaunchMode::RoamProbe},
            {"--benchmark", ApplicationLaunchMode::TerrainLodBenchmark},
        }};

        const auto match = std::find_if(
            modes.begin(),
            modes.end(),
            [argument](const LaunchModeDescriptor& descriptor) {
                return descriptor.Name == argument;
            });
        return match == modes.end() ? ApplicationLaunchMode::Application : match->Mode;
    }

    [[nodiscard]] static const OptionDescriptor* FindDescriptor(std::string_view argument)
    {
        // 别名单独登记但共享处理函数，新增名称不需要扩展条件分支
        static constexpr std::array<OptionDescriptor, 34> descriptors{{
            {"--algorithm", true, &CommandLineParser::HandleAlgorithm},
            {"--runtime-benchmark-algorithms", true, &CommandLineParser::HandleAlgorithmSequence},
            {"--transactional-workers", true, &CommandLineParser::HandleTransactionalLimit},
            {"--transactional-prefix", true, &CommandLineParser::HandleTransactionalLimit},
            {"--transactional-donors", true, &CommandLineParser::HandleTransactionalLimit},
            {"--runtime-benchmark-budget", true, &CommandLineParser::HandleTriangleBudget},
            {"--smoke-test", false, &CommandLineParser::HandleSmokeTest},
            {"--dx12-smoke-test", false, &CommandLineParser::HandleDirect3D12SmokeTest},
            {"--runtime-benchmark", false, &CommandLineParser::HandleRuntimeBenchmark},
            {"--runtime-benchmark-path", true, &CommandLineParser::HandleRuntimeBenchmarkPath},
            {"--runtime-benchmark-policy", true, &CommandLineParser::HandlePassPolicy},
            {"--runtime-benchmark-split-topology-min-candidates", true,
             &CommandLineParser::HandleSplitTopologyMinimum},
            {"--runtime-benchmark-merge-topology-min-candidates", true,
             &CommandLineParser::HandleMergeTopologyMinimum},
            {"--runtime-benchmark-parallel-topology-target-build", true,
             &CommandLineParser::HandleParallelTopologyTargetBuild},
            {"--runtime-benchmark-parallel-topology-phase", true,
             &CommandLineParser::HandleParallelTopologyPhase},
            {"--runtime-benchmark-heightmap", true, &CommandLineParser::HandleHeightMap},
            {"--runtime-benchmark-terrain-size", true, &CommandLineParser::HandleTerrainSize},
            {"--runtime-benchmark-height-scale", true, &CommandLineParser::HandleHeightScale},
            {"--runtime-benchmark-depth", true, &CommandLineParser::HandleMaximumDepth},
            {"--runtime-benchmark-max-depth", true, &CommandLineParser::HandleMaximumDepth},
            {"--runtime-benchmark-split-pixels", true, &CommandLineParser::HandleSplitPixels},
            {"--runtime-benchmark-split-threshold", true, &CommandLineParser::HandleSplitPixels},
            {"--runtime-benchmark-merge-pixels", true, &CommandLineParser::HandleMergePixels},
            {"--runtime-benchmark-merge-threshold", true, &CommandLineParser::HandleMergePixels},
            {"--runtime-benchmark-distance-scale", false,
             &CommandLineParser::HandleRemovedDistanceScale},
            {"--runtime-benchmark-samples", true, &CommandLineParser::HandleSampleCount},
            {"--runtime-benchmark-warmup-samples", true, &CommandLineParser::HandleWarmupCount},
            {"--runtime-benchmark-order-rotation", true, &CommandLineParser::HandleOrderRotation},
            {"--runtime-benchmark-upload-pair", false, &CommandLineParser::HandleUploadPair},
            {"--runtime-benchmark-upload-warmups", true,
             &CommandLineParser::HandleUploadWarmupCount},
            {"--runtime-benchmark-upload-repeats", true,
             &CommandLineParser::HandleUploadRepeatCount},
            {"--runtime-benchmark-upload-targets", true,
             &CommandLineParser::HandleUploadTargetCount},
            {"--runtime-benchmark-duration", true, &CommandLineParser::HandleLegacyDuration},
            {"--runtime-benchmark-label", true, &CommandLineParser::HandleLabel},
        }};

        const auto match = std::find_if(
            descriptors.begin(),
            descriptors.end(),
            [argument](const OptionDescriptor& descriptor) {
                return descriptor.Name == argument;
            });
        return match == descriptors.end() ? nullptr : &*match;
    }

    bool HandleSmokeTest(std::string_view, std::string_view)
    {
        // 普通冒烟测试只需确认窗口、资源和基础渲染能够连续运行
        _result.Options.FixedFrameSmokeTest = true;
        _result.Options.MaxFrameCount = 3;
        return true;
    }

    bool HandleDirect3D12SmokeTest(std::string_view, std::string_view)
    {
#if defined(PARALLEL_ROAM_GRAPHICS_API_D3D12)
        // D3D12 路径需要更多帧覆盖多帧资源轮转
        _result.Options.FixedFrameSmokeTest = true;
        _result.Options.MaxFrameCount = 32;
        return true;
#else
        _result.Error = "--dx12-smoke-test requires PARALLEL_ROAM_GRAPHICS_API=D3D12";
        return false;
#endif
    }

    bool HandleAlgorithm(std::string_view, std::string_view value)
    {
        const auto id = Algorithms::ParseTerrainLodAlgorithm(value);
        if (!id) { _result.Error = "Unknown or unavailable algorithm: " + std::string{value}; return false; }
        auto& overrides = _result.Options.RuntimeBenchmark;
        overrides.HasInteractiveAlgorithm = true;
        overrides.InteractiveAlgorithm = *id;
        MarkRuntimeBenchmarkOverride();
        return true;
    }

    bool HandleAlgorithmSequence(std::string_view, std::string_view value)
    {
        auto& sequence = _result.Options.RuntimeBenchmark.AlgorithmSequence;
        sequence.clear();
        while (true)
        {
            const auto comma = value.find(',');
            const auto id = Algorithms::ParseTerrainLodAlgorithm(value.substr(0, comma));
            if (!id || std::find(sequence.begin(), sequence.end(), *id) != sequence.end())
            { _result.Error = "Invalid, duplicate or unavailable runtime algorithm"; return false; }
            sequence.push_back(*id);
            if (comma == std::string_view::npos) break;
            value.remove_prefix(comma + 1);
        }
        MarkRuntimeBenchmarkOverride();
        return true;
    }

    bool HandleTransactionalLimit(std::string_view option, std::string_view value)
    {
        std::size_t number{};
        if (!ParseNonNegativeSize(option, value, number)) return false;
        const bool workers = option == "--transactional-workers";
        if (number == 0 || number > (workers ? 32U : 640U))
        { _result.Error = "Transactional setting outside supported range"; return false; }
        auto& overrides = _result.Options.RuntimeBenchmark;
        if (workers) overrides.Transactional.WorkerCount = number;
        else if (option == "--transactional-prefix") overrides.Transactional.PrefixLimit = number;
        else overrides.Transactional.DonorLimit = number;
        overrides.HasTransactional = true;
        MarkRuntimeBenchmarkOverride();
        return true;
    }

    bool HandleTriangleBudget(std::string_view option, std::string_view value)
    {
        std::size_t number{};
        if (!ParseNonNegativeSize(option, value, number)) return false;
        if (number < 2 || number > 200000) { _result.Error = "Budget must be in [2, 200000]"; return false; }
        auto& overrides = _result.Options.RuntimeBenchmark;
        overrides.HasTriangleBudget = true; overrides.TriangleBudget = number;
        MarkRuntimeBenchmarkOverride();
        return true;
    }

    bool HandleRuntimeBenchmark(std::string_view, std::string_view)
    {
        _result.Options.AutomaticRuntimeBenchmark = true;
        return true;
    }

    bool HandleRuntimeBenchmarkPath(std::string_view, std::string_view value)
    {
        // 同时保留预算饱和路径的旧短名称
        static constexpr std::array<NamedValue<RuntimeBenchmarkPath>, 3> paths{{
            {"default", RuntimeBenchmarkPath::Default},
            {"budget-saturation", RuntimeBenchmarkPath::BudgetSaturation},
            {"stress", RuntimeBenchmarkPath::BudgetSaturation},
        }};
        if (!ParseNamedValue(value, paths, _result.Options.RuntimeBenchmark.Path))
        {
            _result.Error = "Invalid runtime benchmark path: " + std::string{value};
            return false;
        }

        _result.Options.RuntimeBenchmark.HasPath = true;
        MarkRuntimeBenchmarkOverride();
        return true;
    }

    bool HandlePassPolicy(std::string_view, std::string_view value)
    {
        // 历史短名称继续映射到相同预设，旧实验命令无需改写
        static constexpr std::array<NamedValue<Algorithms::TerrainLodPassPolicy>, 9> policies{{
            {"default", {}},
            {"serial-incremental", Algorithms::MakeTerrainLodSerialIncrementalPolicy()},
            {"serial", Algorithms::MakeTerrainLodSerialIncrementalPolicy()},
            {"maximum-parallel-incremental",
             Algorithms::MakeTerrainLodMaximumSafeParallelIncrementalPolicy()},
            {"maximum-parallel", Algorithms::MakeTerrainLodMaximumSafeParallelIncrementalPolicy()},
            {"parallel", Algorithms::MakeTerrainLodMaximumSafeParallelIncrementalPolicy()},
            {"serial-full", Algorithms::MakeTerrainLodSerialFullOutputPolicy()},
            {"maximum-parallel-full",
             Algorithms::MakeTerrainLodMaximumSafeParallelFullOutputPolicy()},
            {"parallel-full", Algorithms::MakeTerrainLodMaximumSafeParallelFullOutputPolicy()},
        }};
        if (!ParseNamedValue(value, policies, _result.Options.RuntimeBenchmark.PassPolicy))
        {
            _result.Error = "Invalid runtime benchmark policy: " + std::string{value};
            return false;
        }

        _result.Options.RuntimeBenchmark.HasPassPolicy = true;
        MarkRuntimeBenchmarkOverride();
        return true;
    }

    bool HandleSplitTopologyMinimum(std::string_view option, std::string_view value)
    {
        // 并行辅助阈值允许为零，负数没有业务含义
        std::size_t parsed = 0U;
        if (!ParseNonNegativeSize(option, value, parsed))
        {
            return false;
        }
        _result.Options.RuntimeBenchmark.HasSplitTopologyMinParallelCandidateCount = true;
        _result.Options.RuntimeBenchmark.SplitTopologyMinParallelCandidateCount = parsed;
        MarkRuntimeBenchmarkOverride();
        return true;
    }

    bool HandleMergeTopologyMinimum(std::string_view option, std::string_view value)
    {
        std::size_t parsed = 0U;
        if (!ParseNonNegativeSize(option, value, parsed))
        {
            return false;
        }
        _result.Options.RuntimeBenchmark.HasMergeTopologyMinParallelCandidateCount = true;
        _result.Options.RuntimeBenchmark.MergeTopologyMinParallelCandidateCount = parsed;
        MarkRuntimeBenchmarkOverride();
        return true;
    }

    bool HandleParallelTopologyTargetBuild(std::string_view option, std::string_view value)
    {
        std::size_t parsed = 0U;
        if (!ParseNonNegativeSize(option, value, parsed))
        {
            return false;
        }
        _result.Options.RuntimeBenchmark.HasParallelTopologyTargetBuild = true;
        _result.Options.RuntimeBenchmark.ParallelTopologyTargetBuild = parsed;
        MarkRuntimeBenchmarkOverride();
        return true;
    }

    bool HandleParallelTopologyPhase(std::string_view, std::string_view value)
    {
        static constexpr std::array<NamedValue<Algorithms::TerrainLodParallelTopologyPhase>, 3>
            phases{{
                {"both", Algorithms::TerrainLodParallelTopologyPhase::Both},
                {"split", Algorithms::TerrainLodParallelTopologyPhase::SplitOnly},
                {"merge", Algorithms::TerrainLodParallelTopologyPhase::MergeOnly},
            }};
        if (!ParseNamedValue(
                value,
                phases,
                _result.Options.RuntimeBenchmark.ParallelTopologyPhase))
        {
            _result.Error = "Invalid parallel topology phase: " + std::string{value};
            return false;
        }

        _result.Options.RuntimeBenchmark.HasParallelTopologyPhase = true;
        MarkRuntimeBenchmarkOverride();
        return true;
    }

    bool HandleHeightMap(std::string_view option, std::string_view value)
    {
        // 同时接受界面序号、简写名称和带分辨率的完整名称
        static constexpr std::array<NamedValue<int>, 6> heightMaps{{
            {"0", 0},
            {"test", 0},
            {"test129", 0},
            {"1", 1},
            {"peking", 1},
            {"peking513", 1},
        }};
        if (!ParseNamedValue(value, heightMaps, _result.Options.RuntimeBenchmark.HeightMapIndex))
        {
            _result.Error = "Invalid height map for " + std::string{option} + ": " +
                std::string{value};
            return false;
        }

        _result.Options.RuntimeBenchmark.HasHeightMapIndex = true;
        MarkRuntimeBenchmarkOverride();
        return true;
    }

    bool HandleTerrainSize(std::string_view option, std::string_view value)
    {
        if (!ParseFloat(option, value, _result.Options.RuntimeBenchmark.TerrainSize))
        {
            return false;
        }
        _result.Options.RuntimeBenchmark.HasTerrainSize = true;
        MarkRuntimeBenchmarkOverride();
        return true;
    }

    bool HandleHeightScale(std::string_view option, std::string_view value)
    {
        if (!ParseFloat(option, value, _result.Options.RuntimeBenchmark.HeightScale))
        {
            return false;
        }
        _result.Options.RuntimeBenchmark.HasHeightScale = true;
        MarkRuntimeBenchmarkOverride();
        return true;
    }

    bool HandleMaximumDepth(std::string_view option, std::string_view value)
    {
        if (!ParseInteger(option, value, _result.Options.RuntimeBenchmark.MaxDepth))
        {
            return false;
        }
        _result.Options.RuntimeBenchmark.HasMaxDepth = true;
        MarkRuntimeBenchmarkOverride();
        return true;
    }

    bool HandleSplitPixels(std::string_view option, std::string_view value)
    {
        if (!ParseFloat(
                option,
                value,
                _result.Options.RuntimeBenchmark.ScreenSpaceSplitThresholdPixels))
        {
            return false;
        }
        _result.Options.RuntimeBenchmark.HasScreenSpaceSplitThresholdPixels = true;
        MarkRuntimeBenchmarkOverride();
        return true;
    }

    bool HandleMergePixels(std::string_view option, std::string_view value)
    {
        if (!ParseFloat(
                option,
                value,
                _result.Options.RuntimeBenchmark.ScreenSpaceMergeThresholdPixels))
        {
            return false;
        }
        _result.Options.RuntimeBenchmark.HasScreenSpaceMergeThresholdPixels = true;
        MarkRuntimeBenchmarkOverride();
        return true;
    }

    bool HandleRemovedDistanceScale(std::string_view, std::string_view)
    {
        // 明确拒绝旧误差参数，避免用户误以为它仍然影响实验
        _result.Error =
            "--runtime-benchmark-distance-scale was removed; ROAM now uses pixel screen-space error";
        return false;
    }

    bool HandleSampleCount(std::string_view option, std::string_view value)
    {
        int parsed = 0;
        if (!ParseInteger(option, value, parsed))
        {
            return false;
        }
        // 至少两个采样点才能覆盖路径起点和终点
        _result.Options.RuntimeBenchmark.HasSampleCount = true;
        _result.Options.RuntimeBenchmark.SampleCount =
            static_cast<std::size_t>(std::max(parsed, 2));
        MarkRuntimeBenchmarkOverride();
        return true;
    }

    bool HandleWarmupCount(std::string_view option, std::string_view value)
    {
        std::size_t parsed = 0U;
        if (!ParseNonNegativeSize(option, value, parsed))
        {
            return false;
        }
        _result.Options.RuntimeBenchmark.HasWarmupSampleCount = true;
        _result.Options.RuntimeBenchmark.WarmupSampleCount = parsed;
        MarkRuntimeBenchmarkOverride();
        return true;
    }

    bool HandleOrderRotation(std::string_view option, std::string_view value)
    {
        std::size_t parsed = 0U;
        if (!ParseNonNegativeSize(option, value, parsed))
        {
            return false;
        }
        _result.Options.RuntimeBenchmark.HasAlgorithmOrderRotation = true;
        _result.Options.RuntimeBenchmark.AlgorithmOrderRotation = parsed;
        MarkRuntimeBenchmarkOverride();
        return true;
    }

    bool HandleUploadPair(std::string_view, std::string_view)
    {
        _result.Options.AutomaticRuntimeBenchmark = true;
        _result.Options.RuntimeBenchmark.EnableCpuUploadPairReplay = true;
        MarkRuntimeBenchmarkOverride();
        return true;
    }

    bool HandleUploadWarmupCount(std::string_view option, std::string_view value)
    {
        if (!ParseNonNegativeSize(
                option,
                value,
                _result.Options.RuntimeBenchmark.CpuUploadWarmupCount))
        {
            return false;
        }
        MarkRuntimeBenchmarkOverride();
        return true;
    }

    bool HandleUploadRepeatCount(std::string_view option, std::string_view value)
    {
        std::size_t parsed = 0U;
        if (!ParseNonNegativeSize(option, value, parsed) || parsed == 0U)
        {
            if (_result.Error.empty())
            {
                _result.Error = std::string{option} + " must be greater than zero";
            }
            return false;
        }
        _result.Options.RuntimeBenchmark.CpuUploadRepeatCount = parsed;
        MarkRuntimeBenchmarkOverride();
        return true;
    }

    bool HandleUploadTargetCount(std::string_view option, std::string_view value)
    {
        if (!ParseNonNegativeSize(
                option,
                value,
                _result.Options.RuntimeBenchmark.CpuUploadTargetCount))
        {
            return false;
        }
        MarkRuntimeBenchmarkOverride();
        return true;
    }

    bool HandleLegacyDuration(std::string_view option, std::string_view value)
    {
        float parsed = 0.0F;
        if (!ParseFloat(option, value, parsed))
        {
            return false;
        }
        if (!std::isfinite(parsed) || parsed <= 0.0F)
        {
            _result.Error = std::string{option} + " must be a finite positive number";
            return false;
        }

        // 旧参数仅保留兼容性，每个名义秒换算为 60 个离散采样点
        const double convertedSampleCount = static_cast<double>(parsed) * 60.0;
        if (convertedSampleCount > static_cast<double>(std::numeric_limits<int>::max()))
        {
            _result.Error = std::string{option} + " produces too many sample points";
            return false;
        }

        _result.Options.RuntimeBenchmark.HasSampleCount = true;
        _result.Options.RuntimeBenchmark.SampleCount = std::max(
            static_cast<std::size_t>(convertedSampleCount),
            static_cast<std::size_t>(2));
        MarkRuntimeBenchmarkOverride();
        return true;
    }

    bool HandleLabel(std::string_view, std::string_view value)
    {
        _result.Options.RuntimeBenchmark.Label = value;
        MarkRuntimeBenchmarkOverride();
        return true;
    }

    [[nodiscard]] bool ParseInteger(
        std::string_view option,
        std::string_view value,
        int& output)
    {
        // 保留原入口的标准库转换规则和错误文本
        try
        {
            output = std::stoi(std::string{value});
        }
        catch (const std::exception&)
        {
            _result.Error = "Invalid integer for " + std::string{option} + ": " +
                std::string{value};
            return false;
        }
        return true;
    }

    [[nodiscard]] bool ParseFloat(
        std::string_view option,
        std::string_view value,
        float& output)
    {
        // 浮点参数在应用配置阶段再按各自业务范围钳制
        try
        {
            output = std::stof(std::string{value});
        }
        catch (const std::exception&)
        {
            _result.Error = "Invalid float for " + std::string{option} + ": " +
                std::string{value};
            return false;
        }
        return true;
    }

    [[nodiscard]] bool ParseNonNegativeSize(
        std::string_view option,
        std::string_view value,
        std::size_t& output)
    {
        int parsed = 0;
        if (!ParseInteger(option, value, parsed))
        {
            return false;
        }
        if (parsed < 0)
        {
            _result.Error = std::string{option} + " must be non-negative";
            return false;
        }
        output = static_cast<std::size_t>(parsed);
        return true;
    }

    void MarkRuntimeBenchmarkOverride()
    {
        // 即使显式值等于默认值，也要把这次选择写入实验元数据
        _result.Options.HasRuntimeBenchmarkOverrides = true;
    }

    int _argumentCount{0};
    char** _argumentValues{nullptr};
    ApplicationCommandLineParseResult _result;
};
} // 匿名命名空间

ApplicationCommandLineParseResult ParseApplicationCommandLine(
    int argumentCount,
    char** argumentValues)
{
    return CommandLineParser{argumentCount, argumentValues}.Parse();
}
} // 命名空间 ParallelRoam::App
