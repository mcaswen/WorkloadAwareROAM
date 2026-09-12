#pragma once

#include "experiment/roam_materialization/MaterializationHierarchy.h"

#include <map>
#include <optional>

namespace ParallelRoam::Experiment::RoamMaterialization
{
/// <summary>
/// 活动与缓存资格分开，邻接句柄只在叶活动时有效
/// 复制状态后必须重新绑定句柄，不能把旧副本的地址带入新状态
/// </summary>
struct NodeRecord
{
    NodeId Id{0}, Parent{0}, Mate{0};
    std::array<NodeId, 2> Children{};
    Domain Triangle{};
    int Depth{0};
    bool Active{false}, Internal{false};
    std::array<NodeId, 3> Neighbors{};
    std::array<const NodeRecord*, 3> NeighborRecords{};
};

/// <summary>
/// 抑制与资格分别表示，不能把当前不可执行的合法成员从队列中删除
/// </summary>
struct QueueValue
{
    float Score{0};
    bool Suppressed{false};
    bool operator==(const QueueValue&) const = default;
};

/// <summary>
/// 队列索引保存已规范化的排序值，同分按逻辑身份确定唯一顺序
/// </summary>
struct OrderedEntry
{
    float Rank{0};
    NodeId Id{0};
    auto operator<=>(const OrderedEntry&) const = default;
};

/// <summary>
/// 保留上次确认消费的基线和当前目标，合成多次修改时不覆盖基线
/// </summary>
struct PendingEntry
{
    bool Baseline{false}, Target{false};
    bool operator==(const PendingEntry&) const = default;
};

/// <summary>
/// 拥有独立原型的持久状态；生产导入和全量验证由边界适配器负责
/// 普通目标应用只能通过局部操作更新，不公开可写容器给测量编排
/// </summary>
class MaterializationState
{
public:
    MaterializationState(std::shared_ptr<const FrozenEnvironment> environment, std::size_t budget);
    MaterializationState(const MaterializationState& source);
    MaterializationState& operator=(const MaterializationState&) = delete;
    [[nodiscard]] const EventSet& Events() const { return _events; }
    [[nodiscard]] const EventSet& Leaves() const { return _leaves; }
    [[nodiscard]] const auto& Nodes() const { return _nodes; }
    [[nodiscard]] const auto& SplitQueue() const { return _splitQueue; }
    [[nodiscard]] const auto& MergeQueue() const { return _mergeQueue; }
    [[nodiscard]] const auto& Pending() const { return _pending; }
    [[nodiscard]] const auto& Consumed() const { return _consumed; }
    [[nodiscard]] const auto& Slots() const { return _slots; }
    [[nodiscard]] const History& Marks() const { return _history; }
    [[nodiscard]] const FrozenEnvironment& Environment() const { return *_environment; }
    [[nodiscard]] std::size_t Budget() const { return _budget; }
    [[nodiscard]] bool Usable() const { return _usable; }
    [[nodiscard]] bool Event(NodeId id) const;
    [[nodiscard]] bool Leaf(NodeId id) const;
    [[nodiscard]] const NodeRecord& Node(NodeId id) const;
    [[nodiscard]] bool MergeReady(NodeId group) const;
    [[nodiscard]] NodeId Group(NodeId id) const;
    [[nodiscard]] std::vector<NodeId> Members(NodeId id) const;
    [[nodiscard]] float Score(NodeId id) const;
    [[nodiscard]] std::optional<NodeId> PeekSplit() const;
    [[nodiscard]] std::optional<NodeId> PeekMerge() const;
    [[nodiscard]] bool ShouldSplit(NodeId id) const;

    // 这三个公共边界显式计入消费或跨轮成本，不属于局部物化事务
    void ConsumePending(WorkCounters* work = nullptr);
    void AdvanceEpoch(WorkCounters* work = nullptr);
    void SetHistory(History history);

private:
    friend class MaterializationReference;
    friend class MaterializationPatch;
    friend class MaterializationValidation;
    friend class MaterializationDodBridge;

    /// <summary>
    /// 事务失败时丢弃自有状态，避免分配异常后发布半份可续接结果
    /// </summary>
    class WorkSession
    {
    public:
        WorkSession(MaterializationState& state, WorkCounters* work);
        ~WorkSession();
        WorkSession(const WorkSession&) = delete;
    private:
        MaterializationState& _state;
        int _exceptions;
    };

    NodeRecord& Ensure(NodeId id);
    static NodeRecord BuildRecord(NodeId id);
    static float EvaluateScore(const FrozenEnvironment& environment, NodeId id, const Domain& triangle);

    /// <summary>
    /// 在固定目标代上准备资格和评分输入，任务只计算值，队列安装仍由主线程完成
    /// </summary>
    struct CandidateInput
    {
        NodeId Id{0};
        bool Present{false}, Suppressed{false};
        std::size_t Samples{0};
        std::array<NodeId, 2> Members{};
        std::array<Domain, 2> Triangles{};
    };
    CandidateInput PrepareSplit(NodeId id) const;
    CandidateInput PrepareMerge(NodeId group) const;
    static QueueValue EvaluateCandidate(const FrozenEnvironment& environment, const CandidateInput& input);
    void InstallCandidate(const CandidateInput& input, QueueValue value, bool split);
    void SetNeighbor(NodeId id, std::size_t edge, NodeId target);
    void SetLeaf(NodeId id, bool present);
    void SetEvent(NodeId id, bool present);
    void RefreshSplit(NodeId id);
    void RefreshMerge(NodeId group);
    void RefreshAllQueues();
    void FinishMutation();
    void RebindNeighbors();

    std::shared_ptr<const FrozenEnvironment> _environment;
    std::shared_ptr<const StateVersion> _version{std::make_shared<StateVersion>()};
    std::map<NodeId, NodeRecord> _nodes;
    EventSet _events, _leaves;
    std::map<NodeId, QueueValue> _splitQueue, _mergeQueue;
    std::set<OrderedEntry> _splitOrder, _mergeOrder;
    History _history;
    std::map<NodeId, PendingEntry> _pending;
    std::map<NodeId, MeshTriangle> _consumed;
    std::map<NodeId, std::uint64_t> _slots;
    std::vector<std::uint64_t> _freeSlots;
    std::uint64_t _nextSlot{0};
    std::size_t _budget;
    bool _usable{true};
    bool _inTransaction{false};
    WorkCounters* _work{nullptr};
};
}
