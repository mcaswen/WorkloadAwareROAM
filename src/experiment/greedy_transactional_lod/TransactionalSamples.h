#pragma once

#include "experiment/greedy_transactional_lod/TransactionalState.h"
#include "experiment/greedy_transactional_lod/TransactionalCommit.h"

namespace ParallelRoam::Experiment::GreedyTransactionalLod
{
// 负 priority 使首项就是全局最高紧迫性，稳定身份提供唯一同分顺序
using PriorityKey=std::tuple<double,Identity,Slot>;
using DonorKey=std::pair<double,Identity>;
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
/// 样本修复仅保存变更评价与面贡献，全局需求顺序单独计费
/// </summary>
struct PreparedSamples
{
    std::map<Slot,SampleValue> Values;
    std::map<Slot,std::vector<Slot>> Faces;
    std::map<Slot,double> Priorities;
    std::vector<PriorityKey> RemovedOrder;
    std::set<PriorityKey> AddedOrder;
    std::vector<DonorKey> RemovedDonors;
    std::set<DonorKey> AddedDonors;
    std::map<Identity,double> DonorCosts;
    std::set<Identity> InvalidatedRoots, InvalidatedDonors;
    std::size_t FaceSlots{};
};

/// <summary>
/// 视图刷新只暂存投影和评分，不复制拓扑、归属或 mesh
/// 相机变化的全 Q 成本单列，发布前失败保留旧视图
/// </summary>
struct PreparedView
{
    std::vector<std::pair<double,bool>> Projection;
    std::vector<double> Priority;
    std::set<PriorityKey> Order;
    std::set<DonorKey> Donors;
    std::map<Identity,double> DonorCosts;
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
    PreparedSamples Prepare(const TransactionalState& state,const PreparedTopology& target,WorkLedger& work);
    void Publish(PreparedSamples&& prepared) noexcept;
    PreparedView PrepareView(const TransactionalState& state,const Configuration& view,WorkLedger& work,
        const TransactionalExecution& execution={}) const;
    void PublishView(PreparedView&& prepared) noexcept;
    std::array<std::uint32_t, 2> Decode(Slot sample) const;
    Point Parameter(Slot sample) const;
    std::uint32_t Denominator() const { return 6 * (_source.Width - 1); }
    const HeightSource& Source() const { return _source; }
    const std::vector<SampleValue>& Values() const { return _values; }
    const std::vector<Slot>& FaceSamples(Slot face) const { return _faceSamples.at(face); }
    std::vector<Slot> Raw() const { return Prefix(_order.size()); }
    std::size_t RawCount() const { return _order.size(); }
    std::vector<Slot> Prefix(std::size_t limit) const;
    std::vector<Identity> DonorPool(std::size_t limit) const;
    double PrioritySquared(Slot face) const { return _priority.at(face); }
    std::vector<Slot> VisibleSupport(const std::vector<Slot>& support) const;
    double SourceHeight(std::uint32_t x, std::uint32_t y, double scale) const;
    static std::array<double, 4> Clip(const Configuration& config, double u, double v, double height);
    bool Weights(Slot sample, const Point& a, const Point& b, const Point& c, std::array<double,3>& weights) const;
    bool StrictlyInside(Slot sample,const Point& a,const Point& b,const Point& c) const;

private:
    std::vector<Slot> Enumerate(const std::array<Point,3>& points,WorkLedger& work) const;
    SampleValue Evaluate(const Configuration& config,Slot sample,Slot owner,double height,WorkLedger& work) const;
    SampleValue Project(const Configuration& config,Slot sample,SampleValue value,WorkLedger& work) const;
    static double Priority(const Configuration& config,const std::array<Point,3>& points,double maximum);
    std::array<double,3> StoredWeights(Slot sample,const Point& a,const Point& b,const Point& c) const;
    static void BuildOrders(const TransactionalState& state,const std::vector<double>& priority,
        std::set<PriorityKey>& order,std::set<DonorKey>& donors,std::map<Identity,double>& costs);
    // 各组用连续身份区间编码栅格偏移，避免为每个 Q 保存一份坐标
    struct Group { std::uint32_t X, Y, Columns, Rows; Slot Start; };
    HeightSource _source;
    std::array<Group,6> _groups;
    std::vector<SampleValue> _values;
    std::vector<std::vector<Slot>> _faceSamples;
    std::vector<double> _priority;
    std::set<PriorityKey> _order;
    std::set<DonorKey> _donors;
    std::map<Identity,double> _donorCosts;
};
}
