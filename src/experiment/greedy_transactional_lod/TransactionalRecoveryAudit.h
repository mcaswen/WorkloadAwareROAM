#pragma once

#include "experiment/greedy_transactional_lod/TransactionalFitCounterfactual.h"
#include <optional>

namespace ParallelRoam::Experiment::GreedyTransactionalLod
{
/// <summary>
/// 固定外边界内的私有几何；旧点不移动，最多引入一个内部新点。
/// 不携带生产槽位、队列或提交权限。
/// </summary>
struct RecoveryGeometry
{
    std::map<Algorithms::GreedyTransactionalLod::Identity,Algorithms::GreedyTransactionalLod::Point> Points;
    std::vector<std::array<Algorithms::GreedyTransactionalLod::Identity,3>> Faces;
    std::optional<Algorithms::GreedyTransactionalLod::Identity> NewVertex;
};

/// <summary>
/// 在冻结根及直接邻面内调查有限恢复，不把几何候选发布到生产状态。
/// 新连接先定义自己的仿射基准，再由原认证器验证实际高度。
/// </summary>
class TransactionalRecoveryAudit
{
public:
    /// <summary>
    /// 只替换严格凸四边形的对角线，旧点和外边界保持不变。
    /// 此处检查构造前提；最小角由最终复合几何统一验收。
    /// </summary>
    static std::optional<RecoveryGeometry> Flip(const RecoveryGeometry& input,
        Algorithms::GreedyTransactionalLod::Edge edge);
    /// <summary>
    /// 在域内双侧共有边插入中点，净增加两个面。
    /// 单侧外边界及已有新点的输入不属于本轮目录。
    /// </summary>
    static std::optional<RecoveryGeometry> SplitEdge(const RecoveryGeometry& input,
        Algorithms::GreedyTransactionalLod::Edge edge,Algorithms::GreedyTransactionalLod::Identity id);
    /// <summary>
    /// 在指定面的严格内部建立三面扇形，新点初高取当前私有曲面。
    /// 后续改变高度不会改变参数域形状或影响旧点。
    /// </summary>
    static std::optional<RecoveryGeometry> SplitFace(const RecoveryGeometry& input,std::size_t face,
        Algorithms::GreedyTransactionalLod::Point point,Algorithms::GreedyTransactionalLod::Identity id);
    /// <summary>
    /// 为一个自由高度生成可行区间约束，包含翻边引起的固定几何差值。
    /// 区间只提供高度候选，不能替代原认证器对实际浮点结果的检查。
    /// </summary>
    static std::vector<FitSampleConstraint> Model(const Algorithms::GreedyTransactionalLod::TransactionalState& state,
        const Algorithms::GreedyTransactionalLod::TransactionalSamples& samples,
        const Algorithms::GreedyTransactionalLod::Proposal& initial,Algorithms::GreedyTransactionalLod::WorkLedger& work);
    /// <summary>
    /// 输出冻结frame8的完整有限目录与费用；超限或未知保留独立状态。
    /// 未来视图只用于报告见证，不参与选择，原24帧仍应用原批次。
    /// </summary>
    static void Run(const Algorithms::GreedyTransactionalLod::TransactionalState& state,
        const Algorithms::GreedyTransactionalLod::TransactionalSamples& samples,
        const Algorithms::GreedyTransactionalLod::Configuration& future,
        const Algorithms::GreedyTransactionalLod::Point& witness,const std::filesystem::path& output);
};
}
