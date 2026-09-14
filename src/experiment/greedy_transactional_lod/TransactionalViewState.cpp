#include "experiment/greedy_transactional_lod/TransactionalViewState.h"

namespace ParallelRoam::Experiment::GreedyTransactionalLod
{
std::unique_ptr<SampleProjection[]> TransactionalViewState::Allocate(WorkLedger& work) const
{
    // 数组对象已构造，但数值留给覆盖写；不在每次换视图前重复清零
    auto result=std::make_unique_for_overwrite<SampleProjection[]>(_count);
    ++work.ViewBufferAllocations;work.ViewBufferBytes+=_count*sizeof(SampleProjection);
    return result;
}

void TransactionalViewState::Initialize(std::size_t count,WorkLedger& work)
{
    _count=count;_current=Allocate(work);_spare.reset();
    // 初建可能逐面遇到共享样本，当前读视图先具有确定的空值
    std::fill_n(_current.get(),_count,SampleProjection{0,false});
}

std::span<SampleProjection> TransactionalViewState::Prepare(WorkLedger& work)
{
    if (!_spare) _spare=Allocate(work);
    return {_spare.get(),_count};
}

void TransactionalViewState::Publish() noexcept
{
    _current.swap(_spare);
}
}
