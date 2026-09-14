#include "algorithms/greedy_transactional_lod/TransactionalSamples.h"
#include "profiling/CpuProfiling.h"
#include "algorithms/greedy_transactional_lod/TransactionalPredicates.h"

#include <boost/multiprecision/cpp_int.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ParallelRoam::Algorithms::GreedyTransactionalLod
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

void TransactionalSamples::Store(Slot sample,const SampleValue& value) noexcept
{
    _values[sample]={value.ReferenceHeight,value.MeshHeight,value.HeightError,value.Owner};
    _view.Write(sample,{value.ErrorSquared,value.Visible});
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
    SampleValue value;const auto xy=Decode(sid);
    value.Owner=owner;value.MeshHeight=height;
    value.ReferenceHeight=SourceHeight(xy[0],xy[1],config.HeightScale);
    value.HeightError=std::abs(height-value.ReferenceHeight);
    return Project(config,sid,value,work);
}

SampleValue TransactionalSamples::Project(const Configuration& config,Slot sid,SampleValue value,WorkLedger& work) const
{
    const auto uv=Parameter(sid);value.ErrorSquared=0;
    const auto rc=Clip(config,uv.U,uv.V,value.ReferenceHeight);
    if (!std::all_of(rc.begin(),rc.end(),[](double component) { return std::isfinite(component); }))
        throw std::runtime_error("参考投影数值不可定义");
    value.Visible=rc[3]>0 && rc[0]>=-rc[3] && rc[0]<=rc[3] &&
        rc[1]>=-rc[3] && rc[1]<=rc[3] && rc[2]>=-rc[3] && rc[2]<=rc[3];
    if (value.Visible)
    {
        // 参考可见而被测面跨近面必须拒绝，不能跳过该样本降低统计误差
        const auto mc=Clip(config,uv.U,uv.V,value.MeshHeight);
        if (!(mc[3]>0) || mc[2]<-mc[3]) throw std::runtime_error("被测样本跨越近面");
        const double dx=(mc[0]/mc[3]-rc[0]/rc[3])*(config.Width*.5);
        const double dy=(mc[1]/mc[3]-rc[1]/rc[3])*(config.Height*.5);
        value.ErrorSquared=dx*dx+dy*dy;
        if (!std::isfinite(value.ErrorSquared)) throw std::runtime_error("被测投影误差不可定义");
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
    ROAM_CPU_ZONE("gtp.samples.initialize");
    _view.Initialize(_values.size(),work);
    std::fill(_values.begin(),_values.end(),SampleGeometry{});
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
            if (value.Owner==InvalidSlot) Store(sid,Evaluate(state.Config(),sid,slot,height,work));
            else if (std::abs(height-value.MeshHeight)>1e-10*std::max(1.0,std::abs(height)))
                throw std::runtime_error("共享面样本高度不一致");
            const auto& projection=Projection(sid);
            if (projection.Visible) maximum=std::max(maximum,projection.ErrorSquared);
        }
        _priority[slot]=Priority(state.Config(),p,maximum);
    }
    BuildOrders(state,_priority,_order,_donors,work);
    if (std::any_of(_values.begin(),_values.end(),[](const auto& v) { return v.Owner==InvalidSlot; }))
        throw std::runtime_error("公共样本存在覆盖缺失");
}

