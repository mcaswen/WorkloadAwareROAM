#include "experiment/greedy_transactional_lod/TransactionalBoundaryAudit.h"
#include "algorithms/greedy_transactional_lod/TransactionalPipeline.h"
#include "algorithms/greedy_transactional_lod/TransactionalPredicates.h"
#include <cmath>
#include <iostream>

namespace
{
using namespace ParallelRoam::Algorithms::GreedyTransactionalLod;
using Audit = ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalBoundaryAudit;

void Require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

/// <summary>
/// 四面扇形同时包含外边界与双侧内部边，顶点高度不共面
/// </summary>
InitialMesh Fixture()
{
    InitialMesh input;
    input.Config.Budget = 4;
    input.Config.HeightScale = 4;
    input.Config.PreserveSurvivingHeights = true;
    input.Source = {2, 2, {0, 10000, 30000, 40000}};
    input.Vertices = {{0, {0, 0, 0}}, {1, {1, 0, 1}}, {2, {1, 1, 4}},
        {3, {0, 1, 3}}, {4, {.5, .5, 2}}};
    input.Faces = {{0, {0, 1, 4}}, {1, {1, 2, 4}}, {2, {2, 3, 4}}, {3, {3, 0, 4}}};
    return input;
}

void Construction()
{
    const auto input = Fixture();
    const TransactionalState state(input);
    const auto version = state.Version();
    const auto next = state.NextVertexId();
    for (Slot root = 0; root < 4; ++root)
    {
        const auto& face = state.Face(root).Vertices;
        const auto proposal = Audit::Construct(state, root, EdgeKey(face[0], face[1]));
        Require(proposal.Reason.empty(), "合法单侧外边界被拒绝");
        Require(proposal.Support == std::vector<Slot>{root} && proposal.Faces.size() == 2,
            "单侧分割没有形成净一个面");
        Require(proposal.Points.size() == 4 && proposal.Free == std::vector<Identity>{next},
            "私有提案引入了额外自由点");
        const auto& a = state.Vertex(face[0]).Geometry;
        const auto& b = state.Vertex(face[1]).Geometry;
        Require(proposal.Points.at(next) == Point{(a.U + b.U) * .5, (a.V + b.V) * .5, (a.Height + b.Height) * .5},
            "中点初始高度不保持原边");
        for (const auto id : face)
        {
            Require(proposal.Points.at(id) == state.Vertex(id).Geometry, "构造修改了旧点");
        }
        for (const auto& target : proposal.Faces)
        {
            Require(TransactionalPredicates::Shape(proposal.Points.at(target[0]), proposal.Points.at(target[1]),
                proposal.Points.at(target[2])), "规则边界分割形状错误");
        }
    }
    Require(Audit::Construct(state, 0, EdgeKey(0, 4)).Reason == "not_single_sided_root_edge",
        "双侧边进入了边界目录");
    Require(Audit::Construct(state, 0, EdgeKey(2, 3)).Reason == "not_single_sided_root_edge",
        "非根边被当作支持");
    Require(state.Version() == version && state.NextVertexId() == next && state.FaceCount() == 4,
        "只读构造改变生产状态");
}

/// <summary>
/// 原最小角等号保留，严格更小的角仍被拒绝
/// 边界能力不能通过放宽形状判据获得
/// </summary>
void ShapeBoundary()
{
    Require(TransactionalPredicates::Shape({0, 0, 0}, {3, 0, 0}, {3, 1, 0}), "角度等号错误拒绝");
    Require(!TransactionalPredicates::Shape({0, 0, 0}, {4, 0, 0}, {4, 1, 0}), "过小角被错误接受");

    // 构造层不能把洞口边误认成域外边，完整初态合法性仍由独立门禁负责
    auto hole = Fixture();
    hole.Vertices = {{0, {.25, .25, 0}}, {1, {.75, .25, 1}}, {2, {.5, .75, 2}}};
    hole.Faces = {{0, {0, 1, 2}}};
    const TransactionalState state(hole);
    Require(Audit::Construct(state, 0, EdgeKey(0, 1)).Reason == "not_domain_boundary",
        "洞口单侧边被误认成域外边界");
}
}

int main()
{
    try
    {
        Construction();
        ShapeBoundary();
        std::cout << "boundary construction and unchanged shape contract checked\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
