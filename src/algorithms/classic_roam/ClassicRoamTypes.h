#pragma once

#include "terrain/HeightMap.h"
#include "terrain/TerrainMeshBuilder.h"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace ParallelRoam::Algorithms::ClassicRoam
{
/// <summary>
/// Classic ROAM 的跨模块值类型
/// 由 builder 在初始化和 Build 期间创建或复制；类型本身不拥有节点池和 mesh
/// </summary>
struct TriangleDomain
{
    // 三个 UV 点保持逆时针绕序，最终会映射到 XZ 平面
    glm::vec2 A{0.0F};
    glm::vec2 B{0.0F};
    glm::vec2 C{0.0F};
};

struct TriangleDomainChildren
{
    TriangleDomain Left;
    TriangleDomain Right;
};

struct ClassicRoamMeshUpdateRange
{
    std::size_t FirstTriangle{0};
    std::size_t TriangleCount{0};
};

// 按 Classic ROAM 的 base edge 规则生成两个 child domain
[[nodiscard]] TriangleDomainChildren SplitTriangleDomain(const TriangleDomain& domain);

/// <summary>
/// Classic CPU ROAM 的单帧细分、合并和拓扑验证参数
/// 由上层算法适配器按帧传入，builder 只保存当前配置的副本
/// </summary>
struct ClassicRoamSettings
{
    // MaxDepth 限制二叉三角树的最细层级
    int MaxDepth{14};
    // 屏幕误差超过该值时允许 split
    float SplitThreshold{4.0F};
    // 屏幕误差低于该值时允许 merge
    float MergeThreshold{2.0F};
    // 活动 leaf triangle 的硬预算
    std::size_t TriangleBudget{20000U};
    // 是否启用 base neighbor 的局部裂缝约束
    bool EnableLocalConstraints{true};
    // 是否运行额外的全局拓扑验证
    bool EnableTopologyValidation{false};
};

/// <summary>
/// Classic CPU ROAM 的运行统计
/// 由 ClassicRoamMeshBuilder 持有，每次 Build 开始时刷新，Build 结束后供适配器读取
/// </summary>
struct ClassicRoamStats
{
    // 节点池总数，包括 internal 和 leaf 节点
    std::size_t NodeCount{0};
    // 当前用于渲染的活动叶三角形数量
    std::size_t ActiveTriangleCount{0};
    // 尚未发生细分的原始叶三角形数量
    std::size_t OriginalTriangleCount{0};
    // 已细分但仍处于稳定活动状态的叶三角形数量
    std::size_t SubdividedTriangleCount{0};
    // 本次 Build 新激活或由 merge 恢复的叶三角形数量
    std::size_t RebuiltTriangleCount{0};
    // 当前仍处于 split 状态的 internal 节点数量
    std::size_t ActiveSplitCount{0};
    // 本次 Build 成功执行的普通 split 数量
    std::size_t SplitCount{0};
    // 为满足邻居约束额外执行的 forced split 数量
    std::size_t ForcedSplitCount{0};
    // 本次 Build 成功回收的 parent 数量
    std::size_t MergeCount{0};
    // 达到最大深度后仍可能存在裂缝的次数
    std::size_t CrackRiskCount{0};
    // base neighbor 约束传播次数
    std::size_t ConstraintPassCount{0};
    // 两个持久队列成员数量之和的峰值
    std::size_t CandidatePeakCount{0};
    // Build 结束时 split queue 的成员数量
    std::size_t PersistentSplitQueueSize{0};
    // Build 结束时 merge queue 的成员数量
    std::size_t PersistentMergeQueueSize{0};
    // 队列交叉腾挪预算的次数
    std::size_t QueueCrossoverCount{0};
    // 队列成员发生局部变更的次数
    std::size_t QueueMembershipUpdateCount{0};
    // 发生完整 mesh 重建的次数
    std::size_t MeshFullRebuildCount{0};
    // 被重新写入的 mesh 三角形数量
    std::size_t MeshUpdatedTriangleCount{0};
    // 直接复用旧 mesh slot 的三角形数量
    std::size_t MeshReusedTriangleCount{0};
    // 被标记为 dirty 的 mesh range 数量
    std::size_t MeshDirtyRangeCount{0};
    // 非预算原因导致的 split 拒绝次数
    std::size_t RejectedSplitCount{0};
    // 因活动三角形预算不足导致的 split 拒绝次数
    std::size_t BudgetRejectedSplitCount{0};
    // 因 diamond 条件不满足导致的 merge 拒绝次数
    std::size_t RejectedMergeCount{0};
    // validator 发现的 T-junction 数量
    std::size_t TjunctionCount{0};
    // validator 发现的邻接关系错误数量
    std::size_t InvalidNeighborCount{0};
    // validator 发现的拓扑不变量错误数量
    std::size_t InvalidTopologyCount{0};

    // 完整 Build 的 CPU 耗时
    float UpdateMilliseconds{0.0F};
    // 输入准备和状态同步耗时
    float PrepareMilliseconds{0.0F};
    // merge 候选标记耗时
    float MergeCandidateMarkMilliseconds{0.0F};
    // merge topology 操作耗时
    float MergeTopologyMilliseconds{0.0F};
    // 活动叶收集耗时
    float BudgetLeafCollectMilliseconds{0.0F};
    // split 初始扫描耗时
    float SplitInitialScanMilliseconds{0.0F};
    // split/merge 收敛循环耗时
    float SplitSerialConvergenceMilliseconds{0.0F};
    // 单个 split topology 提交的诊断耗时
    float SplitQueueTopologyMilliseconds{0.0F};
    // 最终叶集合收集耗时
    float FinalLeafCollectMilliseconds{0.0F};
    // mesh emit 阶段耗时
    float MeshEmitMilliseconds{0.0F};
    // Build 收尾阶段耗时
    float FinalizeMilliseconds{0.0F};
    // split queue 与 topology 操作总耗时
    float SplitMilliseconds{0.0F};
    // mesh 输出总耗时
    float EmitMilliseconds{0.0F};
    // 拓扑验证耗时
    float ValidateMilliseconds{0.0F};
    // merge 候选和 topology 回收总耗时
    float MergeMilliseconds{0.0F};
    // 本次 Build 观察到的最大深度
    int MaxDepthReached{0};
};
} // namespace ParallelRoam::Algorithms::ClassicRoam
