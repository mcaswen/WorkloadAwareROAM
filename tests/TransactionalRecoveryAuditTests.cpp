#include "experiment/greedy_transactional_lod/TransactionalRecoveryAudit.h"
#include <cmath>
#include <iostream>

namespace
{
using namespace ParallelRoam::Algorithms::GreedyTransactionalLod;
using namespace ParallelRoam::Experiment::GreedyTransactionalLod;
using Audit=TransactionalRecoveryAudit;
void Require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
RecoveryGeometry Square()
{
    return {{{0,{0,0,0}},{1,{1,0,0}},{2,{1,1,1}},{3,{0,1,0}}},{{0,1,2},{0,2,3}},{}};
}
void Construction()
{
    const auto square=Square();const auto flipped=Audit::Flip(square,{0,2});
    Require(flipped && flipped->Faces.size()==2 && flipped->Points==square.Points,"凸翻边没有保留旧点和面数");
    Require(!Audit::Flip(square,{0,1}),"外边界被误当内部边");
    auto concave=square;concave.Points.at(2)={.25,.25,1};
    Require(!Audit::Flip(concave,{0,2}),"凹四边形被错误翻边");
    const auto edge=Audit::SplitEdge(*flipped,{1,3},-1);
    Require(edge && edge->Faces.size()==4 && edge->Points.size()==5,"边细化不是净增加两个面");
    Require(!Audit::SplitEdge(*flipped,{0,1},-1),"局部外边界被切分");
    Require(!Audit::SplitEdge(*edge,{1,3},-2),"有限目录允许了第二次插点");
    Require(!Audit::SplitFace(*flipped,0,{2,2,0},-1),"面外点没有被拒绝");
}
void ChangedBaseline()
{
    // 非共面四边形换对角线后，中心从旧高0.5变成新边上的0
    // 即使自由点在另一面内部，中心的零系数也不能抹去这项固定误差
    const auto geometry=Square();const auto flipped=Audit::Flip(geometry,{0,2});
    const auto refined=Audit::SplitFace(*flipped,0,{1.0/3,1.0/3,0},-1);
    Require(refined.has_value(),"解析面内细分构造失败");
    InitialMesh input;input.Config.Budget=16;
    for (const auto& p : geometry.Points) input.Vertices.push_back(p);
    input.Faces={{0,geometry.Faces[0]},{1,geometry.Faces[1]}};
    input.Source={3,3,{0,0,0,0,32768,0,0,0,65535}};
    TransactionalState state(input);TransactionalSamples samples(input.Source);WorkLedger work;samples.Refresh(state,work);
    Proposal proposal;proposal.Root=0;proposal.Support={0,1};proposal.Points=refined->Points;proposal.Faces=refined->Faces;
    proposal.NewVertex=-1;proposal.Free={-1};proposal.Samples=samples.VisibleSupport(proposal.Support);
    const auto model=Audit::Model(state,samples,proposal,work);bool checked=false;
    for (const auto& row : model)
    {
        const auto q=samples.Parameter(row.Sample);if (q.U!=.5 || q.V!=.5) continue;
        const auto& value=samples.Geometry(row.Sample);
        Require(row.Weight==0 && std::abs(value.MeshHeight-.5)<1e-12,"中心不是预期的旧面/新边见证");
        Require(std::abs(row.Difference-value.ReferenceHeight)<1e-12 && row.Difference>.49,
            "新连接模型偷用了旧状态高度");
        checked=true;
    }
    Require(checked,"解析中心样本缺失");
}
}
int main()
{
    try { Construction();ChangedBaseline();std::cout<<"有限构造与新连接基准核查完成\n";return 0; }
    catch (const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