PreparedSamples TransactionalSamples::Prepare(const TransactionalState& state,const PreparedTopology& target,WorkLedger& work)
{
    ROAM_CPU_ZONE("gtp.samples.prepare");
    PreparedSamples result;result.FaceSlots=target.FinalFaceSlots;
    std::set<Slot> exterior;
    for (auto slot : target.Removed)
    {
        // 同一物理槽可能立刻复用，先按旧语义移除 owner 再参与目标同分
        result.Faces.emplace(slot,std::vector<Slot>{});
        result.Priorities.emplace(slot,std::numeric_limits<double>::infinity());
        for (auto sid : _faceSamples.at(slot))
        {
            auto value=Value(sid);
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
            auto [it,inserted]=result.Values.try_emplace(sid,Value(sid));
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
                const auto it=result.Values.find(sid);const auto value=it==result.Values.end() ? Value(sid) : it->second;
            if (value.Visible) maximum=std::max(maximum,value.ErrorSquared);
        }
        result.Priorities[slot]=Priority(state.Config(),p,maximum);
    }
    work.RepairSamples+=result.Values.size();work.RepairFaces+=exterior.size();
    const auto ordered=std::chrono::steady_clock::now();
    const auto priority=[&](Slot slot) {
        const auto it=result.Priorities.find(slot);return it==result.Priorities.end() ? _priority[slot] : it->second;
    };
    const double threshold=state.Config().SplitPixels*state.Config().SplitPixels;
    ReceiverIndex::Writes orderWrites;DonorIndex::Writes donorWrites;
    std::set<Identity> donorIds=target.DeletedVertices;
    // 新点、保留点邻接和改高都会影响 donor 代理，删除点也必须退出索引
    for (const auto& [id,record] : target.Vertices) { static_cast<void>(record);donorIds.insert(id); }
    for (const auto& [slot,value] : result.Priorities)
    {
        ++work.OrderVisits;
        const bool oldActive=slot<state.Faces().size() && state.Faces()[slot].ActivePosition!=InvalidSlot;
        if (oldActive)
        {
            // 删除旧 key 使用旧逻辑身份，不能拿复用槽中的新面身份去移除
            result.InvalidatedRoots.insert(state.Face(slot).Id);

        }
        orderWrites[slot]=std::nullopt;
        const bool active=newFaces.contains(slot) || !target.Removed.contains(slot);
        if (!active) continue;
        for (auto id : faceAt(slot).Vertices) donorIds.insert(id);
        if (std::isfinite(value) && value>threshold) orderWrites[slot]=PriorityKey{-value,faceAt(slot).Id,slot};
    }
    for (auto id : target.DeletedVertices) donorWrites[state.VertexSlot(id)]=std::nullopt;
    for (auto id : donorIds)
    {
        // root 目录可读取其顶点完整邻域，因此证据失效须扩至这些 incident faces
        result.InvalidatedDonors.insert(id);
        if (target.DeletedVertices.contains(id)) continue;
        const auto added=target.AddedIndex.find(id);
        const auto vertexSlot=added==target.AddedIndex.end() ? state.VertexSlot(id) : added->second;
        donorWrites[vertexSlot]=std::nullopt;
        const auto changed=target.Vertices.find(id);
        const auto& vertex=changed==target.Vertices.end() ? state.Vertex(id) : changed->second;
        // 一般网格的边界固定为单位方形，内部资格不依赖执行次序
        for (auto face : vertex.Incident) result.InvalidatedRoots.insert(faceAt(face).Id);
        if (vertex.Boundary) continue;
        double value=0;for (auto face : vertex.Incident) value=std::max(value,priority(face));
        donorWrites[vertexSlot]=DonorKey{value,id};
    }
    work.CandidateUpdates+=orderWrites.size();work.DonorIndexUpdates+=donorWrites.size();
    result.Order=_order.PrepareRepair(std::move(orderWrites),result.FaceSlots,work);
    result.Donors=_donors.PrepareRepair(std::move(donorWrites),state.Vertices().size()+target.AppendVertices,work);
    work.Seconds["next_order"]+=std::chrono::duration<double>(std::chrono::steady_clock::now()-ordered).count();
    // 只预留容量而不扩大 live 数组，其他组件准备失败时旧缓存仍完整
    work.Reserve(_faceSamples,result.FaceSlots);work.Reserve(_priority,result.FaceSlots);
    return result;
}

void TransactionalSamples::Publish(PreparedSamples&& prepared) noexcept
{
    ROAM_CPU_ZONE("gtp.samples.publish");
    // 容量在 Prepare 中预留，移动局部 vector 不再触发分配
    _faceSamples.resize(prepared.FaceSlots);_priority.resize(prepared.FaceSlots);
    for (const auto& [sid,value] : prepared.Values) Store(sid,value);
    for (auto& [slot,ids] : prepared.Faces) _faceSamples[slot]=std::move(ids);
    for (const auto& [slot,value] : prepared.Priorities) _priority[slot]=value;
    _order.Publish(std::move(prepared.Order));_donors.Publish(std::move(prepared.Donors));
}

