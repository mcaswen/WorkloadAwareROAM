#pragma once

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
    std::uint64_t RepairSamples{}, RepairFaces{}, OrderVisits{}, MeshVertices{}, MeshIndices{}, PendingBlocks{};
    std::uint64_t HeightSamples{}, HeightExactSamples{}, HeightGuardChecks{}, HeightGuardRejected{};
    std::uint64_t CapacityGrowths{}, CapacityBytesReserved{}, CapacityBytesRelocated{};
    std::uint64_t ViewBufferAllocations{}, ViewBufferBytes{};
    std::uint64_t IndexBlocks{},IndexSlots{},IndexComparisons{},IndexQueryBlocks{};
    std::uint64_t ReceiverConstructed{},EvidenceLookups{},EvidenceHits{},EvidenceBuilds{},EvidenceBytes{},EvidenceFaceTests{};
    std::uint64_t FootprintBuilds{},DonorTouched{},DonorCertified{};
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
    double ErrorLower{}, ErrorUpper{};
    std::string Reason;
    // 保护证据只在当前冻结提案内复用，不跨拓扑或视图代际缓存
    mutable std::shared_ptr<HeightEvidence> HeightProof;
};

/// <summary>
/// 认证后的接收方与可选回收方组成不可拆分的预算事务
/// </summary>
struct Exchange
{
    Proposal Receiver;
    Proposal Donor;
    bool HasDonor{};
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
    std::vector<Identity> IntentIds, PoolIds;
    std::vector<std::string> IntentResults;
    std::vector<std::vector<std::pair<char,std::string>>> Attempts;
};
}
