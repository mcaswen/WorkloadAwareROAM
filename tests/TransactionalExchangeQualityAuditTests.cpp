#include "experiment/greedy_transactional_lod/TransactionalExchangeQualityAudit.h"
#include "algorithms/greedy_transactional_lod/TransactionalPipeline.h"

#include <fstream>
#include <iostream>
#include <sstream>

namespace
{
using namespace ParallelRoam::Algorithms::GreedyTransactionalLod;
using ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalExchangeQualityAudit;

void Require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

std::string Read(const std::filesystem::path& path)
{
    std::ifstream input(path);
    std::ostringstream result;
    result << input.rdbuf();
    return result.str();
}

/// <summary>
/// 单位方形的两面共享对角线，所有样本均为零残差
/// 导出空批和恒等提案能独立检查闭支持去重，不依赖生产 Fit 成功
/// </summary>
InitialMesh Square()
{
    InitialMesh input;
    input.Config.Budget = 8;
    input.Source = {3, 3, std::vector<std::uint16_t>(9, 0)};
    input.Vertices = {{0, {0, 0, 0}}, {1, {1, 0, 0}}, {2, {1, 1, 0}}, {3, {0, 1, 0}}};
    input.Faces = {{0, {0, 1, 2}}, {1, {0, 2, 3}}};
    return input;
}
}

int main()
{
    try
    {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        // 与其他测试隔离，失败时保留自己的导出证据以便定位
        const auto output = std::filesystem::temp_directory_path() / ("roam-exchange-audit-" + std::to_string(suffix));
        TransactionalPipeline pipeline(Square());
        WorkLedger work;
        pipeline.Initialize(work);
        const auto& state = pipeline.State();
        const auto& samples = pipeline.Samples();
        const auto version = state.Version();
        const auto oldPrefix = samples.Prefix(64);
        const auto oldSample = samples.Value(0);
        // 空批仍是有效观察，不能为了凑保留率而丢弃零事务状态
        CertifiedBatch batch;
        batch.Version = version;
        TransactionalExchangeQualityAudit::Capture(state, samples, batch, 0, output);
        Require(Read(output / "exchange-quality-0.json").find("\"approved\":0") != std::string::npos,
            "空批被丢弃");

        Exchange exchange;
        exchange.Receiver.Kind = 'R';
        exchange.Receiver.Root = 0;
        exchange.Receiver.Support = {0, 1};
        // 恒等几何只测试采集边界，不伪装成通过生产质量目标的提案
        exchange.Receiver.Faces = {{0, 1, 2}, {0, 2, 3}};
        for (const auto& [id, point] : Square().Vertices)
        {
            exchange.Receiver.Points.emplace(id, point);
        }
        batch.Exchanges.push_back(exchange);
        TransactionalExchangeQualityAudit::Capture(state, samples, batch, 1, output);
        const auto document = Read(output / "exchange-quality-1.json");
        // 两面的闭支持并集恰好覆盖整幅小 Q，对角线贡献只能出现一次
        const auto unique = "\"sampleRecords\":" + std::to_string(samples.SampleCount());
        Require(document.find(unique) != std::string::npos, "闭支持共享样本没有去重");
        Require(document.find("\"donor\":null") != std::string::npos, "Free/R 被强加 donor");
        // const Proposal 含可变证书指针，因此还要检查观察未偷偷填充缓存
        Require(!batch.Exchanges.front().Receiver.HeightProof, "采集器修改了可变证书缓存");
        Require(state.Version() == version && state.FaceCount() == 2 && samples.Prefix(64) == oldPrefix &&
            samples.Value(0).MeshHeight == oldSample.MeshHeight, "审计修改了持久状态");

        // 上限不能把部分批次伪装成完整记录；失败也不应修改活状态
        TransactionalExchangeQualityAudit::Capture(state, samples, batch, 2, output, 1);
        const auto censored = Read(output / "exchange-quality-2.json");
        Require(censored.find("censored-samples") != std::string::npos &&
            censored.find("\"exchanges\":[]") != std::string::npos, "删失仍导出部分交换");
        bool rejected = false;
        // 文件身份和批次代际分别验证，不让重复输出覆盖前一次成功记录
        try
        {
            TransactionalExchangeQualityAudit::Capture(state, samples, batch, 1, output);
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        Require(rejected, "重复证据被覆盖");
        ++batch.Version;
        // 过期批次必须在任何导出前被拒绝，不能在错误状态上收集貌似有效证据
        rejected = false;
        try
        {
            TransactionalExchangeQualityAudit::Capture(state, samples, batch, 3, output);
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        Require(rejected && state.Version() == version, "过期批次未被拒绝");
        // 只清理本测试独占创建的临时目录，不接受外部路径
        std::filesystem::remove_all(output);
        std::cout << "exchange quality audit fixtures passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
