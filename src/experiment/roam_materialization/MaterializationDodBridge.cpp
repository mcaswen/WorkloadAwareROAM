#include "experiment/roam_materialization/MaterializationDodBridge.h"

#include "experiment/roam_materialization/MaterializationValidation.h"
#include "algorithms/RoamGeometry.h"
#include "algorithms/RoamScreenError.h"
#include "algorithms/TerrainLodView.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamPassInput.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamPassMeasurement.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamPassExecution.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamPassEvidence.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"

#include <chrono>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>

namespace ParallelRoam::Experiment::RoamMaterialization
{
namespace
{
namespace Dod = Algorithms::DataOrientedRoam;
using Clock = std::chrono::steady_clock;
double Elapsed(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

void Require(bool value, const std::string& message)
{
    if (!value) throw std::runtime_error("DOD 导入：" + message);
}

// 静态副本不持有节点池、线程池或来源引用；新记录也能按路径查询相同误差树
struct SourceEnvironment
{
    Terrain::HeightMap Height;
    std::array<std::vector<float>, 2> Variance;
    glm::mat4 View;
    std::array<glm::vec4, 6> Planes;
    std::uint32_t Width, HeightPixels;
    float Size, Scale;
};

std::shared_ptr<const FrozenEnvironment> Freeze(const Dod::DataOrientedRoamState& s,
    std::size_t& bytes)
{
    Require(s.HeightMap && s.HeightMap->IsValid(), "没有可拥有的高度图");
    auto data = std::make_shared<SourceEnvironment>(SourceEnvironment{
        *s.HeightMap, s.VarianceTrees, s.ViewProjection, s.FrustumPlanes,
        s.DrawableWidth, s.DrawableHeight, s.TerrainSize, s.HeightScale});
    bytes = sizeof(float) * (static_cast<std::size_t>(data->Height.Width()) *
        static_cast<std::size_t>(data->Height.Height()) + data->Variance[0].size() + data->Variance[1].size());
    auto environment = std::make_shared<FrozenEnvironment>();
    environment->MaxDepth = s.TopologyMaxDepth;
    environment->SplitThreshold = s.Settings.SplitThreshold;
    environment->MergeThreshold = s.Settings.MergeThreshold;
    environment->Score = [data](NodeId id, const Domain& domain) {
        const auto depth = MaterializationHierarchy::Depth(id);
        const bool second = (id >> depth) == RootB;
        const auto heap = second ? (NodeId{1} << depth) + id - (RootB << depth) : id;
        const auto& tree = data->Variance[second ? 1 : 0];
        Require(heap > 0 && heap <= tree.size(), "缺失误差项，路径=" + std::to_string(id));
        const std::array<glm::vec3, 3> triangle{
            Algorithms::Roam::DomainToWorld(data->Height, domain.A, data->Size, data->Scale),
            Algorithms::Roam::DomainToWorld(data->Height, domain.B, data->Size, data->Scale),
            Algorithms::Roam::DomainToWorld(data->Height, domain.C, data->Size, data->Scale)};
        return Algorithms::Roam::ComputeScreenErrorScore({triangle, tree[heap - 1] * data->Scale,
            data->View, data->Planes[static_cast<std::size_t>(Algorithms::TerrainLodFrustumPlane::Near)],
            data->Planes, data->Width, data->HeightPixels});
    };
    environment->Emit = [data](NodeId, const Domain& domain) {
        MeshTriangle result{};
        const std::array<glm::vec2, 3> points{domain.A, domain.B, domain.C};
        for (std::size_t i = 0; i < points.size(); ++i)
        {
            const auto uv = points[i];
            const auto sample = Algorithms::Roam::SampleTerrainWorld(data->Height, uv, data->Size, data->Scale);
            const auto normal = Algorithms::Roam::SampleHeightGradientNormal(data->Height, uv, data->Size, data->Scale);
            const std::array<float, 9> values{sample.Position.x, sample.Position.y, sample.Position.z,
                normal.x, normal.y, normal.z, uv.x, uv.y, sample.Height};
            std::copy(values.begin(), values.end(), result.begin() + static_cast<std::ptrdiff_t>(i * 9));
        }
        return result;
    };
    return environment;
}
}

std::unique_ptr<MaterializationState> MaterializationDodBridge::Import(
    const Dod::DataOrientedRoamState& source, std::shared_ptr<const FrozenEnvironment> environment)
{
    Require(source.Settings.EnableLocalConstraints && source.BuildSequence != 0, "要求共形来源和有效轮次");
    auto state = std::make_unique<MaterializationState>(std::move(environment), source.Settings.TriangleBudget);
    const auto path = [&](Dod::DataOrientedRoamNodeIndex node) -> NodeId {
        if (node == Dod::InvalidDataOrientedRoamNodeIndex) return 0;
        Require(source.IsValidNode(node), "节点引用越界");
        return source.Nodes.PathIdAt(node);
    };
    state->_leaves.clear(); state->_consumed.clear(); state->_pending.clear(); state->_slots.clear();
    state->_nodes.clear(); state->_nextSlot = 0;
    state->_history.Previous.insert(source.PreviousSplitPaths.begin(), source.PreviousSplitPaths.end());
    for (std::size_t i = 0; i < source.Nodes.size(); ++i)
    {
        const auto index = static_cast<Dod::DataOrientedRoamNodeIndex>(i);
        const auto id = path(index);
        Require(id != 0 && !state->_nodes.contains(id), "重复或零路径身份");
        auto& node = state->Ensure(id);
        const auto& domain = source.Nodes.DomainAt(index);
        Require(node.Depth == source.Nodes.DepthAt(index) && node.Parent == path(source.Nodes.ParentAt(index)) &&
            node.Triangle.A == domain.A && node.Triangle.B == domain.B && node.Triangle.C == domain.C,
            "静态字段不同，路径=" + std::to_string(id));
        if (source.SplitQueueBlockedBuildIds.at(i) == source.BuildSequence) state->_history.Blocked.insert(id);
        if (source.Nodes.SplitBuildIdAt(index) == source.BuildSequence) state->_history.Split.insert(id);
        if (source.Nodes.MergeBuildIdAt(index) == source.BuildSequence) state->_history.Merged.insert(id);
    }
    // 活动列表才是有效切割；历史缓存的 IsSplit 位不能决定本轮活动资格
    for (const auto index : source.ActiveInternalNodes)
    {
        const auto id = path(index);
        Require(state->_events.insert(id).second, "重复活动内部节点");
        auto& node = state->_nodes.at(id);
        node.Active = true; node.Internal = true;
        Require(node.Children[0] == path(source.Nodes.LeftChildAt(index)) &&
            node.Children[1] == path(source.Nodes.RightChildAt(index)), "活动孩子编码不同");
    }
    for (const auto index : source.ActiveLeafNodes)
    {
        const auto id = path(index);
        Require(state->_leaves.insert(id).second, "重复活动叶");
        auto& node = state->_nodes.at(id);
        node.Active = true;
        node.Neighbors = {path(source.Nodes.BaseNeighborAt(index)), path(source.Nodes.LeftNeighborAt(index)),
            path(source.Nodes.RightNeighborAt(index))};
    }
    MaterializationValidation::ValidateClosed(state->_events, state->Environment().MaxDepth, state->Budget());
    Require(MaterializationValidation::EnumerateLeaves(state->_events) == state->_leaves, "活动切割不同");
    state->RebindNeighbors();

    // 已写入 CPU 网格是最早消费者基线；此时当轮拓扑编辑可能尚未提交给网格
    const auto& mesh = source.IncrementalMesh;
    Require(!mesh.Metadata.NeedsInitialization && mesh.Data.Vertices.size() == mesh.Metadata.SlotOwners.size() * 3 &&
        mesh.Data.Indices.size() == mesh.Data.Vertices.size(), "消费者基线不可恢复");
    for (std::size_t slot = 0; slot < mesh.Metadata.SlotOwners.size(); ++slot)
    {
        const auto id = path(mesh.Metadata.SlotOwners[slot]);
        MeshTriangle triangle{};
        for (std::size_t corner = 0; corner < 3; ++corner)
        {
            const auto& vertex = mesh.Data.Vertices[slot * 3 + corner];
            const std::array<float, 9> values{vertex.Position.x, vertex.Position.y, vertex.Position.z,
                vertex.Normal.x, vertex.Normal.y, vertex.Normal.z, vertex.TexCoord.x, vertex.TexCoord.y, vertex.Height};
            std::copy(values.begin(), values.end(), triangle.begin() + static_cast<std::ptrdiff_t>(corner * 9));
        }
        const auto base = static_cast<std::uint32_t>(slot * 3);
        const auto& vertices = mesh.Data.Vertices;
        const bool positiveY = glm::cross(vertices[base + 1].Position - vertices[base].Position,
            vertices[base + 2].Position - vertices[base].Position).y >= 0.0F;
        Require(mesh.Data.Indices[base] == base && mesh.Data.Indices[base + 1] == base + (positiveY ? 1U : 2U) &&
            mesh.Data.Indices[base + 2] == base + (positiveY ? 2U : 1U), "消费者索引或绕序不同");
        Require(state->_consumed.emplace(id, triangle).second, "消费者重复槽位身份");
        state->_slots[id] = slot;
        if (!state->_leaves.contains(id)) state->_pending[id] = {true, false};
    }
    state->_nextSlot = mesh.Metadata.SlotOwners.size();
    for (const auto id : state->_leaves)
        if (!state->_consumed.contains(id))
        {
            state->_slots[id] = state->_nextSlot++;
            state->_pending[id] = {false, true};
        }

    // 独立重算是导入费用；必须和来源全部逻辑条目相等，不能重建之后跳过对应检查
    state->RefreshAllQueues();
    std::map<NodeId, QueueValue> split, merge;
    for (const auto& entry : source.SplitQueue)
    {
        const bool suppressed = entry.Score == std::numeric_limits<float>::lowest();
        Require(split.emplace(path(entry.Node), QueueValue{suppressed ? 0.0F : entry.Score, suppressed}).second,
            "来源细分队列重复");
    }
    for (const auto& entry : source.MergeQueue)
    {
        const bool suppressed = entry.Score == std::numeric_limits<float>::max();
        Require(merge.emplace(path(entry.Node), QueueValue{suppressed ? 0.0F : entry.Score, suppressed}).second,
            "来源合并队列重复");
    }
    Require(split == state->_splitQueue, "完整细分队列不对应");
    Require(merge == state->_mergeQueue, "完整合并队列不对应");
    MaterializationValidation::Validate(*state);
    state->FinishMutation();
    return state;
}

DodTargetTask MaterializationDodBridge::Capture(const Dod::DataOrientedRoamState& source)
{
    constexpr auto pass = Algorithms::TerrainLodPassId::SplitTopology;
    DodTargetTask task;
    task.SourceHash = Dod::HashDataOrientedRoamPassInput(source, pass);
    task.SourceRecords = source.Nodes.size();
    auto start = Clock::now();
    task.Environment = Freeze(source, task.StaticBytes);
    task.EnvironmentMs = Elapsed(start);
    start = Clock::now();
    task.Initial = Import(source, task.Environment);
    task.ImportMs = Elapsed(start);

    // 只有自有来源副本运行严格阶段；输出仅提供 J，最终邻接或队列不会传入补丁
    start = Clock::now();
    Dod::DataOrientedRoamState target(source);
    task.DiscoveryCopyMs = Elapsed(start);
    Dod::ConfigureDataOrientedRoamPassAction(target, pass, Algorithms::TerrainLodPassAction::SerialImmediate, 1);
    target.Settings.EnablePassEvidence = false;
    target.Settings.EnableTopologyValidation = false;
    target.Settings.EnableTopologyPairEvidence = false;
    start = Clock::now();
    Dod::ExecuteDataOrientedRoamPass(target, pass);
    task.DiscoveryMs = Elapsed(start);
    const auto evidence = Dod::CaptureDataOrientedRoamPassEvidence(target, pass);
    Require(evidence.ValidationPerformed && evidence.Correct, "目标来源验证失败");
    start = Clock::now();
    EventSet events;
    for (const auto node : target.ActiveInternalNodes) events.insert(target.Nodes.PathIdAt(node));
    task.Request = MaterializationValidation::Difference(*task.Initial, std::move(events));
    task.DifferenceMs = Elapsed(start);
    Require(Dod::HashDataOrientedRoamPassInput(source, pass) == task.SourceHash, "只读来源发生变化");
    return task;
}
}
