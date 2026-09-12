#pragma once

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <vector>

namespace ParallelRoam::Experiment::RoamMaterialization
{
using NodeId = std::uint64_t;
using EventSet = std::set<NodeId>;
inline constexpr NodeId RootA = 1ULL;
inline constexpr NodeId RootB = 1ULL << 32U;
inline constexpr int MaximumDepth = 20;

/// <summary>
/// 保存固定二分层次的参数域，绕序与共享 ROAM 几何函数一致
/// </summary>
struct Domain
{
    glm::vec2 A, B, C;
};

/// <summary>
/// 使用精确二进制网格标识顶点和整边，避免浮点容差把真子段拼成共边
/// </summary>
struct PointKey
{
    std::int64_t X{0}, Y{0};
    auto operator<=>(const PointKey&) const = default;
};

/// <summary>
/// 无向整边用排序后的端点标识，实际绕序由叶记录另外验证
/// </summary>
struct EdgeKey
{
    PointKey First, Second;
    auto operator<=>(const EdgeKey&) const = default;
};

// 每个顶点依次保存位置、法线、UV 和高度；调试颜色不属于逻辑消费结果
using MeshTriangle = std::array<float, 27>;

/// <summary>
/// 由调用方持有不可变评分和网格环境，两种目标应用共享同一份函数输入
/// 回调不得依赖被测执行方式，也不得修改来源节点或延迟补全全局状态
/// </summary>
struct FrozenEnvironment
{
    int MaxDepth{MaximumDepth};
    float SplitThreshold{4.0F}, MergeThreshold{2.0F};
    std::function<float(NodeId, const Domain&)> Score;
    std::function<MeshTriangle(NodeId, const Domain&)> Emit;
};

/// <summary>
/// 历史是完整旧状态的一部分，不能从最终事件集反推
/// </summary>
struct History
{
    EventSet Previous, Blocked, Split, Merged;
    bool operator==(const History&) const = default;
};

/// <summary>
/// 诊断遍统计逻辑调用和实际记录修改，计时遍不安装计数接收器
/// 这些量不代表 C++ 字段访问次数或硬件指令数量
/// </summary>
struct WorkCounters
{
    std::size_t EventQueries{0}, RecordQueries{0}, ScoreEvaluations{0};
    std::size_t SplitRechecks{0}, MergeRechecks{0}, QueueWrites{0};
    std::size_t RecordsCreated{0}, RecordsReused{0}, NeighborWrites{0};
    std::size_t SlotAllocations{0}, SlotReuses{0}, PendingWrites{0};
    std::size_t PrimitiveGroups{0}, PreparedEdges{0}, FullScanItems{0};
    std::size_t LeafSupport{0}, MergeSupport{0};
    // 仅诊断遍开启步骤时钟；它含诊断本身的扰动，不能替代独立事务计时
    double SupportMs{0}, RecordsMs{0}, ConnectMs{0}, MaintenanceMs{0};
};

/// <summary>
/// 差分在认证前保留原始顺序和方向，便于拒绝重复或相互矛盾的条目
/// </summary>
struct TargetRequest
{
    EventSet Target;
    std::vector<NodeId> Added, Removed;
};

// 副本共用旧版本标识，任意已发布修改会换成新标识，防止错用旧认证
struct StateVersion {};
class MaterializationState;
class MaterializationValidation;
class MaterializationReference;
class MaterializationPatch;

/// <summary>
/// 认证结果只能由独立验证器构造；算法获得同一差分，不获得目标邻接或队列答案
/// </summary>
class CertifiedTarget
{
public:
    [[nodiscard]] const std::vector<NodeId>& Added() const { return _added; }
    [[nodiscard]] const std::vector<NodeId>& Removed() const { return _removed; }
    [[nodiscard]] std::size_t TargetLeafCount() const { return _leafCount; }

private:
    friend class MaterializationValidation;
    friend class MaterializationReference;
    friend class MaterializationPatch;
    std::shared_ptr<const StateVersion> _version;
    std::vector<NodeId> _added, _removed;
    std::size_t _leafCount{0};
};
}
