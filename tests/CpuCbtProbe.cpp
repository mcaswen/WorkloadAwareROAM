#include "algorithms/cpu_cbt/CpuCbtMesh.h"
#include "algorithms/cpu_cbt/CpuCbtUpdate.h"
#include "algorithms/TerrainLodView.h"
#include "experiment/ExperimentCsvCodec.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace
{
using namespace ParallelRoam;
using namespace Algorithms;
using namespace Algorithms::CpuCbt;
using namespace Algorithms::Cbt2024;
using Clock = std::chrono::steady_clock;
using Row = Experiment::ExperimentCsvRow;
template<typename T> std::string Number(T value)
{
    if constexpr (std::is_floating_point_v<T>) return Experiment::FormatExperimentCsvDouble(value);
    else return std::to_string(value);
}
void Require(bool condition, const std::string& error)
{
    if (!condition) throw std::runtime_error(error);
}
void Write(std::ostream& stream, const Row& row)
{
    Experiment::WriteExperimentCsvRow(stream,row);
    Require(static_cast<bool>(stream), "CSV 写入失败");
}
double Elapsed(Clock::time_point start)
{
    return std::chrono::duration<double,std::milli>(Clock::now()-start).count();
}

/// <summary>
/// 轻量摘要只编码命名数值，不编码容器容量或结构体填充字节
/// 资产与程序的 SHA-256 由既有外部采集工具另行核查
/// </summary>
struct Digest
{
    std::uint64_t Value{14695981039346656037ULL};
    void Add(std::uint64_t word)
    {
        for (unsigned i = 0; i < 8; ++i) { Value ^= (word >> (i*8)) & 255U; Value *= 1099511628211ULL; }
    }
    void Float(float value) { Add(std::bit_cast<std::uint32_t>(value)); }
};

std::uint64_t TopologyDigest(const CpuCbtState& state)
{
    Digest digest;
    for (const auto slot : state.ActiveIndices)
    {
        digest.Add(slot); digest.Add(state.HeapIds[slot]);
        digest.Add(state.Neighbors[slot].Previous); digest.Add(state.Neighbors[slot].Next); digest.Add(state.Neighbors[slot].Twin);
    }
    return digest.Value;
}
std::uint64_t MeshDigest(const Terrain::TerrainMeshData& mesh)
{
    Digest digest;
    for (const auto& vertex : mesh.Vertices)
    {
        for (int axis = 0; axis < 3; ++axis) { digest.Float(vertex.Position[axis]); digest.Float(vertex.Normal[axis]); }
        digest.Float(vertex.TexCoord.x); digest.Float(vertex.TexCoord.y);
    }
    for (const auto index : mesh.Indices) digest.Add(index);
    return digest.Value;
}

/// <summary>
/// 端点格桶仅缩小查找范围，最终仍按冻结的实际 UV 距离匹配
/// 多个不同端点同时落入容差时拒绝，避免量化哈希掩盖裂缝
/// </summary>
struct VertexMatcher
{
    static constexpr double Tolerance = 1e-6;
    std::map<std::pair<int,int>,std::vector<std::size_t>> Buckets;
    std::vector<glm::dvec2> Points;
    std::size_t Find(glm::dvec2 point)
    {
        Require(std::isfinite(point.x) && std::isfinite(point.y) && point.x >= -Tolerance &&
            point.y >= -Tolerance && point.x <= 1+Tolerance && point.y <= 1+Tolerance, "网格 UV 越界");
        const int x = static_cast<int>(std::floor(point.x/Tolerance));
        const int y = static_cast<int>(std::floor(point.y/Tolerance));
        std::optional<std::size_t> match;
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
            {
                const auto bucket = Buckets.find({x+dx,y+dy});
                if (bucket == Buckets.end()) continue;
                for (auto index : bucket->second)
                    if (glm::length(Points[index]-point) <= Tolerance)
                    {
                        Require(!match.has_value(), "UV 端点匹配存在歧义");
                        match = index;
                    }
            }
        if (match) return *match;
        const auto index = Points.size();
        Points.push_back(point); Buckets[{x,y}].push_back(index);
        return index;
    }
};

/// <summary>
/// 在算法计时外独立检查编号覆盖、邻接和完整边配对，拒绝裂缝及重叠层次
/// </summary>
void Audit(const CpuCbtState& state, const Terrain::TerrainMeshData& mesh)
{
    std::set<std::uint64_t> ids;
    std::size_t physicalActive = 0;
    for (std::uint32_t slot = 0; slot < state.HeapIds.size(); ++slot)
    {
        const bool active = state.HeapIds[slot] != 0U;
        Require(active == (slot >= CpuCbtState::DynamicCapacity || state.Occupancy.GetBit(slot)), "槽位与位域不一致");
        if (active) ++physicalActive;
    }
    Require(physicalActive == state.ActiveIndices.size() && physicalActive == state.Occupancy.BitCount()+6U,
        "活动数量不一致");
    for (auto slot : state.ActiveIndices)
    {
        const auto heap = state.HeapIds[slot];
        Require(ids.insert(heap).second, "逻辑编号重复");
        const auto depth = static_cast<unsigned>(std::bit_width(heap));
        Require(depth >= 4 && depth <= state.Settings.MaxHeapBitDepth && (heap>>(depth-4)) >= 8 &&
            (heap>>(depth-4)) < 14, "逻辑编号超出基础层次");
        const auto& n = state.Neighbors[slot];
        for (auto neighbor : {n.Previous,n.Next,n.Twin})
        {
            if (neighbor == InvalidCbtBisectorIndex) continue;
            Require(neighbor < state.HeapIds.size() && state.HeapIds[neighbor] != 0, "邻接指向空槽");
            const auto& back = state.Neighbors[neighbor];
            Require(back.Previous == slot || back.Next == slot || back.Twin == slot, "邻接没有反向引用");
        }
    }
    for (auto heap : ids)
        for (heap >>= 1; heap >= 8; heap >>= 1) Require(!ids.contains(heap), "活动叶包含祖先重叠");
    Require(mesh.Vertices.size() == physicalActive*3U && mesh.Indices.size() == physicalActive*3U, "网格数量错误");
    VertexMatcher matcher;
    std::map<std::pair<std::size_t,std::size_t>,std::pair<unsigned,int>> edges;
    double area = 0;
    for (std::size_t triangle = 0; triangle < physicalActive; ++triangle)
    {
        std::array<glm::dvec2,3> uv;
        std::array<std::size_t,3> corners;
        for (std::size_t j = 0; j < 3; ++j)
        {
            const auto index = mesh.Indices[triangle*3+j];
            Require(index < mesh.Vertices.size(), "网格索引越界");
            uv[j] = glm::dvec2(mesh.Vertices[index].TexCoord);
            corners[j] = matcher.Find(uv[j]);
        }
        const auto a = uv[1]-uv[0], b = uv[2]-uv[0];
        const double twiceArea = a.x*b.y-a.y*b.x;
        Require(std::isfinite(twiceArea) && std::abs(twiceArea) > 1e-12, "退化三角形");
        area += std::abs(twiceArea)*0.5;
        for (std::size_t j = 0; j < 3; ++j)
        {
            const auto first = corners[j], second = corners[(j+1)%3];
            Require(first != second, "边端点重合");
            auto& edge = edges[std::minmax(first,second)];
            ++edge.first; edge.second += first < second ? 1 : -1;
        }
    }
    Require(std::abs(area-1.0) <= 1e-5, "UV 总面积没有覆盖单位地形");
    for (const auto& [key,value] : edges)
    {
        if (value.first == 2) { Require(value.second == 0, "内部边绕序不相反"); continue; }
        Require(value.first == 1, "非流形多重边");
        const auto a = matcher.Points[key.first], b = matcher.Points[key.second];
        const auto on = [](double x,double y,double side) { return std::abs(x-side)<=1e-6 && std::abs(y-side)<=1e-6; };
        Require(on(a.x,b.x,0) || on(a.x,b.x,1) || on(a.y,b.y,0) || on(a.y,b.y,1), "内部存在未配对完整边");
    }
}

/// <summary>
/// 每轮结果保留算法报告和外层耗时，用于固定段末回看适应延迟
/// </summary>
struct Round
{
    CpuCbtUpdateReport Update;
    double MeshMs{0}, TotalMs{0};
    std::uint64_t TopologyHash{0}, MeshHash{0};
};
std::uint32_t Templates(const CpuCbtUpdateReport& report)
{
    return std::accumulate(report.TemplateCounts.begin(),report.TemplateCounts.end(),0U);
}
bool Stable(const CpuCbtUpdateReport& report)
{
    return Templates(report)==0 && report.PairMergeCount+report.QuadMergeCount==0;
}

void WriteSegments(std::ostream& out, const std::vector<Round>& rounds, std::uint32_t budget)
{
    Write(out,{"segment","firstStableRound","adaptationLatencyRounds","adaptationUpdateCpuMs","stabilityStatus",
        "pendingSplitProposalsAtStableStart","reservationRejectionsAtStableStart","budgetLimitedRoundCount",
        "budgetReservationRejectionCount","finalTriangleCount","finalBudgetUtilization"});
    for (std::size_t segment=0; segment<3; ++segment)
    {
        const auto begin=segment*32, end=begin+32;
        std::optional<std::size_t> first;
        auto suffix=end;
        std::uint64_t limited=0, rejects=0;
        for (auto i=begin; i<end; ++i)
        {
            if (!first && Stable(rounds[i].Update)) first=i;
            limited += rounds[i].Update.BudgetLimitedRound;
            rejects += rounds[i].Update.ReservationRejectedCount;
        }
        while (suffix>begin && Stable(rounds[suffix-1].Update)) --suffix;
        const bool settled=end-suffix>=2;
        double cost=0;
        if (settled) for (auto i=begin; i<=suffix; ++i) cost+=rounds[i].Update.UpdateMs;
        const auto& last=rounds[end-1].Update;
        Write(out,{Number(segment),first?Number(*first-begin+1):"",settled?Number(suffix-begin+1):"",
            settled?Number(cost):"",settled?"observed_stable_tail":"not_stable_within_32_rounds",
            settled?Number(rounds[suffix].Update.SplitProposalCount):"",settled?Number(rounds[suffix].Update.ReservationRejectedCount):"",
            Number(limited),Number(rejects),Number(last.TriangleCountAfter),Number(double(last.TriangleCountAfter)/budget)});
    }
}

void WriteRound(std::ostream& out, std::size_t index, const Round& r)
{
    const auto& u=r.Update;
    Write(out,{Number(index),Number(index/32),Number(u.TriangleCountBefore),Number(u.TriangleCountAfterSplit),Number(u.TriangleCountAfter),
        Number(u.SplitProposalCount),Number(u.MergeProposalCount),u.PoolOnlyRequiredSlots?Number(*u.PoolOnlyRequiredSlots):"",
        u.PoolOnlyReservationRejections?Number(*u.PoolOnlyReservationRejections):"",Number(u.RequiredSlots),Number(u.AcceptedSlots),Number(u.ReleasedSlots),
        Number(u.BudgetRemainingBefore),Number(u.BudgetRemainingAfterSplit),Number(u.BudgetRemainingAfter),Number(u.OldFreeDynamicSlots),
        Number(u.ReservationRejectedCount),Number(u.DuplicateCandidateCount),Number(u.PlannerRemainingSlots),Number(u.BudgetLimitedRound),Number(u.JointlyLimitedRound),
        Number(Templates(u)),Number(u.TemplateCounts[0]),Number(u.TemplateCounts[1]),Number(u.TemplateCounts[2]),Number(u.TemplateCounts[3]),
        Number(u.PairMergeCount),Number(u.QuadMergeCount),Number(u.PairMergeCount+u.QuadMergeCount),Number(std::int64_t(u.AcceptedSlots)-u.ReleasedSlots),
        Number(r.TopologyHash),Number(r.MeshHash),Number(u.PreparationMs),Number(u.ClassificationGeometryMs),Number(u.ClassificationMs),Number(u.MappingMs),
        Number(u.PlanningMs),Number(u.AllocationMs),Number(u.BisectCommitMs),Number(u.SimplifyCommitMs),Number(u.PublishMs),Number(u.DemandDiagnosticMs),
        Number(u.UpdateMs),Number(r.MeshMs),Number(r.TotalMs)});
}

void Run(const std::string& scenario, const std::string& mode, const std::filesystem::path& output)
{
    const bool peking=scenario=="cpu-cbt-peking547-b20000-v1";
    Require(peking || scenario=="cpu-cbt-test129-b4096-v1","未知冻结场景");
    Require(mode=="validate" || mode=="measure","未知原型模式");
    Require(!std::filesystem::exists(output),"输出目录已经存在");
    std::filesystem::create_directories(output);
    const auto asset=std::filesystem::path("assets/heightmaps")/(peking?"Hm_Terrain_Peking_513.png":"Hm_Terrain_Test_129.pgm");
    Terrain::HeightMap map; std::string error;
    auto stage=Clock::now();
    Require(map.LoadFromFile(asset,&error),error);
    const auto loadMs=Elapsed(stage);
    const int dimension=peking?547:129;
    Require(map.Width()==dimension && map.Height()==dimension,"资产实际尺寸不匹配");
    CpuCbtSettings settings; settings.TerrainSize=peking?80.0F:30.0F; settings.HeightScale=peking?12.0F:4.0F;
    settings.TriangleBudget=peking?20000U:4096U;
    CpuCbtState state;
    stage=Clock::now();
    Require(InitializeCpuCbt(state,settings,error),error);
    const auto initializeMs=Elapsed(stage);
    std::array<TerrainLodViewInput,2> views;
    std::ofstream matrices(output/"views.csv");
    Write(matrices,{"height","matrix","column","x","y","z","w"});
    Digest viewDigest;
    for (std::size_t i=0; i<2; ++i)
    {
        const float h=i==0?20.0F:80.0F;
        views[i]=BuildTerrainLodViewInput(glm::lookAtRH(glm::vec3{0,h,0},glm::vec3{0},glm::vec3{0,0,-1}),
            glm::perspectiveRH_NO(glm::radians(60.0F),1280.0F/720.0F,0.05F,1000.0F),{0,h,0},{0,-1,0},1280,720,false);
        const std::array<glm::mat4,3> values{views[i].View,views[i].Projection,views[i].ViewProjection};
        for (std::size_t m=0; m<values.size(); ++m)
            for (int c=0; c<4; ++c)
            {
                const auto v=values[m][c];
                Write(matrices,{Number(h),Number(m),Number(c),Number(v.x),Number(v.y),Number(v.z),Number(v.w)});
                for (int j=0;j<4;++j) viewDigest.Float(v[j]);
            }
    }
    std::ofstream metadata(output/"metadata.csv");
    Write(metadata,{"protocolVersion","scenarioId","mode","heightMapPath","width","height","terrainSize","heightScale","triangleBudget",
        "dynamicCapacity","maxHeapBitDepth","triangleAreaPixels","roundCount","viewDigest","loadMs","initializeMs","sourceReference"});
    Write(metadata,{"1",scenario,mode,asset.generic_string(),Number(dimension),Number(dimension),Number(settings.TerrainSize),Number(settings.HeightScale),
        Number(settings.TriangleBudget),"131072","20","50","96",Number(viewDigest.Value),Number(loadMs),Number(initializeMs),"RoamTesting-d462089"});
    std::ofstream rows(output/"rounds.csv");
    Write(rows,{"round","segment","trianglesBefore","trianglesAfterSplit","activeTriangleCount","splitProposalCount","mergeProposalCount",
        "poolOnlyRequiredSlots","poolOnlyReservationRejections","requiredSlots","acceptedSlots","releasedSlots","budgetRemainingBefore",
        "budgetRemainingAfterSplit","budgetRemainingAfter","oldFreeDynamicSlots","reservationRejectedCount","duplicateCandidateCount","plannerRemainingSlots",
        "budgetLimitedRound","jointlyLimitedRound","splitTemplateCount","centerCount","rightDoubleCount","leftDoubleCount","tripleCount",
        "pairMergeCount","quadMergeCount","simplifyCount","occupancyDelta","topologyDigest","meshDigest","preparationMs","classificationGeometryMs",
        "classificationMs","mappingMs","planningMs","allocationMs","bisectCommitMs","simplifyCommitMs","publishMs","demandDiagnosticMs","updateMs","meshMs","updateAndMeshMs"});
    std::vector<Round> rounds; rounds.reserve(96);
    std::uint64_t previousHash=TopologyDigest(state);
    for (std::size_t i=0; i<96; ++i)
    {
        Round r; Terrain::TerrainMeshData mesh;
        const auto total=Clock::now();
        r.Update=UpdateCpuCbt(state,map,views[i>=32 && i<64?1:0],{mode=="validate"});
        if (!r.Update.Success)
        {
            // 保留失败前已发布的活动状态，反例不覆盖此前完成的轮次数据
            std::ofstream failure(output/"failure.txt"); failure << "round=" << i << '\n' << r.Update.Error << '\n';
            std::ofstream snapshot(output/"failure-state.csv");
            Write(snapshot,{"physicalSlot","heapId","previous","next","twin"});
            for (auto slot : state.ActiveIndices) Write(snapshot,{Number(slot),Number(state.HeapIds[slot]),
                Number(state.Neighbors[slot].Previous),Number(state.Neighbors[slot].Next),Number(state.Neighbors[slot].Twin)});
            throw std::runtime_error("轮 " + Number(i) + ": " + r.Update.Error);
        }
        stage=Clock::now();
        Require(BuildCpuCbtMesh(state,map,mesh,error),error);
        r.MeshMs=Elapsed(stage); r.TotalMs=Elapsed(total);
        r.TopologyHash=TopologyDigest(state); r.MeshHash=MeshDigest(mesh);
        try
        {
            if (mode=="validate") Audit(state,mesh);
            Require(!Stable(r.Update) || previousHash==r.TopologyHash,"无修改轮改变了逻辑拓扑");
        }
        catch (const std::exception& exception)
        {
            std::ofstream failure(output/"failure.txt"); failure << "round=" << i << '\n' << exception.what() << '\n';
            throw;
        }
        previousHash=r.TopologyHash;
        rounds.push_back(r); WriteRound(rows,i,r);
    }
    std::ofstream segments(output/"segments.csv");
    WriteSegments(segments,rounds,settings.TriangleBudget);
    std::cout << scenario << ": 96 轮完成，最终三角形 " << state.ActiveIndices.size() << '\n';
}
}

int main(int argc,char** argv)
{
    try
    {
        Require(argc==7 && std::string(argv[1])=="--scenario" && std::string(argv[3])=="--mode" &&
            std::string(argv[5])=="--output","参数：--scenario 固定场景 --mode validate|measure --output 新目录");
        Run(argv[2],argv[4],argv[6]);
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
