#pragma once

#include "terrain/TerrainMeshBuilder.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <string_view>
#include <type_traits>
#include <vector>

namespace ParallelRoam::Algorithms
{
enum class TerrainLodScoreRefreshAction
{
    Automatic,
    SerialRefresh,
    ParallelRefresh,
};

enum class TerrainLodTopologyAction
{
    Automatic,
    SerialImmediate,
    ParallelAssisted,
};

enum class TerrainLodMeshEmitAction
{
    Automatic,
    SerialDirty,
    ParallelDirty,
    SerialFull,
};

enum class TerrainLodCpuUploadAction
{
    Automatic,
    DirtyRange,
    FullBuffer,
};

enum class TerrainLodParallelTopologyPhase
{
    Both,
    SplitOnly,
    MergeOnly,
};

/// <summary>
/// 保存各阶段请求的执行方式、线程数量和并行启用条件
/// 实际执行仍受工作量和安全条件限制，请求并行不表示一定会派发多个线程
/// </summary>
struct TerrainLodPassPolicy
{
    TerrainLodScoreRefreshAction MergeScore{TerrainLodScoreRefreshAction::Automatic};
    TerrainLodScoreRefreshAction SplitScore{TerrainLodScoreRefreshAction::Automatic};
    TerrainLodTopologyAction MergeTopology{TerrainLodTopologyAction::Automatic};
    TerrainLodTopologyAction SplitTopology{TerrainLodTopologyAction::Automatic};
    TerrainLodMeshEmitAction MeshEmit{TerrainLodMeshEmitAction::Automatic};
    TerrainLodCpuUploadAction CpuUpload{TerrainLodCpuUploadAction::Automatic};
    // 0 由执行阶段自动选择，1 请求串行；这里记录的是请求值而非本次实际线程数量
    std::size_t MergeScoreWorkerCount{0U};
    std::size_t SplitScoreWorkerCount{0U};
    std::size_t MergeTopologyWorkerCount{0U};
    std::size_t SplitTopologyWorkerCount{0U};
    std::size_t MeshEmitWorkerCount{0U};
    // 0 仅解除工作量门槛，串行动作与单线程请求仍优先
    std::size_t MergeScoreMinParallelEntryCount{256U};
    std::size_t SplitScoreMinParallelEntryCount{256U};
    std::size_t MeshEmitMinParallelTriangleCount{256U};
    // 这组参数只决定并行辅助拓扑何时启用，不改变串行收敛和拓扑正确性规则
    std::size_t SplitTopologyMinParallelCandidateCount{32U};
    std::size_t MergeTopologyMinParallelCandidateCount{160U};
    // 0 表示每次更新都允许；非零值只允许对应 BuildSequence 使用并行辅助拓扑
    std::size_t ParallelTopologyTargetBuild{0U};
    TerrainLodParallelTopologyPhase ParallelTopologyPhase{TerrainLodParallelTopologyPhase::Both};

