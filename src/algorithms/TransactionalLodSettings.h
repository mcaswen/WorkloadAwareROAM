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
    // 保留旧点高度只限制拟合自由度，不保证重连后的内部曲面误差
    bool PreserveSurvivingHeights{};
    // 原目录全形状失败时尝试净零翻边，仍采用当前视图的进展认证
    bool EnableFlipRecovery{};
    // 开启后按实际面数预留边界细分，旧点仍固定且不回收边界点
    bool EnableBoundaryRefinement{};
    bool operator==(const TransactionalLodSettings&) const = default;
};
}
