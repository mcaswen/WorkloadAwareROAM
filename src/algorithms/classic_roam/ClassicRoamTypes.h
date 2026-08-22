#pragma once

#include "algorithms/TerrainLodPassTrace.h"
#include "terrain/HeightMap.h"
#include "terrain/TerrainMeshBuilder.h"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace ParallelRoam::Algorithms::ClassicRoam
{
/// <summary>
/// 表示 Classic ROAM 三角形在高度图上的覆盖区域
/// 该值可在模块之间复制，不负责管理节点或网格资源
/// </summary>
struct TriangleDomain
{
    // 三个 UV 点保持逆时针绕序，生成网格时映射到世界空间的 XZ 平面
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

// 沿 Classic ROAM 规定的底边生成两个子三角形区域
[[nodiscard]] TriangleDomainChildren SplitTriangleDomain(const TriangleDomain& domain);

/// <summary>
/// 控制 Classic CPU ROAM 每帧的细分、合并和拓扑验证
/// 上层适配器每帧传入一次，网格生成器只保存本帧副本
/// </summary>
struct ClassicRoamSettings
{
    // MaxDepth 限制二叉三角树能够细分到的最深层级
    int MaxDepth{14};
    // 屏幕误差高于该值时允许细分
    float SplitThreshold{4.0F};
    // 屏幕误差低于该值时允许合并
    float MergeThreshold{2.0F};
    // 当前可用于渲染的活动叶三角形数量上限
    std::size_t TriangleBudget{20000U};
    // 是否同步细分底边邻居，以避免不同层级相接产生裂缝
    bool EnableLocalConstraints{true};
    // 是否在更新后运行完整拓扑检查
    bool EnableTopologyValidation{false};
    // 是否为固定轨迹重放计算结果哈希和队列完整性证据
    bool EnablePassEvidence{false};
};

/// <summary>
/// 记录 Classic CPU ROAM 最近一次更新的规模、结果和各阶段耗时
/// 数据由 ClassicRoamMeshBuilder 填写，更新完成后供适配器读取
/// </summary>
struct ClassicRoamStats
{
    TerrainLodPassTraceArray PassTraces{MakeTerrainLodPassTraces()};
    std::uint64_t BuildSequence{0U};
    std::uint64_t TopologyHash{0U};
    std::uint64_t ActiveLeafHash{0U};
    std::uint64_t MeshHash{0U};
    std::size_t TriangleBudget{0U};
    std::size_t QueueInvariantViolationCount{0U};
    float PassEvidenceMilliseconds{0.0F};
    // 节点池总数，包括内部节点和叶节点
    std::size_t NodeCount{0};
    // 当前用于渲染的活动叶三角形数量
    std::size_t ActiveTriangleCount{0};
    // 尚未发生细分的原始叶三角形数量
    std::size_t OriginalTriangleCount{0};
    // 已细分但仍处于稳定活动状态的叶三角形数量
    std::size_t SubdividedTriangleCount{0};
    // 本次更新中新激活或因合并而恢复的叶三角形数量
    std::size_t RebuiltTriangleCount{0};
    // 当前已细分的内部节点数量
    std::size_t ActiveSplitCount{0};
    // 本次更新中因误差超出阈值而成功细分的次数
    std::size_t SplitCount{0};
    // 为补齐底边邻接关系而额外执行的强制细分次数
    std::size_t ForcedSplitCount{0};
    // 本次更新中成功合并回父节点的次数
    std::size_t MergeCount{0};
    // 达到最大深度后仍可能存在裂缝的次数
    std::size_t CrackRiskCount{0};
    // 底边邻居约束向外传播的次数
    std::size_t ConstraintPassCount{0};
    // 两个跨帧保留队列的成员数量峰值之和
    std::size_t CandidatePeakCount{0};
    // 本帧整批重新评分的细分和合并队列条目数量
    std::size_t SplitScoreEntryCount{0U};
    std::size_t MergeScoreEntryCount{0U};
    // 更新结束时细分队列中的节点数量
    std::size_t PersistentSplitQueueSize{0};
    // 更新结束时合并队列中的菱形数量
    std::size_t PersistentMergeQueueSize{0};
    // 通过先合并再细分来重新分配三角形预算的次数
    std::size_t QueueCrossoverCount{0};
    // 队列成员发生局部变更的次数
    std::size_t QueueMembershipUpdateCount{0};
    // 完整重建 CPU 网格的次数
    std::size_t MeshFullRebuildCount{0};
    // 本次重新写入 CPU 网格的三角形数量
    std::size_t MeshUpdatedTriangleCount{0};
    // 继续复用原有网格槽位的三角形数量
    std::size_t MeshReusedTriangleCount{0};
    // 需要重新上传的网格连续区间数量
    std::size_t MeshDirtyRangeCount{0};
    // 因拓扑或深度等非预算原因被拒绝的细分次数
    std::size_t RejectedSplitCount{0};
    // 因活动三角形预算不足被拒绝的细分次数
    std::size_t BudgetRejectedSplitCount{0};
    // 因菱形合并条件不满足被拒绝的合并次数
    std::size_t RejectedMergeCount{0};
    // 验证器发现的 T 形接缝数量
    std::size_t TjunctionCount{0};
    // 验证器发现的邻接关系错误数量
    std::size_t InvalidNeighborCount{0};
    // 验证器发现的拓扑结构错误数量
    std::size_t InvalidTopologyCount{0};

    // 完整更新的 CPU 耗时
    float UpdateMilliseconds{0.0F};
    // 输入准备和状态同步耗时
    float PrepareMilliseconds{0.0F};
    // 合并候选评分耗时
    float MergeCandidateMarkMilliseconds{0.0F};
    // 实际执行合并并维护拓扑的耗时
    float MergeTopologyMilliseconds{0.0F};
    // 活动叶收集耗时
    float BudgetLeafCollectMilliseconds{0.0F};
    // 刷新并扫描细分队列的耗时
    float SplitInitialScanMilliseconds{0.0F};
    // 主线程反复处理细分和合并候选直到队列稳定的耗时
    float SplitSerialConvergenceMilliseconds{0.0F};
    // 实际执行细分并维护拓扑的耗时
    float SplitQueueTopologyMilliseconds{0.0F};
    // 最终叶集合收集耗时
    float FinalLeafCollectMilliseconds{0.0F};
    // 将拓扑变化写入 CPU 网格的耗时
    float MeshEmitMilliseconds{0.0F};
    // 更新收尾阶段耗时
    float FinalizeMilliseconds{0.0F};
    // 细分队列处理与拓扑修改的总耗时
    float SplitMilliseconds{0.0F};
    // CPU 网格输出总耗时
    float EmitMilliseconds{0.0F};
    // 拓扑验证耗时
    float ValidateMilliseconds{0.0F};
    // 合并候选处理与拓扑回收的总耗时
    float MergeMilliseconds{0.0F};
    // 本次更新观察到的最大深度
    int MaxDepthReached{0};
};
} // 命名空间 ParallelRoam::Algorithms::ClassicRoam