    [[nodiscard]] bool operator==(const TerrainLodPassPolicy&) const = default;
};

[[nodiscard]] constexpr TerrainLodPassPolicy MakeTerrainLodSerialIncrementalPolicy()
{
    TerrainLodPassPolicy policy{};
    policy.MergeScore = TerrainLodScoreRefreshAction::SerialRefresh;
    policy.SplitScore = TerrainLodScoreRefreshAction::SerialRefresh;
    policy.MergeTopology = TerrainLodTopologyAction::SerialImmediate;
    policy.SplitTopology = TerrainLodTopologyAction::SerialImmediate;
    policy.MeshEmit = TerrainLodMeshEmitAction::SerialDirty;
    policy.CpuUpload = TerrainLodCpuUploadAction::DirtyRange;
    return policy;
}

[[nodiscard]] constexpr TerrainLodPassPolicy MakeTerrainLodMaximumSafeParallelIncrementalPolicy()
{
    TerrainLodPassPolicy policy{};
    policy.MergeScore = TerrainLodScoreRefreshAction::ParallelRefresh;
    policy.SplitScore = TerrainLodScoreRefreshAction::ParallelRefresh;
    policy.MergeTopology = TerrainLodTopologyAction::ParallelAssisted;
    policy.SplitTopology = TerrainLodTopologyAction::ParallelAssisted;
    policy.MeshEmit = TerrainLodMeshEmitAction::ParallelDirty;
    policy.CpuUpload = TerrainLodCpuUploadAction::DirtyRange;
    return policy;
}

[[nodiscard]] constexpr TerrainLodPassPolicy MakeTerrainLodSerialFullOutputPolicy()
{
    TerrainLodPassPolicy policy = MakeTerrainLodSerialIncrementalPolicy();
    policy.MeshEmit = TerrainLodMeshEmitAction::SerialFull;
    policy.CpuUpload = TerrainLodCpuUploadAction::FullBuffer;
    return policy;
}

[[nodiscard]] constexpr TerrainLodPassPolicy MakeTerrainLodMaximumSafeParallelFullOutputPolicy()
{
    TerrainLodPassPolicy policy = MakeTerrainLodMaximumSafeParallelIncrementalPolicy();
    // 当前全量网格写入只有串行实现，前面的评分与拓扑阶段仍采用最大安全并行
    policy.MeshEmit = TerrainLodMeshEmitAction::SerialFull;
    policy.CpuUpload = TerrainLodCpuUploadAction::FullBuffer;
    return policy;
}

[[nodiscard]] constexpr TerrainLodPassPolicy MakeTerrainLodFixedSerialPolicy()
{
    return MakeTerrainLodSerialIncrementalPolicy();
}

[[nodiscard]] constexpr TerrainLodPassPolicy MakeTerrainLodMaximumParallelPolicy()
{
    return MakeTerrainLodMaximumSafeParallelIncrementalPolicy();
}

[[nodiscard]] constexpr TerrainLodPassPolicy MakeTerrainLodSerialFullPolicy()
{
    return MakeTerrainLodSerialFullOutputPolicy();
}

/// <summary>
/// 标识研究计划中需要独立观察和比较的处理阶段
/// </summary>
enum class TerrainLodPassId
{
    MergeScore,
    SplitScore,
    MergeTopology,
    SplitTopology,
    MeshEmit,
    CpuUpload,
    Count,
};

/// <summary>
/// 描述调用方请求或当前实现实际采用的处理方式
/// </summary>
enum class TerrainLodPassAction
{
    NotRun,
    Automatic,
    SerialFullRefresh,
    ParallelFullRefresh,
    SerialImmediate,
    ParallelAssisted,
    SerialDirty,
    ParallelDirty,
    SerialFull,
    DirtyRange,
    FullBuffer,
    MixedUpload,
};

/// <summary>
/// 说明请求方式没有直接成为实际方式的原因
/// </summary>
enum class TerrainLodPassFallbackReason
{
    None,
    NoWork,
    ParallelDisabled,
    BelowParallelThreshold,
    ResourceCapacity,
    FrameSlotBacklog,
    MeshInitialization,
};

/// <summary>
/// 记录活动集合和优先队列成员的维护方式
/// </summary>
enum class TerrainLodMembershipUpdateMode
{
    NotApplicable,
    Incremental,
    FullRebuild,
};

/// <summary>
/// 记录与视点相关的优先级是否重新计算
/// </summary>
enum class TerrainLodPriorityRefreshMode
{
    NotApplicable,
    FullAllCurrentEntries,
};

/// <summary>
/// 记录拓扑、网格或上传数据的更新范围
/// </summary>
enum class TerrainLodDataUpdateMode
{
    NotApplicable,
    Incremental,
    Full,
    Mixed,
};

/// <summary>
/// 保存单个处理阶段的请求方式、实际方式、输入规模和内部成本
/// </summary>
struct TerrainLodPassTrace
{
    TerrainLodPassId Id{TerrainLodPassId::MergeScore};
    TerrainLodPassAction RequestedAction{TerrainLodPassAction::NotRun};
    TerrainLodPassAction EffectiveAction{TerrainLodPassAction::NotRun};
    TerrainLodPassFallbackReason FallbackReason{TerrainLodPassFallbackReason::None};
    TerrainLodMembershipUpdateMode MembershipUpdate{TerrainLodMembershipUpdateMode::NotApplicable};
    TerrainLodPriorityRefreshMode PriorityRefresh{TerrainLodPriorityRefreshMode::NotApplicable};
    TerrainLodDataUpdateMode DataUpdate{TerrainLodDataUpdateMode::NotApplicable};
    std::size_t RequestedWorkerCount{0U};
    std::size_t EffectiveWorkerCount{0U};
    std::size_t CandidateCount{0U};
    std::size_t DirtyItemCount{0U};
    // 评分与建堆只用于全量优先级刷新，候选快照属于后续拓扑规划
    float ScoreMilliseconds{0.0F};
    float HeapifyMilliseconds{0.0F};
    float CandidateSnapshotMilliseconds{0.0F};
    // 队列成员由拓扑修改局部维护，不计入评分阶段的包络耗时
    std::size_t MembershipUpdateCount{0U};
    float MembershipUpdateMilliseconds{0.0F};
    float WallMilliseconds{0.0F};
};

/// <summary>
/// 保存一次冻结拓扑输入在指定执行方式下的结果和分项耗时
/// </summary>
struct TerrainLodTopologyReplayEvidence
{
    TerrainLodPassAction Action{TerrainLodPassAction::NotRun};
    std::uint64_t TopologyHash{0U};
    std::uint64_t ActiveLeafHash{0U};
    std::uint64_t QueueMembershipHash{0U};
    std::uint64_t MeshEditHash{0U};
    std::size_t ActiveTriangleCount{0U};
    std::size_t InteriorCandidateCount{0U};
    std::size_t BoundaryCandidateCount{0U};
    std::size_t NonEmptyChunkCount{0U};
    std::size_t EffectiveWorkerCount{0U};
    std::size_t EarlyCommitCount{0U};
    std::size_t BudgetViolationCount{0U};
    std::size_t QueueInvariantViolationCount{0U};
    std::size_t TjunctionCount{0U};
    std::size_t InvalidNeighborCount{0U};
    std::size_t InvalidTopologyCount{0U};
    float StateCloneMilliseconds{0.0F};
    float ChunkBuildMilliseconds{0.0F};
    float QueueInvalidationMilliseconds{0.0F};
    float CommitMilliseconds{0.0F};
    float ResultMergeMilliseconds{0.0F};
    float IndexQueueRefreshMilliseconds{0.0F};
    float SerialConvergenceMilliseconds{0.0F};
    float WallMilliseconds{0.0F};
};

/// <summary>
/// 记录同一候选快照分别走串行和并行辅助路径后的配对证据
/// </summary>
struct TerrainLodTopologyPairEvidence
{
    bool Evaluated{false};
    bool Equivalent{false};
    std::uint64_t FrozenCandidateHash{0U};
    std::size_t FrozenCandidateCount{0U};
    float CandidateSnapshotMilliseconds{0.0F};
    float EvidenceMilliseconds{0.0F};
    TerrainLodTopologyReplayEvidence Serial;
    TerrainLodTopologyReplayEvidence Parallel;
};

inline constexpr std::size_t TerrainLodPassCount = static_cast<std::size_t>(TerrainLodPassId::Count);
using TerrainLodPassTraceArray = std::array<TerrainLodPassTrace, TerrainLodPassCount>;

[[nodiscard]] constexpr TerrainLodPassTraceArray MakeTerrainLodPassTraces()
{
    TerrainLodPassTraceArray traces{};
    for (std::size_t index = 0U; index < traces.size(); ++index)
    {
        traces[index].Id = static_cast<TerrainLodPassId>(index);
    }
    return traces;
}

[[nodiscard]] constexpr TerrainLodPassTrace& TerrainLodPassTraceFor(
    TerrainLodPassTraceArray& traces,
    TerrainLodPassId id)
{
    return traces[static_cast<std::size_t>(id)];
}

[[nodiscard]] constexpr const TerrainLodPassTrace& TerrainLodPassTraceFor(
    const TerrainLodPassTraceArray& traces,
    TerrainLodPassId id)
{
    return traces[static_cast<std::size_t>(id)];
}

[[nodiscard]] constexpr std::string_view ToString(TerrainLodPassId value)
{
    switch (value)
    {
    case TerrainLodPassId::MergeScore: return "mergeScore";
    case TerrainLodPassId::SplitScore: return "splitScore";
    case TerrainLodPassId::MergeTopology: return "mergeTopology";
    case TerrainLodPassId::SplitTopology: return "splitTopology";
    case TerrainLodPassId::MeshEmit: return "meshEmit";
    case TerrainLodPassId::CpuUpload: return "cpuUpload";
    case TerrainLodPassId::Count: break;
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view ToString(TerrainLodPassAction value)
{
    switch (value)
    {
    case TerrainLodPassAction::NotRun: return "notRun";
    case TerrainLodPassAction::Automatic: return "automatic";
    case TerrainLodPassAction::SerialFullRefresh: return "serialFullRefresh";
    case TerrainLodPassAction::ParallelFullRefresh: return "parallelFullRefresh";
    case TerrainLodPassAction::SerialImmediate: return "serialImmediate";
    case TerrainLodPassAction::ParallelAssisted: return "parallelAssisted";
    case TerrainLodPassAction::SerialDirty: return "serialDirty";
    case TerrainLodPassAction::ParallelDirty: return "parallelDirty";
    case TerrainLodPassAction::SerialFull: return "serialFull";
    case TerrainLodPassAction::DirtyRange: return "dirtyRange";
    case TerrainLodPassAction::FullBuffer: return "fullBuffer";
    case TerrainLodPassAction::MixedUpload: return "mixedUpload";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view ToString(TerrainLodPassFallbackReason value)
{
    switch (value)
    {
    case TerrainLodPassFallbackReason::None: return "none";
    case TerrainLodPassFallbackReason::NoWork: return "noWork";
    case TerrainLodPassFallbackReason::ParallelDisabled: return "parallelDisabled";
    case TerrainLodPassFallbackReason::BelowParallelThreshold: return "belowParallelThreshold";
    case TerrainLodPassFallbackReason::ResourceCapacity: return "resourceCapacity";
    case TerrainLodPassFallbackReason::FrameSlotBacklog: return "frameSlotBacklog";
    case TerrainLodPassFallbackReason::MeshInitialization: return "meshInitialization";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view ToString(TerrainLodScoreRefreshAction value)
{
    switch (value)
    {
    case TerrainLodScoreRefreshAction::Automatic: return "automatic";
    case TerrainLodScoreRefreshAction::SerialRefresh: return "serialRefresh";
    case TerrainLodScoreRefreshAction::ParallelRefresh: return "parallelRefresh";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view ToString(TerrainLodTopologyAction value)
{
    switch (value)
    {
    case TerrainLodTopologyAction::Automatic: return "automatic";
    case TerrainLodTopologyAction::SerialImmediate: return "serialImmediate";
    case TerrainLodTopologyAction::ParallelAssisted: return "parallelAssisted";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view ToString(TerrainLodMeshEmitAction value)
{
    switch (value)
    {
    case TerrainLodMeshEmitAction::Automatic: return "automatic";
    case TerrainLodMeshEmitAction::SerialDirty: return "serialDirty";
    case TerrainLodMeshEmitAction::ParallelDirty: return "parallelDirty";
    case TerrainLodMeshEmitAction::SerialFull: return "serialFull";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view ToString(TerrainLodCpuUploadAction value)
{
    switch (value)
    {
    case TerrainLodCpuUploadAction::Automatic: return "automatic";
    case TerrainLodCpuUploadAction::DirtyRange: return "dirtyRange";
    case TerrainLodCpuUploadAction::FullBuffer: return "fullBuffer";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view ToString(TerrainLodParallelTopologyPhase value)
{
    switch (value)
    {
    case TerrainLodParallelTopologyPhase::Both: return "both";
    case TerrainLodParallelTopologyPhase::SplitOnly: return "split";
    case TerrainLodParallelTopologyPhase::MergeOnly: return "merge";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view ToString(TerrainLodMembershipUpdateMode value)
{
    switch (value)
    {
    case TerrainLodMembershipUpdateMode::NotApplicable: return "notApplicable";
    case TerrainLodMembershipUpdateMode::Incremental: return "incremental";
    case TerrainLodMembershipUpdateMode::FullRebuild: return "fullRebuild";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view ToString(TerrainLodPriorityRefreshMode value)
{
    switch (value)
    {
    case TerrainLodPriorityRefreshMode::NotApplicable: return "notApplicable";
    case TerrainLodPriorityRefreshMode::FullAllCurrentEntries: return "fullAllCurrentEntries";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view ToString(TerrainLodDataUpdateMode value)
{
    switch (value)
    {
    case TerrainLodDataUpdateMode::NotApplicable: return "notApplicable";
    case TerrainLodDataUpdateMode::Incremental: return "incremental";
    case TerrainLodDataUpdateMode::Full: return "full";
    case TerrainLodDataUpdateMode::Mixed: return "mixed";
    }
    return "unknown";
}

inline constexpr std::uint64_t TerrainLodHashOffset = 14695981039346656037ULL;
inline constexpr std::uint64_t TerrainLodHashPrime = 1099511628211ULL;

template <typename Value>
void AppendTerrainLodHash(std::uint64_t& hash, const Value& value)
{
    static_assert(std::is_trivially_copyable_v<Value>);
    const auto bytes = std::bit_cast<std::array<std::byte, sizeof(Value)>>(value);
    for (const std::byte byte : bytes)
    {
        hash ^= static_cast<std::uint64_t>(std::to_integer<unsigned int>(byte));
        hash *= TerrainLodHashPrime;
    }
}

inline void AppendTerrainLodHash(std::uint64_t& hash, std::string_view value)
{
    for (const char character : value)
    {
        hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(character));
        hash *= TerrainLodHashPrime;
    }
}

[[nodiscard]] inline std::uint64_t HashTerrainLodPathIds(std::vector<std::uint64_t> pathIds)
{
    std::sort(pathIds.begin(), pathIds.end());
    std::uint64_t hash = TerrainLodHashOffset;
    AppendTerrainLodHash(hash, pathIds.size());
    for (const std::uint64_t pathId : pathIds)
    {
        AppendTerrainLodHash(hash, pathId);
    }
    return hash;
}

[[nodiscard]] inline std::uint64_t HashTerrainLodMesh(const Terrain::TerrainMeshData& mesh)
{
    std::uint64_t hash = TerrainLodHashOffset;
    AppendTerrainLodHash(hash, mesh.Vertices.size());
    AppendTerrainLodHash(hash, mesh.Indices.size());
    AppendTerrainLodHash(hash, mesh.GridWidth);
    AppendTerrainLodHash(hash, mesh.GridHeight);
    AppendTerrainLodHash(hash, mesh.TerrainSize);
    AppendTerrainLodHash(hash, mesh.HeightScale);
    for (const Terrain::TerrainMeshVertex& vertex : mesh.Vertices)
    {
        AppendTerrainLodHash(hash, vertex.Position.x);
        AppendTerrainLodHash(hash, vertex.Position.y);
        AppendTerrainLodHash(hash, vertex.Position.z);
        AppendTerrainLodHash(hash, vertex.Normal.x);
        AppendTerrainLodHash(hash, vertex.Normal.y);
        AppendTerrainLodHash(hash, vertex.Normal.z);
        AppendTerrainLodHash(hash, vertex.TexCoord.x);
        AppendTerrainLodHash(hash, vertex.TexCoord.y);
        AppendTerrainLodHash(hash, vertex.Height);
        AppendTerrainLodHash(hash, vertex.DebugColor.x);
        AppendTerrainLodHash(hash, vertex.DebugColor.y);
        AppendTerrainLodHash(hash, vertex.DebugColor.z);
        AppendTerrainLodHash(hash, vertex.DebugHighlight);
    }
    for (const std::uint32_t index : mesh.Indices)
    {
        AppendTerrainLodHash(hash, index);
    }
    return hash;
}

[[nodiscard]] inline std::uint64_t HashTerrainLodNormalizedMesh(
    const Terrain::TerrainMeshData& mesh,
    const std::vector<std::uint64_t>& slotPathIds)
{
    constexpr std::size_t verticesPerTriangle = 3U;
    if (mesh.Vertices.size() != slotPathIds.size() * verticesPerTriangle ||
        mesh.Indices.size() != slotPathIds.size() * verticesPerTriangle)
    {
        return 0U;
    }

    std::vector<std::size_t> slotOrder(slotPathIds.size());
    std::iota(slotOrder.begin(), slotOrder.end(), 0U);
    std::sort(
        slotOrder.begin(),
        slotOrder.end(),
        [&slotPathIds](std::size_t left, std::size_t right) {
            return slotPathIds[left] < slotPathIds[right];
        });

    std::uint64_t hash = TerrainLodHashOffset;
    AppendTerrainLodHash(hash, slotPathIds.size());
    AppendTerrainLodHash(hash, mesh.GridWidth);
    AppendTerrainLodHash(hash, mesh.GridHeight);
    AppendTerrainLodHash(hash, mesh.TerrainSize);
    AppendTerrainLodHash(hash, mesh.HeightScale);
    for (const std::size_t slot : slotOrder)
    {
        AppendTerrainLodHash(hash, slotPathIds[slot]);
        const std::size_t baseIndex = slot * verticesPerTriangle;
        for (std::size_t offset = 0U; offset < verticesPerTriangle; ++offset)
        {
            const Terrain::TerrainMeshVertex& vertex = mesh.Vertices[baseIndex + offset];
            AppendTerrainLodHash(hash, vertex.Position.x);
            AppendTerrainLodHash(hash, vertex.Position.y);
            AppendTerrainLodHash(hash, vertex.Position.z);
            AppendTerrainLodHash(hash, vertex.Normal.x);
            AppendTerrainLodHash(hash, vertex.Normal.y);
            AppendTerrainLodHash(hash, vertex.Normal.z);
            AppendTerrainLodHash(hash, vertex.TexCoord.x);
            AppendTerrainLodHash(hash, vertex.TexCoord.y);
            AppendTerrainLodHash(hash, vertex.Height);
            // 调试颜色记录网格变化过程，不属于策略之间需要保持一致的地形几何
            const std::uint32_t relativeIndex = mesh.Indices[baseIndex + offset] -
                static_cast<std::uint32_t>(baseIndex);
            AppendTerrainLodHash(hash, relativeIndex);
        }
    }
    return hash;
}
} // 命名空间 ParallelRoam::Algorithms
