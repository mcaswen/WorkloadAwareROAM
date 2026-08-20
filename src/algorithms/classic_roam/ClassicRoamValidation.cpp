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
    // DomainEdge 用 UV 空间表达 leaf 边界
    glm::vec2 Start{0.0F};
    glm::vec2 End{0.0F};
};

struct QuantizedPoint
{
    // 量化点把二分 UV 映射到整数网格，减少浮点比较噪声
    long long X{0};
    long long Y{0};
};

struct QuantizedLineKey
{
    // Direction 描述归一化直线方向
    // Constant 描述直线相对原点的位置
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
        // hash 只服务 validator 的 unordered_map 桶分布
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
    // Line 负责把共线边归组
    // 参数区间负责判断端点是否落入某条粗边内部
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
    // UV 细分会产生浮点中点，需要容差判断端点重合
    constexpr float Epsilon = 0.000001F;
    return DistanceSquared(a, b) <= Epsilon * Epsilon;
}

std::array<DomainEdge, 3> DomainEdges(const TriangleDomain& domain)
{
    // edge 顺序与 base/right/left neighbor 字段保持一致
    return {
        DomainEdge{domain.A, domain.B},
        DomainEdge{domain.B, domain.C},
        DomainEdge{domain.C, domain.A},
    };
}

bool SameUndirectedEdge(const DomainEdge& left, const DomainEdge& right)
{
    // leaf neighbor 重建只匹配完整共享边，不把 T-junction 当作合法邻接
    // 两个方向都要匹配，因为相邻三角形通常以反向顺序保存共享边
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

    // 同一条无向直线必须得到唯一方向
    // 否则相邻三角形的反向边会落入不同桶
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
    // 投影到归一化方向后，二维边界检测可以降成一维区间查找
    return line.DirectionX * point.X + line.DirectionY * point.Y;
}
} // 匿名命名空间

void ClassicRoamMeshBuilder::ValidateTopology()
{
    // validator 使用量化边线索引检查裂缝，避免 leaf 之间两两扫描
    // 这里故意独立于 neighbor 指针做几何裂缝检测
    // 可以同时发现 neighbor 链路正确但 leaf 尺度不一致的问题
    std::vector<ClassicRoamNode*> leafNodes;
    CollectLeafNodes(leafNodes);
    std::unordered_set<const ClassicRoamNode*> leafSet;
    leafSet.reserve(leafNodes.size());

    for (ClassicRoamNode* node : leafNodes)
    {
        // leafSet 用于判断 neighbor 是否仍然指向 active leaf
        leafSet.insert(node);
    }

    const auto validateNeighbor = [&leafSet](const ClassicRoamNode* owner, const ClassicRoamNode* neighbor, const DomainEdge& edge) {
        // 边界边允许为空，非空 neighbor 必须是 active leaf
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
                // 共享边成立后还要检查对侧是否能反向找到 owner
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
    // 每个 leaf 恰好贡献三条边，预分配可避免 validator 抖动
    quantizedEdges.reserve(leafNodes.size() * 3U);

    for (ClassicRoamNode* node : leafNodes)
    {
        const std::array<DomainEdge, 3> edges = DomainEdges(node->Domain);

        for (const DomainEdge& edge : edges)
        {
            // 每条边都映射到量化直线
            // 同线端点集合用于检测粗边内部是否存在细边端点
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

            // 同一直线上的端点参数可用于快速发现粗边内部是否被其他 leaf 顶点切开
            std::vector<long long>& vertexParameters = lineVertices[line];
            vertexParameters.push_back(startParameter);
            vertexParameters.push_back(endParameter);
        }
    }

    for (auto& [line, vertexParameters] : lineVertices)
    {
        (void)line;
        // 同一 leaf 可能贡献重复端点
        // 去重后 interior 检测才不会把端点重合当裂缝
        std::sort(vertexParameters.begin(), vertexParameters.end());
        vertexParameters.erase(std::unique(vertexParameters.begin(), vertexParameters.end()), vertexParameters.end());
    }

    for (const QuantizedEdge& edge : quantizedEdges)
    {
        const auto lineIt = lineVertices.find(edge.Line);
        if (lineIt == lineVertices.end())
        {
            // 正常情况下不会缺线，防御损坏的量化输入
            continue;
        }

        const std::vector<long long>& vertexParameters = lineIt->second;
        const auto interiorIt = std::upper_bound(vertexParameters.begin(), vertexParameters.end(), edge.MinParameter);
        if (interiorIt != vertexParameters.end() && *interiorIt < edge.MaxParameter)
        {
            // validator 只记录 T-junction，不主动 split 修复
            // 修复仍由 split 约束传播负责
            ++_stats.TjunctionCount;
            ++_stats.CrackRiskCount;
        }
    }

    for (ClassicRoamNode* node : leafNodes)
    {
        const std::array<DomainEdge, 3> edges = DomainEdges(node->Domain);

        // 只验证非空 neighbor，边界边允许为空
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
        // 根 diamond 互指是所有后续 diamond 约束的基础
        ++_stats.InvalidTopologyCount;
    }

    // 这里遍历整个持久化池
    // inactive child 也要保持 parent 和 child 指针自洽
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
            // split flag 和 child 指针必须一起成立
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
    // Q_s 必须和 active cut 一一对应，不能包含 inactive 历史 child
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

    // 从 active topology 独立推导 canonical diamonds，验证 Q_m 的局部维护没有漏项或重复项
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
