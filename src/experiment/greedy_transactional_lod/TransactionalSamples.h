#pragma once

#include "experiment/greedy_transactional_lod/TransactionalState.h"

namespace ParallelRoam::Experiment::GreedyTransactionalLod
{
/// <summary>
/// 固定样本的当前评价；唯一 owner 与全部闭面贡献分开保存
/// </summary>
struct SampleValue
{
    double ReferenceHeight{}, MeshHeight{}, ErrorSquared{}, HeightError{};
    Slot Owner{InvalidSlot};
    bool Visible{};
};

/// <summary>
/// 六组公共参数域样本的当前关联与评分，参考由原始高度场定义
/// 视图变化不改变采样身份，闭边样本不能只计入 owner 的评分
/// </summary>
class TransactionalSamples
{
public:
    explicit TransactionalSamples(HeightSource source);
    void Refresh(const TransactionalState& state, WorkLedger& work);
    std::array<std::uint32_t, 2> Decode(Slot sample) const;
    Point Parameter(Slot sample) const;
    std::uint32_t Denominator() const { return 6 * (_source.Width - 1); }
    const HeightSource& Source() const { return _source; }
    const std::vector<SampleValue>& Values() const { return _values; }
    const std::vector<Slot>& FaceSamples(Slot face) const { return _faceSamples.at(face); }
    const std::vector<Slot>& Raw() const { return _raw; }
    double PrioritySquared(Slot face) const { return _priority.at(face); }
    std::vector<Slot> VisibleSupport(const std::vector<Slot>& support) const;
    double SourceHeight(std::uint32_t x, std::uint32_t y, double scale) const;
    static std::array<double, 4> Clip(const Configuration& config, double u, double v, double height);
    bool Weights(Slot sample, const Point& a, const Point& b, const Point& c, std::array<double,3>& weights) const;
    bool StrictlyInside(Slot sample,const Point& a,const Point& b,const Point& c) const;

private:
    // 各组用连续身份区间编码栅格偏移，避免为每个 Q 保存一份坐标
    struct Group { std::uint32_t X, Y, Columns, Rows; Slot Start; };
    HeightSource _source;
    std::array<Group,6> _groups;
    std::vector<SampleValue> _values;
    std::vector<std::vector<Slot>> _faceSamples;
    std::vector<double> _priority;
    std::vector<Slot> _raw;
};
}
