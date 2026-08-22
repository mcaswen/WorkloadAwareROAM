#include "algorithms/classic_roam/ClassicRoamMeshBuilder.h"

#include "algorithms/RoamGeometry.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace ParallelRoam::Algorithms::ClassicRoam
{
namespace
{
constexpr std::size_t VerticesPerTriangle = 3U;
constexpr std::size_t InvalidMeshSlot = std::numeric_limits<std::size_t>::max();
}

/// <summary>
/// 开始记录本次网格更新；拓扑重置时标记为需要重新建立根槽位
/// </summary>
void ClassicRoamMeshBuilder::BeginIncrementalMeshUpdate(bool resetTopology)
{
    ++_meshGeneration;
    _meshRequiresFullUpload = false;
    _dirtyMeshSlots.clear();
    _meshUpdateRanges.clear();
    _meshTopologyEdits.clear();

    if (resetTopology)
    {
        _debugTransitionLeaves.clear();
        _meshNeedsInitialization = true;
    }
}

/// <summary>
/// 清空跨帧保留的网格及槽位映射，使下一次更新从两个根节点重新生成连续网格
/// </summary>
void ClassicRoamMeshBuilder::ResetIncrementalMeshStorage()
{
    _meshData = {};
    _meshSlotOwners.clear();
    _meshSlotDirtyGeneration.clear();
    _dirtyMeshSlots.clear();
    _meshUpdateRanges.clear();
    _debugTransitionLeaves.clear();
    _meshTopologyEdits.clear();
    _meshRequiresFullUpload = true;
    _meshNeedsInitialization = true;
}

/// <summary>
/// 为两个根叶节点建立初始槽位，并按三角形预算预留容量以减少后续扩容
/// </summary>
void ClassicRoamMeshBuilder::InitializeIncrementalMesh()
{
    _meshData.GridWidth = _heightMap != nullptr ? _heightMap->Width() : 0;
    _meshData.GridHeight = _heightMap != nullptr ? _heightMap->Height() : 0;
    _meshData.TerrainSize = _terrainSize;
    _meshData.HeightScale = _heightScale;
    _meshSlotOwners.reserve(_settings.TriangleBudget);
    _meshSlotDirtyGeneration.reserve(_settings.TriangleBudget);
    _meshData.Vertices.reserve(_settings.TriangleBudget * VerticesPerTriangle);
    _meshData.Indices.reserve(_settings.TriangleBudget * VerticesPerTriangle);
    AppendMeshLeaf(_rootA);
    AppendMeshLeaf(_rootB);
    _meshRequiresFullUpload = true;
    _meshNeedsInitialization = false;
}

/// <summary>
/// 记录已经成功提交的细分，后续按相同顺序更新网格
/// </summary>
void ClassicRoamMeshBuilder::RecordMeshSplit(ClassicRoamNode* parent)
{
    _meshTopologyEdits.push_back(MeshTopologyEdit{MeshTopologyEditType::Split, parent});
}

/// <summary>
/// 记录已经成功提交的合并，等待拓扑稳定后再更新网格
/// </summary>
void ClassicRoamMeshBuilder::RecordMeshMerge(ClassicRoamNode* parent)
{
    _meshTopologyEdits.push_back(MeshTopologyEdit{MeshTopologyEditType::Merge, parent});
}

/// <summary>
/// 先恢复上一帧的临时调试颜色，再按提交顺序把本次拓扑变化写入网格
/// </summary>
void ClassicRoamMeshBuilder::ApplyIncrementalMeshUpdates()
{
    if (_meshNeedsInitialization)
    {
        InitializeIncrementalMesh();
    }

    // 用于标出本轮重建节点的颜色只显示一次更新，之后只刷新仍为活动叶节点的槽位
    for (ClassicRoamNode* node : _debugTransitionLeaves)
    {
        if (node != nullptr && node->Active && IsLeaf(node) &&
            node->MeshSlot < _meshSlotOwners.size() && _meshSlotOwners[node->MeshSlot] == node)
        {
            RefreshMeshLeafDebugAttributes(*node);
        }
    }
    _debugTransitionLeaves.clear();

    // 严格保持拓扑修改顺序，才能正确处理同一次更新内由深到浅的连续合并
    for (const MeshTopologyEdit& edit : _meshTopologyEdits)
    {
        if (edit.Type == MeshTopologyEditType::Split)
        {
            ReplaceMeshLeafWithChildren(edit.Parent);
        }
        else
        {
            ReplaceMeshChildrenWithLeaf(edit.Parent);
        }
    }
    _meshTopologyEdits.clear();

    if (_settings.PassPolicy.MeshEmit == TerrainLodMeshEmitAction::SerialFull)
    {
        // 全量对照沿用现有槽位所有者，不重新收集活动叶或改变槽位归属
        // 每个槽位都由主线程重写，并生成覆盖完整网格的单一区间
        for (std::size_t slot = 0U; slot < _meshSlotOwners.size(); ++slot)
        {
            ClassicRoamNode* node = _meshSlotOwners[slot];
            if (node != nullptr && node->Active && IsLeaf(node))
            {
                WriteMeshLeaf(slot, *node);
            }
        }
        _meshRequiresFullUpload = true;
    }
}

/// <summary>
/// 在稠密数组末尾追加一个叶节点槽位，并写入对应顶点和索引
/// </summary>
void ClassicRoamMeshBuilder::AppendMeshLeaf(ClassicRoamNode* node)
{
    if (node == nullptr || node->MeshSlot != InvalidMeshSlot)
    {
        return;
    }

    const std::size_t slot = _meshSlotOwners.size();
    node->MeshSlot = slot;
    _meshSlotOwners.push_back(node);
    _meshSlotDirtyGeneration.resize(_meshSlotOwners.size(), 0U);
    _meshData.Vertices.resize(_meshSlotOwners.size() * VerticesPerTriangle);
    _meshData.Indices.resize(_meshSlotOwners.size() * VerticesPerTriangle);
    WriteMeshLeaf(slot, *node);
}

/// <summary>
/// 删除叶节点槽位；删除中间槽位时用末尾元素填洞，保持绘制范围连续
/// </summary>
void ClassicRoamMeshBuilder::RemoveMeshLeaf(ClassicRoamNode* node)
{
    if (node == nullptr || node->MeshSlot >= _meshSlotOwners.size())
    {
        return;
    }

    const std::size_t removedSlot = node->MeshSlot;
    const std::size_t lastSlot = _meshSlotOwners.size() - 1U;
    if (removedSlot != lastSlot)
    {
        ClassicRoamNode* movedNode = _meshSlotOwners[lastSlot];
        const std::size_t destinationBase = removedSlot * VerticesPerTriangle;
        const std::size_t sourceBase = lastSlot * VerticesPerTriangle;
        for (std::size_t index = 0; index < VerticesPerTriangle; ++index)
        {
            _meshData.Vertices[destinationBase + index] = _meshData.Vertices[sourceBase + index];
            const std::uint32_t relativeIndex =
                _meshData.Indices[sourceBase + index] - static_cast<std::uint32_t>(sourceBase);
            _meshData.Indices[destinationBase + index] =
                static_cast<std::uint32_t>(destinationBase) + relativeIndex;
        }
        _meshSlotOwners[removedSlot] = movedNode;
        movedNode->MeshSlot = removedSlot;
        MarkMeshSlotDirty(removedSlot);
    }

    node->MeshSlot = InvalidMeshSlot;
    _meshSlotOwners.pop_back();
    _meshSlotDirtyGeneration.resize(_meshSlotOwners.size());
    _meshData.Vertices.resize(_meshSlotOwners.size() * VerticesPerTriangle);
    _meshData.Indices.resize(_meshSlotOwners.size() * VerticesPerTriangle);
}

/// <summary>
/// 细分时让左子节点复用父节点槽位，并把右子节点追加到数组末尾
/// </summary>
void ClassicRoamMeshBuilder::ReplaceMeshLeafWithChildren(ClassicRoamNode* parent)
{
    if (parent == nullptr || parent->LeftChild == nullptr || parent->RightChild == nullptr ||
        parent->MeshSlot >= _meshSlotOwners.size())
    {
        return;
    }

    const std::size_t parentSlot = parent->MeshSlot;
    ClassicRoamNode* leftChild = parent->LeftChild;
    ClassicRoamNode* rightChild = parent->RightChild;
    parent->MeshSlot = InvalidMeshSlot;
    leftChild->MeshSlot = parentSlot;
    _meshSlotOwners[parentSlot] = leftChild;
    WriteMeshLeaf(parentSlot, *leftChild);
    AppendMeshLeaf(rightChild);
}

/// <summary>
/// 合并时用一个子节点槽位恢复父节点，并删除另一个子节点槽位
/// </summary>
void ClassicRoamMeshBuilder::ReplaceMeshChildrenWithLeaf(ClassicRoamNode* parent)
{
    if (parent == nullptr || parent->LeftChild == nullptr || parent->RightChild == nullptr ||
        parent->LeftChild->MeshSlot >= _meshSlotOwners.size() ||
        parent->RightChild->MeshSlot >= _meshSlotOwners.size())
    {
        return;
    }

    ClassicRoamNode* retainedChild = parent->LeftChild;
    ClassicRoamNode* removedChild = parent->RightChild;
    const std::size_t lastSlot = _meshSlotOwners.size() - 1U;
    if (retainedChild->MeshSlot == lastSlot)
    {
        // RemoveMeshLeaf 会用末尾槽位填补空洞，因此保留非末尾子节点可避免覆盖新父节点
        std::swap(retainedChild, removedChild);
    }

    const std::size_t parentSlot = retainedChild->MeshSlot;
    retainedChild->MeshSlot = InvalidMeshSlot;
    parent->MeshSlot = parentSlot;
    _meshSlotOwners[parentSlot] = parent;
    WriteMeshLeaf(parentSlot, *parent);
    RemoveMeshLeaf(removedChild);
}

/// <summary>
/// 重新生成单个叶节点槽位的顶点属性和局部索引，并保证法线朝向正 Y
/// </summary>
void ClassicRoamMeshBuilder::WriteMeshLeaf(std::size_t slot, const ClassicRoamNode& node)
{
    const std::size_t baseIndex = slot * VerticesPerTriangle;
    if (baseIndex + VerticesPerTriangle > _meshData.Vertices.size())
    {
        return;
    }

    const TriangleDomain& domain = node.Domain;
    const std::array<glm::vec2, VerticesPerTriangle> uvs{domain.A, domain.B, domain.C};
    const glm::vec3 debugColor = DebugColorForLeaf(node);
    const float debugHighlight = DebugHighlightForLeaf(node);
    for (std::size_t index = 0; index < VerticesPerTriangle; ++index)
    {
        const glm::vec2 uv = uvs[index];
        Terrain::TerrainMeshVertex& vertex = _meshData.Vertices[baseIndex + index];
        const Roam::TerrainWorldSample terrainSample =
            Roam::SampleTerrainWorld(*_heightMap, uv, _terrainSize, _heightScale);
        vertex.Position = terrainSample.Position;
        vertex.Normal = Roam::SampleHeightGradientNormal(*_heightMap, uv, _terrainSize, _heightScale);
        vertex.TexCoord = uv;
        vertex.Height = terrainSample.Height;
        vertex.DebugColor = debugColor;
        vertex.DebugHighlight = debugHighlight;
    }

    const glm::vec3 edge0 =
        _meshData.Vertices[baseIndex + 1U].Position - _meshData.Vertices[baseIndex].Position;
    const glm::vec3 edge1 =
        _meshData.Vertices[baseIndex + 2U].Position - _meshData.Vertices[baseIndex].Position;
    const bool pointsTowardPositiveY = glm::cross(edge0, edge1).y >= 0.0F;
    _meshData.Indices[baseIndex] = static_cast<std::uint32_t>(baseIndex);
    _meshData.Indices[baseIndex + 1U] = static_cast<std::uint32_t>(
        baseIndex + (pointsTowardPositiveY ? 1U : 2U));
    _meshData.Indices[baseIndex + 2U] = static_cast<std::uint32_t>(
        baseIndex + (pointsTowardPositiveY ? 2U : 1U));
    MarkMeshSlotDirty(slot);
}

/// <summary>
/// 拓扑不变时只更新上一轮留下的重建标记颜色，避免重复采样几何数据
/// </summary>
void ClassicRoamMeshBuilder::RefreshMeshLeafDebugAttributes(ClassicRoamNode& node)
{
    const std::size_t baseIndex = node.MeshSlot * VerticesPerTriangle;
    if (node.MeshSlot >= _meshSlotOwners.size() ||
        baseIndex + VerticesPerTriangle > _meshData.Vertices.size())
    {
        return;
    }

    const glm::vec3 debugColor = DebugColorForLeaf(node);
    const float debugHighlight = DebugHighlightForLeaf(node);
    for (std::size_t index = 0; index < VerticesPerTriangle; ++index)
    {
        _meshData.Vertices[baseIndex + index].DebugColor = debugColor;
        _meshData.Vertices[baseIndex + index].DebugHighlight = debugHighlight;
    }
    MarkMeshSlotDirty(node.MeshSlot);
}

/// <summary>
/// 用网格版本号对待更新槽位去重，使同一次更新内多次修改的槽位只上传一次
/// </summary>
void ClassicRoamMeshBuilder::MarkMeshSlotDirty(std::size_t slot)
{
    if (slot >= _meshSlotDirtyGeneration.size())
    {
        return;
    }
    if (_meshSlotDirtyGeneration[slot] != _meshGeneration)
    {
        _meshSlotDirtyGeneration[slot] = _meshGeneration;
        _dirtyMeshSlots.push_back(slot);
    }
}

/// <summary>
/// 移除已经无效的待更新槽位、合并连续区间，并生成本次增量上传范围和统计
/// </summary>
void ClassicRoamMeshBuilder::FinalizeIncrementalMeshUpdate()
{
    std::sort(_dirtyMeshSlots.begin(), _dirtyMeshSlots.end());
    _dirtyMeshSlots.erase(
        std::remove_if(
            _dirtyMeshSlots.begin(),
            _dirtyMeshSlots.end(),
            [this](std::size_t slot) { return slot >= _meshSlotOwners.size(); }),
        _dirtyMeshSlots.end());
    _dirtyMeshSlots.erase(std::unique(_dirtyMeshSlots.begin(), _dirtyMeshSlots.end()), _dirtyMeshSlots.end());

    _meshUpdateRanges.clear();
    if (_meshRequiresFullUpload && !_meshSlotOwners.empty())
    {
        _meshUpdateRanges.push_back(ClassicRoamMeshUpdateRange{0U, _meshSlotOwners.size()});
    }
    else
    {
        for (std::size_t slot : _dirtyMeshSlots)
        {
            if (_meshUpdateRanges.empty() ||
                _meshUpdateRanges.back().FirstTriangle + _meshUpdateRanges.back().TriangleCount != slot)
            {
                _meshUpdateRanges.push_back(ClassicRoamMeshUpdateRange{slot, 1U});
            }
            else
            {
                ++_meshUpdateRanges.back().TriangleCount;
            }
        }
    }

    const std::size_t updatedTriangleCount = _meshRequiresFullUpload
        ? _meshSlotOwners.size()
        : _dirtyMeshSlots.size();
    _stats.MeshFullRebuildCount = _meshRequiresFullUpload ? 1U : 0U;
    _stats.MeshUpdatedTriangleCount = updatedTriangleCount;
    _stats.MeshReusedTriangleCount = _meshSlotOwners.size() > updatedTriangleCount
        ? _meshSlotOwners.size() - updatedTriangleCount
        : 0U;
    _stats.MeshDirtyRangeCount = _meshUpdateRanges.size();

    _debugTransitionLeaves.clear();
    const auto appendTransition = [this](ClassicRoamNode* node) {
        if (node != nullptr &&
            (node->ActivatedBuildId == _buildSequence || node->MergeBuildId == _buildSequence))
        {
            _debugTransitionLeaves.push_back(node);
        }
    };
    if (_meshRequiresFullUpload)
    {
        for (ClassicRoamNode* node : _meshSlotOwners)
        {
            appendTransition(node);
        }
    }
    else
    {
        for (std::size_t slot : _dirtyMeshSlots)
        {
            appendTransition(_meshSlotOwners[slot]);
        }
    }
}
} // 命名空间 ParallelRoam::Algorithms::ClassicRoam