void TransactionalSamples::BuildOrders(const TransactionalState& state,const std::vector<double>& priority,
    ReceiverIndex& order,DonorIndex& donors,WorkLedger& work)
{
    ROAM_CPU_ZONE("gtp.samples.orders");
    std::vector<std::optional<PriorityKey>> faces(state.Faces().size());
    std::vector<std::optional<DonorKey>> vertices(state.Vertices().size());
    for (auto slot : state.ActiveFaces())
        if (std::isfinite(priority[slot]) && priority[slot]>state.Config().SplitPixels*state.Config().SplitPixels)
            faces[slot]=PriorityKey{-priority[slot],state.Face(slot).Id,slot};
    for (std::size_t slot=0;slot<state.Vertices().size();++slot)
    {
        const auto& vertex=state.Vertices()[slot];
        if (!vertex.Active || vertex.Boundary) continue;
        double value=0;for (auto face : vertex.Incident) value=std::max(value,priority[face]);
        vertices[slot]=DonorKey{value,vertex.Id};
    }
    order.Rebuild(std::move(faces),work);donors.Rebuild(std::move(vertices),work);
}

std::vector<Slot> TransactionalSamples::Prefix(std::size_t limit,WorkLedger* work) const
{
    std::vector<Slot> result;
    for (const auto& key : _order.Prefix(limit,work)) result.push_back(std::get<2>(key));
    return result;
}

std::vector<Identity> TransactionalSamples::DonorPool(std::size_t limit,WorkLedger* work) const
{
    std::vector<Identity> result;
    for (const auto& key : _donors.Prefix(limit,work)) result.push_back(key.second);
    return result;
}

std::vector<Slot> TransactionalSamples::VisibleSupport(const std::vector<Slot>& support) const
{
    // 相邻面共享样本只认证一次，但成员取完整闭补丁并集
    std::vector<Slot> result;
    for (auto face : support) for (auto sid : FaceSamples(face)) if (Projection(sid).Visible) result.push_back(sid);
    std::sort(result.begin(),result.end()); result.erase(std::unique(result.begin(),result.end()),result.end());
    return result;
}

PreparedView TransactionalSamples::PrepareView(const TransactionalState& state,const Configuration& view,WorkLedger& work,
    const TransactionalExecution& execution)
{
    ROAM_CPU_ZONE("gtp.samples.prepare_view");
    PreparedView result;const auto projection=_view.Prepare(work);result.Priority.resize(_priority.size());
    // 投影暂存不保存第二份参考高度或 owner，保持视图工作与拓扑状态分离
    execution.Run("view_projection",_values.size(),work,[&](auto first,auto last,WorkLedger& local) {
        for (auto index=first;index<last;++index)
        {
            const auto sid=static_cast<Slot>(index);
            if ((sid&4095U)==0) local.CheckLimit();
            const auto& g=_values[sid];
            const auto value=Project(view,sid,{g.ReferenceHeight,g.MeshHeight,0,g.HeightError,g.Owner,false},local);
            projection[sid]={value.ErrorSquared,value.Visible};
        }
    });
    execution.Run("view_scores",state.FaceCount(),work,[&](auto first,auto last,WorkLedger& local) {
        for (auto index=first;index<last;++index)
        {
            const auto slot=state.ActiveFaces()[index];double maximum=0;
            // 每个面的归约保持原样本顺序，线程只划分面，避免改变浮点决策
            for (auto sid : _faceSamples[slot])
            {
                ++local.SampleContributions;
                if (projection[sid].Visible) maximum=std::max(maximum,projection[sid].ErrorSquared);
            }
            const auto& f=state.Face(slot).Vertices;
            result.Priority[slot]=Priority(view,{state.Vertex(f[0]).Geometry,state.Vertex(f[1]).Geometry,state.Vertex(f[2]).Geometry},maximum);
        }
    });
    const auto started=std::chrono::steady_clock::now();
    BuildOrders(state,result.Priority,result.Order,result.Donors,work);
    work.OrderVisits+=state.FaceCount();
    work.Seconds["view_order"]+=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
    return result;
}

void TransactionalSamples::PublishView(PreparedView&& prepared) noexcept
{
    ROAM_CPU_ZONE("gtp.samples.publish_view");
    // 参数域及高度缓存未变，直接发布已完整填写的备用投影数组
    _view.Publish();
    _priority=std::move(prepared.Priority);_order=std::move(prepared.Order);
    _donors=std::move(prepared.Donors);
}
}
