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
/// 开始一个 mesh generation，并在 topology reset 时要求重新建立根槽位。
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
/// 丢弃持久 mesh 的有效内容；下一次 apply 会从两个 root 重新建立 dense cut。
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
/// 建立两个根 leaf 的初始槽位，并为硬预算预留稳定容量。
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
/// 记录已成功提交的 split；edit 必须保持与 topology transaction 相同顺序。
/// </summary>
void ClassicRoamMeshBuilder::RecordMeshSplit(ClassicRoamNode* parent)
{
    _meshTopologyEdits.push_back(MeshTopologyEdit{MeshTopologyEditType::Split, parent});
}

/// <summary>
/// 记录已成功提交的 merge，供 topology 稳定后的 mesh replay 使用。
/// </summary>
void ClassicRoamMeshBuilder::RecordMeshMerge(ClassicRoamNode* parent)
{
    _meshTopologyEdits.push_back(MeshTopologyEdit{MeshTopologyEditType::Merge, parent});
}

/// <summary>
/// 先结束上一 Build 的调试色过渡，再按提交顺序重放本次 topology edits。
/// </summary>
void ClassicRoamMeshBuilder::ApplyIncrementalMeshUpdates()
{
    if (_meshNeedsInitialization)
    {
        InitializeIncrementalMesh();
    }

    // Rebuilt 颜色只维持一个 Build；拓扑稳定后只刷新仍是 leaf 的旧成员。
    for (ClassicRoamNode* node : _debugTransitionLeaves)
    {
        if (node != nullptr && node->Active && IsLeaf(node) &&
            node->MeshSlot < _meshSlotOwners.size() && _meshSlotOwners[node->MeshSlot] == node)
        {
            RefreshMeshLeafDebugAttributes(*node);
        }
    }
    _debugTransitionLeaves.clear();

    // edit 顺序与 topology 提交顺序一致，支持同一 Build 内从深层向 parent 级联合并。
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
}

/// <summary>
/// 在 dense arrays 末尾追加一个 leaf，并立即生成其三个顶点和索引。
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
/// 删除 leaf 槽；非末槽通过 move-last compaction 保持 draw range 连续。
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
/// split 时让 left child 继承 parent 槽，right child 追加到数组末尾。
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
/// merge 时用一个 child 槽恢复 parent，并安全删除另一个 child 槽。
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
        // RemoveMeshLeaf 会把末尾槽位压入被删除的空洞
        // 保留非末尾 child，避免压缩过程覆盖新 parent
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
/// 重建单个 leaf 槽的完整顶点属性和局部索引，并统一正 Y 绕序。
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
/// topology 不变时只刷新上一 Build 的 Rebuilt 调试属性，避免重采样几何。
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
/// 用 mesh generation 对 dirty slot 去重，使一个 Build 内的多次 edit 只上传一次。
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
/// 清理无效 dirty slots、合并连续范围，并发布本 Build 的增量输出统计。
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
