#pragma once

#include "algorithms/greedy_transactional_lod/TransactionalCertification.h"
#include <filesystem>
#include <span>

namespace ParallelRoam::Experiment::GreedyTransactionalLod
{
/// <summary>
/// 一个可见样本对单个高度增量的约束事实，固定于同一未拟合提案
/// 投影分母和近面条件随增量变化，不能视为固定误差权重
/// </summary>
struct FitSampleConstraint
{
    Algorithms::GreedyTransactionalLod::Slot Sample{};
    double Weight{}, Difference{}, Factor{}, ClipW{}, WDerivative{}, NearValue{}, NearDerivative{};
};

/// <summary>
/// 区间求交的模型状态；数值未知不能合并成几何不可行
/// </summary>
enum class FitQueryStatus { Feasible, Empty, NumericUnknown };

/// <summary>
/// 浮点保守模型的一次阈值查询，保留收紧区间的样本与约束行
/// 判空只属于该模型，最终接受仍由原质量认证决定
/// </summary>
struct FitIntervalQuery
{
    FitQueryStatus Status{FitQueryStatus::NumericUnknown};
    double Target{}, Lower{}, Upper{};
    Algorithms::GreedyTransactionalLod::Slot LowerSample{Algorithms::GreedyTransactionalLod::InvalidSlot};
    Algorithms::GreedyTransactionalLod::Slot UpperSample{Algorithms::GreedyTransactionalLod::InvalidSlot};
    int LowerRow{-1}, UpperRow{-1};
};

/// <summary>
/// 有限二分的全部查询与最后可行端，未收敛也保留已知候选
/// </summary>
struct FitIntervalSearch
{
    std::vector<FitIntervalQuery> Queries;
    FitIntervalQuery Best;
    double LowerTarget{};
    bool HasEmptyLower{};
    std::string Status;
};

/// <summary>
/// 独立探针对冻结的一维拟合做反事实，不改变生产状态或实际批次
/// 区间模型与最终原生认证分开，查询预算和未知结果均显式记录
/// </summary>
class TransactionalFitCounterfactual
{
public:
    static FitIntervalQuery Query(std::span<const FitSampleConstraint> samples,double target,
        double lower,double upper,Algorithms::GreedyTransactionalLod::WorkLedger& work);
    static FitIntervalSearch Search(std::span<const FitSampleConstraint> samples,const FitIntervalQuery& original,
        Algorithms::GreedyTransactionalLod::WorkLedger& work);
    static void Run(const Algorithms::GreedyTransactionalLod::TransactionalState& state,
        const Algorithms::GreedyTransactionalLod::TransactionalSamples& samples,
        const Algorithms::GreedyTransactionalLod::Proposal& approved,
        const Algorithms::GreedyTransactionalLod::Configuration& future,
        const Algorithms::GreedyTransactionalLod::Configuration& returned,
        const Algorithms::GreedyTransactionalLod::Point& witness,const std::filesystem::path& output);
};
}
