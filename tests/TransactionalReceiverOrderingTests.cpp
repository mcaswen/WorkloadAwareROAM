#include "algorithms/greedy_transactional_lod/TransactionalBoundaryRefinement.h"
#include "algorithms/greedy_transactional_lod/TransactionalPipeline.h"
#include "experiment/greedy_transactional_lod/TransactionalValidation.h"
#include "tools/CpuTaskExecutor.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace ParallelRoam::Algorithms::GreedyTransactionalLod;
using ParallelRoam::Algorithms::TransactionalReceiverOrder;
using ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalValidation;

void Require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

template<class Action> void Throws(Action&& action)
{
    bool rejected = false;
    try
    {
        action();
    }
    catch (const std::exception&)
    {
        rejected = true;
    }
    Require(rejected, "应拒绝的操作未拒绝");
}

/// <summary>
/// 左侧小投影跨度区域包含参考峰值，下边大跨度面由密度项主导
/// 所有旧点为零高，换高度投影系数不会改变密度项
/// </summary>
InitialMesh Fan(TransactionalReceiverOrder order)
{
    InitialMesh input;
    input.Config.Budget = 16;
    input.Config.PrefixLimit = 2;
    input.Config.PreserveSurvivingHeights = true;
    input.Config.EnableBoundaryRefinement = true;
    input.Config.ReceiverOrder = order;
    input.Vertices = {{0, {0, 0, 0}}, {1, {1, 0, 0}}, {2, {1, 1, 0}}, {3, {0, 1, 0}}, {4, {.5, .5, 0}}};
    input.Faces = {{0, {0, 1, 4}}, {1, {1, 2, 4}}, {2, {2, 3, 4}}, {3, {3, 0, 4}}};
    input.Source = {5, 5, std::vector<std::uint16_t>(25, 0)};
    input.Source.Values[10] = 4000;
    return input;
}

double Error(const TransactionalSamples& samples, Slot slot)
{
    double maximum = 0;
    for (const auto sample : samples.FaceSamples(slot))
    {
        const auto value = samples.Value(sample);
        if (value.Visible)
        {
            maximum = std::max(maximum, value.ErrorSquared);
        }
    }
    return maximum;
}

/// <summary>
/// 独立全排序由闭面样本和值域资格重建，不调用生产键构造函数
/// 同时检查完整尾部，不能只比较碰巧相同的首项
/// </summary>
void Oracle(const TransactionalState& state, const TransactionalSamples& samples)
{
    std::vector<PriorityKey> expected;
    for (const auto slot : state.ActiveFaces())
    {
        const double priority = samples.PrioritySquared(slot);
        if (!std::isfinite(priority) || priority <= state.Config().SplitPixels * state.Config().SplitPixels)
        {
            continue;
        }
        const double value = state.Config().ReceiverOrder == TransactionalReceiverOrder::ErrorFirst ?
            Error(samples, slot) : priority;
        expected.emplace_back(-value, state.Face(slot).Id, slot);
    }
    std::sort(expected.begin(), expected.end());
    std::vector<Slot> slots;
    for (const auto& key : expected)
    {
        slots.push_back(std::get<2>(key));
    }
    Require(samples.Raw() == slots, "实际接收全序不同于独立排序");
    Require(samples.Prefix(1000) == slots && samples.RawCount() == slots.size(), "不足前缀或人口计数错误");
}

void PopulationAndView()
{
    auto input = Fan(TransactionalReceiverOrder::Composite);
    TransactionalPipeline composite(input);
    input.Config.ReceiverOrder = TransactionalReceiverOrder::ErrorFirst;
    TransactionalPipeline errorFirst(input);
    WorkLedger work;
    composite.Initialize(work);
    errorFirst.Initialize(work);
    Oracle(composite.State(), composite.Samples());
    Oracle(errorFirst.State(), errorFirst.Samples());
    Require(composite.Samples().Raw().front() == 0 && errorFirst.Samples().Raw().front() == 3,
        "未构成密度与纯误差顺序相反的夹具");
    auto a = composite.Samples().Raw();
    auto b = errorFirst.Samples().Raw();
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    Require(a == b && composite.Samples().DonorPool(100) == errorFirst.Samples().DonorPool(100),
        "排序改变同状态人口或捐赠池");
    for (const auto slot : a)
    {
        Require(composite.Samples().PrioritySquared(slot) == errorFirst.Samples().PrioritySquared(slot), "复合分数改变");
    }

    TransactionalState state(input);
    TransactionalSamples samples(input.Source);
    samples.Refresh(state, work);
    auto view = input.Config;
    view.Matrix[5] = .5;
    const double oldError = Error(samples, 3);
    const auto oldRaw = samples.Raw();
    {
        auto prepared = samples.PrepareView(state, view, work);
        Require(prepared.Priority[3] == samples.PrioritySquared(3), "夹具未保持复合分数");
        const auto keys = prepared.Order.Prefix(100);
        Require(std::get<0>(keys.front()) == -oldError * .25, "P 未变时 E 键没有更新");
    }
    Require(samples.Raw() == oldRaw && Error(samples, 3) == oldError, "放弃准备改变 live 状态");
    errorFirst.SetView(view, work);
    Oracle(errorFirst.State(), errorFirst.Samples());
    Require(Error(errorFirst.Samples(), 3) == oldError * .25, "视图发布未更新误差");
    auto changed = view;
    changed.ReceiverOrder = TransactionalReceiverOrder::Composite;
    Throws([&] { errorFirst.SetView(changed, work); });

    // 零误差同分按身份排序，不能偷偷加入复合优先级作为次级键
    input.Source.Values.assign(25, 0);
    TransactionalPipeline flat(input);
    flat.Initialize(work);
    Require(flat.Samples().Raw() == std::vector<Slot>({0, 1, 2, 3}), "E 同分未按稳定身份排序");
    input.Config.Matrix[7] = 2;
    TransactionalPipeline noVisibleSupport(input);
    noVisibleSupport.Initialize(work);
    Require(noVisibleSupport.Samples().Raw() == std::vector<Slot>({0, 1, 2, 3}),
        "无可见样本的有限密度候选被删除");
    for (Slot sample = 0; sample < noVisibleSupport.Samples().SampleCount(); ++sample)
    {
        Require(!noVisibleSupport.Samples().Value(sample).Visible, "空可见支持夹具仍有可见样本");
    }
    input.Config.Matrix[7] = 0;
    input.Config.SplitPixels = 10;
    TransactionalPipeline threshold(input);
    threshold.Initialize(work);
    Require(threshold.Samples().RawCount() == 0, "恰好等于门槛仍进入人口");
    input.Config.SplitPixels = 0;
    input.Config.Matrix[15] = -1;
    TransactionalPipeline invisible(input);
    invisible.Initialize(work);
    Require(invisible.Samples().RawCount() == 0, "无穷复合评分未排除");
    input.Config.ReceiverOrder = static_cast<TransactionalReceiverOrder>(99);
    Throws([&] { TransactionalState invalid(input); });
}

