#include "algorithms/cpu_cbt/CpuCbtState.h"

#include <cmath>
#include <exception>
#include <utility>

namespace ParallelRoam::Algorithms::CpuCbt
{
bool ValidateCpuCbtSettings(const CpuCbtSettings& settings, std::string& error)
{
    // 首版固定参考支持的容量和深度范围，避免在移位或预算减法后才发现非法输入
    if (!std::isfinite(settings.TerrainSize) || settings.TerrainSize <= 0.0F ||
        !std::isfinite(settings.HeightScale) || settings.HeightScale < 0.0F ||
        !std::isfinite(settings.TriangleAreaPixels) || settings.TriangleAreaPixels <= 0.0F ||
        settings.MaxHeapBitDepth < Cbt2024::CbtBaseDepth || settings.MaxHeapBitDepth > 20U ||
        settings.TriangleBudget < Cbt2024::CbtBaseBisectorCount ||
        settings.TriangleBudget > CpuCbtState::DynamicCapacity + Cbt2024::CbtBaseBisectorCount)
    {
        error = "CPU CBT 配置的尺度、面积、位长或预算越界";
        return false;
    }
    error.clear();
    return true;
}

bool InitializeCpuCbt(CpuCbtState& state, const CpuCbtSettings& settings, std::string& error)
{
    if (!ValidateCpuCbtSettings(settings, error)) return false;
    try
    {
        CpuCbtState next;
        next.Settings = settings;
        const auto base = Cbt2024::BuildSquareCbtBaseTopology(Cbt2024::CbtOccupancyCapacity::Capacity128K);
        next.ControlPoints = base.ControlPoints;
        next.HeapIds.resize(base.Layout.TotalElementCount, 0U);
        next.Neighbors.resize(next.HeapIds.size());
        next.Data.resize(next.HeapIds.size());
        // 空槽元数据也初始化为哨兵，不能让默认零值被误解为可用分配结果
        for (auto& data : next.Data)
        {
            data.Indices.fill(Cbt2024::InvalidCbtBisectorIndex);
            data.ProblematicNeighbor = Cbt2024::InvalidCbtBisectorIndex;
            data.PropagationId = Cbt2024::InvalidCbtBisectorIndex;
        }
        for (std::uint32_t i = 0U; i < Cbt2024::CbtBaseBisectorCount; ++i)
        {
            const auto physical = CpuCbtState::DynamicCapacity + i;
            next.HeapIds[physical] = base.HeapIds[i];
            next.Neighbors[physical] = base.Neighbors[i];
            next.Data[physical] = base.BisectorData[i];
            next.ActiveIndices.push_back(physical);
        }
        next.Occupancy.Reduce();
        // 初始化与更新采用相同的发布原则，所有可失败工作都在临时对象中完成
        state = std::move(next);
        error.clear();
        return true;
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        return false;
    }
}
} // 命名空间 ParallelRoam::Algorithms::CpuCbt
