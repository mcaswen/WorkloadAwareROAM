#include "algorithms/TerrainLodAlgorithmRegistry.h"
#include "algorithms/classic_roam/ClassicRoamTerrainLodAlgorithm.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamTerrainLodAlgorithm.h"
#if defined(PARALLEL_ROAM_TRANSACTIONAL_LOD_RUNTIME)
#include "algorithms/greedy_transactional_lod/TransactionalTerrainLodAlgorithm.h"
#endif

namespace ParallelRoam::Algorithms
{
std::unique_ptr<ITerrainLodAlgorithm> CreateTerrainLodAlgorithm(TerrainLodAlgorithmId id)
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
    default:
        return nullptr;
    }
}
}