/// <summary>
/// 用实际单侧事务覆盖删除、新面槽复用、共享接口和连续 Pending
/// 准备配额失败与过期批次都必须保留已发布索引
/// </summary>
void LocalContinuation()
{
    const auto input = Fan(TransactionalReceiverOrder::ErrorFirst);
    TransactionalPipeline pipeline(input);
    WorkLedger work;
    pipeline.Initialize(work);
    ParallelRoam::Terrain::TerrainMeshData mirror;
    TransactionalValidation::Consume(pipeline.ConsumeMesh(), mirror);
    for (Slot root : {3U, 0U})
    {
        const auto edge = root == 3 ? EdgeKey(3, 0) : EdgeKey(0, 1);
        auto proposal = TransactionalBoundaryRefinement::Construct(pipeline.State(), input.Source, root,
            edge, pipeline.State().NextVertexId());
        Require(proposal.Reason.empty(), "续接夹具没有合法单侧几何");
        // 此处隔离排序修复；真实动态认证由边界专项与自然轨迹验证
        proposal.Reason = "certified";
        CertifiedBatch batch;
        batch.Version = pipeline.State().Version();
        batch.Exchanges.push_back({proposal, {}, false, ExchangeKind::BoundaryRefinement});
        const auto before = pipeline.Samples().Raw();
        WorkLedger expired;
        expired.Deadline = std::chrono::steady_clock::now() - std::chrono::seconds(1);
        Throws([&] { pipeline.Apply(batch, expired); });
        Require(pipeline.Samples().Raw() == before, "失败准备改变接收索引");
        pipeline.Apply(batch, work);
        Oracle(pipeline.State(), pipeline.Samples());
        TransactionalValidation::Samples(pipeline.State(), pipeline.Samples(), work);
        TransactionalValidation::Validate(pipeline.State());
        Throws([&] { pipeline.Apply(batch, work); });
    }
    const auto pending = pipeline.ConsumeMesh();
    Require(!pending.Full && !pending.Vertices.empty(), "连续修改没有增量 Pending");
    TransactionalValidation::Consume(pending, mirror);
    TransactionalValidation::Mesh(pipeline.State(), mirror);
}

void Threads()
{
    const auto input = Fan(TransactionalReceiverOrder::ErrorFirst);
    ParallelRoam::Tools::CpuTaskExecutor executor(8);
    TransactionalPipeline serial(input);
    TransactionalPipeline parallel(input, {8, false,
        [&](auto count, const auto& task) { executor.Dispatch(count, task); }});
    for (int round = 0; round < 3; ++round)
    {
        WorkLedger first, second;
        auto view = input.Config;
        view.Matrix[5] = 1 + round * .1;
        serial.SetView(view, first);
        parallel.SetView(view, second);
        Require(serial.Samples().Raw() == parallel.Samples().Raw(), "同政策线程改变意图顺序");
        const auto a = serial.Update(first);
        const auto b = parallel.Update(second);
        Require(a.Executed == b.Executed && a.FreeExecuted == b.FreeExecuted && a.Exchanges.size() == b.Exchanges.size(),
            "同政策线程改变事务选择");
        Require(TransactionalValidation::Equivalent(serial.State(), parallel.State()), "同政策线程改变最终状态");
        Oracle(parallel.State(), parallel.Samples());
    }
}
}

int main()
{
    try
    {
        PopulationAndView();
        LocalContinuation();
        Threads();
        std::cout << "接收资格、纯误差全序和局部续接核查完成\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
