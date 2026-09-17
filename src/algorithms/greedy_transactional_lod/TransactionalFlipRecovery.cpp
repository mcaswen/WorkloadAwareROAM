#include "algorithms/greedy_transactional_lod/TransactionalFlipRecovery.h"
#include "algorithms/greedy_transactional_lod/TransactionalCertification.h"
#include "algorithms/greedy_transactional_lod/TransactionalPredicates.h"
#include "algorithms/greedy_transactional_lod/TransactionalPointwiseQuality.h"
#include "profiling/CpuProfiling.h"

#include <algorithm>

namespace ParallelRoam::Algorithms::GreedyTransactionalLod
{
namespace
{
using Predicates=TransactionalPredicates;
using Face=std::array<Identity,3>;
std::vector<Face> Canonical(std::vector<Face> faces)
{
    for (auto& face : faces)
    {
        const auto smallest=std::min_element(face.begin(),face.end());
        std::rotate(face.begin(),smallest,face.end());
    }
    std::sort(faces.begin(),faces.end());return faces;
}
}

Proposal TransactionalFlipRecovery::Construct(const TransactionalState& state,Slot root,Edge edge)
{
    Proposal p;p.Kind='R';p.Root=root;p.Reason="flip_not_interior";
    edge=EdgeKey(edge[0],edge[1]);const auto found=state.Edges().find(edge);
    if (found==state.Edges().end() || found->second.Count!=2) return p;
    const auto& use=found->second.Faces;
    if (use[0]!=root && use[1]!=root) { p.Reason="flip_not_root_edge";return p; }
    p.Support={use[0],use[1]};std::sort(p.Support.begin(),p.Support.end());
    std::array<Identity,2> opposite{};
    for (std::size_t i=0;i<2;++i)
        for (auto id : state.Face(use[i]).Vertices)
        {
            p.Points.emplace(id,state.Vertex(id).Geometry);
            if (id!=edge[0] && id!=edge[1]) opposite[i]=id;
        }
    p.Reason="flip_nonconvex_or_existing_diagonal";
    const auto c=opposite[0],d=opposite[1];
    if (p.Points.size()!=4 || c==d || state.Edges().contains(EdgeKey(c,d))) return p;
    const auto& a=p.Points.at(edge[0]);const auto& b=p.Points.at(edge[1]);
    const auto& pc=p.Points.at(c);const auto& pd=p.Points.at(d);
    // 两条对角线严格相交才可替换；共线和已有新边都不能靠容差绕过
    if (Predicates::Orientation(a,b,pc)*Predicates::Orientation(a,b,pd)>=0 ||
        Predicates::Orientation(pc,pd,a)*Predicates::Orientation(pc,pd,b)>=0) return p;
    p.Faces={{c,d,edge[0]},{d,c,edge[1]}};
    for (auto& f : p.Faces)
        if (Predicates::Orientation(p.Points.at(f[0]),p.Points.at(f[1]),p.Points.at(f[2]))<0) std::swap(f[0],f[1]);
    p.Reason="shape_infeasible";
    for (const auto& f : p.Faces)
        if (!Predicates::Shape(p.Points.at(f[0]),p.Points.at(f[1]),p.Points.at(f[2]))) return p;
    p.Reason.clear();return p;
}

std::optional<Proposal> TransactionalFlipRecovery::FirstCertified(const TransactionalState& state,const TransactionalSamples& samples,
    Slot root,WorkLedger& work,std::vector<std::pair<char,std::string>>& attempts)
{
    ROAM_CPU_ZONE("gtp.flip_recovery");
    const auto started=std::chrono::steady_clock::now();++work.FlipTriggered;
    const auto& f=state.Face(root).Vertices;std::array<Edge,3> edges;
    for (std::size_t i=0;i<3;++i) edges[i]=EdgeKey(f[i],f[(i+1)%3]);
    std::sort(edges.begin(),edges.end());std::optional<Proposal> result;
    for (const auto& edge : edges)
    {
        work.CheckLimit();++work.FlipAttempts;auto p=Construct(state,root,edge);
        if (p.Reason.empty())
        {
            // 两面闭支持是认证域；换连接后不能把旧曲面当作拟合基准
            p.Samples=samples.VisibleSupport(p.Support);
            p.Reason=TransactionalCertification::SetProgressTarget(state,samples,p,work);
            if (p.Reason.empty())
            {
                if (!TransactionalCertification::Measure(state,samples,p,work)) p.Reason="projection_unknown";
                else p.Reason=TransactionalCertification::Accepts(state,samples,p,p.TargetMicropixels,work) ? "certified" : "quality_infeasible";
            }
        }
        if (p.Reason == "certified" && TransactionalPointwiseQuality::Enabled(state.Config()))
        {
            p.Reason = TransactionalPointwiseQuality::Certify(state, samples, p, true, work);
        }
        attempts.emplace_back('R',p.Reason);++work.Reasons["flip_"+p.Reason];
        if (p.Reason=="certified") { ++work.FlipCertified;result=std::move(p);break; }
    }
    work.Seconds["flip_recovery"]+=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
    return result;
}

bool TransactionalFlipRecovery::IsUnchangedGeometryFlip(const TransactionalState& state,const Proposal& p)
{
    if (p.Kind!='R' || p.Support.size()!=2 || p.Faces.size()!=2 || p.Points.size()!=4 || !p.Free.empty()) return false;
    const auto& f=state.Face(p.Support[0]).Vertices;
    // 重建最多三条局部边的结构证书，既验证外边界，也禁止退回原连接冒充修复
    for (std::size_t i=0;i<3;++i)
    {
        const auto expected=Construct(state,p.Root,EdgeKey(f[i],f[(i+1)%3]));
        if (!expected.Reason.empty()) continue;
        auto support=p.Support;std::sort(support.begin(),support.end());
        if (expected.Support==support && expected.Points==p.Points && Canonical(expected.Faces)==Canonical(p.Faces)) return true;
    }
    return false;
}
}
