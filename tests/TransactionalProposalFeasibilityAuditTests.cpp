#include "experiment/greedy_transactional_lod/TransactionalProposalFeasibilityAudit.h"
#include "algorithms/greedy_transactional_lod/TransactionalPipeline.h"

#include <boost/property_tree/json_parser.hpp>
#include <fstream>
#include <iostream>

namespace
{
using namespace ParallelRoam::Algorithms::GreedyTransactionalLod;
using ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalProposalFeasibilityAudit;

void Require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

InitialMesh Square()
{
    // 两面共享一条对角线，提供可独立核对的闭样本并集
    InitialMesh input;
    input.Config.Budget = 8;
    input.Config.PreserveSurvivingHeights = true;
    input.Config.ReceiverOrder = ParallelRoam::Algorithms::TransactionalReceiverOrder::ErrorFirst;
    input.Config.QualityPolicy = ParallelRoam::Algorithms::TransactionalQualityPolicy::PointwiseTarget;
    // 平地消除几何改善自由度，合法构造与接收进展因此成为两个独立检查
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
        const auto directory = std::filesystem::temp_directory_path() /
            ("roam-proposal-feasibility-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(directory);
        // 每次夹具独占输出，不复用上次结果掩盖缺少捕获的错误
        const auto spec = directory / "spec.json";
        std::ofstream(spec) << "{\"protocol\":\"qpc04g-v1\",\"requests\":[{\"frame\":0,\"roots\":[0]}]}";
        TransactionalPipeline pipeline(Square());
        WorkLedger work;
        pipeline.Initialize(work);
        // 同时监视代次、逻辑几何、前缀及样本；单看面数不能排除只读接口泄漏
        const auto version = pipeline.State().Version();
        const auto prefix = pipeline.Samples().Prefix(8);
        const auto value = pipeline.Samples().Value(0);
        TransactionalProposalFeasibilityAudit audit(spec, 1);
        audit.Observe(pipeline.State(), pipeline.Samples(), 0, directory / "capture");
        boost::property_tree::ptree record;
        boost::property_tree::read_json((directory / "capture/proposal-0-0-0.json").string(), record);
        // 共享对角线的闭支持只计一次；不要求平地必须产生有进展事务
        Require(record.get<std::string>("status") == "complete", "捕获未完成");
        Require(record.get_child("samples").size() == pipeline.Samples().SampleCount(), "闭支持遗漏或重复");
        Require(record.get<std::string>("oldReason") == "below_progress_margin", "旧路径理由改变");
        Require(pipeline.State().Version() == version && pipeline.State().FaceCount() == 2 &&
            pipeline.Samples().Prefix(8) == prefix && pipeline.Samples().Value(0).MeshHeight == value.MeshHeight,
            "诊断修改了生产状态");
        bool rejected = false;
        // 规划中的机会计数是半开范围，frameCount=0时frame0也必须越界
        try
        {
            TransactionalProposalFeasibilityAudit invalid(spec, 0);
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        Require(rejected, "越界机会没有拒绝");
        // 合法范围但错误几何绑定，必须在调用质量认证之前拒绝
        boost::property_tree::ptree witness, entry, entries;
        witness.put("sourceFile", (directory / "capture/feasibility-source.json").string());
        // 使用真实源内容，只破坏局部输入绑定，避免被更早的源错误遮蔽测试
        entry.put("frame", 0);
        entry.put("root", 0);
        entry.put("ordinal", 0);
        entry.put("height", 0);
        entry.put("binding", "wrong-input");
        entries.push_back({"", entry});
        witness.add_child("entries", entries);
        const auto witnessFile = directory / "witness.json";
        boost::property_tree::write_json(witnessFile.string(), witness);
        boost::property_tree::ptree config;
        boost::property_tree::read_json(spec.string(), config);
        config.put("witnessFile", witnessFile.string());
        boost::property_tree::write_json(spec.string(), config);
        rejected = false;
        try
        {
            TransactionalProposalFeasibilityAudit invalid(spec, 1);
            invalid.Observe(pipeline.State(), pipeline.Samples(), 0, directory / "invalid");
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        Require(rejected && pipeline.State().Version() == version, "过期见证未拒绝或修改了状态");
        // 绑定正确仍不保证有进展；平地高度应走完整认证后给出语义拒绝
        entries.clear();
        entry.put("binding", record.get<std::string>("binding"));
        entries.push_back({"", entry});
        witness.put_child("entries", entries);
        boost::property_tree::write_json(witnessFile.string(), witness);
        TransactionalProposalFeasibilityAudit valid(spec, 1);
        // 即使外部给定高度不被接受，也应留下可审计结果而非静默跳过见证
        valid.Observe(pipeline.State(), pipeline.Samples(), 0, directory / "verified");
        boost::property_tree::ptree checked;
        boost::property_tree::read_json((directory / "verified/proposal-0-0-0.json").string(), checked);
        Require(checked.get_child("verifiedWitnesses").size() == 1, "绑定正确的见证未被复核");
        // 不依赖某一种数值过滤分支的错误字符串，仅约束不能通过接收进展
        Require(checked.get_child("verifiedWitnesses").front().second.get<std::string>("pointwiseReason") != "certified",
            "无进展平地被误认证");
        // 仅清理本夹具自建目录，失败证据在异常分支保留
        std::filesystem::remove_all(directory);
        std::cout << "proposal feasibility fixtures passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
