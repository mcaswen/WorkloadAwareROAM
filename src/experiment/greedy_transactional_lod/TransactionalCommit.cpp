#include "experiment/greedy_transactional_lod/TransactionalCommit.h"
#include "experiment/greedy_transactional_lod/TransactionalReservation.h"
#include "experiment/greedy_transactional_lod/TransactionalPredicates.h"

#include <algorithm>
#include <set>
#include <stdexcept>

namespace ParallelRoam::Experiment::GreedyTransactionalLod
{
void TransactionalCommit::Apply(TransactionalState& state,const CertifiedBatch& batch,WorkLedger& work)
{
    const auto started=std::chrono::steady_clock::now();
    if (batch.Version!=state.Version()) throw std::runtime_error("批次快照已过期");
    if (batch.Exchanges.empty()) return;
    std::set<Slot> removed;
    std::set<Identity> deletedVertices;
    std::map<Identity,Point> geometry;
    std::vector<std::array<Identity,3>> newFaces;
    std::vector<TransactionFootprint> footprints;
    const auto collect=[&](const Proposal& proposal,bool donor) {
        // 提交重新核对封闭批次的结构义务，但不再运行发现或高度拟合
        if (proposal.Reason!="certified") throw std::runtime_error("拒绝未认证提案");
        const auto footprint=TransactionalReservation::Footprint(state,proposal);
        for (const auto& other : footprints)
            if (TransactionalReservation::Conflict(footprint,other)) throw std::runtime_error("批次包含未解决读写冲突");
        footprints.push_back(footprint);
        for (auto face : proposal.Support)
            if (!removed.insert(face).second) throw std::runtime_error("重复替换旧面");
        for (const auto& face : proposal.Faces)
        {
            if (!TransactionalPredicates::Shape(proposal.Points.at(face[0]),proposal.Points.at(face[1]),proposal.Points.at(face[2])))
                throw std::runtime_error("发布几何不满足形状条件");
            auto canonical=face;
            for (std::size_t i=1;i<3;++i)
            {
                const std::array<Identity,3> rotation{face[i],face[(i+1)%3],face[(i+2)%3]};
                canonical=std::min(canonical,rotation);
            }
            newFaces.push_back(canonical);
        }
        if (donor) deletedVertices.insert(proposal.Center);
        else for (auto id : proposal.Free)
        {
            const auto point=proposal.Points.at(id);
            if (!geometry.emplace(id,point).second) throw std::runtime_error("同一高度被多次写入");
        }
    };
    for (const auto& exchange : batch.Exchanges)
    {
        if (exchange.Receiver.Faces.size()!=exchange.Receiver.Support.size()+2)
            throw std::runtime_error("接收方预算证书不一致");
        collect(exchange.Receiver,false);
        if (exchange.HasDonor)
        {
            if (exchange.Donor.Support.size()!=exchange.Donor.Faces.size()+2)
                throw std::runtime_error("回收方预算证书不一致");
            collect(exchange.Donor,true);
        }
    }
    if (state.FaceCount()-removed.size()+newFaces.size()>state.Config().Budget)
        throw std::runtime_error("活动三角形预算不足");
    std::sort(newFaces.begin(),newFaces.end());
    // 逻辑连接决定新面身份，输入事务的枚举顺序不能泄漏到下一轮同分选择
    if (std::adjacent_find(newFaces.begin(),newFaces.end())!=newFaces.end()) throw std::runtime_error("新面重复");

    // 物理槽分配只读取本次可释放集合和空闲表前缀，不扫描全部存活记录
    std::vector<Slot> faceSlots(removed.begin(),removed.end());
    const auto needed=newFaces.size();
    std::size_t usedFreeFaces=0,appendFaces=0;
    while (faceSlots.size()<needed)
    {
        if (usedFreeFaces<state._freeFaces.size())
            faceSlots.push_back(state._freeFaces[state._freeFaces.size()-1-usedFreeFaces++]);
        else faceSlots.push_back(static_cast<Slot>(state._faces.size()+appendFaces++));
    }
    std::map<Identity,Slot> addedIndex;
    // 被删除中心的槽可直接转交新点，逻辑身份索引在最后统一替换
    std::vector<Slot> vertexSlots;
    for (auto id : deletedVertices) vertexSlots.push_back(state._vertexIndex.at(id));
    std::size_t assignedVertices=0,usedFreeVertices=0,appendVertices=0;
    for (const auto& [id,point] : geometry)
    {
        static_cast<void>(point);
        if (deletedVertices.contains(id)) throw std::runtime_error("被删除点仍有高度写入");
        if (state._vertexIndex.contains(id)) continue;
        Slot slot;
        if (assignedVertices<vertexSlots.size()) slot=vertexSlots[assignedVertices];
        else if (usedFreeVertices<state._freeVertices.size()) slot=state._freeVertices[state._freeVertices.size()-1-usedFreeVertices++];
        else slot=static_cast<Slot>(state._vertices.size()+appendVertices++);
        ++assignedVertices;addedIndex.emplace(id,slot);
    }
    std::map<Identity,VertexRecord> vertices;
    // 保留点也可能共享邻接更新；这里合成并集，一次发布最终 incident 列表
    const auto vertex=[&](Identity id)->VertexRecord& {
        auto it=vertices.find(id);
        if (it!=vertices.end()) return it->second;
        const auto old=state._vertexIndex.find(id);
        if (old==state._vertexIndex.end()) return vertices.emplace(id,VertexRecord{id,geometry.at(id),{},true}).first->second;
        return vertices.emplace(id,state._vertices[old->second]).first->second;
    };
    std::map<Edge,EdgeRecord> edges;
    // 外接口的未修改邻面从旧边记录继承，新面只替换局部关联
    const auto edge=[&](const Edge& key)->EdgeRecord& {
        auto it=edges.find(key);
        if (it!=edges.end()) return it->second;
        const auto old=state._edges.find(key);
        return edges.emplace(key,old==state._edges.end() ? EdgeRecord{} : old->second).first->second;
    };
    for (auto slot : removed)
    {
        const auto& f=state.Face(slot).Vertices;
        for (std::size_t i=0;i<3;++i)
        {
            auto& incident=vertex(f[i]).Incident;
            incident.erase(std::remove(incident.begin(),incident.end(),slot),incident.end());
            auto& e=edge(EdgeKey(f[i],f[(i+1)%3]));
            const auto found=std::find(e.Faces.begin(),e.Faces.begin()+static_cast<std::ptrdiff_t>(e.Count),slot);
            if (found==e.Faces.begin()+static_cast<std::ptrdiff_t>(e.Count)) throw std::runtime_error("旧边邻接缺失");
            *found=e.Faces[--e.Count];e.Faces[e.Count]=InvalidSlot;
        }
    }
    std::vector<std::pair<Slot,FaceRecord>> faces;faces.reserve(needed);
    Identity nextFace=state._nextFaceId,nextVertex=state._nextVertexId;
    for (std::size_t j=0;j<needed;++j)
    {
        const auto& f=newFaces[j];const auto slot=faceSlots[j];
        faces.push_back({slot,{{nextFace--,f},InvalidSlot}});
        for (std::size_t i=0;i<3;++i)
        {
            if (deletedVertices.contains(f[i])) throw std::runtime_error("目标面引用删除点");
            vertex(f[i]).Incident.push_back(slot);
            auto& e=edge(EdgeKey(f[i],f[(i+1)%3]));
            if (e.Count==2) throw std::runtime_error("局部发布产生非流形边");
            e.Faces[e.Count++]=slot;
        }
    }
    for (const auto& [id,p] : geometry)
    {
        vertex(id).Geometry=p;nextVertex=std::min(nextVertex,id-1);
    }
    for (auto id : deletedVertices)
    {
        // 若支持外还引用中心，说明事务证书遗漏了邻域，必须在发布前拒绝
        if (!vertex(id).Incident.empty()) throw std::runtime_error("删除点仍被支持外引用");
        vertices.erase(id);
    }
    for (auto& [id,v] : vertices)
    {
        static_cast<void>(id);std::sort(v.Incident.begin(),v.Incident.end());
        if (v.Incident.empty() || std::adjacent_find(v.Incident.begin(),v.Incident.end())!=v.Incident.end())
            throw std::runtime_error("局部顶点邻接非法");
    }
    work.PreparedFaces+=faces.size();work.PreparedVertices+=vertices.size();work.PreparedEdges+=edges.size();

    // 后续 node handle 转移和预留数组写入不分配；容量变化不改变已发布逻辑状态
    state._faces.reserve(state._faces.size()+appendFaces);state._vertices.reserve(state._vertices.size()+appendVertices);
    state._activeFaces.reserve(state.FaceCount()-removed.size()+needed);
    state._freeFaces.reserve(state._freeFaces.size()+removed.size());
    state._freeVertices.reserve(state._freeVertices.size()+deletedVertices.size());
    work.CheckLimit();
    state._faces.resize(state._faces.size()+appendFaces);
    state._vertices.resize(state._vertices.size()+appendVertices);
    work.Seconds["prepare"]+=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
    const auto publication=std::chrono::steady_clock::now();
    for (auto slot : removed)
    {
        // 稠密活动数组仅作尾部搬移，稳定面身份不随物理次序改变
        auto& f=state._faces[slot];const auto last=state._activeFaces.back();
        state._activeFaces[f.ActivePosition]=last;state._faces[last].ActivePosition=f.ActivePosition;
        state._activeFaces.pop_back();f.ActivePosition=InvalidSlot;
    }
    state._freeFaces.resize(state._freeFaces.size()-usedFreeFaces);
    for (std::size_t i=needed;i<faceSlots.size();++i) state._freeFaces.push_back(faceSlots[i]);
    for (const auto& [slot,prepared] : faces)
    {
        state._faces[slot]=prepared;state._faces[slot].ActivePosition=static_cast<Slot>(state._activeFaces.size());
        state._activeFaces.push_back(slot);
    }
    for (auto id : deletedVertices)
    {
        const auto slot=state._vertexIndex.at(id);state._vertices[slot].Active=false;state._vertices[slot].Incident.clear();
        state._vertexIndex.erase(id);
    }
    state._freeVertices.resize(state._freeVertices.size()-usedFreeVertices);
    for (std::size_t i=assignedVertices;i<vertexSlots.size();++i) state._freeVertices.push_back(vertexSlots[i]);
    while (!addedIndex.empty()) state._vertexIndex.insert(addedIndex.extract(addedIndex.begin()));
    // 预分配的树节点转移不分配新内存，旧记录销毁也不触发用户回调
    for (auto& [id,record] : vertices) state._vertices[state._vertexIndex.at(id)]=std::move(record);
    while (!edges.empty())
    {
        auto node=edges.extract(edges.begin());state._edges.erase(node.key());
        if (node.mapped().Count) state._edges.insert(std::move(node));
    }
    state._nextFaceId=nextFace;state._nextVertexId=nextVertex;++state._version;
    // 只推进一次代际，任何基于旧状态的后续批次都必须重新规划
    work.Seconds["publish"]+=std::chrono::duration<double>(std::chrono::steady_clock::now()-publication).count();
}
}
