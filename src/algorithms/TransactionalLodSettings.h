#pragma once

#include <cstddef>

namespace ParallelRoam::Algorithms
{
/// <summary>
/// 事务化算法的持续配置；任一字段变化都重新初始化独立状态
/// 旧 DOD 阶段动作不映射到这些设置
/// </summary>
struct TransactionalLodSettings
{
    std::size_t WorkerCount{4};
    std::size_t PrefixLimit{64};
    std::size_t DonorLimit{64};
    std::size_t SampleVisitLimit{1000000};
    bool HeightGuard{};
    bool operator==(const TransactionalLodSettings&) const = default;
};
}
