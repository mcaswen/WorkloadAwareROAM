#include "algorithms/cpu_cbt/CpuCbtMesh.h"
#include "algorithms/cpu_cbt/CpuCbtUpdate.h"
#include "algorithms/cbt_2024/CbtBisectCommit.h"
#include "algorithms/cbt_2024/CbtSimplifyCommit.h"
#include "algorithms/cbt_2024/CbtSplitPlanner.h"
#include "algorithms/TerrainLodView.h"
#include "CpuCbtExecutionSupport.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <algorithm>
#include <barrier>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <set>
#include <stdexcept>

namespace
{
using namespace ParallelRoam;
using namespace Algorithms;
using namespace Algorithms::CpuCbt;
using namespace Algorithms::Cbt2024;
void Check(bool passed, const std::string& message)
{
    if (!passed) throw std::runtime_error(message);
}

TerrainLodViewInput View(float height)
{
    return BuildTerrainLodViewInput(glm::lookAtRH(glm::vec3{0, height, 0}, glm::vec3{0}, glm::vec3{0, 0, -1}),
        glm::perspectiveRH_NO(glm::radians(60.0F), 1280.0F / 720.0F, 0.05F, 1000.0F),
        {0, height, 0}, {0, -1, 0}, 1280U, 720U, false);
}

bool SameState(const CpuCbtState& a, const CpuCbtState& b)
{
    if (a.HeapIds != b.HeapIds || a.ActiveIndices != b.ActiveIndices || a.Generation != b.Generation ||
        a.Occupancy.Bitfield() != b.Occupancy.Bitfield() || a.Occupancy.PackedTree() != b.Occupancy.PackedTree()) return false;
    // 比较命名字段，避免预留容量或对象填充字节影响事务不变性断言
    for (std::size_t i = 0; i < a.Data.size(); ++i)
    {
        const auto& x = a.Data[i]; const auto& y = b.Data[i];
        if (x.SubdivisionPattern != y.SubdivisionPattern || x.Indices != y.Indices ||
            x.ProblematicNeighbor != y.ProblematicNeighbor || x.BisectorState != y.BisectorState ||
            x.Flags != y.Flags || x.PropagationId != y.PropagationId ||
            a.Neighbors[i].Previous != b.Neighbors[i].Previous || a.Neighbors[i].Next != b.Neighbors[i].Next ||
            a.Neighbors[i].Twin != b.Neighbors[i].Twin) return false;
    }
    return true;
}

void CheckTopology(const CpuCbtState& state)
{
    CbtBisectCommitResult snapshot;
    snapshot.HeapIds = state.HeapIds;
    snapshot.Neighbors = state.Neighbors;
    snapshot.BisectorData = state.Data;
    std::string error;
    Check(ValidateCbtCommittedTopology(snapshot, CpuCbtState::DynamicCapacity, &error), error);
    Check(state.ActiveIndices.size() == state.Occupancy.BitCount() + 6U, "活动叶与占用归约不一致");
}

bool SameData(const CbtBisectorData& a, const CbtBisectorData& b)
{
    return a.SubdivisionPattern == b.SubdivisionPattern && a.Indices == b.Indices &&
        a.ProblematicNeighbor == b.ProblematicNeighbor && a.BisectorState == b.BisectorState &&
        a.Flags == b.Flags && a.PropagationId == b.PropagationId;
}

bool SameCommit(const CbtBisectCommitResult& a, const CbtBisectCommitResult& b)
{
    if (a.Valid != b.Valid || a.HeapIds != b.HeapIds || a.TemplateCounts != b.TemplateCounts ||
        a.CommittedDynamicSlots != b.CommittedDynamicSlots || a.PropagationNodes != b.PropagationNodes ||
        a.Neighbors.size() != b.Neighbors.size() || a.BisectorData.size() != b.BisectorData.size()) return false;
    for (std::size_t i = 0; i < a.HeapIds.size(); ++i)
        if (a.HeapIds[i] != 0 && (!SameData(a.BisectorData[i],b.BisectorData[i]) ||
            a.Neighbors[i].Previous != b.Neighbors[i].Previous || a.Neighbors[i].Next != b.Neighbors[i].Next ||
            a.Neighbors[i].Twin != b.Neighbors[i].Twin)) return false;
    return true;
}

/// <summary>
/// 同一输入直接对照参考和新内核，改变填写区间顺序时仍保留原传播顺序
/// </summary>
CbtBisectCommitResult CompareCommits(const std::vector<std::uint64_t>& ids,
    const std::vector<CbtBisectorNeighbors>& neighbors, const std::vector<CbtBisectorData>& data,
    const std::vector<std::uint32_t>& nodes, std::uint32_t capacity)
{
    const auto expected = CommitCbtBisects(ids,neighbors,data,nodes,capacity);
    Check(expected.Valid,"对照输入未形成有效参考结果");
    Check(SameCommit(expected,CommitCpuCbtBisects(ids,neighbors,data,nodes,capacity).Topology),"新串行提交不等价");
    for (const auto threads : {1U,2U,4U})
    {
        Tests::CpuCbtTestExecutor execution(threads);
        const auto actual = CommitCpuCbtBisects(ids,neighbors,data,nodes,capacity,execution.Executor());
        Check(SameCommit(expected,actual.Topology),"线程执行提交不等价");
    }
    const CpuCbtRangeExecutor reverse = [](std::size_t count,const CpuCbtRangeTask& task) {
        for (auto i=count; i>0; --i) task(i-1,i);
    };
    Check(SameCommit(expected,CommitCpuCbtBisects(ids,neighbors,data,nodes,capacity,reverse).Topology),
        "反向填写改变结果或传播顺序");
    return expected;
}

/// <summary>
/// 复用参考局部模板的解析布局，直接核对四种模板的逻辑编号
/// 此夹具只验证模板语义，不冒充覆盖整个单位地形的自然网格
/// </summary>
void CheckTemplatesAndPlanning()
{
    const std::array<std::uint32_t, 4> patterns{CbtCenterSplitPattern, CbtRightDoubleSplitPattern,
        CbtLeftDoubleSplitPattern, CbtTripleSplitPattern};
    const std::array<std::array<std::uint64_t, 4>, 4> expected{{{16,17,0,0}, {32,17,33,0}, {16,34,35,0}, {32,34,33,35}}};
    for (std::size_t mode = 0; mode < patterns.size(); ++mode)
    {
        std::vector<std::uint64_t> ids(16U, 0U);
        std::vector<CbtBisectorNeighbors> neighbors(16U);
        std::vector<CbtBisectorData> data(16U);
        for (auto& d : data)
        {
            d.Indices.fill(InvalidCbtBisectorIndex);
            d.ProblematicNeighbor = d.PropagationId = InvalidCbtBisectorIndex;
        }
        for (std::uint32_t i = 8U; i <= 12U; ++i) ids[i] = i;
        neighbors[8] = {9U,10U,InvalidCbtBisectorIndex};
        neighbors[9].Previous = neighbors[10].Previous = 8U;
        data[9].SubdivisionPattern = CbtCenterSplitPattern; data[9].Indices[0] = 11U;
        data[10].SubdivisionPattern = mode < 2U ? CbtNoSplitPattern : CbtCenterSplitPattern;
        data[10].Indices[0] = 12U;
        data[8].SubdivisionPattern = patterns[mode]; data[8].Indices = {0,1,2};
        const auto r = CompareCommits(ids, neighbors, data, {8U}, 8U);
        Check(r.Valid && r.TemplateCounts[mode] == 1U, "四模板参考提交失败");
        Check(r.HeapIds[8] == expected[mode][0] && r.HeapIds[0] == expected[mode][1] &&
            r.HeapIds[1] == expected[mode][2] && r.HeapIds[2] == expected[mode][3], "四模板逻辑编号错误");
    }
    const auto node = [](std::uint32_t depth, std::uint32_t previous, std::uint32_t twin) {
        return CbtSplitPlanningNode{std::uint64_t{1} << (depth - 1U), {previous, InvalidCbtBisectorIndex, twin}, 0};
    };
    const auto invalid = InvalidCbtBisectorIndex;
    std::vector<CbtSplitPlanningNode> chain{node(8,invalid,1),node(7,0,2),node(6,1,3),node(6,invalid,invalid)};
    const auto enough = PlanCbtSplits(chain,{0},4,7);
    const auto rejected = PlanCbtSplits(chain,{0},4,6);
    Check(enough.Valid && enough.RequiredSlotCount == 6U && enough.RemainingMemory == 1U, "保守预留返还错误");
    Check(rejected.Valid && rejected.RequiredSlotCount == 0U && rejected.RejectedCandidateCount == 1U &&
        rejected.AllocationNodes.empty(), "整条请求拒绝后留下部分模板");
    std::vector<CbtSplitPlanningNode> shared{node(6,invalid,2),node(6,invalid,2),node(6,invalid,invalid)};
    const auto plan = PlanCbtSplits(shared,{0,1},4,6);
    Check(plan.Valid && plan.RequiredSlotCount == 3U && plan.AllocationNodes.size() == 3U, "共享闭包未去重");
    CbtOccupancyTree full{CbtOccupancyCapacity::Capacity128K};
    for (std::uint32_t i = 0; i < 131072U; ++i) Check(full.SetBit(i,true), "占用设置失败");
    full.Reduce();
    Check(!AllocateCbtSplitSlots(plan,full).Valid, "满槽池仍分配成功");
}

/// <summary>
/// 两个相互独立的边界请求中，一个旧合并被同轮细分覆盖，另一个仍可合并
/// 使用完整基础邻接验证阶段组合，并让参考面对面模板覆盖四节点合并
/// </summary>
void CheckOldMergeRevalidation()
{
    CpuCbtState state; std::string error;
    Check(InitializeCpuCbt(state, {}, error), error);
    const auto a = CpuCbtState::DynamicCapacity;
    const auto b = a + 5U;
    auto data = state.Data;
    data[a].SubdivisionPattern = data[b].SubdivisionPattern = data[a+2].SubdivisionPattern = CbtCenterSplitPattern;
    data[a].Indices[0] = 0; data[b].Indices[0] = 1; data[a+2].Indices[0] = 2;
    const auto initial = CompareCommits(state.HeapIds,state.Neighbors,data,{a,b,a+2},a);
    Check(initial.Valid, "两组边界初始细分失败");
    data = initial.BisectorData;
    for (auto& d : data) { d.SubdivisionPattern = 0; d.BisectorState = CbtUnchangedElement; }
    for (const auto slot : {a,b,0U,1U}) data[slot].BisectorState = CbtSimplifyElement;
    // 槽 2 的面对面细分强制覆盖 a；a 与旧兄弟原本是可合并的边界对
    Check(initial.Neighbors[2].Twin == a && initial.Neighbors[a].Twin == 2, "面对面夹具关系错误");
    data[2].BisectorState = CbtBisectElement;
    const auto eligible = CommitCbtSimplifications(initial.HeapIds,initial.Neighbors,data,{a,b},a);
    Check(eligible.Valid && eligible.PairMergeCount == 2, "细分前旧合并请求并非同时有效");
    data[a].SubdivisionPattern = data[2].SubdivisionPattern = CbtCenterSplitPattern;
    data[a].Indices[0] = 3; data[2].Indices[0] = 4;
    const auto refined = CompareCommits(initial.HeapIds,initial.Neighbors,data,{2,a},a);
    Check(refined.Valid, "同轮细分夹具失败");
    const auto merged = CommitCbtSimplifications(refined.HeapIds,refined.Neighbors,refined.BisectorData,{a,b},a);
    Check(merged.Valid && merged.PairMergeCount == 1 && merged.HeapIds[a] == 32 &&
        merged.HeapIds[b] == 13 && merged.HeapIds[1] == 0, "旧合并候选重验语义错误");
    Check(ValidateCbtSimplifiedTopology(merged,a,&error), error);

    // 面对面基础叶共同细分后，其四个子叶应由唯一代表合并
    data = state.Data;
    data[a+1].SubdivisionPattern = data[a+3].SubdivisionPattern = CbtCenterSplitPattern;
    data[a+1].Indices[0] = 0; data[a+3].Indices[0] = 1;
    const auto split = CompareCommits(state.HeapIds,state.Neighbors,data,{a+1,a+3},a);
    Check(split.Valid, "面对面细分失败");
    data = split.BisectorData;
    for (auto& d : data) d.BisectorState = CbtSimplifyElement;
    const auto quad = CommitCbtSimplifications(split.HeapIds,split.Neighbors,data,{a+3,a+1},a);
    Check(quad.Valid && quad.QuadMergeCount == 1 && quad.ReleasedDynamicSlots.size() == 2, "四节点合并不唯一");
    Check(ValidateCbtSimplifiedTopology(quad,a,&error), error);
}

void CheckContinuousRound()
{
    // 平坦二乘二资产是解析夹具，不运行或调参任何冻结自然场景
    const auto path = std::filesystem::temp_directory_path() /
        ("cpu-cbt-flat-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".pgm");
    { std::ofstream out(path,std::ios::binary); out << "P5\n2 2\n255\n"; const char pixels[4]{}; out.write(pixels,4); }
    Terrain::HeightMap map; std::string error;
    const bool loaded = map.LoadFromFile(path,&error);
    std::filesystem::remove(path);
    Check(loaded,error);
    CpuCbtState state;
    CpuCbtSettings settings; settings.TriangleBudget = 96;
    Check(InitializeCpuCbt(state,settings,error),error);
    auto diagnosed = state;
    auto serial = state, parallel = state;
    Tests::CpuCbtTestExecutor execution(4U);
    std::uint64_t splits = 0, merges = 0;
    bool reused = false;
    std::vector<bool> released(CpuCbtState::DynamicCapacity,false);
    for (int round = 0; round < 24; ++round)
    {
        const auto before = state.Occupancy.Bitfield();
        const auto view = View(round < 8 || round >= 16 ? 20.0F : 500.0F);
        const auto r = UpdateCpuCbt(state,map,view);
        const auto d = UpdateCpuCbt(diagnosed,map,view,{true});
        const CpuCbtUpdateOptions local{false,CpuCbtBisectImplementation::LocalTemplates};
        const auto s = UpdateCpuCbt(serial,map,view,local);
        const auto p = UpdateCpuCbt(parallel,map,view,local,execution.Executor());
        Check(r.Success, "组合轮 " + std::to_string(round) + ": " + r.Error);
        Check(d.Success && SameState(state,diagnosed), "只读诊断改变状态");
        Check(s.Success && p.Success && SameState(state,serial) && SameState(state,parallel),
            "持续三档状态不等价");
        Check(s.AcceptedSlots == r.AcceptedSlots && p.ReleasedSlots == r.ReleasedSlots &&
            s.LocalBisectTimings && p.LocalBisectTimings && !r.LocalBisectTimings,"工作量或计时采集边界错误");
        Check(!r.PoolOnlyRequiredSlots && d.PoolOnlyRequiredSlots.has_value(), "未采集需求未保留空值");
        Check(r.TriangleCountAfterSplit <= settings.TriangleBudget && r.TriangleCountAfter <= settings.TriangleBudget,
            "硬预算被突破");
        CheckTopology(state);
        splits += r.AcceptedSlots; merges += r.ReleasedSlots;
        for (std::uint32_t slot = 0; slot < CpuCbtState::DynamicCapacity; ++slot)
        {
            const bool was = ((before[slot/64] >> (slot%64)) & 1U) != 0;
            if (was && !state.Occupancy.GetBit(slot)) released[slot] = true;
            if (!was && state.Occupancy.GetBit(slot) && released[slot]) reused = true;
        }
        Terrain::TerrainMeshData mesh;
        Check(BuildCpuCbtMesh(state,map,mesh,error),error);
        Check(mesh.Indices.size() == state.ActiveIndices.size()*3U, "网格数量错误");
    }
    Check(splits > 0 && merges > 0 && reused, "连续夹具未覆盖释放后复用");
    Check(InitializeCpuCbt(serial,settings,error),error);
    const auto unpublished = serial;
    const CpuCbtRangeExecutor failedDispatch = [&](std::size_t count,const CpuCbtRangeTask& task) {
        // 已接受区间先完成再抛出，更新边界必须丢弃临时新代，不能发布或继续借用
        execution.Executor()(count,task);
        throw std::runtime_error("定向派发失败");
    };
    Check(!UpdateCpuCbt(serial,map,View(20),{false,CpuCbtBisectImplementation::LocalTemplates},failedDispatch).Success &&
        SameState(serial,unpublished),"派发异常造成半发布");
    settings.TriangleBudget = 6;
    Check(InitializeCpuCbt(state,settings,error),error);
    const auto capped = UpdateCpuCbt(state,map,View(20),{true});
    Check(capped.Success && capped.TriangleCountAfter == 6 && capped.AcceptedSlots == 0 &&
        capped.ReservationRejectedCount > 0 && capped.BudgetLimitedRound, "六叶预算门禁失效");
    // 各失败都比较完整状态，避免只核对数量而漏掉半发布的邻接或占用树
    state.Neighbors.back().Twin = 0;
    auto before = state;
    Check(!UpdateCpuCbt(state,map,View(20)).Success && SameState(state,before), "非法邻接造成半发布");
    Check(InitializeCpuCbt(state,settings,error),error);
    state.HeapIds.back() = std::uint64_t{1} << 63;
    before = state;
    Check(!UpdateCpuCbt(state,map,View(20)).Success && SameState(state,before), "非法深度造成半发布");
    settings.MaxHeapBitDepth = 64;
    Check(!InitializeCpuCbt(state,settings,error) && SameState(state,before), "非法初始化改写状态");
}

void CheckOwnershipAndThreads()
{
    CpuCbtState state; std::string error;
    Check(InitializeCpuCbt(state,{},error),error);
    const auto a = CpuCbtState::DynamicCapacity + 1U, b = CpuCbtState::DynamicCapacity + 3U;
    auto data = state.Data;
    data[a].SubdivisionPattern = data[b].SubdivisionPattern = CbtCenterSplitPattern;
    data[a].Indices[0] = 0U; data[b].Indices[0] = 1U;
    const std::vector<std::uint32_t> nodes{a,b};
    const auto expected = CompareCommits(state.HeapIds,state.Neighbors,data,nodes,CpuCbtState::DynamicCapacity);

    Tests::CpuCbtTestExecutor execution(2U);
    std::vector<Tests::CpuCbtRangeEvidence> evidence;
    std::barrier arrival{2};
    const CpuCbtRangeExecutor simultaneous = [&](std::size_t count,const CpuCbtRangeTask& task) {
        execution.Executor(&evidence)(count,[&](std::size_t begin,std::size_t end) {
            arrival.arrive_and_wait();
            task(begin,end);
        });
    };
    const auto actual = CommitCpuCbtBisects(state.HeapIds,state.Neighbors,data,nodes,
        CpuCbtState::DynamicCapacity,simultaneous);
    Check(SameCommit(expected,actual.Topology) && evidence.size()==2 && evidence[0].Begin<evidence[0].End &&
        evidence[1].Begin<evidence[1].End && evidence[0].Thread!=evidence[1].Thread &&
        evidence[0].Thread!=std::this_thread::get_id() && evidence[1].Thread!=std::this_thread::get_id(),
        "真实非空模板区间未由两个线程同时处理");

    bool called=false;
    const CpuCbtRangeExecutor observe = [&](std::size_t count,const CpuCbtRangeTask& task) { called=true;task(0,count); };
    const auto reject = [&](const std::vector<CbtBisectorData>& badData,const std::vector<std::uint32_t>& badNodes) {
        called=false;
        Check(!CommitCpuCbtBisects(state.HeapIds,state.Neighbors,badData,badNodes,
            CpuCbtState::DynamicCapacity,observe).Topology.Valid && !called,"非法所有权仍派发模板");
    };
    reject(data,{a,a});
    auto bad=data; bad[b].Indices[0]=0; reject(bad,nodes);
    bad=data; bad[a].Indices[0]=b; reject(bad,nodes);
    bad=data; bad[a].Indices[0]=InvalidCbtBisectorIndex; reject(bad,nodes);
    bad=data; bad[a].SubdivisionPattern=16U; reject(bad,nodes);
    reject(data,{static_cast<std::uint32_t>(state.HeapIds.size())});
    bad.pop_back(); reject(bad,nodes);
    auto ids=state.HeapIds; ids[a]=std::uint64_t{1}<<63;
    Check(!CommitCpuCbtBisects(ids,state.Neighbors,data,nodes,CpuCbtState::DynamicCapacity).Topology.Valid,
        "编号溢出未拒绝");
    const CpuCbtRangeExecutor omitted = [](std::size_t,const CpuCbtRangeTask&) {};
    Check(!CommitCpuCbtBisects(state.HeapIds,state.Neighbors,data,nodes,
        CpuCbtState::DynamicCapacity,omitted).Topology.Valid,"遗漏区间仍形成有效结果");
    called=false;
    const auto empty=CommitCpuCbtBisects(state.HeapIds,state.Neighbors,state.Data,{},CpuCbtState::DynamicCapacity,observe);
    Check(empty.Topology.Valid && !called && empty.Timings.TemplateFillWallMs==0,"空批次仍派发");
}

/// <summary>
/// 固定种子只决定根请求，原计划器形成全部合法模板；所有生成轮都参与对照
/// </summary>
void CheckGeneratedBatches()
{
    CpuCbtState state; std::string error;
    Check(InitializeCpuCbt(state,{},error),error);
    std::mt19937 random{0x43425402U};
    for (unsigned round=0; round<8; ++round)
    {
        std::vector<std::uint32_t> inverse(state.HeapIds.size(),InvalidCbtBisectorIndex), requests;
        for (std::size_t i=0; i<state.ActiveIndices.size(); ++i) inverse[state.ActiveIndices[i]]=static_cast<std::uint32_t>(i);
        std::vector<CbtSplitPlanningNode> nodes;
        auto data=state.Data;
        for (auto slot : state.ActiveIndices)
        {
            data[slot]={}; data[slot].Indices.fill(InvalidCbtBisectorIndex);
            data[slot].ProblematicNeighbor=data[slot].PropagationId=InvalidCbtBisectorIndex;
            const auto local=[&](std::uint32_t n) { return n==InvalidCbtBisectorIndex?n:inverse[n]; };
            const auto n=state.Neighbors[slot];
            nodes.push_back({state.HeapIds[slot],{local(n.Previous),local(n.Next),local(n.Twin)},0});
            if (random()%3U!=0U) requests.push_back(static_cast<std::uint32_t>(nodes.size()-1U));
        }
        const auto plan=PlanCbtSplits(nodes,requests,4U,96U-static_cast<std::uint32_t>(nodes.size()));
        Check(plan.Valid,"固定种子计划失败");
        const auto allocated=AllocateCbtSplitSlots(plan,state.Occupancy);
        Check(allocated.Valid,"固定种子分配失败");
        std::vector<std::uint32_t> physical;
        for (auto local : plan.AllocationNodes)
        {
            const auto slot=state.ActiveIndices[local];
            data[slot].SubdivisionPattern=plan.SubdivisionPatterns[local];
            data[slot].Indices=allocated.NodeIndices[local];
            physical.push_back(slot);
        }
        auto result=CompareCommits(state.HeapIds,state.Neighbors,data,physical,CpuCbtState::DynamicCapacity);
        for (auto slot : result.CommittedDynamicSlots) Check(state.Occupancy.SetBit(slot,true),"固定种子置位失败");
        state.Occupancy.Reduce(); state.ActiveIndices=state.Occupancy.ActiveIndices();
        for (auto slot=CpuCbtState::DynamicCapacity; slot<state.HeapIds.size(); ++slot) state.ActiveIndices.push_back(slot);
        state.HeapIds=std::move(result.HeapIds); state.Neighbors=std::move(result.Neighbors); state.Data=std::move(result.BisectorData);
        CheckTopology(state);
    }
}
}

int main()
{
    try
    {
        CheckTemplatesAndPlanning();
        CheckOldMergeRevalidation();
        CheckContinuousRound();
        CheckOwnershipAndThreads();
        CheckGeneratedBatches();
        std::cout << "CPU CBT 定向夹具完成\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
