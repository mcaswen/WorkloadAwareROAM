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
    weights=StoredWeights(sample,a,b,c);return true;
}

std::array<double,3> TransactionalSamples::StoredWeights(Slot sample,const Point& a,const Point& b,const Point& c) const
{
    const auto xy=Decode(sample);
    // 已证明包含的样本只计算插值权重，避免重复精确定位
    const double factor=1048576.0*Denominator();
    const auto cross=[](double ax,double ay,double bx,double by,double cx,double cy) {
        return (bx-ax)*(cy-ay)-(by-ay)*(cx-ax);
    };
    const double area=cross(a.U*factor,a.V*factor,b.U*factor,b.V*factor,c.U*factor,c.V*factor);
    const double x=static_cast<double>(xy[0])*1048576, y=static_cast<double>(xy[1])*1048576;
    const double w0=cross(x,y,b.U*factor,b.V*factor,c.U*factor,c.V*factor);
    const double w1=cross(a.U*factor,a.V*factor,x,y,c.U*factor,c.V*factor);
    return {w0/area,w1/area,(area-w0-w1)/area};
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

std::vector<Slot> TransactionalSamples::Enumerate(const std::array<Point,3>& p,WorkLedger& work) const
{
    // 固定栅格身份允许从局部包围框反查 Q，不需要扫描整个 reference
    std::vector<Slot> result;
    const double xmin=std::min({p[0].U,p[1].U,p[2].U}),xmax=std::max({p[0].U,p[1].U,p[2].U});
    const double ymin=std::min({p[0].V,p[1].V,p[2].V}),ymax=std::max({p[0].V,p[1].V,p[2].V});
    for (const auto& group : _groups)
    {
        // 包围框外扩只扩大候选枚举，真实整数比上的闭面判断决定归属
        const int xl=std::max(0,static_cast<int>(std::floor((xmin*Denominator()-group.X)/6))-1);
        const int xr=std::min(static_cast<int>(group.Columns)-1,static_cast<int>(std::ceil((xmax*Denominator()-group.X)/6))+1);
        const int yl=std::max(0,static_cast<int>(std::floor((ymin*Denominator()-group.Y)/6))-1);
        const int yr=std::min(static_cast<int>(group.Rows)-1,static_cast<int>(std::ceil((ymax*Denominator()-group.Y)/6))+1);
        for (int y=yl;y<=yr;++y) for (int x=xl;x<=xr;++x)
        {
            const auto sid=group.Start+static_cast<Slot>(y)*group.Columns+static_cast<Slot>(x);
            std::array<double,3> weights{};++work.LocationTests;
            if (Weights(sid,p[0],p[1],p[2],weights)) result.push_back(sid);
        }
    }
    work.SampleContributions+=result.size();work.CheckLimit();
    return result;
}

SampleValue TransactionalSamples::Evaluate(const Configuration& config,Slot sid,Slot owner,double height,WorkLedger& work) const
{
    // 可见性只由独立参考决定，被测高度不能改变自己的评价人口
    SampleValue value;const auto xy=Decode(sid);const auto uv=Parameter(sid);
    value.Owner=owner;value.MeshHeight=height;
    value.ReferenceHeight=SourceHeight(xy[0],xy[1],config.HeightScale);
    value.HeightError=std::abs(height-value.ReferenceHeight);
    const auto rc=Clip(config,uv.U,uv.V,value.ReferenceHeight);
    value.Visible=rc[3]>0 && rc[0]>=-rc[3] && rc[0]<=rc[3] &&
        rc[1]>=-rc[3] && rc[1]<=rc[3] && rc[2]>=-rc[3] && rc[2]<=rc[3];
    if (value.Visible)
    {
        // 参考可见而被测面跨近面必须拒绝，不能跳过该样本降低统计误差
        const auto mc=Clip(config,uv.U,uv.V,height);
        if (!(mc[3]>0) || mc[2]<-mc[3]) throw std::runtime_error("被测样本跨越近面");
        const double dx=(mc[0]/mc[3]-rc[0]/rc[3])*(config.Width*.5);
        const double dy=(mc[1]/mc[3]-rc[1]/rc[3])*(config.Height*.5);
        value.ErrorSquared=dx*dx+dy*dy;
    }
    ++work.SampleEvaluations;return value;
}

double TransactionalSamples::Priority(const Configuration& config,const std::array<Point,3>& p,double maximum)
{
    std::array<std::array<double,4>,3> clips{};
    for (std::size_t i=0;i<3;++i)
    {
        clips[i]=Clip(config,p[i].U,p[i].V,p[i].Height);
        if (!(clips[i][3]>0)) return std::numeric_limits<double>::infinity();
    }
    // 密度紧迫性保留原始含义，空证据需求不会因此从人口中消失
    double longest=0;
    for (std::size_t i=0;i<3;++i)
    {
        const auto& a=clips[i];const auto& b=clips[(i+1)%3];
        const double dx=a[0]/a[3]*(config.Width*.5)-b[0]/b[3]*(config.Width*.5);
        const double dy=a[1]/a[3]*(config.Height*.5)-b[1]/b[3]*(config.Height*.5);
        longest=std::max(longest,dx*dx+dy*dy);
    }
    return std::max(maximum,.04*longest);
}

void TransactionalSamples::Refresh(const TransactionalState& state,WorkLedger& work)
{
    std::fill(_values.begin(),_values.end(),SampleValue{});
    _faceSamples.assign(state.Faces().size(),{});
    _priority.assign(state.Faces().size(),std::numeric_limits<double>::infinity());
    auto order=state.ActiveFaces();
    // 全量入口仅用于初建或独立 oracle，稳定身份先序确定共享边 owner
    std::sort(order.begin(),order.end(),[&](Slot a,Slot b) { return state.Face(a).Id<state.Face(b).Id; });
    for (auto slot : order)
    {
        const auto& f=state.Face(slot).Vertices;
        const std::array<Point,3> p{state.Vertex(f[0]).Geometry,state.Vertex(f[1]).Geometry,state.Vertex(f[2]).Geometry};
        _faceSamples[slot]=Enumerate(p,work);double maximum=0;
        for (auto sid : _faceSamples[slot])
        {
            const auto weights=StoredWeights(sid,p[0],p[1],p[2]);
            const double height=(weights[0]*p[0].Height+weights[1]*p[1].Height)+weights[2]*p[2].Height;
            auto& value=_values[sid];
            if (value.Owner==InvalidSlot) value=Evaluate(state.Config(),sid,slot,height,work);
            else if (std::abs(height-value.MeshHeight)>1e-10*std::max(1.0,std::abs(height)))
                throw std::runtime_error("共享面样本高度不一致");
            if (value.Visible) maximum=std::max(maximum,value.ErrorSquared);
        }
        _priority[slot]=Priority(state.Config(),p,maximum);
    }
    _raw.clear();
    for (auto slot : order)
        if (std::isfinite(_priority[slot]) && _priority[slot]>state.Config().SplitPixels*state.Config().SplitPixels)
            _raw.push_back(slot);
    std::sort(_raw.begin(),_raw.end(),[&](Slot a,Slot b) {
        return _priority[a]!=_priority[b] ? _priority[a]>_priority[b] : state.Face(a).Id<state.Face(b).Id;
    });
    if (std::any_of(_values.begin(),_values.end(),[](const auto& v) { return v.Owner==InvalidSlot; }))
        throw std::runtime_error("公共样本存在覆盖缺失");
}

PreparedSamples TransactionalSamples::Prepare(const TransactionalState& state,const PreparedTopology& target,WorkLedger& work)
{
    PreparedSamples result;result.FaceSlots=target.FinalFaceSlots;
    std::set<Slot> exterior;
    for (auto slot : target.Removed)
    {
        // 同一物理槽可能立刻复用，先按旧语义移除 owner 再参与目标同分
        result.Faces.emplace(slot,std::vector<Slot>{});
        result.Priorities.emplace(slot,std::numeric_limits<double>::infinity());
        for (auto sid : _faceSamples.at(slot))
        {
            auto value=_values[sid];
            if (target.Removed.contains(value.Owner)) value.Owner=InvalidSlot;
            result.Values.emplace(sid,value);
        }
        // 保留的接口面也共享样本评价，不能仅刷新被替换面上的 P
        for (auto id : state.Face(slot).Vertices)
            for (auto face : state.Vertex(id).Incident)
                if (!target.Removed.contains(face)) exterior.insert(face);
    }
    std::map<Slot,const Triangle*> newFaces;
    // 目标查询仅覆盖本批新面，接口外的完整记录继续借用旧状态
    for (const auto& [slot,record] : target.Faces) newFaces.emplace(slot,&record.Geometry);
    const auto faceAt=[&](Slot slot)->const Triangle& {
        const auto it=newFaces.find(slot);return it==newFaces.end() ? state.Face(slot) : *it->second;
    };
    for (const auto& [slot,record] : target.Faces)
    {
        const auto& f=record.Geometry.Vertices;
        const std::array<Point,3> p{target.Geometry(state,f[0]),target.Geometry(state,f[1]),target.Geometry(state,f[2])};
        auto ids=Enumerate(p,work);
        for (auto sid : ids)
        {
            // 边界样本可能仍由外部保留面拥有，只有更小的最终身份才能取代
            auto [it,inserted]=result.Values.try_emplace(sid,_values[sid]);
            static_cast<void>(inserted);auto& value=it->second;
            if (value.Owner!=InvalidSlot && faceAt(value.Owner).Id<record.Geometry.Id) continue;
            const auto weights=StoredWeights(sid,p[0],p[1],p[2]);
            const double height=(weights[0]*p[0].Height+weights[1]*p[1].Height)+weights[2]*p[2].Height;
            value=Evaluate(state.Config(),sid,slot,height,work);
        }
        result.Faces[slot]=std::move(ids);exterior.insert(slot);
    }
    for (const auto& [sid,value] : result.Values)
    {
        // 旧闭补丁中的所有样本必须找到目标 owner，局部有洞不能延后修复
        static_cast<void>(sid);
        if (value.Owner==InvalidSlot) throw std::runtime_error("局部样本覆盖缺失");
    }
    for (auto slot : exterior)
    {
        // 先完成所有 owner 评价，再算共享面最大值，避免枚举次序进入 P
        const auto& f=faceAt(slot).Vertices;
        const std::array<Point,3> p{target.Geometry(state,f[0]),target.Geometry(state,f[1]),target.Geometry(state,f[2])};
        const auto found=result.Faces.find(slot);
        const auto& ids=found==result.Faces.end() ? _faceSamples[slot] : found->second;
        double maximum=0;
        for (auto sid : ids)
        {
            const auto it=result.Values.find(sid);const auto& value=it==result.Values.end() ? _values[sid] : it->second;
            if (value.Visible) maximum=std::max(maximum,value.ErrorSquared);
        }
        result.Priorities[slot]=Priority(state.Config(),p,maximum);
    }
    work.RepairSamples+=result.Values.size();work.RepairFaces+=exterior.size();
    const auto ordered=std::chrono::steady_clock::now();
    const auto priority=[&](Slot slot) {
        const auto it=result.Priorities.find(slot);return it==result.Priorities.end() ? _priority[slot] : it->second;
    };
    const auto consider=[&](Slot slot) {
        ++work.OrderVisits;const auto value=priority(slot);
        if (std::isfinite(value) && value>state.Config().SplitPixels*state.Config().SplitPixels) result.Raw.push_back(slot);
    };
    // 全局候选顺序是显式成本，不冒充局部样本修复
    for (auto slot : state.ActiveFaces()) if (!target.Removed.contains(slot)) consider(slot);
    for (const auto& [slot,record] : target.Faces) { static_cast<void>(record);consider(slot); }
    std::sort(result.Raw.begin(),result.Raw.end(),[&](Slot a,Slot b) {
        const auto pa=priority(a),pb=priority(b);return pa!=pb ? pa>pb : faceAt(a).Id<faceAt(b).Id;
    });
    work.Seconds["next_order"]+=std::chrono::duration<double>(std::chrono::steady_clock::now()-ordered).count();
    // 只预留容量而不扩大 live 数组，其他组件准备失败时旧缓存仍完整
    work.Reserve(_faceSamples,result.FaceSlots);work.Reserve(_priority,result.FaceSlots);
    return result;
}

void TransactionalSamples::Publish(PreparedSamples&& prepared) noexcept
{
    // 容量在 Prepare 中预留，移动局部 vector 不再触发分配
    _faceSamples.resize(prepared.FaceSlots);_priority.resize(prepared.FaceSlots);
    for (const auto& [sid,value] : prepared.Values) _values[sid]=value;
    for (auto& [slot,ids] : prepared.Faces) _faceSamples[slot]=std::move(ids);
    for (const auto& [slot,value] : prepared.Priorities) _priority[slot]=value;
    _raw=std::move(prepared.Raw);
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
