#pragma once

#include "algorithms/greedy_transactional_lod/TransactionalTypes.h"
#include <span>

namespace ParallelRoam::Algorithms::GreedyTransactionalLod
{
/// <summary>
/// 与相机代际绑定的样本投影，几何高度和 owner 由 Samples 单独持有
/// 备用存储必须完整填写，未发布的数据不能作为当前视图读取
/// </summary>
struct SampleProjection
{
    double ErrorSquared;
    bool Visible;
};

/// <summary>
/// 当前和备用投影数组的唯一所有者，发布只交换所有权
/// 准备失败保留当前数组，下一次准备完整覆盖未发布的备用内容
/// </summary>
class TransactionalViewState
{
public:
    /// <summary>
    /// 首建当前投影并释放旧备用；不用于已发布状态的增量替换。
    /// </summary>
    void Initialize(std::size_t count,WorkLedger& work);
    /// <summary>
    /// 借出不可见备用数组；调用方必须在成功发布前覆盖每个样本。
    /// </summary>
    std::span<SampleProjection> Prepare(WorkLedger& work);
    /// <summary>
    /// 完整准备成功后交换两代，局部修复随后写入新的当前数组。
    /// </summary>
    void Publish() noexcept;
    // 初始化后的有效样本槽才能读取；返回引用不跨 Publish 保留。
    const SampleProjection& Read(Slot sample) const { return _current[sample]; }
    void Write(Slot sample,SampleProjection value) noexcept { _current[sample]=value; }

private:
    std::unique_ptr<SampleProjection[]> Allocate(WorkLedger& work) const;
    std::size_t _count{};
    std::unique_ptr<SampleProjection[]> _current, _spare;
};
}
