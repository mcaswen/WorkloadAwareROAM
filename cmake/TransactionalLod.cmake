# 核心独立于测试目录，运行适配与研究入口使用同一份计算实现
option(PARALLEL_ROAM_BUILD_TRANSACTIONAL_LOD "Build the transactional CPU LOD core" OFF)
option(PARALLEL_ROAM_ENABLE_TRANSACTIONAL_LOD_RUNTIME "Enable the experimental transactional platform adapter" OFF)
if(PARALLEL_ROAM_ENABLE_TRANSACTIONAL_LOD_RUNTIME AND NOT PARALLEL_ROAM_BUILD_TRANSACTIONAL_LOD)
    message(FATAL_ERROR "Transactional runtime requires PARALLEL_ROAM_BUILD_TRANSACTIONAL_LOD=ON.")
endif()
if(NOT PARALLEL_ROAM_BUILD_TRANSACTIONAL_LOD)
    return()
endif()

set(PARALLEL_ROAM_TRANSACTIONAL_BOOST_INCLUDE "" CACHE PATH "Pinned Boost 1.90.0 include directory")
if(NOT EXISTS "${PARALLEL_ROAM_TRANSACTIONAL_BOOST_INCLUDE}/boost/version.hpp")
    message(FATAL_ERROR "Transactional LOD requires explicit Boost 1.90.0 headers.")
endif()
file(STRINGS "${PARALLEL_ROAM_TRANSACTIONAL_BOOST_INCLUDE}/boost/version.hpp" transactional_boost_version
    REGEX "^#define BOOST_VERSION 109000$")
if(NOT transactional_boost_version)
    message(FATAL_ERROR "Transactional LOD pins BOOST_VERSION=109000.")
endif()
parallel_roam_ensure_glm()
if(NOT TARGET ParallelROAM::GLM)
    message(FATAL_ERROR "Transactional LOD requires GLM.")
endif()
set(transactional_core_dir "${PROJECT_SOURCE_DIR}/src/algorithms/greedy_transactional_lod")
add_library(parallel_roam_transactional_lod_core STATIC
    "${transactional_core_dir}/TransactionalStateInvariant.cpp"
    "${transactional_core_dir}/TransactionalState.cpp" "${transactional_core_dir}/TransactionalSamples.cpp"
    "${transactional_core_dir}/TransactionalViewState.cpp" "${transactional_core_dir}/TransactionalProposalEvidence.cpp"
    "${transactional_core_dir}/TransactionalPredicates.cpp" "${transactional_core_dir}/TransactionalCertification.cpp"
    "${transactional_core_dir}/TransactionalQualityEvaluation.cpp"
    "${transactional_core_dir}/TransactionalQualitySampleEvidence.cpp"
    "${transactional_core_dir}/TransactionalQualityFaceEvidence.cpp"
    "${transactional_core_dir}/TransactionalHeightRejection.cpp"
    "${transactional_core_dir}/TransactionalRejectionHints.cpp"
    "${transactional_core_dir}/TransactionalIdlePlanCache.cpp"
    "${transactional_core_dir}/TransactionalPointwiseQuality.cpp"
    "${transactional_core_dir}/TransactionalProposals.cpp" "${transactional_core_dir}/TransactionalReservation.cpp"
    "${transactional_core_dir}/TransactionalSourceHeightReceiver.cpp"
    "${transactional_core_dir}/TransactionalFlipRecovery.cpp"
    "${transactional_core_dir}/TransactionalBoundaryRefinement.cpp"
    "${transactional_core_dir}/TransactionalCommit.cpp" "${transactional_core_dir}/TransactionalPipeline.cpp"
    "${transactional_core_dir}/TransactionalMesh.cpp" "${transactional_core_dir}/TransactionalExecution.cpp")
target_include_directories(parallel_roam_transactional_lod_core PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_include_directories(parallel_roam_transactional_lod_core SYSTEM PRIVATE "${PARALLEL_ROAM_TRANSACTIONAL_BOOST_INCLUDE}")
target_link_libraries(parallel_roam_transactional_lod_core PUBLIC parallel_roam_project_options ParallelROAM::GLM
    PRIVATE parallel_roam_project_warnings)
# 严格数值选项不反向施加到 Classic/DOD 消费者
if(MSVC)
    target_compile_options(parallel_roam_transactional_lod_core PRIVATE /fp:strict)
else()
    target_compile_options(parallel_roam_transactional_lod_core PRIVATE -fno-fast-math -ffp-contract=off)
endif()

if(PARALLEL_ROAM_ENABLE_TRANSACTIONAL_LOD_RUNTIME)
    add_library(parallel_roam_transactional_lod_adapter STATIC
        "${transactional_core_dir}/TransactionalTerrainLodAlgorithm.cpp"
        "${transactional_core_dir}/TransactionalSeedBuilder.cpp"
        "${transactional_core_dir}/TransactionalRenderBridge.cpp")
    target_link_libraries(parallel_roam_transactional_lod_adapter PUBLIC parallel_roam_transactional_lod_core
        PRIVATE parallel_roam_project_warnings)
    target_compile_definitions(parallel_roam_transactional_lod_adapter PUBLIC PARALLEL_ROAM_TRANSACTIONAL_LOD_RUNTIME=1)
    if(MSVC)
        target_compile_options(parallel_roam_transactional_lod_adapter PRIVATE /fp:strict)
    else()
        target_compile_options(parallel_roam_transactional_lod_adapter PRIVATE -fno-fast-math -ffp-contract=off)
    endif()
    # 最终平台/测试入口提供现有 DOD、HeightMap、视图与执行工具，不在适配库复制它们
endif()
