#include "algorithms/greedy_transactional_lod/TransactionalState.h"
#include "algorithms/greedy_transactional_lod/TransactionalPredicates.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ParallelRoam::Algorithms::GreedyTransactionalLod
{
void WorkLedger::CheckLimit() const
{
    if (std::chrono::steady_clock::now() > Deadline || SampleTouches > VisitLimit)
        throw std::runtime_error("认证配额用尽，未发布批次");
}

void WorkLedger::Touch() { ++SampleTouches; CheckLimit(); }

TransactionalState::TransactionalState(const InitialMesh& input) : _config(input.Config)
{
    if (_config.ReceiverOrder != TransactionalReceiverOrder::Composite &&
        _config.ReceiverOrder != TransactionalReceiverOrder::ErrorFirst)
    {
        throw std::invalid_argument("未知接收方排序政策");
    }
    if (_config.EnableFlipRecovery && (!_config.PreserveSurvivingHeights || _config.HeightGuard))
        throw std::runtime_error("翻边恢复需要固定旧点且关闭 HeightGuard");
    if (_config.EnableBoundaryRefinement && (!_config.PreserveSurvivingHeights || _config.HeightGuard))
    {
        throw std::runtime_error("边界细分需要固定旧点且关闭 HeightGuard");
    }
    if (_config.Budget < input.Faces.size() || input.Faces.empty() ||
        !std::isfinite(_config.TerrainSize) || !std::isfinite(_config.HeightScale) ||
        !(_config.TerrainSize > 0) || !(_config.HeightScale > 0) ||
        !std::isfinite(_config.SplitPixels) || _config.SplitPixels<0 || !_config.Width || !_config.Height ||
        !std::all_of(_config.Matrix.begin(),_config.Matrix.end(),[](double x) { return std::isfinite(x); }))
        throw std::runtime_error("初始预算或地形尺度非法");
    for (const auto& [id, point] : input.Vertices)
    {
        if (id<std::numeric_limits<Identity>::min()/2 || id>std::numeric_limits<Identity>::max()/2)
            throw std::runtime_error("顶点身份超出预留范围");
        if (!std::isfinite(point.U) || !std::isfinite(point.V) || !std::isfinite(point.Height) ||
            point.U < 0 || point.U > 1 || point.V < 0 || point.V > 1 ||
            point.Height < -_config.HeightScale || point.Height > 2 * _config.HeightScale)
            throw std::runtime_error("顶点超出声明域");
        const auto slot = static_cast<Slot>(_vertices.size());
        if (!_vertexIndex.emplace(id, slot).second) throw std::runtime_error("顶点身份重复");
        _vertices.push_back({id, point, {}, true});
        _nextVertexId = std::min(_nextVertexId, id - 1);
    }
    auto sorted = input.Faces;
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.Id < b.Id; });
    for (auto face : sorted)
    {
        if (face.Id<std::numeric_limits<Identity>::min()/2 || face.Id>std::numeric_limits<Identity>::max()/2)
            throw std::runtime_error("面身份超出预留范围");
        if (!_faces.empty() && _faces.back().Geometry.Id == face.Id) throw std::runtime_error("面身份重复");
        // 只规范方向，不吸附坐标；后续连接必须使用实际存储值
        if (TransactionalPredicates::Orientation(Vertex(face.Vertices[0]).Geometry,
            Vertex(face.Vertices[1]).Geometry, Vertex(face.Vertices[2]).Geometry) < 0)
            std::swap(face.Vertices[1], face.Vertices[2]);
        const auto slot = static_cast<Slot>(_faces.size());
        _faces.push_back({face, slot});
        _activeFaces.push_back(slot);
        _nextFaceId = std::min(_nextFaceId, face.Id - 1);
        for (std::size_t i = 0; i < 3; ++i)
        {
            _vertices[_vertexIndex.at(face.Vertices[i])].Incident.push_back(slot);
            auto& edge = _edges[EdgeKey(face.Vertices[i], face.Vertices[(i + 1) % 3])];
            if (edge.Count == 2) throw std::runtime_error("非流形边");
            edge.Faces[edge.Count++] = slot;
        }
    }
    // 初建恢复边界事实，相机只读；后续新边界点由有证书的局部提交创建
    for (const auto& [key,edge] : _edges)
        if (edge.Count==1)
            for (auto id : key) _vertices[_vertexIndex.at(id)].Boundary=true;
}

const VertexRecord& TransactionalState::Vertex(Identity id) const { return _vertices.at(_vertexIndex.at(id)); }
const Triangle& TransactionalState::Face(Slot slot) const
{
    const auto& record = _faces.at(slot);
    if (record.ActivePosition == InvalidSlot) throw std::runtime_error("访问非活动面");
    return record.Geometry;
}

bool TransactionalState::IsBoundary(Identity id) const
{
    return Vertex(id).Boundary;
}
}
