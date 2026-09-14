#pragma once

#include <cstddef>
#include <cstdint>

namespace ParallelRoam::Algorithms
{
/// <summary>
/// 明确当前阶段的运行结果，失败不能混入正常更新时间样本
/// </summary>
enum class TransactionalLodStatus
{
    Ready, Paused, InputRejected, SeedRejected, ViewRejected, UpdateRejected,
    QuotaExceeded, AllocationFailed, ExecutorStopped, OutputRejected, RetrySuppressed,
};

/// <summary>
/// 公共适配只输出有限统计，研究用全量 map 和尾扫不进入平台热路径
/// 请求线程数与实际线程参与证据分开，后者未经诊断时不填造值
/// </summary>
struct TransactionalLodStats
{
    TransactionalLodStatus Status{TransactionalLodStatus::Ready};
    bool Updated{}, ColdStart{}, HasPublishedMesh{}, ViewPublished{};
    std::size_t RequestedWorkers{}, PrefixLimit{}, DonorLimit{}, SeedTriangles{}, Samples{};
    std::size_t RawCandidates{}, Examined{}, Receivers{}, Need{}, Feasible{}, Exchanges{}, FreeExecuted{};
    std::uint64_t PairChecks{}, Conflicts{}, DonorReuse{}, SampleTouches{}, SampleEvaluations{};
    std::uint64_t VertexWrites{}, IndexWrites{}, SourceBytes{};
    double SeedMilliseconds{}, InitializeMilliseconds{}, ViewMilliseconds{};
    double ReceiverMilliseconds{}, DonorMilliseconds{}, ReservationMilliseconds{};
    double TopologyPrepareMilliseconds{}, TopologyPublishMilliseconds{}, SampleRepairMilliseconds{};
    double MeshPrepareMilliseconds{}, ContinuationPublishMilliseconds{}, AdapterMilliseconds{};
    double CpuReadyMilliseconds{};
};
}
