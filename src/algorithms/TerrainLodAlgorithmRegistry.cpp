#include "algorithms/TerrainLodAlgorithmRegistry.h"
#include "algorithms/classic_roam/ClassicRoamTerrainLodAlgorithm.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamTerrainLodAlgorithm.h"
#if defined(PARALLEL_ROAM_TRANSACTIONAL_LOD_RUNTIME)
#include "algorithms/greedy_transactional_lod/TransactionalTerrainLodAlgorithm.h"
#endif
#if defined(PARALLEL_ROAM_CBT_2024_RUNTIME)
#include "algorithms/cbt_2024/Cbt2024Support.h"
#include "algorithms/cbt_2024/d3d12/D3D12CbtTerrainLodAlgorithm.h"
#include "render/D3D12GraphicsBackend.h"
#endif

namespace ParallelRoam::Algorithms
{
std::unique_ptr<ITerrainLodAlgorithm> CreateTerrainLodAlgorithm(
    TerrainLodAlgorithmId id,
    const TerrainLodCreationContext& context)
{
    switch (id)
    {
    case TerrainLodAlgorithmId::ClassicCpuRoam:
        return std::make_unique<ClassicRoam::ClassicRoamTerrainLodAlgorithm>();
    case TerrainLodAlgorithmId::DataOrientedCpuRoam:
        return std::make_unique<DataOrientedRoam::DataOrientedRoamTerrainLodAlgorithm>();
    case TerrainLodAlgorithmId::TransactionalCpuLod:
#if defined(PARALLEL_ROAM_TRANSACTIONAL_LOD_RUNTIME)
        return std::make_unique<GreedyTransactionalLod::TransactionalTerrainLodAlgorithm>();
#else
        return nullptr;
#endif
    case TerrainLodAlgorithmId::Cbt2024:
#if defined(PARALLEL_ROAM_CBT_2024_RUNTIME)
        if (auto* backend = dynamic_cast<Render::D3D12GraphicsBackend*>(context.GraphicsBackend))
        {
            if (Cbt2024::QueryCbt2024Availability(*backend).Available)
            {
                return std::make_unique<Cbt2024::D3D12::D3D12CbtTerrainLodAlgorithm>(*backend);
            }
        }
#else
        (void)context;
#endif
        return nullptr;
    default:
        return nullptr;
    }
}
}
