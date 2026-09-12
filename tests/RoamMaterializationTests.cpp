#include "experiment/roam_materialization/MaterializationReference.h"
#include "experiment/roam_materialization/MaterializationPatch.h"
#include "experiment/roam_materialization/MaterializationValidation.h"
#include "experiment/roam_materialization/MaterializationDodBridge.h"
#include "experiment/roam_materialization/MaterializationExecutor.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamPipeline.h"
#include "algorithms/TerrainLodView.h"
#include "benchmark/formal/FormalCpuInput.h"
#include "experiment/formal/FormalExperimentCamera.h"
#include "experiment/formal/FormalExperimentManifest.h"

#include <iostream>
#include <atomic>
#include <barrier>
#include <limits>
#include <stdexcept>

using namespace ParallelRoam::Experiment::RoamMaterialization;

namespace
{
void Require(bool result, const char* message)
{
    if (!result) throw std::runtime_error(message);
}

std::shared_ptr<const FrozenEnvironment> Environment(int depth = MaximumDepth, float score = 10)
{
    auto environment = std::make_shared<FrozenEnvironment>();
    environment->MaxDepth = depth;
    environment->Score = [score](NodeId, const Domain&) { return score; };
    environment->Emit = [](NodeId, const Domain& domain) {
        MeshTriangle triangle{};
        const std::array vertices{domain.A, domain.B, domain.C};
        for (std::size_t i = 0; i < 3; ++i)
        {
            triangle[i * 9] = vertices[i].x; triangle[i * 9 + 2] = vertices[i].y;
            triangle[i * 9 + 4] = 1; triangle[i * 9 + 6] = vertices[i].x;
            triangle[i * 9 + 7] = vertices[i].y;
        }
        return triangle;
    };
    return environment;
}

CertifiedTarget Target(const MaterializationState& s, EventSet events)
{
    return MaterializationValidation::Certify(s, MaterializationValidation::Difference(s, std::move(events)));
}

void AddClosure(EventSet& events, NodeId id)
{
    if (id == 0 || !events.insert(id).second) return;
    AddClosure(events, MaterializationHierarchy::Parent(id));
    AddClosure(events, MaterializationHierarchy::Mate(id));
}

std::size_t Exhaustive()
{
    MaterializationExecutor executor(4);
    const auto execution = executor.Execution();
    std::vector<NodeId> domain;
    for (const auto root : {RootA, RootB})
        for (int depth = 0; depth < 3; ++depth)
            for (NodeId offset = 0; offset < (1ULL << depth); ++offset)
                domain.push_back((root << depth) + offset);
    std::vector<EventSet> cuts;
    for (std::size_t mask = 0; mask < (1ULL << domain.size()); ++mask)
    {
        EventSet events;
        for (std::size_t bit = 0; bit < domain.size(); ++bit) if ((mask >> bit) & 1U) events.insert(domain[bit]);
        try { MaterializationValidation::ValidateClosed(events, 3, 32); }
        catch (const std::logic_error&) { continue; }
        cuts.push_back(std::move(events));
    }
    std::size_t pairs = 0;
    for (const auto& initial : cuts)
    {
        MaterializationState seed(Environment(3), 32);
        MaterializationReference::Apply(seed, Target(seed, initial));
        seed.AdvanceEpoch(); seed.ConsumePending();
        for (const auto& final : cuts)
        {
            auto reference = seed, direct = seed, parallel = seed;
            std::map<NodeId, const NodeRecord*> oldAddresses;
            for (const auto id : seed.Leaves()) oldAddresses[id] = &direct.Node(id);
            const auto task = Target(seed, final);
            WorkCounters work;
            MaterializationReference::Apply(reference, task);
            MaterializationPatch::Apply(direct, task, &work);
            WorkCounters parallelWork;
            MaterializationPatch::Apply(parallel, task, &parallelWork, execution);
            MaterializationValidation::Compare(reference, direct);
            MaterializationValidation::Compare(reference, parallel);
            // 比较真实逻辑工作，诊断墙钟和线程身份不属于算法工作等价
            const auto counts = [](const WorkCounters& w) {
                return std::array{w.EventQueries, w.RecordQueries, w.ScoreEvaluations, w.SplitRechecks,
                    w.MergeRechecks, w.QueueWrites, w.RecordsCreated, w.RecordsReused, w.NeighborWrites,
                    w.SlotAllocations, w.SlotReuses, w.PendingWrites, w.PrimitiveGroups, w.PreparedEdges,
                    w.FullScanItems, w.LeafSupport, w.MergeSupport, w.DescriptorItems, w.RecordPatches};
            };
            Require(counts(work) == counts(parallelWork), "串行与并行逻辑工作不同");
            for (const auto id : seed.Leaves())
                if (direct.Leaf(id))
                {
                    Require(direct.Slots().at(id) == seed.Slots().at(id), "共同叶槽位被搬移");
                    Require(&direct.Node(id) == oldAddresses.at(id), "共同记录地址被重建");
                    Require(direct.SplitQueue().at(id) == seed.SplitQueue().at(id), "范围外细分条目被修改");
                }
            const auto k = task.Added().size() + task.Removed().size();
            Require(work.LeafSupport <= 3 * k && work.MergeSupport <= 2 * k, "支持集超界");
            Require(work.FullScanItems == 0 && work.PrimitiveGroups == 0, "直接路径进行了全域恢复或重演");
            // 同一对象继续回到旧目标，刻意不先消费上一份差分
            MaterializationReference::Apply(reference, Target(reference, initial));
            MaterializationPatch::Apply(direct, Target(direct, initial));
            MaterializationPatch::Apply(parallel, Target(parallel, initial), nullptr, execution);
            MaterializationValidation::Compare(reference, direct);
            MaterializationValidation::Compare(reference, parallel);
            reference.ConsumePending(); direct.ConsumePending(); parallel.ConsumePending();
            MaterializationValidation::Compare(reference, direct);
            MaterializationValidation::Compare(reference, parallel);
            ++pairs;
        }
    }
    std::cout << "合法切割=" << cuts.size() << ", 目标对=" << pairs << '\n';
    return pairs;
}

void BoundaryAndContinuation(const MaterializationExecution& execution)
{
    MaterializationState seed(Environment(), 20000);
    History history;
    history.Previous = {RootA, RootB}; history.Blocked = {RootA * 4};
    history.Split = {RootA * 2}; history.Merged = {RootB * 4};
    seed.SetHistory(history);
    EventSet deep;
    AddClosure(deep, RootA << 19);
    auto reference = seed, direct = seed;
    const auto task = Target(seed, deep);
    MaterializationReference::Apply(reference, task);
    MaterializationPatch::Apply(direct, task, nullptr, execution);
    MaterializationValidation::Compare(reference, direct);
    for (int transaction = 0; transaction < 3; ++transaction)
    {
        const EventSet destination = transaction == 1 ? deep : EventSet{};
        MaterializationReference::Apply(reference, Target(reference, destination));
        MaterializationPatch::Apply(direct, Target(direct, destination), nullptr, execution);
        MaterializationValidation::Compare(reference, direct);
        if (transaction == 1) { reference.ConsumePending(); direct.ConsumePending(); }
    }
    reference.AdvanceEpoch(); direct.AdvanceEpoch();
    Require(MaterializationReference::TrySplit(reference, RootA) ==
        MaterializationReference::TrySplit(direct, RootA), "续接细分不同");
    MaterializationValidation::Compare(reference, direct);

    // 另一冻结评分环境使合并可执行，避免用改变闭包目标绕开合并消费者
    MaterializationState low(Environment(3, 0), 32);
    MaterializationPatch::Apply(low, Target(low, EventSet{RootA, RootB}), nullptr, execution);
    low.AdvanceEpoch();
    Require(MaterializationReference::TryMerge(low, RootA), "低评分菱形无法继续合并");
    low.ConsumePending(); MaterializationValidation::Validate(low);
    Require(low.Events().empty(), "合并后仍保留根事件");

    MaterializationState full(Environment(), 2);
    Require(!MaterializationReference::TrySplit(full, RootA), "预算拒绝失败");
    const auto zero = Target(full, {});
    full.AdvanceEpoch();
    bool rejected = false;
    try { MaterializationPatch::Apply(full, zero, nullptr, execution); } catch (const std::invalid_argument&) { rejected = true; }
    Require(rejected && full.Usable(), "过期认证未在修改前拒绝");
    for (const EventSet bad : {EventSet{RootA}, EventSet{RootA * 4}, EventSet{RootA, RootB}})
    {
        rejected = false;
        try { static_cast<void>(Target(full, bad)); } catch (const std::exception&) { rejected = true; }
        Require(rejected && full.Events().empty(), "非法目标修改了旧状态");
    }
    rejected = false;
    try { MaterializationState invalid(Environment(3, std::numeric_limits<float>::quiet_NaN()), 32); }
    catch (const std::exception&) { rejected = true; }
    Require(rejected, "NaN 评分未拒绝");
}

void PrerequisitesAndLifetime(const MaterializationExecution& execution)
{
    MaterializationState state(Environment(), 8);
    MaterializationPatch::Apply(state, Target(state, {RootA, RootB}), nullptr, execution);
    state.AdvanceEpoch();
    Require(MaterializationHierarchy::Mate(4) == 7 && state.Leaf(2) && state.Leaf(3), "手算前置几何不对应");
    Require(MaterializationReference::TrySplit(state, 2), "边界前置无法执行");
    Require(!MaterializationReference::TrySplit(state, 4), "缺少伙伴叶时不应执行");
    Require(MaterializationReference::TrySplit(state, 3), "另一边界前置无法执行");
    Require(MaterializationReference::TrySplit(state, 4) && state.Leaves().size() == 8, "完整菱形未填满硬预算");
    Require(!MaterializationReference::TrySplit(state, 8), "预算已满仍能继续细分");
    MaterializationValidation::Validate(state);

    // 确认后再删除，旧槽位要等第二次确认才可复用；记录缓存则始终保留身份
    state.ConsumePending();
    const auto cached = state.Nodes().size();
    MaterializationPatch::Apply(state, Target(state, {}), nullptr, execution);
    Require(state.Slots().size() > state.Leaves().size() && !state.Pending().empty(), "退役槽位过早释放");
    state.ConsumePending();
    WorkCounters reuse;
    MaterializationPatch::Apply(state, Target(state, {RootA, RootB}), &reuse, execution);
    Require(reuse.RecordsCreated == 0 && reuse.SlotReuses == 4 && state.Nodes().size() == cached,
        "消费确认后没有复用槽位和记录");
    MaterializationValidation::Validate(state);

    // H 的迟滞必须实际参与判断，K/G 则覆盖它；S 保留合并资格但抑制执行
    MaterializationState hysteresis(Environment(3, 3), 32);
    Require(!hysteresis.ShouldSplit(RootA), "没有历史时误入迟滞区间");
    hysteresis.SetHistory(History{{RootA}, {}, {}, {}});
    Require(hysteresis.ShouldSplit(RootA), "历史未恢复迟滞资格");
    hysteresis.SetHistory(History{{RootA}, {RootA}, {}, {RootB}});
    Require(!hysteresis.ShouldSplit(RootA) && !hysteresis.ShouldSplit(RootB), "抑制未覆盖迟滞");
    MaterializationPatch::Apply(hysteresis, Target(hysteresis, {RootA, RootB}), nullptr, execution);
    Require(hysteresis.MergeQueue().at(RootA).Suppressed, "新细分标记没有抑制合并");

    const auto valid = MaterializationValidation::Difference(state, {});
    for (int mutation = 0; mutation < 3; ++mutation)
    {
        auto invalid = valid;
        if (mutation == 0) invalid.Removed.push_back(RootA);
        if (mutation == 1) invalid.Added.push_back(RootA);
        if (mutation == 2) invalid.Removed.clear();
        bool rejected = false;
        try { static_cast<void>(MaterializationValidation::Certify(state, invalid)); }
        catch (const std::exception&) { rejected = true; }
        Require(rejected && state.Usable(), "重复、冲突或缺失差分未被拒绝");
    }
}

void ExecutionFailure()
{
    MaterializationExecutor executor(4);
    auto execution = executor.Execution();
    std::barrier together(4);
    std::array<std::thread::id, 4> ids;
    std::atomic<int> finished{0};
    bool rejected = false;
    // 只在执行器夹具中同步，真实算法不通过等待制造线程参与证据
    try
    {
        execution.Dispatch(4, [&](std::size_t i) {
            ids[i] = std::this_thread::get_id();
            together.arrive_and_wait();
            ++finished;
            if (i == 0) throw std::runtime_error("受控任务失败");
        });
    }
    catch (const std::runtime_error&) { rejected = true; }
    Require(rejected && finished == 4 && std::set(ids.begin(), ids.end()).size() == 4,
        "执行器没有结束全部真实线程任务");

    auto environment = std::make_shared<FrozenEnvironment>(*Environment(4));
    std::atomic<int> failure{0};
    environment->Score = [&](NodeId, const Domain&) {
        if (failure == 1) throw std::runtime_error("受控评分失败");
        return failure == 2 ? std::numeric_limits<float>::quiet_NaN() : 10.0F;
    };
    MaterializationState seed(environment, 32);
    const auto target = Target(seed, {RootA, RootB});
    for (const int mode : {1, 2})
    {
        auto state = seed;
        failure = mode; rejected = false;
        try { MaterializationPatch::Apply(state, target, nullptr, execution); }
        catch (const std::exception&) { rejected = true; }
        Require(rejected && !state.Usable(), "失败评分仍发布了可用状态");
        failure = 0;
    }
    // 模拟执行边界部分提交后失败；先等待已提交任务，再把异常交给事务
    std::atomic<bool> completed{false};
    MaterializationExecution partial{4, [&](std::size_t, const auto& task) {
        std::thread submitted([&] { task(0); completed = true; });
        submitted.join();
        throw std::runtime_error("受控提交失败");
    }};
    auto state = seed; rejected = false;
    try { MaterializationPatch::Apply(state, target, nullptr, partial); }
    catch (const std::runtime_error&) { rejected = true; }
    Require(rejected && completed && !state.Usable(), "部分提交失败未结束任务或未废弃状态");
    WorkCounters zero;
    MaterializationPatch::Apply(seed, Target(seed, {}), &zero, execution);
    Require(zero.Phases[0].Items == 0 && zero.DescriptorItems == 0, "空差分仍产生任务");
}

void NaturalInputs(const char* scenariosPath, const char* camerasPath)
{
    using namespace ParallelRoam;
    using namespace Experiment::Formal;
    using namespace Algorithms::DataOrientedRoam;
    const auto scenarios = LoadScenarioManifest(scenariosPath, std::filesystem::current_path(),
        {"test129-a-b4096", "peking547-a-b20000"});
    const auto cameras = LoadCameraManifest(camerasPath, scenarios);
    for (const auto& scenario : scenarios)
    {
        Terrain::HeightMap height;
        std::string error;
        Require(height.LoadFromFile(scenario.HeightMapPath, &error), error.c_str());
        const auto settings = Benchmark::Formal::MakeCpuSourceSettings(scenario);
        DataOrientedRoamPipeline pipeline;
        bool captured = false;
        for (const auto& camera : cameras)
        {
            if (camera.ScenarioId != scenario.ScenarioId || camera.SampleIndex > 14) continue;
            static_cast<void>(pipeline.BuildWithPassObserver(height, scenario.Settings.TerrainSize,
                scenario.Settings.HeightScale, BuildCameraView(camera), settings,
                [&](const auto& source, Algorithms::TerrainLodPassId pass) {
                    if (camera.SampleIndex != 14 || pass != Algorithms::TerrainLodPassId::SplitTopology) return;
                    std::cout << "导入 " << scenario.ScenarioId << std::endl;
                    auto task = MaterializationDodBridge::Capture(source);
                    const auto target = MaterializationValidation::Certify(*task.Initial, task.Request);
                    auto reference = *task.Initial, direct = *task.Initial;
                    WorkCounters work;
                    MaterializationReference::Apply(reference, target);
                    MaterializationPatch::Apply(direct, target, &work);
                    MaterializationValidation::Compare(reference, direct);
                    for (const std::size_t workers : {2U, 4U})
                    {
                        MaterializationExecutor executor(workers);
                        auto parallel = *task.Initial;
                        MaterializationPatch::Apply(parallel, target, nullptr, executor.Execution());
                        MaterializationValidation::Compare(reference, parallel);
                        auto continued = reference;
                        continued.ConsumePending(); parallel.ConsumePending();
                        continued.AdvanceEpoch(); parallel.AdvanceEpoch();
                        MaterializationValidation::Compare(continued, parallel);
                    }
                    reference.ConsumePending(); direct.ConsumePending();
                    reference.AdvanceEpoch(); direct.AdvanceEpoch();
                    MaterializationValidation::Compare(reference, direct);
                    std::cout << "来源=" << task.SourceHash << ", k=" << task.Request.Added.size() +
                        task.Request.Removed.size() << ", 旧 Pending=" << task.Initial->Pending().size() << '\n';
                    captured = true;
                }));
        }
        Require(captured, "缺少冻结自然样本");
    }
}
}

int main(int argc, char** argv)
{
    try
    {
        for (const std::size_t workers : {1U, 2U, 4U})
        {
            MaterializationExecutor executor(workers);
            BoundaryAndContinuation(executor.Execution());
            PrerequisitesAndLifetime(executor.Execution());
        }
        ExecutionFailure();
        Require(Exhaustive() != 0, "有限域没有测试对象");
        Require(argc == 1 || argc == 3, "需要同时给出场景和相机清单");
        if (argc == 3) NaturalInputs(argv[1], argv[2]);
        std::cout << "完整状态、续接与支持集核查完成\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n'; return 1;
    }
}
