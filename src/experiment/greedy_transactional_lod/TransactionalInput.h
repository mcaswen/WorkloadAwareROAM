#pragma once

#include "experiment/greedy_transactional_lod/TransactionalState.h"
#include <filesystem>

namespace ParallelRoam::Experiment::GreedyTransactionalLod
{
/// <summary>
/// 中立几何的文件适配，不保存来源引用，不参与正常事务循环
/// </summary>
class TransactionalInput
{
public:
    static InitialMesh Load(const std::filesystem::path& snapshot);
    static void Write(const TransactionalState& state,const std::filesystem::path& output);
};
}
