#pragma once

#include "algorithms/TransactionalLodSettings.h"

#include <array>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

namespace ParallelRoam::Algorithms::GreedyTransactionalLod
{
using Identity = std::int64_t;
using Slot = std::uint32_t;
constexpr Slot InvalidSlot = std::numeric_limits<Slot>::max();

/// <summary>
/// 参数坐标和实际存储高度；共享顶点只有一份已发布几何值
/// </summary>
struct Point
{
    double U{}, V{}, Height{};
    bool operator==(const Point&) const = default;
};

/// <summary>
/// 认证与输出共用同一舍入位置，避免私有几何与公开几何悄然分叉
/// </summary>
inline std::array<float, 3> PublishedPosition(const Point& point, double terrainSize)
{
    return {static_cast<float>((point.U - .5) * terrainSize),
        static_cast<float>(point.Height), static_cast<float>((point.V - .5) * terrainSize)};
}

/// <summary>
/// 面连接使用稳定逻辑身份，物理槽只用于局部存储与访问
/// </summary>
struct Triangle
{
    Identity Id{};
    std::array<Identity, 3> Vertices{};
    bool operator==(const Triangle&) const = default;
};

using Edge = std::array<Identity, 2>;
inline Edge EdgeKey(Identity a, Identity b) { return a < b ? Edge{a, b} : Edge{b, a}; }

/// <summary>
/// 一次运行的冻结视图和规则；矩阵按行存储，不依赖渲染后端
/// </summary>
struct Configuration
{
    std::string Scenario;
    std::uint32_t SampleIndex{};
    std::size_t Budget{};
    double TerrainSize{1}, HeightScale{1}, SplitPixels{4};
    std::uint32_t Width{100}, Height{100};
    std::array<double, 16> Matrix{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    std::size_t PrefixLimit{64}, DonorLimit{64};
    std::size_t SampleVisitLimit{1000000};
    bool HeightGuard{};
    // 旧点在整个存活期保持高度，新点来源由接收高度政策独立选择
    bool PreserveSurvivingHeights{};
    // 净零恢复只在固定旧点政策下开放，切换需要重新建立种子
    bool EnableFlipRecovery{};
    // 单侧中点只创建源高新点，子边不短于源栅格间隔
    bool EnableBoundaryRefinement{};
    TransactionalReceiverOrder ReceiverOrder{TransactionalReceiverOrder::Composite};
    TransactionalQualityPolicy QualityPolicy{TransactionalQualityPolicy::Legacy};
    TransactionalReceiverHeightPolicy ReceiverHeightPolicy{TransactionalReceiverHeightPolicy::LegacyFit};
    double QualityTargetPixels{0.5};
    double QualityHeightRatio{1.0 / 256.0};
    bool UsesZeroToOneDepth{};
};

/// <summary>
/// 原始 uint16 高度与连续双线性参考；不从被测网格反推参考值
/// </summary>
struct HeightSource
{
    std::uint32_t Width{}, Height{};
    std::vector<std::uint16_t> Values;
};

/// <summary>
/// 自有状态的初始化值，不携带来源队列或任何目标网格
/// </summary>
struct InitialMesh
{
    Configuration Config;
    HeightSource Source;
    std::vector<std::pair<Identity, Point>> Vertices;
    std::vector<Triangle> Faces;
};

/// <summary>
/// 工作计数与阶段时间分开，配额到期抛出异常而不发布部分批次
/// </summary>
struct WorkLedger
{
    std::uint64_t LocationTests{}, SampleContributions{}, SampleEvaluations{};
    std::uint64_t Proposals{}, SampleTouches{}, Constraints{}, ExactChecks{}, FilterChecks{};
    std::uint64_t EarTests{}, RingVisits{}, PairChecks{}, ReservationChecks{};
    std::uint64_t DonorReuse{}, Conflicts{}, PreparedFaces{}, PreparedVertices{}, PreparedEdges{};
    // 源高准备与旧拟合分开记账，不能把删掉拟合后的认证增长隐藏掉
    std::uint64_t SourceHeightAttempts{}, SourceHeightPrepared{};
    std::uint64_t RepairSamples{}, RepairFaces{}, OrderVisits{}, MeshVertices{}, MeshIndices{}, PendingBlocks{};
    std::uint64_t HeightSamples{}, HeightExactSamples{}, HeightGuardChecks{}, HeightGuardRejected{};
    std::uint64_t QualityMaxRationalBits{};
    std::uint64_t CapacityGrowths{}, CapacityBytesReserved{}, CapacityBytesRelocated{};
    std::uint64_t ViewBufferAllocations{}, ViewBufferBytes{};
    std::uint64_t IndexBlocks{},IndexSlots{},IndexComparisons{},IndexQueryBlocks{};
    std::uint64_t ReceiverConstructed{},EvidenceLookups{},EvidenceHits{},EvidenceBuilds{},EvidenceBytes{},EvidenceFaceTests{};
    std::uint64_t FootprintBuilds{},DonorTouched{},DonorCertified{};
    std::uint64_t FlipTriggered{}, FlipAttempts{}, FlipCertified{}, FlipConflicts{};
    std::uint64_t BoundaryAttempts{}, BoundaryCertified{}, BoundaryResolutionRejected{}, BoundaryConflicts{};
    std::uint64_t CandidateUpdates{}, DonorIndexUpdates{}, ReceiverCacheHits{}, DonorCacheHits{}, CacheInvalidations{}, RootObservations{};
    std::map<std::string, std::uint64_t> Reasons;
    std::map<std::string, double> Seconds;
    // 诊断记录每段处理项、分块数和实际线程数，不把任务数当作线程证据
    std::map<std::string,std::array<std::size_t,3>> Execution;
    std::chrono::steady_clock::time_point Deadline{std::chrono::steady_clock::time_point::max()};
    std::size_t VisitLimit{1000000};
    void CheckLimit() const;
    void Touch();
    /// <summary>
    /// 连续存储按几何容量增长，显式登记偶发搬移而不隐瞒为局部工作
    /// </summary>
    template<class T> void Reserve(std::vector<T>& values,std::size_t needed)
    {
        if (needed<=values.capacity()) return;
        const auto extra=values.capacity()/2+1;
        const auto grown=values.capacity()>values.max_size()-extra ? values.max_size() : values.capacity()+extra;
        const auto capacity=std::max(needed,grown);
        values.reserve(capacity);++CapacityGrowths;
        CapacityBytesReserved+=capacity*sizeof(T);CapacityBytesRelocated+=values.size()*sizeof(T);
    }
};

struct HeightEvidence;
struct ProposalQualityCertificate;
/// <summary>
/// 局部替换提案在只读快照上形成，所有新面均引用 Points 内几何
/// Free 仅列允许改高的点，Support 是被替换的旧面槽
/// </summary>
struct Proposal
{
    char Kind{};
    Slot Root{InvalidSlot};
    Identity Center{}, NewVertex{};
    std::vector<Slot> Support;
    std::vector<std::array<Identity, 3>> Faces;
    std::map<Identity, Point> Points;
    std::vector<Identity> Free;
    std::vector<Slot> Samples;
    std::int64_t TargetMicropixels{};
    // 源高接收项没有旧最大误差门槛；零值不得被解释为有效的旧证据
    bool HasLegacyQualityEvidence{true};
    double ErrorLower{}, ErrorUpper{};
    std::string Reason;
    // 保护证据只在当前冻结提案内复用，不跨拓扑或视图代际缓存
    mutable std::shared_ptr<HeightEvidence> HeightProof;
    // 只读证书拥有提案身份及支持，不借用游标临时几何
    std::shared_ptr<const ProposalQualityCertificate> QualityProof;
};

/// <summary>
/// 净零连接修复不占细分额度，不能用是否携带 donor 推断事务种类
/// </summary>
enum class ExchangeKind { Refinement, ConnectivityRepair, BoundaryRefinement };

/// <summary>
/// 每项意图的冻结资金来源，混合面数时获空额项不一定构成连续前缀
/// </summary>
enum class BudgetFunding { None, Free, Donor, ZeroCost };

/// <summary>
/// 接收方成本按面计，失败后的命名额度仍归原意图所有
/// </summary>
struct IntentBudget
{
    std::size_t Faces{};
    BudgetFunding Funding{BudgetFunding::None};
};

/// <summary>
/// 认证后的接收方与可选回收方组成不可拆分事务
/// </summary>
struct Exchange
{
    Proposal Receiver;
    Proposal Donor;
    bool HasDonor{};
    ExchangeKind Kind{ExchangeKind::Refinement};
};

/// <summary>
/// 已封闭批次绑定唯一快照代际；统计包含失败需求，不只保存成功项
/// </summary>
struct CertifiedBatch
{
    std::uint64_t Version{};
    std::vector<Exchange> Exchanges;
    bool PairAuditComplete{true};
    std::size_t Raw{}, Examined{}, Receivers{}, Need{}, Feasible{}, Executed{}, FreeExecuted{};
    std::size_t AssignedCredits{}, UnusedCredits{};
    std::size_t FlipExecuted{};
    std::size_t AssignedFaces{}, ConsumedFreeFaces{}, UnusedFaces{}, ReleasedFaces{};
    std::size_t BoundaryFreeExecuted{}, BoundaryPairedExecuted{};
    std::int64_t NetFaceChange{};
    std::vector<IntentBudget> IntentBudgets;
    std::vector<Identity> IntentIds, PoolIds;
    std::vector<std::string> IntentResults;
    std::vector<std::vector<std::pair<char,std::string>>> Attempts;
};
}
