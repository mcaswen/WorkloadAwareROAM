#pragma once

#include "terrain/TerrainMeshBuilder.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <vector>

namespace ParallelRoam::Algorithms
{
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
/// 保存单个处理阶段的请求方式、实际方式、输入规模和完整包络耗时
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
    float WallMilliseconds{0.0F};
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
} // 命名空间 ParallelRoam::Algorithms
