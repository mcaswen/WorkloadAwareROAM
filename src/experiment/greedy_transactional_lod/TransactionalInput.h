#pragma once

#include <string_view>

#include "algorithms/greedy_transactional_lod/TransactionalState.h"
#include <filesystem>

namespace ParallelRoam::Experiment::GreedyTransactionalLod
{
using Algorithms::GreedyTransactionalLod::InitialMesh;
using Algorithms::GreedyTransactionalLod::Configuration;
using Algorithms::GreedyTransactionalLod::TransactionalState;
/// <summary>
/// 中立几何的文件适配，不保存来源引用，不参与正常事务循环
/// </summary>
class TransactionalInput
{
public:
    /// <summary>
    /// 读取自有几何与独立原始高度，并在初始化前冻结额度策略。
    /// 离线结果可显式复用来源目录，不从当前网格反推参考高度。
    /// </summary>
    static InitialMesh Load(const std::filesystem::path& snapshot,std::string_view limitPolicy="fixed64",
        const std::filesystem::path& sourceDirectory={});
    static std::vector<Configuration> Views(const std::filesystem::path& root,const Configuration& initial);
    static void Write(const TransactionalState& state,const std::filesystem::path& output);
};
}
