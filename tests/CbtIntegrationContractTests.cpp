#include "algorithms/ITerrainLodAlgorithm.h"
#include "algorithms/TerrainLodGpuOutput.h"
#include "algorithms/cbt_2024/Cbt2024Baseline.h"
#include "algorithms/cbt_2024/CbtBisectorTopology.h"

#include <iostream>

namespace
{
bool Expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
    }
    return condition;
}
} // 匿名命名空间

int main()
{
    using namespace ParallelRoam::Algorithms;
    using namespace ParallelRoam::Algorithms::Cbt2024;

    static_assert(sizeof(ParallelRoam::Terrain::TerrainMeshVertex) == 52U);
    static_assert(sizeof(CbtDrawState) == 40U);
    static_assert(sizeof(CbtBisectorData) == 32U);

    TerrainLodBuildInput input{};
    const std::uint64_t cpuInputHash = HashTerrainLodBuildInput(input);
    bool passed = Expect(input.Settings.Cbt.TriangleAreaPixels == 50.0F,
        "CBT default area changed");
    passed &= Expect(input.Settings.Cbt.Capacity == TerrainLodCbtCapacity::Capacity128K,
        "CBT default pool capacity changed");

    // CBT 设置不属于旧 CPU 输入身份，不能改变既有证据的哈希解释
    input.Settings.Cbt.Capacity = TerrainLodCbtCapacity::Capacity1M;
    input.Settings.Cbt.TriangleAreaPixels = 2.05F;
    passed &= Expect(HashTerrainLodBuildInput(input) == cpuInputHash,
        "CBT settings contaminated the CPU input identity");
    passed &= Expect(input.Settings.TriangleBudget == 20000U,
        "CBT capacity changed the CPU hard budget");

    const TerrainLodGpuOutput emptyOutput{};
    passed &= Expect(emptyOutput.NativeResourceApi == TerrainLodNativeResourceApi::None &&
        emptyOutput.GpuResourceGeneration == 0U && emptyOutput.TopologyGeneration == 0U,
        "an empty GPU output claims live resources");
    passed &= Expect(OfficialBaselineV1::BaselineId == "cbt-2024-official-baseline-v1",
        "imported reference identity changed");
    return passed ? 0 : 1;
}
