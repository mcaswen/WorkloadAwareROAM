#pragma once

#include "algorithms/greedy_transactional_lod/TransactionalState.h"
#include "algorithms/greedy_transactional_lod/TransactionalCommit.h"
#include "algorithms/greedy_transactional_lod/TransactionalViewState.h"
#include "algorithms/greedy_transactional_lod/TransactionalPriorityIndex.h"

namespace ParallelRoam::Algorithms::GreedyTransactionalLod
{
// 负排序值选择最高复合紧迫性或几何误差，稳定身份提供唯一同分顺序
using PriorityKey=std::tuple<double,Identity,Slot>;
using DonorKey=std::pair<double,Identity>;
using ReceiverIndex=TransactionalPriorityIndex<PriorityKey>;
using DonorIndex=TransactionalPriorityIndex<DonorKey>;
/// <summary>
/// 样本的持久几何与归属，只在地形补丁发布时变化
/// 视图值单独存储，换相机无需复制这些字段
/// </summary>
struct SampleGeometry
{
    double ReferenceHeight{}, MeshHeight{}, HeightError{};
    Slot Owner{InvalidSlot};
};

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
    ReceiverIndex::Repair Order;
    DonorIndex::Repair Donors;
    std::set<Identity> InvalidatedRoots, InvalidatedDonors;
    std::size_t FaceSlots{};
};

/// <summary>
/// 视图刷新只暂存投影和评分，不复制拓扑、归属或 mesh
/// 相机变化的全 Q 成本单列，发布前失败保留旧视图
/// </summary>
struct PreparedView
{
    std::vector<double> Priority;
    ReceiverIndex Order;
    DonorIndex Donors;
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
        const TransactionalExecution& execution={});
    void PublishView(PreparedView&& prepared) noexcept;
    std::array<std::uint32_t, 2> Decode(Slot sample) const;
    Point Parameter(Slot sample) const;
    std::uint32_t Denominator() const { return 6 * (_source.Width - 1); }
    const HeightSource& Source() const { return _source; }
    std::size_t SampleCount() const { return _values.size(); }
    const SampleGeometry& Geometry(Slot sample) const { return _values.at(sample); }
    const SampleProjection& Projection(Slot sample) const { return _view.Read(sample); }
    SampleValue Value(Slot sample) const
    {
        const auto& g=Geometry(sample);const auto& p=Projection(sample);
        return {g.ReferenceHeight,g.MeshHeight,p.ErrorSquared,g.HeightError,g.Owner,p.Visible};
    }
    const std::vector<Slot>& FaceSamples(Slot face) const { return _faceSamples.at(face); }
    std::vector<Slot> Raw() const { return Prefix(_order.Count()); }
    std::size_t RawCount() const { return _order.Count(); }
    std::vector<Slot> Prefix(std::size_t limit,WorkLedger* work=nullptr) const;
    std::vector<Identity> DonorPool(std::size_t limit,WorkLedger* work=nullptr) const;
    double PrioritySquared(Slot face) const { return _priority.at(face); }
    std::vector<Slot> VisibleSupport(const std::vector<Slot>& support) const;
    double SourceHeight(std::uint32_t x, std::uint32_t y, double scale) const;
    /// <summary>
    /// 查询未必落在公共样本格上的参数点，源数据由调用者只读借用
    /// </summary>
    static double SourceHeightAt(const HeightSource& source, double u, double v, double scale);
    static std::array<double, 4> Clip(const Configuration& config, double u, double v, double height);
    bool Weights(Slot sample, const Point& a, const Point& b, const Point& c, std::array<double,3>& weights) const;
    bool StrictlyInside(Slot sample,const Point& a,const Point& b,const Point& c) const;
    /// <summary>
    /// 返回局部矩形内的保守样本候选，调用方仍须在准确数值域检查覆盖
    /// </summary>
    std::vector<Slot> BoxCandidates(double minU, double minV, double maxU, double maxV,
        WorkLedger& work) const;

private:
    void Store(Slot sample,const SampleValue& value) noexcept;
    // 枚举只需闭面成员资格，实际插值调用者另取同一权重表达式
    bool Contains(Slot sample, const Point& a, const Point& b, const Point& c) const;
    std::vector<Slot> Enumerate(const std::array<Point,3>& points,WorkLedger& work) const;
    SampleValue Evaluate(const Configuration& config,Slot sample,Slot owner,double height,WorkLedger& work) const;
    SampleValue Project(const Configuration& config,Slot sample,SampleValue value,WorkLedger& work) const;
    static double Priority(const Configuration& config,const std::array<Point,3>& points,double maximum);
    /// <summary>
    /// 旧政策按复合分数判断资格，显式质量目标只产生误差超标请求
    /// </summary>
    static std::optional<PriorityKey> ReceiverKey(const Configuration& config, double priority,
        double maximum, Identity id, Slot slot);
    std::array<double,3> StoredWeights(Slot sample,const Point& a,const Point& b,const Point& c) const;
    static void BuildOrders(const TransactionalState& state,const std::vector<double>& priority,
        std::vector<std::optional<PriorityKey>> faces, ReceiverIndex& order,DonorIndex& donors,WorkLedger& work);
    // 各组用连续身份区间编码栅格偏移，避免为每个 Q 保存一份坐标
    struct Group { std::uint32_t X, Y, Columns, Rows; Slot Start; };
    HeightSource _source;
    std::array<Group,6> _groups;
    std::vector<SampleGeometry> _values;
    TransactionalViewState _view;
    std::vector<std::vector<Slot>> _faceSamples;
    std::vector<double> _priority;
    ReceiverIndex _order;
    DonorIndex _donors;
};
}
