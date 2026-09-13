#include "experiment/greedy_transactional_lod/TransactionalSamples.h"
#include "experiment/greedy_transactional_lod/TransactionalPredicates.h"

#include <boost/multiprecision/cpp_int.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ParallelRoam::Experiment::GreedyTransactionalLod
{
TransactionalSamples::TransactionalSamples(HeightSource source) : _source(std::move(source))
{
    if (_source.Width < 2 || _source.Width != _source.Height ||
        _source.Values.size() != static_cast<std::size_t>(_source.Width)*_source.Height)
        throw std::runtime_error("参考栅格尺寸不支持");
    const auto n=_source.Width-1;
    // 分组的起点就是持久样本身份，视图刷新不重新编号
    _groups={Group{0,0,n+1,n+1,0},Group{3,0,n,n+1,0},Group{0,3,n+1,n,0},
        Group{3,3,n,n,0},Group{4,2,n,n,0},Group{2,4,n,n,0}};
    std::size_t count=0;
    for (auto& group : _groups)
    {
        group.Start=static_cast<Slot>(count);
        count+=static_cast<std::size_t>(group.Columns)*group.Rows;
    }
    if (count>=InvalidSlot) throw std::runtime_error("样本身份超出范围");
    _values.resize(count);
}

std::array<std::uint32_t,2> TransactionalSamples::Decode(Slot sample) const
{
    for (std::size_t i=_groups.size(); i>0; --i)
    {
        const auto& group=_groups[i-1];
        if (sample<group.Start) continue;
        const auto local=sample-group.Start;
        return {6*(local%group.Columns)+group.X,6*(local/group.Columns)+group.Y};
    }
    throw std::out_of_range("样本身份非法");
}

Point TransactionalSamples::Parameter(Slot sample) const
{
    // 返回的是计算用近似位置；认证时仍由 Decode 恢复精确整数比
    const auto xy=Decode(sample);
    return {static_cast<double>(xy[0])/Denominator(),static_cast<double>(xy[1])/Denominator(),0};
}

double TransactionalSamples::SourceHeight(std::uint32_t ix, std::uint32_t iy, double scale) const
{
    // 上边界落入最后一个 raster cell，插值参数仍可恰好等于一
    const auto n=_source.Width-1, x=std::min(ix/6,n-1), y=std::min(iy/6,n-1);
    const double tx=static_cast<double>(ix-6*x)/6, ty=static_cast<double>(iy-6*y)/6;
    const auto index=static_cast<std::size_t>(y)*_source.Width+x;
    const auto& a=_source.Values;
    const double bottom=(a[index]*(1-tx)+a[index+1]*tx)/65535;
    const double top=(a[index+_source.Width]*(1-tx)+a[index+_source.Width+1]*tx)/65535;
    return ((1-ty)*bottom+ty*top)*scale;
}

std::array<double,4> TransactionalSamples::Clip(const Configuration& config, double u, double v, double height)
{
    const double x=(u-.5)*config.TerrainSize, z=(v-.5)*config.TerrainSize;
    std::array<double,4> result{};
    for (std::size_t i=0; i<4; ++i)
        result[i]=((config.Matrix[i*4]*x+config.Matrix[i*4+1]*height)+config.Matrix[i*4+2]*z)+config.Matrix[i*4+3];
    return result;
}

bool TransactionalSamples::Weights(Slot sample, const Point& a, const Point& b, const Point& c,
    std::array<double,3>& weights) const
{
    const auto q=Parameter(sample);
    // 同时考虑边的方向和真实样本坐标，不能靠负权重容差接收边界点
    const std::array<Point,3> points{a,b,c};
    const auto xy=Decode(sample);
    for (std::size_t i=0; i<3; ++i)
    {
        const auto& p=points[i]; const auto& r=points[(i+1)%3];
        const double first=(r.U-p.U)*(q.V-p.V), second=(r.V-p.V)*(q.U-p.U);
        // 样本的真实坐标是整数比；过滤误差还须包含这次除法的舍入
        const double bound=32*std::numeric_limits<double>::epsilon()*
            (std::abs(first)+std::abs(second)+std::abs(r.U-p.U)+std::abs(r.V-p.V));
        if (first-second>bound) continue;
        if (first-second<-bound) return false;
        using R=boost::multiprecision::cpp_rational;
        const R u=R(xy[0])/Denominator(), v=R(xy[1])/Denominator();
        const R exact=(R(r.U)-p.U)*(v-p.V)-(R(r.V)-p.V)*(u-p.U);
        if (exact<0) return false;
    }
    // 初始来源是 dyadic；缩放后的乘法顺序与冻结 Python 评分一致
    const double factor=1048576.0*Denominator();
    const auto cross=[](double ax,double ay,double bx,double by,double cx,double cy) {
        return (bx-ax)*(cy-ay)-(by-ay)*(cx-ax);
    };
    const double area=cross(a.U*factor,a.V*factor,b.U*factor,b.V*factor,c.U*factor,c.V*factor);
    const double x=static_cast<double>(xy[0])*1048576, y=static_cast<double>(xy[1])*1048576;
    const double w0=cross(x,y,b.U*factor,b.V*factor,c.U*factor,c.V*factor);
    const double w1=cross(a.U*factor,a.V*factor,x,y,c.U*factor,c.V*factor);
    weights={w0/area,w1/area,(area-w0-w1)/area};
    return true;
}

bool TransactionalSamples::StrictlyInside(Slot sample,const Point& a,const Point& b,const Point& c) const
{
    using R=boost::multiprecision::cpp_rational;
    const auto xy=Decode(sample);const R u=R(xy[0])/Denominator(),v=R(xy[1])/Denominator();
    const std::array<Point,3> points{a,b,c};
    // 目录是否包含样本点由真实整数比决定，舍入不能把闭边样本变成面内请求
    for (std::size_t i=0;i<3;++i)
    {
        const auto& p=points[i];const auto& q=points[(i+1)%3];
        const R side=(R(q.U)-p.U)*(v-p.V)-(R(q.V)-p.V)*(u-p.U);
        if (side<=0) return false;
    }
    return true;
}

void TransactionalSamples::Refresh(const TransactionalState& state, WorkLedger& work)
{
    std::fill(_values.begin(),_values.end(),SampleValue{});
    // 全量路径用于初建与独立诊断；正常局部续接由后续专门接口承担
    _faceSamples.assign(state.Faces().size(),{});
    _priority.assign(state.Faces().size(),std::numeric_limits<double>::infinity());
    auto order=state.ActiveFaces();
    // 物理槽的复用顺序不能改变共享样本 owner 的稳定身份同分规则
    std::sort(order.begin(),order.end(),[&](Slot a,Slot b) { return state.Face(a).Id<state.Face(b).Id; });
    for (auto slot : order)
    {
        work.CheckLimit();
        const auto& face=state.Face(slot);
        const std::array<Point,3> p{state.Vertex(face.Vertices[0]).Geometry,
            state.Vertex(face.Vertices[1]).Geometry,state.Vertex(face.Vertices[2]).Geometry};
        const double xmin=std::min({p[0].U,p[1].U,p[2].U}), xmax=std::max({p[0].U,p[1].U,p[2].U});
        const double ymin=std::min({p[0].V,p[1].V,p[2].V}), ymax=std::max({p[0].V,p[1].V,p[2].V});
        double maximum=0;
        for (const auto& group : _groups)
        {
            // 包围框向外扩一格只是候选枚举，精确闭面判断决定是否真正归属
            const int xl=std::max(0,static_cast<int>(std::floor((xmin*Denominator()-group.X)/6))-1);
            const int xr=std::min(static_cast<int>(group.Columns)-1,static_cast<int>(std::ceil((xmax*Denominator()-group.X)/6))+1);
            const int yl=std::max(0,static_cast<int>(std::floor((ymin*Denominator()-group.Y)/6))-1);
            const int yr=std::min(static_cast<int>(group.Rows)-1,static_cast<int>(std::ceil((ymax*Denominator()-group.Y)/6))+1);
            for (int y=yl; y<=yr; ++y) for (int x=xl; x<=xr; ++x)
            {
                const auto sid=group.Start+static_cast<Slot>(y)*group.Columns+static_cast<Slot>(x);
                std::array<double,3> weights{};
                ++work.LocationTests;
                if (!Weights(sid,p[0],p[1],p[2],weights)) continue;
                const double height=(weights[0]*p[0].Height+weights[1]*p[1].Height)+weights[2]*p[2].Height;
                auto& value=_values[sid];
                if (value.Owner==InvalidSlot)
                {
                    // 只计算一次样本误差，后续闭面仍会共享同一评价贡献
                    const auto xy=Decode(sid); const auto uv=Parameter(sid);
                    value.Owner=slot; value.MeshHeight=height;
                    value.ReferenceHeight=SourceHeight(xy[0],xy[1],state.Config().HeightScale);
                    value.HeightError=std::abs(height-value.ReferenceHeight);
                    const auto rc=Clip(state.Config(),uv.U,uv.V,value.ReferenceHeight);
                    value.Visible=rc[3]>0 && rc[0]>=-rc[3] && rc[0]<=rc[3] &&
                        rc[1]>=-rc[3] && rc[1]<=rc[3] && rc[2]>=-rc[3] && rc[2]<=rc[3];
                    if (value.Visible)
                    {
                        // 参考可见但被测曲面跨近面时，不能把该样本略去后报告低误差
                        const auto mc=Clip(state.Config(),uv.U,uv.V,height);
                        if (!(mc[3]>0) || mc[2]<-mc[3]) throw std::runtime_error("被测样本跨越近面");
                        const double dx=(mc[0]/mc[3]-rc[0]/rc[3])*(state.Config().Width*.5);
                        const double dy=(mc[1]/mc[3]-rc[1]/rc[3])*(state.Config().Height*.5);
                        value.ErrorSquared=dx*dx+dy*dy;
                    }
                    ++work.SampleEvaluations;
                }
                else if (std::abs(height-value.MeshHeight)>1e-10*std::max(1.0,std::abs(height)))
                    throw std::runtime_error("共享面样本高度不一致");
                _faceSamples[slot].push_back(sid); ++work.SampleContributions;
                if (value.Visible) maximum=std::max(maximum,value.ErrorSquared);
            }
        }
        std::array<std::array<double,4>,3> clips{};
        bool unknown=false;
        for (std::size_t i=0;i<3;++i) { clips[i]=Clip(state.Config(),p[i].U,p[i].V,p[i].Height); unknown|=!(clips[i][3]>0); }
        if (unknown) continue;
        // 密度紧迫性和误差紧迫性分别计算，空可见证据不抹掉原始优先级
        double longest=0;
        for (std::size_t i=0;i<3;++i)
        {
            const auto& a=clips[i];const auto& b=clips[(i+1)%3];
            const double dx=a[0]/a[3]*(state.Config().Width*.5)-b[0]/b[3]*(state.Config().Width*.5);
            const double dy=a[1]/a[3]*(state.Config().Height*.5)-b[1]/b[3]*(state.Config().Height*.5);
            longest=std::max(longest,dx*dx+dy*dy);
        }
        _priority[slot]=std::max(maximum,.04*longest);
    }
    _raw.clear();
    // 只冻结原始请求全序，接收认证失败时不会从排序尾部补取
    for (auto slot : order)
        if (std::isfinite(_priority[slot]) && _priority[slot]>state.Config().SplitPixels*state.Config().SplitPixels)
            _raw.push_back(slot);
    std::sort(_raw.begin(),_raw.end(),[&](Slot a,Slot b) {
        return _priority[a]!=_priority[b] ? _priority[a]>_priority[b] : state.Face(a).Id<state.Face(b).Id;
    });
    if (std::any_of(_values.begin(),_values.end(),[](const auto& v) { return v.Owner==InvalidSlot; }))
        throw std::runtime_error("公共样本存在覆盖缺失");
}

std::vector<Slot> TransactionalSamples::VisibleSupport(const std::vector<Slot>& support) const
{
    // 相邻面共享样本只认证一次，但成员取完整闭补丁并集
    std::vector<Slot> result;
    for (auto face : support) for (auto sid : FaceSamples(face)) if (_values[sid].Visible) result.push_back(sid);
    std::sort(result.begin(),result.end()); result.erase(std::unique(result.begin(),result.end()),result.end());
    return result;
}
}
