#include "experiment/roam_materialization/MaterializationReference.h"
#include "experiment/roam_materialization/MaterializationPatch.h"
#include "experiment/roam_materialization/MaterializationValidation.h"
#include "experiment/roam_materialization/MaterializationDodBridge.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamPipeline.h"
#include "algorithms/TerrainLodView.h"
#include "benchmark/formal/FormalCpuInput.h"
#include "experiment/formal/FormalExperimentCamera.h"
#include "experiment/formal/FormalExperimentManifest.h"

#include <iostream>
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
            auto reference = seed, direct = seed;
            std::map<NodeId, const NodeRecord*> oldAddresses;
            for (const auto id : seed.Leaves()) oldAddresses[id] = &direct.Node(id);
            const auto task = Target(seed, final);
            WorkCounters work;
            MaterializationReference::Apply(reference, task);
            MaterializationPatch::Apply(direct, task, &work);
            MaterializationValidation::Compare(reference, direct);
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
            MaterializationValidation::Compare(reference, direct);
            reference.ConsumePending(); direct.ConsumePending();
            MaterializationValidation::Compare(reference, direct);
            ++pairs;
        }
    }
    std::cout << "合法切割=" << cuts.size() << ", 目标对=" << pairs << '\n';
    return pairs;
}

void BoundaryAndContinuation()
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
    MaterializationPatch::Apply(direct, task);
    MaterializationValidation::Compare(reference, direct);
    for (int transaction = 0; transaction < 3; ++transaction)
    {
        const EventSet destination = transaction == 1 ? deep : EventSet{};
        MaterializationReference::Apply(reference, Target(reference, destination));
        MaterializationPatch::Apply(direct, Target(direct, destination));
        MaterializationValidation::Compare(reference, direct);
        if (transaction == 1) { reference.ConsumePending(); direct.ConsumePending(); }
    }
    reference.AdvanceEpoch(); direct.AdvanceEpoch();
    Require(MaterializationReference::TrySplit(reference, RootA) ==
        MaterializationReference::TrySplit(direct, RootA), "续接细分不同");
    MaterializationValidation::Compare(reference, direct);

    // 另一冻结评分环境使合并可执行，避免用改变闭包目标绕开合并消费者
    MaterializationState low(Environment(3, 0), 32);
    MaterializationPatch::Apply(low, Target(low, EventSet{RootA, RootB}));
    low.AdvanceEpoch();
    Require(MaterializationReference::TryMerge(low, RootA), "低评分菱形无法继续合并");
    low.ConsumePending(); MaterializationValidation::Validate(low);
    Require(low.Events().empty(), "合并后仍保留根事件");

    MaterializationState full(Environment(), 2);
    Require(!MaterializationReference::TrySplit(full, RootA), "预算拒绝失败");
    const auto zero = Target(full, {});
    full.AdvanceEpoch();
    bool rejected = false;
    try { MaterializationPatch::Apply(full, zero); } catch (const std::invalid_argument&) { rejected = true; }
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

void PrerequisitesAndLifetime()
{
    MaterializationState state(Environment(), 8);
    MaterializationPatch::Apply(state, Target(state, {RootA, RootB}));
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
    MaterializationPatch::Apply(state, Target(state, {}));
    Require(state.Slots().size() > state.Leaves().size() && !state.Pending().empty(), "退役槽位过早释放");
    state.ConsumePending();
    WorkCounters reuse;
    MaterializationPatch::Apply(state, Target(state, {RootA, RootB}), &reuse);
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
    MaterializationPatch::Apply(hysteresis, Target(hysteresis, {RootA, RootB}));
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
        BoundaryAndContinuation();
        PrerequisitesAndLifetime();
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
