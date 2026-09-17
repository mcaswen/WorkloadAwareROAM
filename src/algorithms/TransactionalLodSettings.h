#pragma once

#include <cstddef>

namespace ParallelRoam::Algorithms
{
/// <summary>
/// 接收方全局前缀的排序政策；候选资格与捐赠评分仍使用复合紧迫性
/// </summary>
enum class TransactionalReceiverOrder
{
    Composite,
    ErrorFirst
};

/// <summary>
/// 质量目标政策与旧排序独立；新政策显式约束每个被修改的样本
/// </summary>
enum class TransactionalQualityPolicy
{
    Legacy,
    PointwiseTarget
};

/// <summary>
/// 接收提案的新点高度来源；源高度只生成候选，仍须通过逐点质量认证。
/// 不改变旧点高度、目录连接或边界与翻边的既有规则。
/// </summary>
enum class TransactionalReceiverHeightPolicy
{
    LegacyFit,
    SourceHeight
};

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
    TransactionalReceiverOrder ReceiverOrder{TransactionalReceiverOrder::Composite};
    TransactionalQualityPolicy QualityPolicy{TransactionalQualityPolicy::Legacy};
    TransactionalReceiverHeightPolicy ReceiverHeightPolicy{TransactionalReceiverHeightPolicy::LegacyFit};
    double QualityTargetPixels{0.5};
    double QualityHeightRatio{1.0 / 256.0};
    bool operator==(const TransactionalLodSettings&) const = default;
};
}
