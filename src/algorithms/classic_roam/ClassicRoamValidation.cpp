#include "algorithms/classic_roam/ClassicRoamMeshBuilder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <memory>
#include <numeric>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ParallelRoam::Algorithms::ClassicRoam
{
namespace
{
struct DomainEdge
{
    // DomainEdge 用 UV 坐标表示叶三角形的一条边
    glm::vec2 Start{0.0F};
    glm::vec2 End{0.0F};
};

struct QuantizedPoint
{
    // 将二分产生的 UV 坐标量化到整数网格，避免浮点误差破坏端点匹配
    long long X{0};
    long long Y{0};
};

struct QuantizedLineKey
{
    // Direction 保存统一朝向后的直线方向
    // Constant 保存直线相对原点的有符号偏移
    long long DirectionX{0};
    long long DirectionY{0};
    long long Constant{0};

    bool operator==(const QuantizedLineKey& other) const
    {
        return DirectionX == other.DirectionX &&
               DirectionY == other.DirectionY &&
               Constant == other.Constant;
    }
};

struct QuantizedLineKeyHash
{
    std::size_t operator()(const QuantizedLineKey& key) const
    {
        // 哈希只用于验证器内部无序表的分组，不参与拓扑判定
        std::size_t seed = 1469598103934665603ULL;
        const auto mix = [&seed](long long value) {
            const std::size_t hashedValue = std::hash<long long>{}(value);
            seed ^= hashedValue + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
        };
        mix(key.DirectionX);
        mix(key.DirectionY);
        mix(key.Constant);
        return seed;
    }
};

struct QuantizedEdge
{
    // Line 将共线边分到同一组
    // 参数区间用于判断细边端点是否落在较粗边的内部
    QuantizedLineKey Line;
    long long MinParameter{0};
    long long MaxParameter{0};
};

float DistanceSquared(const glm::vec2& a, const glm::vec2& b)
{
    const glm::vec2 delta = b - a;
    return glm::dot(delta, delta);
}

bool SamePoint(const glm::vec2& a, const glm::vec2& b)
{
    // UV 细分产生的中点可能有浮点误差，因此用容差判断端点是否重合
    constexpr float Epsilon = 0.000001F;
    return DistanceSquared(a, b) <= Epsilon * Epsilon;
}

std::array<DomainEdge, 3> DomainEdges(const TriangleDomain& domain)
{
    // 边的顺序与底边、右边和左边邻居字段保持一致
    return {
        DomainEdge{domain.A, domain.B},
        DomainEdge{domain.B, domain.C},
        DomainEdge{domain.C, domain.A},
    };
}

bool SameUndirectedEdge(const DomainEdge& left, const DomainEdge& right)
{
    // 重建叶节点邻接时只接受整条共享边，不把 T 形接缝误认为合法邻接
    // 相邻三角形通常反向保存共享边，因此两个方向都需要匹配
    return (SamePoint(left.Start, right.Start) && SamePoint(left.End, right.End)) ||
           (SamePoint(left.Start, right.End) && SamePoint(left.End, right.Start));
}

long long AbsoluteGcd(long long a, long long b)
{
    return std::gcd(std::llabs(a), std::llabs(b));
}

QuantizedPoint QuantizePoint(const glm::vec2& point, int maxDepth)
{
    const auto scale = static_cast<long long>(1ULL << static_cast<unsigned int>(std::clamp(maxDepth, 0, 30)));
    return QuantizedPoint{
        static_cast<long long>(std::llround(static_cast<double>(point.x) * static_cast<double>(scale))),
        static_cast<long long>(std::llround(static_cast<double>(point.y) * static_cast<double>(scale))),
    };
}

QuantizedLineKey MakeLineKey(const QuantizedPoint& start, const QuantizedPoint& end)
{
    long long directionX = end.X - start.X;
    long long directionY = end.Y - start.Y;
    const long long divisor = std::max(AbsoluteGcd(directionX, directionY), 1LL);
    directionX /= divisor;
    directionY /= divisor;

    // 同一条无向直线必须统一为同一个方向，否则反向边会被分到不同组
    if (directionX < 0 || (directionX == 0 && directionY < 0))
    {
        directionX = -directionX;
        directionY = -directionY;
    }

    return QuantizedLineKey{
        directionX,
        directionY,
        directionY * start.X - directionX * start.Y,
    };
}

long long ProjectToLineParameter(const QuantizedPoint& point, const QuantizedLineKey& line)
{
    // 投影到统一后的直线方向，可以把二维共线边检测简化为一维区间查找
    return line.DirectionX * point.X + line.DirectionY * point.Y;
}
} // 匿名命名空间

void ClassicRoamMeshBuilder::ValidateTopology()
{
    // 验证器用量化直线索引检查裂缝，避免在所有叶三角形之间两两比较
    // 几何裂缝检测不依赖邻居指针，因此也能发现指针互相匹配但边尺度不一致的问题
    std::vector<ClassicRoamNode*> leafNodes;
    CollectLeafNodes(leafNodes);
    std::unordered_set<const ClassicRoamNode*> leafSet;
    leafSet.reserve(leafNodes.size());

    for (ClassicRoamNode* node : leafNodes)
    {
        // leafSet 用于确认邻居仍然属于当前活动叶集合
        leafSet.insert(node);
    }

    const auto validateNeighbor = [&leafSet](const ClassicRoamNode* owner, const ClassicRoamNode* neighbor, const DomainEdge& edge) {
        // 地形外边界可以没有邻居，其余非空邻居必须是活动叶节点
        if (neighbor == nullptr)
        {
            return false;
        }

        if (leafSet.find(neighbor) == leafSet.end())
        {
            return false;
        }

        for (const DomainEdge& neighborEdge : DomainEdges(neighbor->Domain))
        {
            if (SameUndirectedEdge(edge, neighborEdge))
            {
                // 确认共享边后还要检查对侧能否反向找到当前叶节点
                return neighbor->BaseNeighbor == owner ||
                       neighbor->LeftNeighbor == owner ||
                       neighbor->RightNeighbor == owner;
            }
        }

        return false;
    };

    std::unordered_map<QuantizedLineKey, std::vector<long long>, QuantizedLineKeyHash> lineVertices;
    lineVertices.reserve(leafNodes.size() * 3U);
    std::vector<QuantizedEdge> quantizedEdges;
    // 每个叶三角形固定贡献三条边，提前预留空间可减少验证过程中的扩容
    quantizedEdges.reserve(leafNodes.size() * 3U);

    for (ClassicRoamNode* node : leafNodes)
    {
        const std::array<DomainEdge, 3> edges = DomainEdges(node->Domain);

        for (const DomainEdge& edge : edges)
        {
            // 每条边都映射到一条量化直线
            // 同线端点集合用于查找较粗边内部是否出现额外细分端点
            const QuantizedPoint start = QuantizePoint(edge.Start, _settings.MaxDepth);
            const QuantizedPoint end = QuantizePoint(edge.End, _settings.MaxDepth);
            const QuantizedLineKey line = MakeLineKey(start, end);
            const long long startParameter = ProjectToLineParameter(start, line);
            const long long endParameter = ProjectToLineParameter(end, line);
            QuantizedEdge quantizedEdge{};
            quantizedEdge.Line = line;
            quantizedEdge.MinParameter = std::min(startParameter, endParameter);
            quantizedEdge.MaxParameter = std::max(startParameter, endParameter);
            quantizedEdges.push_back(quantizedEdge);

            // 同一直线上的端点参数可以快速判断较粗边是否被其他叶节点顶点切开
            std::vector<long long>& vertexParameters = lineVertices[line];
            vertexParameters.push_back(startParameter);
            vertexParameters.push_back(endParameter);
        }
    }

    for (auto& [line, vertexParameters] : lineVertices)
    {
        (void)line;
        // 同一叶三角形可能贡献重复端点，去重后才不会把正常端点重合误判为裂缝
        std::sort(vertexParameters.begin(), vertexParameters.end());
        vertexParameters.erase(std::unique(vertexParameters.begin(), vertexParameters.end()), vertexParameters.end());
    }

    for (const QuantizedEdge& edge : quantizedEdges)
    {
        const auto lineIt = lineVertices.find(edge.Line);
        if (lineIt == lineVertices.end())
        {
            // 正常情况下直线索引必然存在，此处防御异常量化结果
            continue;
        }

        const std::vector<long long>& vertexParameters = lineIt->second;
        const auto interiorIt = std::upper_bound(vertexParameters.begin(), vertexParameters.end(), edge.MinParameter);
        if (interiorIt != vertexParameters.end() && *interiorIt < edge.MaxParameter)
        {
            // 验证器只记录 T 形接缝，不主动修改拓扑
            // 裂缝修复仍由细分约束传播负责
            ++_stats.TjunctionCount;
            ++_stats.CrackRiskCount;
        }
    }

    for (ClassicRoamNode* node : leafNodes)
    {
        const std::array<DomainEdge, 3> edges = DomainEdges(node->Domain);

        // 只检查存在的邻居，地形外边界允许为空
        if (node->BaseNeighbor != nullptr && !validateNeighbor(node, node->BaseNeighbor, edges[0]))
        {
            ++_stats.InvalidNeighborCount;
        }

        if (node->RightNeighbor != nullptr && !validateNeighbor(node, node->RightNeighbor, edges[1]))
        {
            ++_stats.InvalidNeighborCount;
        }

        if (node->LeftNeighbor != nullptr && !validateNeighbor(node, node->LeftNeighbor, edges[2]))
        {
            ++_stats.InvalidNeighborCount;
        }
    }

    if (_rootA == nullptr || _rootB == nullptr || _rootA->BaseNeighbor != _rootB || _rootB->BaseNeighbor != _rootA)
    {
        // 根菱形的底边互指是后续所有菱形约束的起点
        ++_stats.InvalidTopologyCount;
    }

    // 此处遍历整个跨帧保留的节点池，停用的历史子节点也必须保持父子指针一致
    for (const std::unique_ptr<ClassicRoamNode>& ownedNode : _nodes)
    {
        const ClassicRoamNode* node = ownedNode.get();
        if (node == nullptr)
        {
            ++_stats.InvalidTopologyCount;
            continue;
        }

        if (node->IsSplit && (node->LeftChild == nullptr || node->RightChild == nullptr))
        {
            // 细分标记和两个子节点指针必须同时成立
            ++_stats.InvalidTopologyCount;
        }

        if (node->LeftChild != nullptr && node->LeftChild->Parent != node)
        {
            ++_stats.InvalidTopologyCount;
        }

        if (node->RightChild != nullptr && node->RightChild->Parent != node)
        {
            ++_stats.InvalidTopologyCount;
        }

        if (node != _rootA && node != _rootB && node->Parent == nullptr)
        {
            ++_stats.InvalidTopologyCount;
        }
    }

    ValidatePersistentQueues(leafNodes);
    ValidateIncrementalMesh(leafNodes);
}

void ClassicRoamMeshBuilder::ValidatePersistentQueues(const std::vector<ClassicRoamNode*>& leafNodes)
{
    // Q_s 必须与当前活动叶集合一一对应，不能包含已经停用的历史子节点
    std::unordered_set<const ClassicRoamNode*> splitMembers;
    splitMembers.reserve(_splitQueue.size());
    if (_splitQueue.size() != leafNodes.size() || _splitQueue.size() > _settings.TriangleBudget)
    {
        ++_stats.InvalidTopologyCount;
    }
    for (std::size_t index = 0U; index < _splitQueue.size(); ++index)
    {
        const ClassicRoamNode* node = _splitQueue[index].Node;
        if (node == nullptr || !node->Active || !IsLeaf(node) || node->SplitQueueIndex != index ||
            !splitMembers.insert(node).second)
        {
            ++_stats.InvalidTopologyCount;
        }
        if (index > 0U && SplitEntryPrecedes(_splitQueue[index], _splitQueue[(index - 1U) / 2U]))
        {
            ++_stats.InvalidTopologyCount;
        }
    }
    for (const ClassicRoamNode* leaf : leafNodes)
    {
        if (!leaf->Active || splitMembers.find(leaf) == splitMembers.end())
        {
            ++_stats.InvalidTopologyCount;
        }
    }

    // 从活动拓扑中按固定规则重新找出全部菱形，用于检查 Q_m 是否遗漏或重复
    std::unordered_set<const ClassicRoamNode*> expectedMergeMembers;
    for (const std::unique_ptr<ClassicRoamNode>& ownedNode : _nodes)
    {
        ClassicRoamNode* representative = CanonicalMergeQueueNode(ownedNode.get());
        if (representative != nullptr)
        {
            expectedMergeMembers.insert(representative);
        }
    }
    if (expectedMergeMembers.size() != _mergeQueue.size())
    {
        ++_stats.InvalidTopologyCount;
    }
    for (std::size_t index = 0U; index < _mergeQueue.size(); ++index)
    {
        const ClassicRoamNode* representative = _mergeQueue[index].Node;
        if (representative == nullptr || representative->MergeQueueIndex != index ||
            representative->MergeQueueRepresentative != representative ||
            expectedMergeMembers.find(representative) == expectedMergeMembers.end())
        {
            ++_stats.InvalidTopologyCount;
        }
        if (representative != nullptr && representative->MergeQueuePartner != nullptr &&
            representative->MergeQueuePartner->MergeQueueRepresentative != representative)
        {
            ++_stats.InvalidTopologyCount;
        }
        if (index > 0U && MergeEntryPrecedes(_mergeQueue[index], _mergeQueue[(index - 1U) / 2U]))
        {
            ++_stats.InvalidTopologyCount;
        }
    }
}

void ClassicRoamMeshBuilder::ValidateIncrementalMesh(const std::vector<ClassicRoamNode*>& leafNodes)
{
    constexpr std::size_t elementsPerTriangle = 3U;
    if (_meshSlotOwners.size() != leafNodes.size() ||
        _meshData.Vertices.size() != _meshSlotOwners.size() * elementsPerTriangle ||
        _meshData.Indices.size() != _meshSlotOwners.size() * elementsPerTriangle)
    {
        ++_stats.InvalidTopologyCount;
        return;
    }

    std::unordered_set<const ClassicRoamNode*> leafSet{leafNodes.begin(), leafNodes.end()};
    std::unordered_set<const ClassicRoamNode*> meshOwners;
    meshOwners.reserve(_meshSlotOwners.size());
    for (std::size_t slot = 0U; slot < _meshSlotOwners.size(); ++slot)
    {
        const ClassicRoamNode* node = _meshSlotOwners[slot];
        if (node == nullptr || !node->Active || !IsLeaf(node) || node->MeshSlot != slot ||
            leafSet.find(node) == leafSet.end() || !meshOwners.insert(node).second)
        {
            ++_stats.InvalidTopologyCount;
            continue;
        }

        const std::size_t baseIndex = slot * elementsPerTriangle;
        for (std::size_t localIndex = 0U; localIndex < elementsPerTriangle; ++localIndex)
        {
            const std::uint32_t index = _meshData.Indices[baseIndex + localIndex];
            if (index < baseIndex || index >= baseIndex + elementsPerTriangle)
            {
                ++_stats.InvalidTopologyCount;
            }
        }
    }
}
} // 命名空间 ParallelRoam::Algorithms::ClassicRoam
